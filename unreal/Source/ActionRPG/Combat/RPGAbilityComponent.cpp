#include "RPGAbilityComponent.h"
#include "RPGData.h"
#include "RPGCombat.h"
#include "RPGFX.h"
#include "RPGProjectile.h"
#include "RPGStory.h"
#include "RPGPlayerCharacter.h"
#include "RPGEnemy.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

URPGAbilityComponent::URPGAbilityComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

ARPGPlayerCharacter* URPGAbilityComponent::Player() const { return Cast<ARPGPlayerCharacter>(GetOwner()); }

void URPGAbilityComponent::Setup(const TArray<FString>& InIds)
{
	Ids = InIds;
	Cooldowns.Reset();
	for (const FString& Id : Ids) Cooldowns.Add(Id, 0.f);
}

RPGJson::FObj URPGAbilityComponent::Def(const FString& Id) const { return URPGData::Get(this).Entry(TEXT("abilities"), Id); }

bool URPGAbilityComponent::Unlocked(const FString& Id) const
{
	return Player() && Player()->Level() >= int32(RPGJson::Num(Def(Id), TEXT("unlockLevel"), 1));
}

bool URPGAbilityComponent::CanAfford(const RPGJson::FObj& D) const
{
	const ARPGPlayerCharacter* P = Player();
	return P && P->Stats->Mana >= RPGJson::Num(D, TEXT("mana"), 0) && P->Stats->Stamina >= RPGJson::Num(D, TEXT("stamina"), 0);
}

void URPGAbilityComponent::TickCooldowns(float Dt)
{
	for (auto& KV : Cooldowns) KV.Value = FMath::Max(0.f, KV.Value - Dt);
}

void URPGAbilityComponent::Fail(const FString& Msg) const
{
	if (const ARPGPlayerCharacter* P = Player()) URPGStory::Get(P)->Float(P->Head() + FVector(0, 0, 30), Msg, FLinearColor(0.67f, 0.67f, 0.73f), 0.8f);
}

float URPGAbilityComponent::Scaled(const RPGJson::FObj& D, float V) const
{
	return V * RPGCombat::ScaleBy(Player()->Stats, FName(RPGJson::Str(D, TEXT("scaling"))));
}

bool URPGAbilityComponent::TryActivate(int32 Slot)
{
	ARPGPlayerCharacter* P = Player();
	if (!P || !Ids.IsValidIndex(Slot) || P->IsDead()) return false;
	const FString Id = Ids[Slot];
	const RPGJson::FObj D = Def(Id);
	if (!Unlocked(Id)) { Fail(FString::Printf(TEXT("%s unlocks at Lv %d"), *RPGJson::Str(D, TEXT("name")), int32(RPGJson::Num(D, TEXT("unlockLevel"), 1)))); return false; }
	if (P->IsDodging() || P->Tags.Has(TEXT("Staggered"))) return false;
	if (Cooldowns.FindRef(Id) > 0.f) { Fail(TEXT("Not ready")); return false; }
	if (P->Stats->Mana < RPGJson::Num(D, TEXT("mana"), 0)) { Fail(TEXT("Not enough mana")); return false; }
	if (P->Stats->Stamina < RPGJson::Num(D, TEXT("stamina"), 0)) { Fail(TEXT("Not enough stamina")); return false; }
	if (RPGJson::Str(D, TEXT("requires")) == TEXT("shield") && RPGJson::Str(RPGJson::Obj(P->Style(), TEXT("secondary")), TEXT("type")) != TEXT("block"))
	{
		Fail(TEXT("Needs a shield (press X)"));
		return false;
	}

	P->Tags.Remove(TEXT("Hidden"));   // acting breaks stealth (Smoke Bomb re-applies it)
	if (!Run(D)) return false;
	P->OnAbilityUsed(D);
	P->Stats->Mana -= float(RPGJson::Num(D, TEXT("mana"), 0));
	if (const double St = RPGJson::Num(D, TEXT("stamina"), 0)) P->Stats->SpendStamina(float(St));
	Cooldowns.Add(Id, float(RPGJson::Num(D, TEXT("cooldown"), 1)));
	return true;
}

ARPGCharacterBase* URPGAbilityComponent::TargetNearAim(float Range) const
{
	// The opponent closest to the crosshair line, within range and line of sight.
	const ARPGPlayerCharacter* P = Player();
	const FVector Aim = P->AimDirection();
	ARPGCharacterBase* Best = nullptr;
	float BestScore = -1.f;
	for (ARPGCharacterBase* E : RPGCombat::Opponents(P))
	{
		const FVector To = E->Chest() - P->Chest();
		if (To.Size() > Range) continue;
		FHitResult H;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(AbilityTarget), false, P);
		Q.AddIgnoredActor(E);
		if (P->GetWorld()->LineTraceSingleByChannel(H, P->Chest(), E->Chest(), ECC_Visibility, Q)) continue;
		const float Score = FVector::DotProduct(Aim, To.GetSafeNormal());
		if (Score > 0.75f && Score > BestScore) { BestScore = Score; Best = E; }
	}
	return Best;
}

bool URPGAbilityComponent::Run(const RPGJson::FObj& D)
{
	ARPGPlayerCharacter* P = Player();
	UWorld* W = P->GetWorld();
	const URPGData& Data = URPGData::Get(this);
	URPGStory* Story = URPGStory::Get(P);
	const FString Type = RPGJson::Str(D, TEXT("type"));
	const FLinearColor Color = RPGJson::Color(RPGJson::Str(D, TEXT("color")), FLinearColor::White);
	auto MakeHit = [&](float Base) {
		FRPGHit H;
		H.Base = Base;
		H.Scaling = FName(RPGJson::Str(D, TEXT("scaling")));
		H.Poise = float(RPGJson::Num(D, TEXT("poise"), 0));
		H.Knockback = float(RPGJson::Num(D, TEXT("knockback"), 0));
		return H;
	};
	const FVector Ground = P->GetActorLocation() - FVector(0, 0, P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 4.f);

	if (Type == TEXT("projectile"))   // Fireball, Volley
	{
		P->FaceAim();
		if (RPGJson::Bool(D, TEXT("arrow"))) P->OnAbilityUsed(D);   // bow out before we find the muzzle
		const FVector From = P->Muzzle();
		const FVector Dir = P->AimDirection(From);
		const int32 N = int32(RPGJson::Num(D, TEXT("count"), 1));
		const float Spread = float(RPGJson::Num(D, TEXT("spread"), 0));
		const bool bArrows = RPGJson::Bool(D, TEXT("arrow"));
		const float Range = Data.Px(RPGJson::Num(D, TEXT("range"), 420));
		const FVector Land = bArrows ? P->ArrowTarget(From, Range) : FVector::ZeroVector;
		for (int32 I = 0; I < N; ++I)
		{
			const float Angle = (I - (N - 1) * 0.5f) * Spread;
			const FVector ShotDir = Dir.RotateAngleAxis(Angle, FVector::UpVector);
			if (bArrows)
			{
				// Arrows arc: fan the landing points around the aim point.
				const FVector To = From + (Land - From).RotateAngleAxis(Angle, FVector::UpVector);
				ARPGProjectile::FireArrow(P, From + ShotDir * 15.f, To, Data.Px(RPGJson::Num(D, TEXT("speed"), 400)),
					Data.Px(RPGJson::Num(D, TEXT("radius"), 6)), Color, MakeHit(float(RPGJson::Num(D, TEXT("damage"), 10))));
				continue;
			}
			ARPGProjectile::Fire(P, From + ShotDir * 15.f, ShotDir, Data.Px(RPGJson::Num(D, TEXT("speed"), 400)), Range,
				Data.Px(RPGJson::Num(D, TEXT("radius"), 6)), Color, false, MakeHit(float(RPGJson::Num(D, TEXT("damage"), 10))));
		}
		return true;
	}
	if (Type == TEXT("aoe"))   // Cleave, Frost Nova
	{
		const float R = Data.Px(RPGJson::Num(D, TEXT("radius"), 80));
		ARPGFX::Ring(W, Ground, R, Color, 0.4f);
		const RPGJson::FObj Tag = RPGJson::Obj(D, TEXT("applyTag"));
		for (ARPGCharacterBase* E : RPGCombat::Opponents(P))
		{
			if (FVector::Dist2D(E->GetActorLocation(), P->GetActorLocation()) > R + E->Radius()) continue;
			FRPGHit H = MakeHit(float(RPGJson::Num(D, TEXT("damage"), 10)));
			H.Dir = (E->GetActorLocation() - P->GetActorLocation()).GetSafeNormal2D();
			if (RPGCombat::Deal(P, E, H) && Tag) E->Tags.Add(FName(RPGJson::Str(Tag, TEXT("tag"))), float(RPGJson::Num(Tag, TEXT("duration"), 3)));
		}
		return true;
	}
	if (Type == TEXT("cone"))   // Shield Bash
	{
		P->FaceAim();
		const float Range = Data.Px(RPGJson::Num(D, TEXT("range"), 40)), Arc = float(RPGJson::Num(D, TEXT("arc"), 90));
		ARPGFX::Burst(W, P->Chest() + P->Facing() * 70.f, 45.f, Color, 0.2f);
		for (ARPGCharacterBase* E : RPGCombat::Opponents(P))
		{
			const FVector To = E->GetActorLocation() - P->GetActorLocation();
			if (To.Size2D() - E->Radius() > Range + P->Radius()) continue;
			if (FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(P->Facing(), To.GetSafeNormal2D()))) > Arc * 0.5f) continue;
			if (RPGCombat::Deal(P, E, MakeHit(float(RPGJson::Num(D, TEXT("damage"), 8)))) && !E->IsDead())
				E->Stagger(float(RPGJson::Num(D, TEXT("stagger"), 1.0)));
		}
		return true;
	}
	if (Type == TEXT("dashStrike"))   // Charge, Shadow Dash
	{
		P->FaceAim();
		const float Duration = float(RPGJson::Num(D, TEXT("duration"), 0.2));
		P->StartDash(P->AimDirection().GetSafeNormal2D(), Data.Px(RPGJson::Num(D, TEXT("distance"), 190)) / Duration, Duration, D);
		return true;
	}
	if (Type == TEXT("buff"))   // Rallying Cry, Ward
	{
		FRPGEffect E;
		E.Id = FName(RPGJson::Str(D, TEXT("name")));
		E.Name = RPGJson::Str(D, TEXT("name"));
		E.Duration = float(RPGJson::Num(D, TEXT("duration"), 8));
		if (const RPGJson::FObj Mods = RPGJson::Obj(D, TEXT("mods"))) for (const auto& KV : Mods->Values) E.Mods.Add(FName(*KV.Key), float(KV.Value->AsNumber()));
		if (RPGJson::Has(D, TEXT("absorb"))) E.Absorb = FMath::RoundToFloat(Scaled(D, float(RPGJson::Num(D, TEXT("absorb"), 30))));
		P->Stats->AddEffect(E);
		ARPGFX::Ring(W, Ground, 140.f, Color, 0.5f);
		return true;
	}
	if (Type == TEXT("blink"))   // stops at the first wall/water — can't skip the bridge
	{
		const FVector Dir = P->AimDirection().GetSafeNormal2D();
		const float Max = Data.Px(RPGJson::Num(D, TEXT("distance"), 170));
		const float R = P->Radius(), HalfH = P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		FVector Best = P->GetActorLocation();
		bool bAny = false;
		for (float S = 30.f; S <= Max; S += 30.f)
		{
			const FVector Test = P->GetActorLocation() + Dir * S;
			FCollisionQueryParams Q(SCENE_QUERY_STAT(Blink), false, P);
			if (W->OverlapAnyTestByChannel(Test, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(R, HalfH - 5.f), Q)) break;
			Best = Test; bAny = true;
		}
		if (!bAny) return false;
		ARPGFX::Burst(W, P->Chest(), 50.f, Color, 0.3f);
		P->SetActorLocation(Best, false, nullptr, ETeleportType::TeleportPhysics);
		P->SetActorRotation(Dir.Rotation());
		P->Tags.Add(TEXT("Invulnerable"), float(RPGJson::Num(D, TEXT("iframes"), 0.15)));
		ARPGFX::Burst(W, P->Chest(), 50.f, Color, 0.3f);
		return true;
	}
	if (Type == TEXT("chain"))   // Chain Lightning
	{
		ARPGCharacterBase* Target = TargetNearAim(Data.Px(RPGJson::Num(D, TEXT("range"), 320)));
		if (!Target) { Fail(TEXT("No target")); return false; }
		TArray<FVector> Points = { P->Chest() + P->Facing() * 40.f };
		TSet<ARPGCharacterBase*> Hit;
		float Base = float(RPGJson::Num(D, TEXT("damage"), 20));
		const int32 Jumps = int32(RPGJson::Num(D, TEXT("jumps"), 3));
		const float JumpRange = Data.Px(RPGJson::Num(D, TEXT("jumpRange"), 140));
		for (int32 I = 0; I <= Jumps && Target; ++I)
		{
			Hit.Add(Target);
			Points.Add(Target->Chest());
			FRPGHit H = MakeHit(Base);
			H.Knockback = 60.f;
			RPGCombat::Deal(P, Target, H);
			Base *= float(RPGJson::Num(D, TEXT("falloff"), 0.75));
			ARPGCharacterBase* From = Target;
			Target = nullptr;
			float BestD = JumpRange;
			for (ARPGCharacterBase* E : RPGCombat::Opponents(P))
			{
				const float Dd = FVector::Dist(From->GetActorLocation(), E->GetActorLocation());
				if (!Hit.Contains(E) && Dd <= BestD) { BestD = Dd; Target = E; }
			}
		}
		P->FaceAim();
		ARPGFX::Bolt(W, Points, Color);
		return true;
	}
	if (Type == TEXT("smoke"))   // Smoke Bomb
	{
		const float R = Data.Px(RPGJson::Num(D, TEXT("radius"), 120));
		const float Duration = float(RPGJson::Num(D, TEXT("duration"), 3));
		const float StaggerTime = float(RPGJson::Num(D, TEXT("stagger"), 0));
		ARPGFX::Smoke(W, Ground, R, Duration);
		P->Tags.Add(TEXT("Hidden"), Duration);
		for (ARPGCharacterBase* C : RPGCombat::Opponents(P))
		{
			ARPGEnemy* E = Cast<ARPGEnemy>(C);
			if (!E) continue;
			const float Dist = FVector::Dist2D(E->GetActorLocation(), P->GetActorLocation());
			// Caught in the blast: staggered (neutral factions are left alone - it isn't an attack).
			if (StaggerTime > 0.f && Dist < R + E->Radius() && !E->IsPassive())
			{
				E->Stagger(StaggerTime);
				URPGStory::Get(P)->Float(E->Head() + FVector(0, 0, 30), TEXT("Staggered"), FLinearColor(0.75f, 0.78f, 0.82f), 0.8f);
			}
			// Further out, anyone hunting you loses track.
			if (Dist < R * 3.f && (E->State == ERPGEnemyState::Chase || E->State == ERPGEnemyState::Windup || E->State == ERPGEnemyState::Recover))
				E->State = ERPGEnemyState::Idle;
		}
		return true;
	}
	if (Type == TEXT("weaponBuff"))   // Poison Blade
	{
		FRPGEffect E;
		E.Id = FName(RPGJson::Str(D, TEXT("name")));
		E.Name = RPGJson::Str(D, TEXT("name"));
		E.Duration = float(RPGJson::Num(D, TEXT("duration"), 8));
		E.OnHitPoison = RPGJson::Obj(D, TEXT("poison"));
		P->Stats->AddEffect(E);
		ARPGFX::Ring(W, Ground, 90.f, Color, 0.4f);
		return true;
	}
	if (Type == TEXT("heal"))   // Mend
	{
		FRPGEffect E;
		E.Id = FName(RPGJson::Str(D, TEXT("name")));
		E.Name = RPGJson::Str(D, TEXT("name"));
		E.Duration = float(RPGJson::Num(D, TEXT("duration"), 4));
		E.Period = float(RPGJson::Num(D, TEXT("period"), 0.5));
		E.Heal = Scaled(D, float(RPGJson::Num(D, TEXT("healPerTick"), 7)));
		P->Stats->AddEffect(E);
		ARPGFX::Ring(W, Ground, 110.f, Color, 0.6f);
		return true;
	}
	if (Type == TEXT("daze"))   // Silver Words
	{
		ARPGCharacterBase* T = TargetNearAim(Data.Px(RPGJson::Num(D, TEXT("range"), 260)));
		if (!T) { Fail(TEXT("No target")); return false; }
		const ARPGEnemy* E = Cast<ARPGEnemy>(T);
		if (!E || !RPGJson::Bool(E->Def, TEXT("intelligent"))) { Story->Float(T->Head(), TEXT("It doesn't understand words"), FLinearColor(0.67f, 0.67f, 0.73f), 0.8f); return false; }
		if (T->IsPassive()) { Story->Float(T->Head(), TEXT("They're not fighting you"), FLinearColor(0.67f, 0.67f, 0.73f), 0.8f); return false; }
		T->Stagger(float(RPGJson::Num(D, TEXT("daze"), 3)));
		Story->Float(T->Head() + FVector(0, 0, 40), TEXT("DAZED"), Color, 1.1f);
		ARPGFX::Bolt(W, { P->Chest(), T->Chest() }, Color);
		const FString Parley = RPGJson::Str(E->Def, TEXT("parleyDialogue"));
		if (!Parley.IsEmpty()) Story->OpenDialogue(T, Parley);   // talk, mid-fight
		return true;
	}
	if (Type == TEXT("mark"))   // Insight
	{
		ARPGCharacterBase* T = TargetNearAim(Data.Px(RPGJson::Num(D, TEXT("range"), 320)));
		if (!T) { Fail(TEXT("No target")); return false; }
		T->Tags.Add(TEXT("Marked"), float(RPGJson::Num(D, TEXT("duration"), 8)));
		T->MarkMul = float(RPGJson::Num(D, TEXT("damageTakenMul"), 1.25));
		ARPGFX::Ring(W, T->GetActorLocation() - FVector(0, 0, 85), 90.f, Color, 0.6f);
		return true;
	}
	UE_LOG(LogTemp, Warning, TEXT("[RPG] Unknown ability type %s"), *Type);
	return false;
}
