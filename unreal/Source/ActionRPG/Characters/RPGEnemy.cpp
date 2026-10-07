#include "RPGEnemy.h"
#include "TSFeedback.h"
#include "ActionRPG.h"
#include "TSData.h"
#include "RPGAssets.h"
#include "TSCombat.h"
#include "RPGSession.h"
#include "LMStory.h"
#include "RPGPlayerCharacter.h"
#include "TSProjectile.h"
#include "RPGLoot.h"

#include "ProceduralMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "AIController.h"

namespace
{
	/** Seconds from a montage section's start to its "Do Attack Trace" notify (when the blow lands). */
	float HitOffset(UAnimMontage* M, int32 SectionIndex)
	{
		if (!M || !M->CompositeSections.IsValidIndex(SectionIndex)) return 0.4f;
		const float Start = M->CompositeSections[SectionIndex].GetTime();
		const float End = SectionIndex + 1 < M->CompositeSections.Num() ? M->CompositeSections[SectionIndex + 1].GetTime() : M->GetPlayLength();
		for (const FAnimNotifyEvent& E : M->Notifies)
		{
			const float At = E.GetTriggerTime();
			if (At >= Start && At < End && E.Notify && E.Notify->GetClass()->GetName().Contains(TEXT("DoAttackTrace"))) return At - Start;
		}
		return (End - Start) * 0.4f;
	}
}

ARPGEnemy::ARPGEnemy()
{
	Team = ETSTeam::Hostile;
	Telegraph = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Telegraph"));
	Telegraph->SetupAttachment(RootComponent);
	Telegraph->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Telegraph->SetCastShadow(false);
	Telegraph->SetVisibility(false);
	// CharacterMovement only moves a pawn that has a controller, so give it the stock AIController.
	// Our state machine still makes every decision; the controller just lets movement input through.
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = AAIController::StaticClass();
}

void ARPGEnemy::Init(const FString& InType, const FVector& InHome)
{
	const UTSData& D = UTSData::Get(this);
	Type = InType;
	Def = D.Entry(TEXT("enemies"), Type);
	Home = InHome;
	Region = D.RegionAt(Home.Y);
	DisplayName = TSJson::Str(Def, TEXT("name"), Type);
	DialogueRoot = TSJson::Str(Def, TEXT("dialogue"));
	TalkKey = Type;
	NameColor = FLinearColor(1.f, 0.85f, 0.78f);

	SetLookFromData(Type);
	{
		const TSJson::FObj W3 = D.World();
		const FString Weapon = TSJson::Str(TSJson::Obj(TSJson::Obj(W3, TEXT("looks")), Type), TEXT("weapon"));
		TArray<FString> Kits;
		for (const TSharedPtr<FJsonValue>& V : TSJson::Arr(TSJson::Obj(W3, TEXT("enemyKits")), Weapon)) Kits.Add(V->AsString());
		SetWeaponKits(Kits);
	}
	Stats->Base.Reset();
	Stats->Base.Add(TEXT("hpFlat"), float(TSJson::Num(Def, TEXT("hp"), 30)));
	Stats->Base.Add(TEXT("armor"), float(TSJson::Num(Def, TEXT("armor"), 0)));
	Stats->Fill();
	MaxPoise = Poise = float(TSJson::Num(Def, TEXT("poise"), 30));
	GetCharacterMovement()->MaxWalkSpeed = D.Px(TSJson::Num(Def, TEXT("speed"), 80));

	TelegraphMat = UMaterialInstanceDynamic::Create(TSAssets::Material(this, TEXT("telegraph")), this);
	Telegraph->SetMaterial(0, TelegraphMat);
	Telegraph->SetRelativeLocation(FVector(0, 0, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() + 4.f));
	Strafe = FMath::RandBool() ? 1.f : -1.f;
}

bool ARPGEnemy::IsPassive() const
{
	const FString F = FactionId();
	if (F.IsEmpty()) return false;
	const URPGSession* S = URPGSession::Get(this);
	return S && S->Story()->Faction(F) != TEXT("hostile") && S->Duel.Get() != this;
}

FString ARPGEnemy::FactionId() const { return TSJson::Str(Def, TEXT("faction")); }

void ARPGEnemy::OnDamaged(ATSCharacter* Src)
{
	if (!Cast<ARPGPlayerCharacter>(Src)) return;
	bProvoked = true;   // fights back across regions
	if ((State == ERPGEnemyState::Idle || State == ERPGEnemyState::Return) && !IsPassive())
	{
		State = ERPGEnemyState::Chase;
		UTSFeedback::Get(this)->Float(Head() + FVector(0, 0, 30), TEXT("!"), FLinearColor(1.f, 0.88f, 0.3f), 1.4f);
	}
}

void ARPGEnemy::OnStruck(ATSCharacter* Src)
{
	if (Src->Team != ETSTeam::Player || FactionId().IsEmpty() || !IsPassive()) return;
	URPGSession* Session = URPGSession::Get(this);
	Session->SetHostile(FactionId(), Session->Duel.IsValid() ? TEXT("You broke the duel! The Red Hands attack!") : TEXT("You attacked the Red Hands!"));
}

bool ARPGEnemy::OnHurt(ATSCharacter* Src)
{
	URPGSession* Session = URPGSession::Get(this);
	if (Session->Duel.Get() != this || Stats->Health() > Stats->MaxHealth() * 0.2f) return false;
	Stats->Health() = FMath::Max(1.f, FMath::RoundToFloat(Stats->Health()));
	Session->Duel = nullptr;
	Tags.Clear();
	Session->OpenDialogue(this, YieldDialogueId());
	return true;
}

float ARPGEnemy::HealthFloor() const
{
	// In a duel, poison and burns can't finish the opponent off: the duel ends in a yield.
	const URPGSession* Session = URPGSession::Get(this);
	return Session && Session->Duel.Get() == this ? Stats->MaxHealth() * 0.2f + 1.f : 0.f;
}

void ARPGEnemy::LoseTrack()
{
	if (State == ERPGEnemyState::Chase || State == ERPGEnemyState::Windup || State == ERPGEnemyState::Recover) State = ERPGEnemyState::Idle;
}

void ARPGEnemy::OnStaggered()
{
	if (State == ERPGEnemyState::Windup) { State = ERPGEnemyState::Chase; Cool = 0.4f; }
	SwingDelay = -1.f;
	Telegraph->SetVisibility(false);
	if (UAnimInstance* A = Anim()) A->Montage_Stop(0.15f);
}

float ARPGEnemy::WindupProgress() const
{
	if (State != ERPGEnemyState::Windup || !CurAtk.IsValid()) return 0.f;
	const float W = float(TSJson::Num(CurAtk, TEXT("windup"), 0.5));
	return FMath::Clamp(1.f - T / W, 0.f, 1.f);
}

TSJson::FObj ARPGEnemy::NextAttack() const
{
	const TArray<TSharedPtr<FJsonValue>> A = TSJson::Arr(Def, TEXT("attacks"));
	return A.IsEmpty() ? nullptr : A[AtkIndex % A.Num()]->AsObject();
}

// ---------------------------------------------------------------------------------------------

void ARPGEnemy::Tick(float Dt)
{
	Super::Tick(Dt);
	const UTSData& D = UTSData::Get(this);

	if (bDead)
	{
		DeathHide -= Dt;
		if (DeathHide <= 0.f && !IsHidden()) SetActorHiddenInGame(true);
		RespawnTimer -= Dt;
		const ARPGPlayerCharacter* P = URPGSession::Get(this)->Player();
		if (RespawnTimer <= 0.f && P && FVector::Dist2D(P->GetActorLocation(), Home) > D.Px(D.Tuning(TEXT("respawnMinDistance"), 420))) Respawn();
		return;
	}

	RevealT -= Dt;
	Stats->TickStats(Dt, [this](float H) { TSCombat::Heal(this, H); }, [this](float Dmg, AActor* Src) { TSCombat::Dot(this, Dmg, Src); });

	if (State == ERPGEnemyState::Leaving)
	{
		T -= Dt;
		AddMovementInput(FVector(1, 0, 0), 1.f);   // walk off east, then gone for good
		if (T <= 0.f) { bDead = true; RespawnTimer = BIG_NUMBER; DeathHide = 0.f; GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
		return;
	}

	// Swing montage timed so the blow lands as the wind-up ends.
	if (SwingDelay >= 0.f)
	{
		SwingDelay -= Dt;
		if (SwingDelay < 0.f && SwingMontage) PlayMontage(SwingMontage, SwingRate, SwingSection);
	}

	// Telegraph brightens through the wind-up.
	if (State == ERPGEnemyState::Windup && TelegraphMat)
	{
		TelegraphMat->SetScalarParameterValue(TEXT("Opacity"), 0.12f + 0.38f * WindupProgress());
	}

	if (Tags.Has(TEXT("Staggered"))) return;
	RunAI(Dt);
}

bool ARPGEnemy::CanSeePlayer(float Dist) const
{
	const UTSData& D = UTSData::Get(this);
	const ARPGPlayerCharacter* P = URPGSession::Get(this)->Player();
	if (!P || P->IsDead() || P->Tags.Has(TEXT("Hidden"))) return false;
	const bool bSameSide = bProvoked || D.RegionAt(P->GetActorLocation().Y) == Region;
	if (!bSameSide || Dist >= D.Px(TSJson::Num(Def, TEXT("aggro"), 170))) return false;

	const TSJson::FObj EV = TSJson::Obj(D.Section(TEXT("tuning")), TEXT("enemyVision"));
	const float Hear = D.Px(TSJson::Num(Def, TEXT("hearRadius"), TSJson::Num(EV, TEXT("hearRadius"), 70)));
	const float Cone = float(TSJson::Num(Def, TEXT("visionAngle"), TSJson::Num(EV, TEXT("coneAngle"), 120)));
	const FVector To = (P->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	const bool bInView = Dist < Hear || FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Facing(), To))) <= Cone * 0.5f;
	if (!bInView) return false;

	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(EnemySight), false, this);
	Q.AddIgnoredActor(P);
	return !GetWorld()->LineTraceSingleByChannel(Hit, Head(), P->Head(), ECC_Visibility, Q);
}

void ARPGEnemy::MoveToward(const FVector& Target, float SpeedMul)
{
	const FVector Dir = (Target - GetActorLocation()).GetSafeNormal2D();
	AddMovementInput(Dir, SpeedMul);
}

void ARPGEnemy::FaceToward(const FVector& Target, float Dt, float Rate)
{
	const FRotator Want = (Target - GetActorLocation()).GetSafeNormal2D().Rotation();
	SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), FRotator(0, Want.Yaw, 0), Dt, Rate));
}

void ARPGEnemy::RunAI(float Dt)
{
	const UTSData& D = UTSData::Get(this);
	URPGSession* Session = URPGSession::Get(this);
	ARPGPlayerCharacter* P = Session->Player();
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = D.Px(TSJson::Num(Def, TEXT("speed"), 80)) * (Tags.Has(TEXT("Slowed")) ? 0.4f : 1.f);

	if (IsPassive())
	{
		// Stand your ground and watch the player.
		if (P && FVector::Dist2D(P->GetActorLocation(), GetActorLocation()) < D.Px(260)) FaceToward(P->GetActorLocation(), Dt, 360.f);
		State = ERPGEnemyState::Idle;
		return;
	}
	if (!P) return;

	const FVector PL = P->GetActorLocation();
	const float Dist = FVector::Dist2D(PL, GetActorLocation());
	const bool bPlayerHidden = P->Tags.Has(TEXT("Hidden"));
	const bool bSameSide = bProvoked || D.RegionAt(PL.Y) == Region;

	switch (State)
	{
	case ERPGEnemyState::Idle:
	{
		if (CanSeePlayer(Dist))
		{
			State = ERPGEnemyState::Chase;
			UTSFeedback::Get(Session)->Float(Head() + FVector(0, 0, 30), TEXT("!"), FLinearColor(1.f, 0.88f, 0.3f), 1.4f);
			break;
		}
		T -= Dt;
		if (!bHasWander || T <= 0.f)
		{
			T = FMath::FRandRange(1.5f, 4.f);
			const float A = FMath::FRandRange(0.f, UE_TWO_PI), R = FMath::FRandRange(0.f, D.Px(60));
			WanderTarget = Home + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 0);
			bHasWander = true;
		}
		if (FVector::Dist2D(WanderTarget, GetActorLocation()) > 15.f) MoveToward(WanderTarget, 0.35f);
		break;
	}
	case ERPGEnemyState::Chase:
	{
		if (bPlayerHidden) { State = ERPGEnemyState::Idle; bHasWander = false; break; }       // lost you in the smoke
		if (!bSameSide) { State = ERPGEnemyState::Return; T = 6.f; break; }             // you left its territory
		if (P->IsDead() || FVector::Dist2D(GetActorLocation(), Home) > D.Px(TSJson::Num(Def, TEXT("leash"), 360)) ||
			Dist > D.Px(TSJson::Num(Def, TEXT("aggro"), 170)) * 1.8f)
		{
			State = ERPGEnemyState::Return; T = 6.f; break;
		}
		const TSJson::FObj Atk = NextAttack();
		if (!Atk) break;
		FaceToward(PL, Dt);
		Move->bOrientRotationToMovement = false;
		Cool -= Dt;
		const FString AType = TSJson::Str(Atk, TEXT("type"));
		if (AType == TEXT("ranged"))
		{
			const float Want = D.Px(TSJson::Num(Def, TEXT("preferredDist"), 200));
			const float Dir = Dist < Want - D.Px(40) ? -1.f : (Dist > Want + D.Px(40) ? 1.f : 0.f);
			if (FMath::FRand() < Dt * 0.4f) Strafe = -Strafe;
			const FVector Fwd = (PL - GetActorLocation()).GetSafeNormal2D();
			const FVector Side(-Fwd.Y, Fwd.X, 0);
			AddMovementInput(Fwd * Dir + Side * Strafe * 0.6f, 1.f);
			if (Cool <= 0.f && Dist < D.Px(TSJson::Num(Atk, TEXT("range"), 260)) && CanSeePlayer(Dist)) BeginWindup(Atk);
		}
		else
		{
			const float Reach = D.Px(TSJson::Num(Atk, TEXT("range"), 22));
			if (Dist - P->Radius() > Reach * 0.8f) MoveToward(PL, 1.f);
			if (Cool <= 0.f && Dist - P->Radius() <= Reach + Radius() * 0.5f) BeginWindup(Atk);
		}
		break;
	}
	case ERPGEnemyState::Windup:
	{
		if (!bSameSide) { State = ERPGEnemyState::Return; T = 6.f; Telegraph->SetVisibility(false); SwingDelay = -1.f; if (Anim()) Anim()->Montage_Stop(0.2f); break; }
		T -= Dt;
		if (TSJson::Str(CurAtk, TEXT("type")) == TEXT("ranged")) FaceToward(PL, Dt, 240.f);   // archers track you
		if (T <= 0.f)
		{
			PerformAttack();
			SpriteAttackAt = GetWorld()->GetTimeSeconds();
			State = ERPGEnemyState::Recover;
			T = float(TSJson::Num(CurAtk, TEXT("recover"), 0.6));
		}
		break;
	}
	case ERPGEnemyState::Recover:
	{
		T -= Dt;
		if (T <= 0.f) State = ERPGEnemyState::Chase;
		break;
	}
	case ERPGEnemyState::Return:
	{
		Move->bOrientRotationToMovement = true;
		T -= Dt;
		MoveToward(Home, 1.f);
		if (CanSeePlayer(Dist) && FVector::Dist2D(GetActorLocation(), Home) < D.Px(TSJson::Num(Def, TEXT("leash"), 360)) * 0.6f) { State = ERPGEnemyState::Chase; break; }
		if (FVector::Dist2D(GetActorLocation(), Home) < 40.f || T <= 0.f) ResetToHome();
		break;
	}
	default: break;
	}
}

void ARPGEnemy::BeginWindup(const TSJson::FObj& Atk)
{
	CurAtk = Atk;
	State = ERPGEnemyState::Windup;
	T = float(TSJson::Num(Atk, TEXT("windup"), 0.5));
	const ARPGPlayerCharacter* P = URPGSession::Get(this)->Player();
	if (P) SetActorRotation(FRotator(0, (P->GetActorLocation() - GetActorLocation()).GetSafeNormal2D().Rotation().Yaw, 0));
	BuildTelegraph();

	// Pick the montage swing that matches the attack and time it to land at the end of the wind-up.
	SwingMontage = nullptr;
	SwingDelay = -1.f;
	const FString AType = TSJson::Str(Atk, TEXT("type"));
	if (MeshKind == TEXT("slime") || AType == TEXT("ranged")) return;
	if (AType == TEXT("slam")) SwingMontage = TSAssets::Load<UAnimMontage>(RPGAssets::ChargedMontage);
	else SwingMontage = TSAssets::Load<UAnimMontage>(RPGAssets::ComboMontage);
	if (!SwingMontage || SwingMontage->CompositeSections.IsEmpty()) return;
	const int32 Section = AType == TEXT("slam") ? SwingMontage->CompositeSections.Num() - 1 : AtkIndex % SwingMontage->CompositeSections.Num();
	SwingSection = SwingMontage->CompositeSections[Section].SectionName;
	const float Offset = HitOffset(SwingMontage, Section);
	SwingRate = Offset > T ? Offset / T : 1.f;
	SwingDelay = FMath::Max(0.f, T - Offset / SwingRate);
}

void ARPGEnemy::BuildTelegraph()
{
	const UTSData& D = UTSData::Get(this);
	const FString AType = TSJson::Str(CurAtk, TEXT("type"));
	const float Scale = GetActorScale3D().X;
	TArray<FVector> V; TArray<int32> Tri;

	auto Fan = [&](float Radius, float ArcDeg)
	{
		const int32 N = FMath::Max(8, int32(ArcDeg / 6.f));
		V.Add(FVector::ZeroVector);
		for (int32 I = 0; I <= N; ++I)
		{
			const float A = FMath::DegreesToRadians(-ArcDeg * 0.5f + ArcDeg * I / N);
			V.Add(FVector(FMath::Cos(A), FMath::Sin(A), 0) * Radius);
			if (I > 0) Tri.Append({ 0, I + 1, I });
		}
	};

	if (AType == TEXT("slam")) Fan(D.Px(TSJson::Num(CurAtk, TEXT("radius"), 95)) / Scale, 360.f);
	else if (AType == TEXT("ranged"))
	{
		const float L = D.Px(TSJson::Num(CurAtk, TEXT("range"), 260)) / Scale, W = 9.f / Scale;
		V = { FVector(0, -W, 0), FVector(0, W, 0), FVector(L, W, 0), FVector(L, -W, 0) };
		Tri = { 0, 1, 3, 1, 2, 3 };
	}
	else Fan((D.Px(TSJson::Num(CurAtk, TEXT("range"), 22)) + Radius()) / Scale, float(TSJson::Num(CurAtk, TEXT("arc"), 110)));

	TArray<FVector> N; N.Init(FVector::UpVector, V.Num());
	Telegraph->CreateMeshSection(0, V, Tri, N, TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	Telegraph->SetMaterial(0, TelegraphMat);
	TelegraphMat->SetScalarParameterValue(TEXT("Opacity"), 0.12f);
	Telegraph->SetVisibility(true);
}

void ARPGEnemy::PerformAttack()
{
	const UTSData& D = UTSData::Get(this);
	URPGSession* Session = URPGSession::Get(this);
	ARPGPlayerCharacter* P = Session->Player();
	Telegraph->SetVisibility(false);
	const TSJson::FObj A = CurAtk;
	const FString AType = TSJson::Str(A, TEXT("type"));
	FTSHit Hit;
	Hit.Base = float(TSJson::Num(A, TEXT("damage"), 8));
	Hit.Poise = float(TSJson::Num(A, TEXT("poise"), 0));
	Hit.Knockback = float(TSJson::Num(A, TEXT("knockback"), 0));

	if (AType == TEXT("melee"))
	{
		if (const double Lunge = TSJson::Num(A, TEXT("lunge"), 0)) Knock(Facing() * D.Px(Lunge));
		if (P && !P->IsDead())
		{
			const FVector To = P->GetActorLocation() - GetActorLocation();
			const float Angle = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Facing(), To.GetSafeNormal2D())));
			if (To.Size2D() - P->Radius() <= D.Px(TSJson::Num(A, TEXT("range"), 22)) + Radius() * 0.5f && Angle <= TSJson::Num(A, TEXT("arc"), 110) * 0.5)
				TSCombat::Deal(this, P, Hit);
		}
	}
	else if (AType == TEXT("slam"))
	{
		UTSFeedback::Get(Session)->Shake(6.f);
		if (P && !P->IsDead() && FVector::Dist2D(P->GetActorLocation(), GetActorLocation()) <= D.Px(TSJson::Num(A, TEXT("radius"), 95)) + P->Radius())
			TSCombat::Deal(this, P, Hit);
	}
	else if (AType == TEXT("ranged") && P)
	{
		const TSJson::FObj Pr = TSJson::Obj(A, TEXT("projectile"));
		const FVector From = Chest() + Facing() * (Radius() + 20.f);
		// An arrow lobbed at where the player stands now: keep moving and it lands behind you.
		ATSProjectile::FireArrow(this, From, P->Chest(), D.Px(TSJson::Num(Pr, TEXT("speed"), 300)),
			D.Px(TSJson::Num(Pr, TEXT("radius"), 4)), TSJson::Color(TSJson::Str(Pr, TEXT("fletch"), TEXT("#c83a2a"))), Hit);
	}

	RevealT = float(TSJson::Num(TSJson::Obj(D.Section(TEXT("tuning")), TEXT("threatSense")), TEXT("revealAfterAttack"), 1.5));
	++AtkIndex;
	Cool = float(TSJson::Num(A, TEXT("cooldown"), 0));
}

// ---------------------------------------------------------------------------------------------

void ARPGEnemy::Leave()
{
	State = ERPGEnemyState::Leaving;
	T = 3.f;
	Telegraph->SetVisibility(false);
	GetCharacterMovement()->bOrientRotationToMovement = true;
}

void ARPGEnemy::ResetToHome()
{
	SetActorLocation(Home + FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 10.f), false, nullptr, ETeleportType::TeleportPhysics);
	State = ERPGEnemyState::Idle;
	Tags.Clear();
	Stats->Effects.Reset();
	Stats->Fill();
	Poise = MaxPoise;
	bProvoked = false;
	bHasWander = false;
	Telegraph->SetVisibility(false);
	GetCharacterMovement()->bOrientRotationToMovement = true;
}

void ARPGEnemy::Die(AActor* Killer)
{
	if (bDead) return;
	Super::Die(Killer);
	Telegraph->SetVisibility(false);
	SwingDelay = -1.f;
	const double Respawn = TSJson::Num(Def, TEXT("respawn"), 40);
	RespawnTimer = Respawn < 0 ? BIG_NUMBER : float(Respawn);
	DeathHide = 4.f;

	URPGSession* Session = URPGSession::Get(this);
	Session->Story()->OnKill(Type);
	if (ARPGPlayerCharacter* P = Cast<ARPGPlayerCharacter>(Killer)) P->GainXp(int32(TSJson::Num(Def, TEXT("xp"), 0)));
	RPGLoot::Drop(this);

	const FString Resolve = TSJson::Str(Def, TEXT("resolveOnDeath"));
	FString Enc, Outcome;
	if (Resolve.Split(TEXT(":"), &Enc, &Outcome)) Session->Story()->Resolve(Enc, Outcome);
}

void ARPGEnemy::Respawn()
{
	bDead = false;
	SetActorHiddenInGame(false);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	if (MeshKind != TEXT("slime")) GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	ResetToHome();
}
