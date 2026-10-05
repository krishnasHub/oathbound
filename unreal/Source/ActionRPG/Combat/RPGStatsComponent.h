#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGStatsComponent.generated.h"

/** A timed effect: heal-over-time, poison, stat buff, damage absorb (Ward), weapon buff... */
struct FRPGEffect
{
	FName Id;
	FString Name;
	float Duration = 0, Remaining = 0, Period = 0, TickTimer = 0;
	float Heal = 0;                  // per tick
	float Dot = 0;                   // damage per tick
	float Absorb = 0;                // damage soaked before HP (Ward)
	TMap<FName, float> Mods;         // stat bonuses while active
	TSharedPtr<class FJsonObject> OnHitPoison;   // Poison Blade
	TWeakObjectPtr<AActor> Source;
};

/** Gameplay tags with optional durations (Invulnerable, Staggered, Dodging, Hidden, Slowed, Marked...). */
struct FRPGTags
{
	TMap<FName, float> Map;   // tag -> seconds left (BIG_NUMBER = until removed)

	void Add(FName Tag, float Duration = BIG_NUMBER) { float& T = Map.FindOrAdd(Tag); T = FMath::Max(T, Duration); }
	bool Has(FName Tag) const { return Map.Contains(Tag); }
	void Remove(FName Tag) { Map.Remove(Tag); }
	void Clear() { Map.Empty(); }
	void Tick(float Dt)
	{
		for (auto It = Map.CreateIterator(); It; ++It)
		{
			if (It->Value >= BIG_NUMBER * 0.5f) continue;
			It->Value -= Dt;
			if (It->Value <= 0) It.RemoveCurrent();
		}
	}
};

/**
 * Attributes, derived stats, resource pools, modifiers and timed effects — a straight port of the
 * prototype's StatsComponent, reading the same formulas from game-data.json "tuning".
 *
 * Unreal (later): this maps onto a GAS AttributeSet + GameplayEffects if/when we move to GAS.
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class ACTIONRPG_API URPGStatsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGStatsComponent();

	TMap<FName, float> Base;

	float HP = 0, Stamina = 0, Mana = 0;
	float StaminaDelay = 0;
	float StaminaRegenMul = 1.f;   // lowered while blocking
	float ManaLock = 0;            // seconds of no mana regen (mana shield shattered)
	TArray<FRPGEffect> Effects;

	float Get(FName Stat) const;
	float MaxHP() const;
	float MaxStamina() const;
	float MaxMana() const;
	float CritChance() const;
	float Armor() const { return FMath::Max(0.f, Get(TEXT("armor"))); }

	void Fill() { HP = MaxHP(); Stamina = MaxStamina(); Mana = MaxMana(); }
	void ClampPools();
	void AddModifiers(FName Source, const TMap<FName, float>& InMods);
	void RemoveModifiers(FName Source);
	bool SpendStamina(float Amount);
	void AddEffect(const FRPGEffect& E);

	/** Called by the owner each frame. Heal/DoT ticks are reported through the callbacks. */
	void TickStats(float Dt, TFunctionRef<void(float)> OnHeal, TFunctionRef<void(float, AActor*)> OnDot);

private:
	struct FMod { FName Source; FName Stat; float Value; };
	TArray<FMod> Mods;

	double Tune(const TCHAR* Key, double Default) const;
};
