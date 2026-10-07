#include "RPGStatsComponent.h"
#include "TSData.h"

URPGStatsComponent::URPGStatsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;   // ticked by the owning character
}

double URPGStatsComponent::Tune(const TCHAR* Key, double Default) const
{
	return UTSData::Get(this).Tuning(Key, Default);
}

float URPGStatsComponent::Get(FName Stat) const
{
	float V = Base.FindRef(Stat);
	for (const FMod& M : Mods) if (M.Stat == Stat) V += M.Value;
	for (const FRPGEffect& E : Effects) if (const float* B = E.Mods.Find(Stat)) V += *B;
	return V;
}

float URPGStatsComponent::MaxHP() const
{
	return FMath::RoundToFloat(Get(TEXT("hpFlat")) + Get(TEXT("vitality")) * Tune(TEXT("hpPerVitality"), 10) + Get(TEXT("level")) * Tune(TEXT("hpPerLevel"), 5));
}

float URPGStatsComponent::MaxStamina() const
{
	return float(Tune(TEXT("staminaBase"), 80) + Get(TEXT("agility")) * Tune(TEXT("staminaPerAgility"), 4));
}

float URPGStatsComponent::MaxMana() const
{
	return float(Get(TEXT("manaFlat")) + Get(TEXT("focus")) * Tune(TEXT("manaPerFocus"), 5));
}

float URPGStatsComponent::CritChance() const
{
	return FMath::Max(0.f, Get(TEXT("critPct")) + Get(TEXT("agility")) * float(Tune(TEXT("critPctPerAgility"), 1))) / 100.f;
}

void URPGStatsComponent::ClampPools()
{
	HP = FMath::Min(HP, MaxHP());
	Stamina = FMath::Min(Stamina, MaxStamina());
	Mana = FMath::Min(Mana, MaxMana());
}

void URPGStatsComponent::AddModifiers(FName Source, const TMap<FName, float>& InMods)
{
	for (const auto& KV : InMods) Mods.Add({ Source, KV.Key, KV.Value });
	ClampPools();
}

void URPGStatsComponent::RemoveModifiers(FName Source)
{
	Mods.RemoveAll([&](const FMod& M) { return M.Source == Source; });
	ClampPools();
}

bool URPGStatsComponent::SpendStamina(float Amount)
{
	if (Stamina < Amount) return false;
	Stamina -= Amount;
	StaminaDelay = float(Tune(TEXT("staminaRegenDelay"), 0.55));
	return true;
}

void URPGStatsComponent::AddEffect(const FRPGEffect& E)
{
	Effects.RemoveAll([&](const FRPGEffect& X) { return X.Id == E.Id; });
	FRPGEffect Copy = E;
	Copy.Remaining = E.Duration;
	Copy.TickTimer = E.Period;
	Effects.Add(Copy);
}

void URPGStatsComponent::TickStats(float Dt, TFunctionRef<void(float)> OnHeal, TFunctionRef<void(float, AActor*)> OnDot)
{
	if (StaminaDelay > 0) StaminaDelay -= Dt;
	else Stamina = FMath::Min(MaxStamina(), Stamina + float(Tune(TEXT("staminaRegen"), 38)) * StaminaRegenMul * Dt);

	if (ManaLock > 0) ManaLock -= Dt;
	else
	{
		const float* Regen = Base.Find(TEXT("manaRegen"));
		Mana = FMath::Min(MaxMana(), Mana + (Regen ? *Regen : float(Tune(TEXT("manaRegen"), 2.5))) * Dt);
	}

	for (FRPGEffect& E : Effects)
	{
		E.Remaining -= Dt;
		if (E.Period > 0)
		{
			E.TickTimer -= Dt;
			while (E.TickTimer <= 0)
			{
				E.TickTimer += E.Period;
				if (E.Heal > 0) OnHeal(E.Heal);
				if (E.Dot > 0) OnDot(E.Dot, E.Source.Get());
			}
		}
	}
	Effects.RemoveAll([](const FRPGEffect& E) { return E.Remaining <= 0; });
	ClampPools();
}
