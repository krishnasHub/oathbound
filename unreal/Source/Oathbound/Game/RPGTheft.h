#pragma once

#include "CoreMinimal.h"
#include "TSJson.h"

class ATSCharacter;
class ARPGPlayerCharacter;

/**
 * The Thief's sticky fingers (TODO.md, Thief plan). Anyone with "pockets" (enemies.<id> / npcs.<id>) can be robbed, once:
 *
 *   who        the Thief, crouched (Space), within reach, unseen: asleep, or out of its sight cone and too quiet to hear
 *   how        E: a short lift (Tessera UTSChannel; pockets.time); stay put and unseen until it's done
 *   what       pockets { gold: [min, max], items: [ { item, chance } ], hint, time, level: { min, asleep },
 *              do: [ Loom actions run on success (resolve an encounter, scatter a band...) ], villager: true }
 *   caught     it notices mid-lift: "Thief!", it turns on you (a foe and its faction), the world's mood drops
 *              (tuning.moodCaught; villagers moodCaughtVillager, and word gets round: prices rise)
 *   unseen     foes: no mood change; villagers: tuning.moodTheftVillager
 */
namespace RPGTheft
{
	/** Its pockets (null: nothing to take). */
	TSJson::FObj Pockets(const ATSCharacter* C);
	/** The story key that marks these pockets emptied. */
	FString PocketKey(const ATSCharacter* C);
	/** Would C notice the hero right now (its own senses; asleep: only right beside it)? */
	bool Notices(const ATSCharacter* C, const ATSCharacter* Hero);
	/** How close the hero must be (uu, centre to centre). */
	float Reach(const ATSCharacter* Hero, const ATSCharacter* C);
	/** The nearest character with pockets left to empty, within reach (whether or not it can be robbed right now). */
	ATSCharacter* Target(const ARPGPlayerCharacter* P);
	/** Why it can't be robbed now ("" = it can). */
	FString Why(const ARPGPlayerCharacter* P, const ATSCharacter* C, bool bIgnoreReach = false);
	/** Its pockets' hint ("a heavy purse"). */
	FString Hint(const ATSCharacter* C);
	/** E while crouched: start lifting from C (floats why not). */
	void Begin(ARPGPlayerCharacter* P, ATSCharacter* C);
	/** Caught in the act. */
	void Caught(ARPGPlayerCharacter* P, ATSCharacter* C);
}
