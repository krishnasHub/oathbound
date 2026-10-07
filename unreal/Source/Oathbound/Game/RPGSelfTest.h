#pragma once

#include "CoreMinimal.h"
#include "TSTestRunner.h"
#include "RPGSelfTest.generated.h"

class ARPGPlayerCharacter;
class ARPGEnemy;
class ARPGCharacterBase;

/**
 * Scripted scenarios that drive the real game (real animation notifies, AI, dialogue, UI) and log the
 * results, so behaviour can be verified without someone at the keyboard.
 *
 *   -RPGTest=combat   knight vs a slime: combo until it dies, report XP / loot / quest progress
 *   -RPGTest=bridge   knight walks up to Brask (auto parley), wins the honor check, duels him to a yield
 *   -RPGTest=mage     mage vs an archer: arcane bolts, mana shield absorbing arrows
 *   -RPGTest=thief    thief: bow draw/release and a backstab on an unaware slime
 *   -RPGTest=walk     top-down click-to-move: path around a cottage, click-to-talk, click-to-attack
 *   -RPGTest=picker   Shift+wheel ability picker (slow motion, cycle, cast); talk mode vs click-to-fight
 *   -RPGTest=smoke    thief Smoke Bomb: foes in the blast stagger, the cloud clears after its duration
 *   -RPGTest=pause    Esc: picker first, then the pause menu; Resume; New Game reloads into character select
 *   -RPGTest=click    one real mouse click on a dialogue choice answers it (no double click)
 *   -RPGTest=frost    mage Frost Nova: a slime and a villager in the sphere freeze, stay put, then thaw
 *   -RPGTest=barrier  mage barrier: a slime closing in is held at the barrier's edge, never inside
 *
 * Combine with -RPGShot=<sec> to capture a frame mid-scenario. Stepping, reporting, quitting, screenshots and real
 * clicks come from Tessera's ATSTestRunner (TesseraTest).
 */
UCLASS()
class OATHBOUND_API ARPGSelfTest : public ATSTestRunner
{
	GENERATED_BODY()

protected:
	virtual void RunStep() override;

private:
	ARPGPlayerCharacter* P() const;
	ARPGEnemy* Find(const FString& Type) const;
	void Place(const FVector& At, float Yaw);
	void AimAt(const FVector& Point);

	TWeakObjectPtr<ARPGEnemy> Target;
	int32 Swings = 0;
	int32 Undrawn = 0;                        // trader: looks at Grot where he wasn't drawn
	FVector WalkGoal = FVector::ZeroVector;   // walk: where the ground click went
	int32 Corners = 0;
	float Started = 0.f;
	FString ClickNode;   // click: the dialogue line before the click
	TWeakObjectPtr<ARPGCharacterBase> Other;   // frost: the villager caught in the nova
	FVector HeldAt = FVector::ZeroVector;      // frost: where the frozen slime stood
	float MinDist = 0.f;                       // barrier: the closest the slime got
	bool bShotTaken = false;
};
