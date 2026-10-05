#pragma once

#include "CoreMinimal.h"

class ARPGCharacterBase;
class URPGStatsComponent;
class AActor;
class UObject;

/** One hit. Damage numbers are game-data units; Knockback is in prototype px/s (converted inside). */
struct FRPGHit
{
	float Base = 0.f;
	FName Scaling;                       // "might" | "agility" | "focus" | none
	float Poise = 0.f;
	float Knockback = 0.f;
	TOptional<FVector> Dir;              // knockback direction (default: away from the source)
	TOptional<FVector> From;             // where the hit comes from, for blocking (default: the source)
	bool bIgnoreDuelYield = false;
};

/**
 * The damage pipeline — same order of operations as the prototype's dealDamage():
 * hostility -> damage roll (scaling, crit, armor, Insight mark, variance) -> block / perfect block ->
 * Ward absorb -> mana shield -> HP -> duel yield -> knockback -> provoke/alert -> poise/stagger -> death.
 *
 * Unreal (later): a GameplayEffectExecutionCalculation if we move to GAS.
 */
namespace RPGCombat
{
	/** Returns true if the hit connected (including blocked/absorbed), false if it passed through (i-frames). */
	bool Deal(ARPGCharacterBase* Src, ARPGCharacterBase* Target, const FRPGHit& Hit);
	void Heal(ARPGCharacterBase* Target, float Amount);
	/** Damage over time: no crit/armor/poise; can't finish a duel opponent. */
	void Dot(ARPGCharacterBase* Target, float Amount, AActor* Src);

	float ScaleBy(const URPGStatsComponent* Stats, FName Stat);
	/** Live opponents of a character (player -> enemies not leaving; enemies -> the player). */
	TArray<ARPGCharacterBase*> Opponents(const ARPGCharacterBase* Of);
}
