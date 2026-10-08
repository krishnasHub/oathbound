#include "RPGEnemy.h"
#include "TSFeedback.h"
#include "Oathbound.h"
#include "TSData.h"
#include "RPGAssets.h"
#include "TSCombat.h"
#include "RPGSession.h"
#include "LMStory.h"
#include "RPGPlayerCharacter.h"
#include "TSProjectile.h"
#include "TSPerception.h"
#include "RPGLoot.h"
#include "RPGGhost.h"
#include "TSRoutine.h"
#include "TSSleep.h"
#include "TSSky.h"
#include "RPGTheft.h"
#include "TSInteractable.h"
#include "TSLook.h"
#include "TSAssets.h"
#include "Components/StaticMeshComponent.h"

#include "ProceduralMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "AIController.h"
#include "EngineUtils.h"

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
	// "base": another enemy this one is a variant of (its fields, overridden by this one's).
	if (const FString Base = TSJson::Str(Def, TEXT("base")); !Base.IsEmpty())
		if (const TSJson::FObj B = D.Entry(TEXT("enemies"), Base))
		{
			const TSJson::FObj Merged = MakeShared<FJsonObject>();
			for (const auto& KV : B->Values) Merged->SetField(KV.Key, KV.Value);
			for (const auto& KV : Def->Values) Merged->SetField(KV.Key, KV.Value);
			Def = Merged;
		}
	Home = InHome;
	Region = D.RegionAt(Home.Y);
	DisplayName = TSJson::Str(Def, TEXT("name"), Type);
	DialogueRoot = TSJson::Str(Def, TEXT("dialogue"));
	TalkKey = Type;
	// Several of a kind, each with a mind of its own: a want of its own, and its own memory in the story.
	if (const TArray<TSharedPtr<FJsonValue>> Wants = TSJson::Arr(Def, TEXT("wants")); Wants.Num())
	{
		const FIntPoint Tile(FMath::FloorToInt(Home.X / D.TileSize), FMath::FloorToInt(Home.Y / D.TileSize));
		int32 Before = 0;   // round the list in spawn order, so neighbours want different things
		for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (*It != this && It->Type == Type) ++Before;
		Wish = Wants[Before % Wants.Num()]->AsString();
		TalkKey = FString::Printf(TEXT("%s@%d,%d"), *Type, Tile.X, Tile.Y);
	}
	NameColor = FLinearColor(1.f, 0.85f, 0.78f);

	const FString LookId = TSJson::Str(Def, TEXT("look"), Type);   // variants can borrow another's look
	SetLookFromData(LookId);
	if (TSJson::Has(Def, TEXT("scale"))) SetActorScale3D(FVector(float(TSJson::Num(Def, TEXT("scale"), 1))));
	{
		const TSJson::FObj W3 = D.World();
		const FString Weapon = TSJson::Str(TSJson::Obj(TSJson::Obj(W3, TEXT("looks")), LookId), TEXT("weapon"));
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
	SetupSleep();
}

void ARPGEnemy::SetupSleep()
{
	const ETSSleepHours Hours = UTSSleep::Parse(TSJson::Str(Def, TEXT("sleeps")));
	if (Hours == ETSSleepHours::Never) return;
	const UTSData& D = UTSData::Get(this);
	URPGSession* Session = URPGSession::Get(this);
	const TSJson::FObj Rest = TSJson::Obj(D.Section(TEXT("restPlaces")), TSJson::Str(Def, TEXT("rest")));
	// Its bed: the rest place's spot nearest home that nobody has taken yet (none: sleeps where it stands).
	FVector Bed = Home;
	bool bBed = false;
	float Best = BIG_NUMBER;
	for (const TSharedPtr<FJsonValue>& V : TSJson::Arr(Rest, TEXT("spots")))
	{
		const TArray<TSharedPtr<FJsonValue>>& T2 = V->AsArray();
		const FVector At = Session->GroundAt(int32(T2[0]->AsNumber()), int32(T2[1]->AsNumber()));
		bool bTaken = false;
		for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It)
			if (*It != this && It->Sleep && It->Sleep->bHasBed && It->Sleep->Bed.Equals(At, 5.f)) { bTaken = true; break; }
		const float Dist = FVector::Dist2D(At, Home);
		if (!bTaken && Dist < Best) { Best = Dist; Bed = At; bBed = true; }
	}
	Sleep = UTSSleep::Add(this, Hours, Bed, bBed);
	const TArray<TSharedPtr<FJsonValue>> Entry = TSJson::Arr(Rest, TEXT("entry"));
	if (Entry.Num() == 2) Sleep->SetEntry(Session->GroundAt(int32(Entry[0]->AsNumber()), int32(Entry[1]->AsNumber())));
	// Into the ground through its burrow (the graveyard's bone-hole), and out again: you hear it dig.
	if (Entry.Num() == 2)
	{
		const FString Dig = TSJson::Str(Rest, TEXT("entryBark"));
		auto Scrape = [this, Dig]() { if (!Dig.IsEmpty() && Sleep) UTSFeedback::Get(this)->Float(Sleep->Entry + FVector(0, 0, 90), Dig, FLinearColor(0.75f, 0.62f, 0.45f), 0.7f); };
		Sleep->OnFellAsleep.AddWeakLambda(this, [Scrape](ATSCharacter*) { Scrape(); });
		Sleep->OnWoke.AddWeakLambda(this, [this, Scrape](ATSCharacter*) { if (!Sleep->IsBedtime()) Scrape(); });
	}
	// Up at the end of its night: back to its post.
	Sleep->OnWoke.AddWeakLambda(this, [this](ATSCharacter*)
	{
		if (Sleep->IsBedtime() || bPacified || bDead || IsProwling()) return;
		State = ERPGEnemyState::Return;
		T = 60.f;
	});
}

bool ARPGEnemy::TickSleep(float Dt)
{
	if (!Sleep || Routine) return false;
	URPGSession* Session = URPGSession::Get(this);
	if (IsHunting() || Session->Duel.Get() == this || Session->DialogueNpc() == this) Sleep->Hold();
	const ARPGPlayerCharacter* P = Session->Player();
	const float Dist = P ? FVector::Dist2D(P->GetActorLocation(), GetActorLocation()) : BIG_NUMBER;
	if (Sleep->IsAsleep())
	{
		// A hero right beside it (TSPerception: a sleeper only hears, and only close) wakes it.
		if (!bPacified && !Sleep->IsIndoors() && CanSeePlayer(Dist))
		{
			Sleep->Wake();
			UTSFeedback::Get(this)->Float(Head() + FVector(0, 0, 30), TEXT("!"), FLinearColor(1.f, 0.88f, 0.3f), 1.4f);
			if (!IsPassive()) State = ERPGEnemyState::Chase;
		}
		return true;
	}
	if (!Sleep->IsTurningIn() || !(State == ERPGEnemyState::Idle || State == ERPGEnemyState::Return)) return false;
	if (!bPacified && !IsPassive() && CanSeePlayer(Dist)) return false;   // spotted the hero on the way: the AI takes over
	const UTSData& D = UTSData::Get(this);
	GetCharacterMovement()->MaxWalkSpeed = D.Px(TSJson::Num(Def, TEXT("speed"), 80));
	GetCharacterMovement()->bOrientRotationToMovement = true;
	State = ERPGEnemyState::Idle;
	const FVector Dir = Sleep->Direction(Dt);
	if (!Dir.IsNearlyZero()) AddMovementInput(Dir, 0.8f);
	return true;
}

bool ARPGEnemy::IsPassive() const
{
	const URPGSession* S = URPGSession::Get(this);
	const FString F = FactionId();
	if (bPacified) return true;
	if (!F.IsEmpty()) return S && S->Story()->Faction(F) != TEXT("hostile") && S->Duel.Get() != this && IsReasonable();
	// No faction: it waits for a hero who can talk to it (its language, something to say), until provoked.
	return !bProvoked && !DialogueRoot.IsEmpty() && IsReasonable();
}

bool ARPGEnemy::IsReasonable() const
{
	const URPGSession* S = URPGSession::Get(this);
	const ARPGPlayerCharacter* P = S ? S->Player() : nullptr;
	return P && P->Speaks(Speaks());
}

FString ARPGEnemy::NotReasonableWhy() const
{
	return Speaks().IsEmpty() ? FString(TEXT("It can't be reasoned with.")) : ARPGPlayerCharacter::CantSpeakWhy(this, Speaks());
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
	Session->SetHostile(FactionId(), Session->Duel.IsValid() ? TEXT("You broke the duel! The Red Hands attack!") : TSJson::Str(Def, TEXT("struckBark"), TEXT("You attacked the Red Hands!")), /*bByHero*/ true);
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
	if (IsFrozen()) return;   // frozen solid: the AI, a wind-up and its swing all wait for the thaw

	if (State == ERPGEnemyState::Leaving)
	{
		T -= Dt;
		if (!bResting) AddMovementInput(FVector(1, 0, 0), 1.f);   // walk off east, then gone for good (or crumble where it stands)
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

	if (TickProwl(Dt)) return;
	if (TickSleep(Dt)) return;

	if (bPacified)
	{
		// Won over: stands easy, or (a trader) walks its rounds, and sleeps when they bring him home at bedtime.
		if (Routine)
		{
			const FName SleepAt(TSJson::Str(TSJson::Obj(Def, TEXT("trade")), TEXT("sleepAt"), TEXT("mine")));
			if (Sleep && Sleep->Hours != ETSSleepHours::Never)
			{
				if (Sleep->IsAsleep()) { PlaceSack(); return; }
				if (Sleep->IsBedtime() && Routine->CurrentTag() == SleepAt && Routine->IsWaiting()) { bCarrying = false; Sleep->FallAsleep(false); PlaceSack(); return; }
				Sleep->Hold();   // (on his rounds: never asleep on his feet)
			}
			const FVector Dir = Routine->Direction(Dt);
			if (!Dir.IsNearlyZero()) AddMovementInput(Dir, 1.f);
		}
		PlaceSack();
		return;
	}
	if (Tags.Has(TEXT("Staggered"))) return;
	RunAI(Dt);
}

bool ARPGEnemy::CanSeePlayer(float Dist) const
{
	// Territory first: it only notices the player in its own region, unless provoked.
	const UTSData& D = UTSData::Get(this);
	const ARPGPlayerCharacter* P = URPGSession::Get(this)->Player();
	if (!P || !(bProvoked || D.RegionAt(P->GetActorLocation().Y) == Region)) return false;
	return TSPerception::CanNotice(this, P, FTSSenses::Read(this, Def, D.Px(TSJson::Num(Def, TEXT("aggro"), 170)), TEXT("enemyVision")));
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
		// Watch the hero once it has noticed them (not a crouched Thief creeping up behind); otherwise go about its
		// business: back to its post if it strayed far (up from its bed), else roam round it like any idle foe.
		State = ERPGEnemyState::Idle;
		if (P && FVector::Dist2D(P->GetActorLocation(), GetActorLocation()) < D.Px(260) && (Session->DialogueNpc() == this || RPGTheft::Notices(this, P)))
		{
			FaceToward(P->GetActorLocation(), Dt, 360.f);
			return;
		}
		if (Sleep && FVector::Dist2D(Home, GetActorLocation()) > D.Px(RoamRadius() + 60.0))
		{
			Move->bOrientRotationToMovement = true;
			const FVector Dir = Sleep->DirectionTo(Home, Dt);
			if (!Dir.IsNearlyZero()) { AddMovementInput(Dir, 0.8f); return; }
		}
		Wander(Dt);
		return;
	}
	if (!P) return;

	const FVector PL = P->GetActorLocation();
	const float Dist = FVector::Dist2D(PL, GetActorLocation());
	const bool bPlayerHidden = TSPerception::IsHidden(P);
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
		Wander(Dt);
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
		if (Sleep) { const FVector Dir = Sleep->DirectionTo(Home, Dt); AddMovementInput(Dir.IsNearlyZero() ? (Home - GetActorLocation()).GetSafeNormal2D() : Dir, 1.f); }   // round the graveyard wall
		else MoveToward(Home, 1.f);
		if (CanSeePlayer(Dist) && FVector::Dist2D(GetActorLocation(), Home) < D.Px(TSJson::Num(Def, TEXT("leash"), 360)) * 0.6f) { State = ERPGEnemyState::Chase; break; }
		// Home, or lost for a long while: put back at its post, but never in front of the hero (no popping).
		if (FVector::Dist2D(GetActorLocation(), Home) < 40.f) ResetToHome();
		else if (T <= 0.f && (!P || (FVector::Dist2D(P->GetActorLocation(), GetActorLocation()) > 2500.f && FVector::Dist2D(P->GetActorLocation(), Home) > 2500.f))) ResetToHome();
		else if (T <= 0.f) T = 5.f;
		break;
	}
	default: break;
	}
}

double ARPGEnemy::RoamRadius() const
{
	// How far it roams from its post (px): "wander", or { day, night } (a lair beast lurks wide at night).
	const TSharedPtr<FJsonValue> W = Def->TryGetField(TEXT("wander"));
	return !W ? 60.0 : W->Type == EJson::Object ? TSJson::Num(W->AsObject(), ATSSky::Night() >= 0.5f ? TEXT("night") : TEXT("day"), 60) : W->AsNumber();
}

void ARPGEnemy::Wander(float Dt)
{
	// Amble to a spot, linger there a while (tuning.wanderDwell [min, max] s), then on to the next: still long enough
	// for a thief to work. While someone is lifting its purse (tag "PickedAt") it lingers on: it's already standing.
	const UTSData& D = UTSData::Get(this);
	GetCharacterMovement()->bOrientRotationToMovement = true;
	if (!bHasWander)
	{
		const double Roam = RoamRadius();
		const float A = FMath::FRandRange(0.f, UE_TWO_PI), R = FMath::FRandRange(Roam > 100.0 ? D.Px(Roam * 0.4) : 0.f, D.Px(Roam));
		WanderTarget = Home + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 0);
		bHasWander = true;
		bLingering = false;
		T = 8.f;   // (give up walking there after this long: something's in the way)
	}
	if (!bLingering)
	{
		T -= Dt;
		if (FVector::Dist2D(WanderTarget, GetActorLocation()) > 20.f && T > 0.f) { MoveToward(WanderTarget, 0.35f); return; }
		const TArray<TSharedPtr<FJsonValue>> Dwell = TSJson::Arr(D.Section(TEXT("tuning")), TEXT("wanderDwell"));
		T = Dwell.Num() == 2 ? FMath::FRandRange(float(Dwell[0]->AsNumber()), float(Dwell[1]->AsNumber())) : FMath::FRandRange(3.f, 7.f);
		bLingering = true;
		return;
	}
	if (!Tags.Has(TEXT("PickedAt"))) T -= Dt;
	if (T <= 0.f) bHasWander = false;
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

	RevealT = TSPerception::RevealAfterAttack(this);
	++AtkIndex;
	Cool = float(TSJson::Num(A, TEXT("cooldown"), 0));
}

// ---------------------------------------------------------------------------------------------

void ARPGEnemy::Provoke()
{
	if (bDead || IsLeaving()) return;
	if (Sleep) Sleep->Wake();
	bProvoked = true;
	if (!IsPassive()) { State = ERPGEnemyState::Chase; UTSFeedback::Get(this)->Float(Head() + FVector(0, 0, 30), TEXT("!"), FLinearColor(1.f, 0.88f, 0.3f), 1.4f); }
}

void ARPGEnemy::Leave(bool bRest, bool bPeace)
{
	if (State == ERPGEnemyState::Leaving || bDead) return;
	if (Sleep) { Sleep->Reset(); Sleep->Hours = ETSSleepHours::Never; }   // up and off, whatever the hour
	if (!bPacified && bPeace) GrantPeace(TEXT("talked down "));
	State = ERPGEnemyState::Leaving;
	T = 3.f;
	bResting = bRest;
	if (bRest) { T = 0.7f; ARPGGhost::Rise(this, /*bCalm*/ true); GetCharacterMovement()->StopMovementImmediately(); }
	Telegraph->SetVisibility(false);
	GetCharacterMovement()->bOrientRotationToMovement = true;
}

void ARPGEnemy::GrantPeace(const TCHAR* How)
{
	// A peaceful win: worth at least what killing it was (data: tuning.peaceXpMul), and the world brightens.
	const UTSData& D = UTSData::Get(this);
	if (ARPGPlayerCharacter* P = URPGSession::Get(this)->Player())
		P->GainXp(FMath::RoundToInt(float(TSJson::Num(Def, TEXT("xp"), 0)) * float(D.Tuning(TEXT("peaceXpMul"), 1.2))));
	URPGSession::Get(this)->Story()->AddMood(float(D.Tuning(TEXT("moodPeace"), 3)), How + DisplayName);
}

void ARPGEnemy::Pacify(const TSJson::FObj& Become)
{
	if (bPacified || bDead || IsLeaving()) return;
	GrantPeace(TEXT("won over "));
	bPacified = true;
	Team = ETSTeam::Neutral;
	State = ERPGEnemyState::Idle;
	Tags.Clear();
	Telegraph->SetVisibility(false);
	if (Become)
	{
		const TSJson::FObj Merged = MakeShared<FJsonObject>();
		for (const auto& KV : Def->Values) Merged->SetField(KV.Key, KV.Value);
		for (const auto& KV : Become->Values) Merged->SetField(KV.Key, KV.Value);
		Def = Merged;
		DisplayName = TSJson::Str(Def, TEXT("name"), DisplayName);
		DialogueRoot = TSJson::Str(Def, TEXT("dialogue"), DialogueRoot);
		TalkKey = Type + TEXT("_won");
	}
}

UTSRoutine* ARPGEnemy::MakeRoutine(const TSJson::FObj& Spec, FName Name)
{
	// Its rounds (Tessera's routine): tiles in the data, uu here.
	const UTSData& D = UTSData::Get(this);
	UTSRoutine* R = NewObject<UTSRoutine>(this, Name);
	R->RegisterComponent();
	for (const TSharedPtr<FJsonValue>& V : TSJson::Arr(Spec, TEXT("stops")))
	{
		const TSJson::FObj S = V->AsObject();
		const TArray<TSharedPtr<FJsonValue>> At = TSJson::Arr(S, TEXT("at"));
		if (At.Num() != 2) continue;
		R->AddStop(D.TileCenter(int32(At[0]->AsNumber()), int32(At[1]->AsNumber())), float(TSJson::Num(S, TEXT("wait"), 4)),
			FName(TSJson::Str(S, TEXT("when"), TEXT("any"))), FName(TSJson::Str(S, TEXT("tag"))));
	}
	R->ArriveDistance = 120.f + Radius();
	// Its lair is in the cave: the routine walks to the door between areas, and through it.
	R->FindDoor = [this](const FVector& From, const FVector& Goal, FVector& Door)
	{
		const UTSData& Data = UTSData::Get(this);
		const FTSArea* Here = Data.AreaAt(From);
		const FTSArea* There = Data.AreaAt(Goal);
		if (Here == There) return false;
		for (TActorIterator<ATSInteractable> It(GetWorld()); It; ++It)
		{
			const ATSInteractable* To = It->DoorTarget();
			if (To && Data.AreaAt(It->GetActorLocation()) == Here && Data.AreaAt(To->GetActorLocation()) == There) { Door = It->GetActorLocation(); return true; }
		}
		return false;
	};
	R->ThroughDoor = [this](const FVector& Door)
	{
		for (TActorIterator<ATSInteractable> It(GetWorld()); It; ++It)
			if (It->GetActorLocation().Equals(Door, 1.f))
				if (const ATSInteractable* To = It->DoorTarget())
					TeleportTo(To->ExitPoint(UTSData::Get(this).TileSize) + FVector(0, 0, GetSimpleCollisionHalfHeight() + 30.f), GetActorRotation(), false, true);
	};
	return R;
}

bool ARPGEnemy::IsProwling() const
{
	if (bDead || bPacified || IsLeaving() || !TSJson::Has(Def, TEXT("prowl"))) return false;
	const ULMStory* L = URPGSession::Get(this)->Story();
	return L->HasFlag(TEXT("relic_stolen")) && !L->HasFlag(TEXT("relic_returned")) && !L->HasFlag(TEXT("relic_paid")) && !L->HasFlag(TEXT("relic_brute_slain"));
}

bool ARPGEnemy::TickProwl(float Dt)
{
	if (!IsProwling())
	{
		// Given back or paid off: home to its lair (out of sight it simply turns up there).
		if (Prowl) { Prowl->DestroyComponent(); Prowl = nullptr; if (!UTSData::Get(this).AreaAt(GetActorLocation())) { State = ERPGEnemyState::Return; T = 8.f; } }
		return false;
	}
	const UTSData& D = UTSData::Get(this);
	const TSJson::FObj Spec = TSJson::Obj(Def, TEXT("prowl"));
	if (!Prowl)
	{
		Prowl = MakeRoutine(Spec, TEXT("Prowl"));
		const FString Bark = TSJson::Str(Spec, TEXT("bark"));
		Prowl->OnArrive.AddWeakLambda(this, [this, Bark](int32, FName Tag)
		{
			if (Tag == TEXT("village") && !Bark.IsEmpty()) UTSFeedback::Get(this)->Float(Head() + FVector(0, 0, 40), Bark, FLinearColor(1.f, 0.6f, 0.45f), 1.2f);
		});
		Prowl->Begin();
	}
	if (IsHunting()) { if (Sleep) Sleep->Hold(); return false; }   // found someone: the AI fights
	if (State == ERPGEnemyState::Return) State = ERPGEnemyState::Idle;  // lost them: back to the hunt for its shiny
	if (Sleep && Sleep->IsAsleep()) return false;                      // (TickSleep: asleep on its hay)
	// It sleeps only once it's back on its hay, by day.
	if (Sleep)
	{
		if (Prowl->CurrentTag() == TEXT("hay") && Prowl->IsWaiting() && Sleep->IsBedtime()) { Sleep->FallAsleep(false); return true; }
		Sleep->Hold();
	}
	const ARPGPlayerCharacter* P = URPGSession::Get(this)->Player();
	if (P && CanSeePlayer(FVector::Dist2D(P->GetActorLocation(), GetActorLocation())))
	{
		bProvoked = true;
		State = ERPGEnemyState::Chase;
		UTSFeedback::Get(this)->Float(Head() + FVector(0, 0, 30), TEXT("SHINY THIEF!"), FLinearColor(1.f, 0.45f, 0.35f), 1.3f);
		return false;
	}
	GetCharacterMovement()->MaxWalkSpeed = D.Px(TSJson::Num(Spec, TEXT("speed"), 70));
	GetCharacterMovement()->bOrientRotationToMovement = true;
	const FVector Dir = Prowl->Direction(Dt);
	if (!Dir.IsNearlyZero()) AddMovementInput(Dir, 1.f);
	return true;
}

void ARPGEnemy::BecomeTrader()
{
	const TSJson::FObj Trade = TSJson::Obj(Def, TEXT("trade"));
	if (!Trade || Routine) return;
	Pacify(TSJson::Obj(Trade, TEXT("become")));
	// A working life now: he keeps village hours (trade.sleeps), sleeping at the stop tagged trade.sleepAt (his hay
	// in the mine) once his rounds bring him there. (Not wherever bedtime finds him: that laid him down mid-walk.)
	if (Sleep)
	{
		Sleep->Reset();
		Sleep->Hours = UTSSleep::Parse(TSJson::Str(Trade, TEXT("sleeps"), TEXT("night")));
	}
	Routine = MakeRoutine(Trade, TEXT("Routine"));
	// Gold from the mine to the village; on the way back, its pay (food). The sack shows while it carries gold.
	const FName CarryTo(TSJson::Str(Trade, TEXT("carryTo"), TEXT("village")));
	const FString Delivered = TSJson::Str(Trade, TEXT("deliveredBark"));
	Routine->OnDepart.AddWeakLambda(this, [this, CarryTo](int32, FName Tag) { bCarrying = Tag == CarryTo; });
	Routine->OnArrive.AddWeakLambda(this, [this, CarryTo, Delivered](int32, FName Tag)
	{
		if (Tag != CarryTo) return;
		bCarrying = false;
		if (!Delivered.IsEmpty()) UTSFeedback::Get(this)->Float(Head() + FVector(0, 0, 40), Delivered, FLinearColor(1.f, 0.85f, 0.35f), 1.4f);
	});
	Sack = NewObject<UStaticMeshComponent>(this, TEXT("Sack"));
	Sack->SetStaticMesh(TSAssets::Shape(TEXT("Plane")));
	Sack->SetMaterial(0, TSLook::PropMaterial(TSJson::Str(Trade, TEXT("sack"), TEXT("PR_GoldSack"))));
	Sack->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sack->SetUsingAbsoluteRotation(true);
	Sack->SetUsingAbsoluteScale(true);
	Sack->SetupAttachment(RootComponent);
	Sack->RegisterComponent();
	Sack->SetVisibility(false);
	GetCharacterMovement()->MaxWalkSpeed = UTSData::Get(this).Px(TSJson::Num(Trade, TEXT("speed"), 60));
	Routine->Begin();
}

void ARPGEnemy::PlaceSack()
{
	if (!Sack) return;
	Sack->SetVisibility(bCarrying && !bDead);
	if (!bCarrying) return;
	// Over its shoulder: a card beside the body, a little up, bobbing with the walk.
	const FRotator R = TSLook::CardRotation();
	const FVector Up = -FRotationMatrix(R).GetUnitAxis(EAxis::Y), Right = FRotationMatrix(R).GetUnitAxis(EAxis::X);
	const float Bob = FMath::Abs(FMath::Sin(GetWorld()->GetTimeSeconds() * 6.f)) * 8.f * (GetVelocity().Size2D() > 10.f);
	const FVector Feet = GetActorLocation() - FVector(0, 0, GetSimpleCollisionHalfHeight());
	const float Size = 90.f;
	Sack->SetWorldLocationAndRotation(Feet + Up * (GetSimpleCollisionHalfHeight() * 1.5f + Bob) + Right * (Radius() * 0.9f) + FRotationMatrix(R).GetUnitAxis(EAxis::Z) * 12.f, R);   // a little toward the camera: in front of him
	Sack->SetWorldScale3D(FVector(Size / 100.f, Size / 100.f, 1.f));
}

void ARPGEnemy::ResetToHome()
{
	if (Sleep) Sleep->Reset();   // out of bed; settles in again next tick if it's its hours
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
	if (IsProwling()) URPGSession::Get(this)->Story()->SetFlag(TEXT("relic_brute_slain"));   // the hunt for its shiny ends here
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
