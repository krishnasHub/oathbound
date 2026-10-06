#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGSelfTest.generated.h"

class ARPGPlayerCharacter;
class ARPGEnemy;

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
 *
 * Combine with -RPGShot=<sec> to capture a frame mid-scenario.
 */
UCLASS()
class ACTIONRPG_API ARPGSelfTest : public AActor
{
	GENERATED_BODY()

public:
	ARPGSelfTest();
	FString Scenario;
	virtual void Tick(float DeltaSeconds) override;

private:
	ARPGPlayerCharacter* P() const;
	ARPGEnemy* Find(const FString& Type) const;
	void Place(const FVector& At, float Yaw);
	void AimAt(const FVector& Point);
	void Report(const FString& Line);

	float T = 0.f;
	int32 Step = 0;
	float Next = 0.f;
	TWeakObjectPtr<ARPGEnemy> Target;
	int32 Swings = 0;
	FVector WalkGoal = FVector::ZeroVector;   // walk: where the ground click went
	int32 Corners = 0;
	float Started = 0.f;
	FString ClickNode;   // click: the dialogue line before the click
};
