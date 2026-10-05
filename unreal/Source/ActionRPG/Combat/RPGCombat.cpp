#include "RPGCombat.h"
#include "RPGData.h"
#include "RPGAssets.h"
#include "RPGStory.h"
#include "RPGCharacterBase.h"
#include "RPGPlayerCharacter.h"
#include "RPGEnemy.h"

#include "EngineUtils.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace
{
	float Angle2D(const FVector& Forward, const FVector& To)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward.GetSafeNormal2D(), To.GetSafeNormal2D()), -1.f, 1.f)));
	}
}

float RPGCombat::ScaleBy(const URPGStatsComponent* Stats, FName Stat)
{
	if (Stat.IsNone() || !Stats) return 1.f;
	const URPGData& D = URPGData::Get(Stats);
	const double PerPoint = Stat == TEXT("might") ? D.Tuning(TEXT("mightScaling"), 0.06)
	                      : Stat == TEXT("agility") ? D.Tuning(TEXT("agilityScaling"), 0.06)
	                      : Stat == TEXT("focus") ? D.Tuning(TEXT("focusScaling"), 0.07) : 0.0;
	return 1.f + Stats->Get(Stat) * float(PerPoint);
}

TArray<ARPGCharacterBase*> RPGCombat::Opponents(const ARPGCharacterBase* Of)
{
	TArray<ARPGCharacterBase*> Out;
	if (!Of) return Out;
	for (TActorIterator<ARPGCharacterBase> It(Of->GetWorld()); It; ++It)
	{
		ARPGCharacterBase* C = *It;
		if (C == Of || C->IsDead() || C->IsLeaving() || C->Team == ERPGTeam::Villager || C->Team == Of->Team) continue;
		Out.Add(C);
	}
	return Out;
}

void RPGCombat::Heal(ARPGCharacterBase* Target, float Amount)
{
	if (!Target || Target->IsDead()) return;
	const float Before = Target->Stats->HP;
	Target->Stats->HP = FMath::Min(Target->Stats->MaxHP(), Target->Stats->HP + Amount);
	const int32 Gained = FMath::RoundToInt(Target->Stats->HP - Before);
	if (Gained > 0) URPGStory::Get(Target)->Float(Target->Head(), FString::Printf(TEXT("+%d"), Gained), FLinearColor(0.37f, 0.88f, 0.54f), 0.9f);
}

void RPGCombat::Dot(ARPGCharacterBase* Target, float Amount, AActor* Src)
{
	if (!Target || Target->IsDead()) return;
	URPGStory* Story = URPGStory::Get(Target);
	int32 N = FMath::Max(1, FMath::RoundToInt(Amount));
	if (Story->Duel.Get() == Target) N = FMath::Min(N, FMath::Max(0, FMath::FloorToInt(Target->Stats->HP - Target->Stats->MaxHP() * 0.2f - 1.f)));
	if (N <= 0) return;
	Target->Stats->HP -= N;
	Story->Float(Target->Head(), FString::FromInt(N), FLinearColor(0.5f, 0.82f, 0.5f), 0.8f);
	if (Target->Stats->HP <= 0) { Target->Stats->HP = 0; Target->Die(Src); }
}

bool RPGCombat::Deal(ARPGCharacterBase* Src, ARPGCharacterBase* Target, const FRPGHit& Hit)
{
	if (!Src || !Target || Target->IsDead() || Target->Tags.Has(TEXT("Invulnerable"))) return false;
	const URPGData& D = URPGData::Get(Target);
	URPGStory* Story = URPGStory::Get(Target);
	const bool bTargetIsPlayer = Target->Team == ERPGTeam::Player;
	const FVector TextAt = Target->Head() + FVector(0, 0, 20);

	// Striking a neutral faction member (or a bystander during a duel) turns the whole faction hostile.
	if (Src->Team == ERPGTeam::Player && !Target->FactionId().IsEmpty() && Target->IsPassive())
	{
		Story->SetHostile(Target->FactionId(), Story->Duel.IsValid() ? TEXT("You broke the duel! The Red Hands attack!") : TEXT("You attacked the Red Hands!"));
	}

	// --- damage roll ---
	float Dmg = Hit.Base * ScaleBy(Src->Stats, Hit.Scaling);
	bool bCrit = false;
	if (FMath::FRand() < Src->Stats->CritChance()) { Dmg *= float(D.Tuning(TEXT("critMultiplier"), 1.6)); bCrit = true; }
	const float K = float(D.Tuning(TEXT("armorConstant"), 100));
	Dmg *= K / (K + Target->Stats->Armor());
	if (Target->Tags.Has(TEXT("Marked"))) Dmg *= Target->MarkMul;
	const float Var = float(D.Tuning(TEXT("damageVariance"), 0.1));
	Dmg *= FMath::FRandRange(1.f - Var, 1.f + Var);
	int32 Amount = FMath::Max(1, FMath::RoundToInt(Dmg));
	float Poise = Hit.Poise, Knock = Hit.Knockback;

	// --- block / perfect block ---
	if (const RPGJson::FObj Guard = Target->GuardStyle())
	{
		const FVector From = Hit.From.IsSet() ? Hit.From.GetValue() : Src->GetActorLocation();
		if (Angle2D(Target->GetActorForwardVector(), From - Target->GetActorLocation()) <= RPGJson::Num(Guard, TEXT("arc"), 120) * 0.5)
		{
			const float Perfect = float(RPGJson::Num(Guard, TEXT("perfectWindow"), 0));
			if (Perfect > 0.f && Target->GuardTime <= Perfect)
			{
				Story->Float(TextAt + FVector(0, 0, 20), TEXT("PERFECT BLOCK"), FLinearColor::White, 1.1f);
				if (Src != Target && !Src->IsDead() && FVector::Dist2D(Src->GetActorLocation(), Target->GetActorLocation()) < D.Px(140))
				{
					Src->Stagger(float(D.Tuning(TEXT("staggerTime"), 0.55)) * 1.8f);
					Story->Float(Src->Head() + FVector(0, 0, 40), TEXT("STAGGER"), FLinearColor(0.56f, 0.82f, 1.f), 0.9f);
				}
				Story->Shake(3.f);
				return true;
			}
			const float Cost = Amount * float(RPGJson::Num(Guard, TEXT("staminaPerDamage"), 0.8));
			if (Target->Stats->Stamina >= Cost)
			{
				Target->Stats->Stamina -= Cost;
				Target->Stats->StaminaDelay = float(D.Tuning(TEXT("staminaRegenDelay"), 0.55));
				Amount = FMath::RoundToInt(Amount * (1.f - float(RPGJson::Num(Guard, TEXT("reduction"), 0.7))));
				Poise *= 0.3f; Knock *= 0.3f;
				Story->Float(TextAt + FVector(0, 0, 25), TEXT("BLOCK"), FLinearColor(0.78f, 0.83f, 0.88f), 0.8f);
			}
			else
			{
				Target->Stats->Stamina = 0;
				Target->Stagger(float(D.Tuning(TEXT("staggerTime"), 0.55)) * 1.5f);
				Story->Float(TextAt + FVector(0, 0, 30), TEXT("GUARD BREAK"), FLinearColor(1.f, 0.6f, 0.35f), 1.1f);
			}
		}
	}

	// --- absorb effects (Ward) ---
	for (FRPGEffect& E : Target->Stats->Effects)
	{
		if (E.Absorb <= 0.f || Amount <= 0) continue;
		const int32 Soaked = FMath::Min(int32(E.Absorb), Amount);
		E.Absorb -= Soaked; Amount -= Soaked;
		Story->Float(TextAt, FString::Printf(TEXT("(%d)"), Soaked), FLinearColor(1.f, 0.91f, 0.66f), 0.9f);
	}

	// --- mana shield ---
	if (const RPGJson::FObj MS = Target->PassiveStyle())
	{
		if (RPGJson::Str(MS, TEXT("type")) == TEXT("manaShield") && Amount > 0 && Target->Stats->Mana > 0)
		{
			const float PerMana = float(RPGJson::Num(MS, TEXT("damagePerMana"), 1));
			const int32 Absorbed = FMath::Min(Amount, FMath::FloorToInt(Target->Stats->Mana * PerMana));
			Target->Stats->Mana -= Absorbed / PerMana;
			Amount -= Absorbed;
			if (Absorbed > 0) Story->Float(TextAt, FString::Printf(TEXT("(%d)"), Absorbed), FLinearColor(0.62f, 0.72f, 1.f), 1.f);
			if (Amount == 0) Poise *= 0.5f;
			if (Target->Stats->Mana < 1.f)
			{
				Target->Stats->Mana = 0;
				Target->Stats->ManaLock = float(RPGJson::Num(MS, TEXT("breakRegenLock"), 3));
				Target->Stagger(float(RPGJson::Num(MS, TEXT("breakStagger"), 0.6)));
				Story->Float(TextAt + FVector(0, 0, 30), TEXT("SHIELD SHATTERED"), FLinearColor(0.62f, 0.72f, 1.f), 1.1f);
			}
		}
	}

	// --- HP ---
	Target->Flash();
	if (Amount > 0)
	{
		Target->Stats->HP -= Amount;
		const FLinearColor C = bTargetIsPlayer ? FLinearColor(1.f, 0.35f, 0.35f) : (bCrit ? FLinearColor(1.f, 0.83f, 0.3f) : FLinearColor::White);
		Story->Float(TextAt, bCrit ? FString::Printf(TEXT("%d!"), Amount) : FString::FromInt(Amount), C, bCrit ? 1.35f : 1.f);
	}
	if (UNiagaraSystem* FX = RPGAssets::Load<UNiagaraSystem>(RPGAssets::DamageFX))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(Target->GetWorld(), FX, Target->Chest(), FRotator::ZeroRotator, FVector(0.6f));
	}

	// --- duel opponents yield instead of dying ---
	if (Story->Duel.Get() == Target && !Hit.bIgnoreDuelYield && Target->Stats->HP <= Target->Stats->MaxHP() * 0.2f)
	{
		Target->Stats->HP = FMath::Max(1.f, FMath::RoundToFloat(Target->Stats->HP));
		Story->Duel = nullptr;
		Target->Tags.Clear();
		Story->OpenDialogue(Target, Target->YieldDialogueId());
		return true;
	}

	// --- knockback ---
	if (Knock > 0.f)
	{
		FVector Dir = Hit.Dir.IsSet() ? Hit.Dir.GetValue() : (Target->GetActorLocation() - Src->GetActorLocation());
		Dir = Dir.GetSafeNormal2D();
		const ARPGEnemy* E = Cast<ARPGEnemy>(Target);
		const float Mul = E ? float(RPGJson::Num(E->Def, TEXT("knockbackMul"), 1.0)) : 1.f;
		Target->Knock(Dir * D.Px(Knock) * Mul);
	}

	// --- provoke / alert, poise, stagger ---
	Target->OnDamaged(Src);
	Target->Poise -= Poise;
	Target->PoiseTimer = float(D.Tuning(TEXT("poiseRegenDelay"), 2));
	if (Target->Poise <= 0.f)
	{
		Target->Poise = Target->MaxPoise;
		Target->Stagger(float(D.Tuning(TEXT("staggerTime"), 0.55)));
		Story->Float(TextAt + FVector(0, 0, 35), TEXT("STAGGER"), FLinearColor(0.56f, 0.82f, 1.f), 0.9f);
	}
	if (bTargetIsPlayer)
	{
		Target->Tags.Add(TEXT("Invulnerable"), float(D.Tuning(TEXT("playerHitIframes"), 0.45)));
		Story->Shake(7.f);
	}
	else Story->Shake(bCrit ? 3.f : 1.5f);

	if (Target->Stats->HP <= 0.f) { Target->Stats->HP = 0; Target->Die(Src); }
	return true;
}
