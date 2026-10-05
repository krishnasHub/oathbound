#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RPGGameMode.generated.h"

class ARPGWorldBuilder;

/**
 * Builds the world from game-data.json, places the player at the map's 'P' marker, and runs the
 * command-line self-test switches (so the game can be verified without someone at the keyboard):
 *
 *   -RPGShot=<sec>          screenshot to Saved/Screenshots/RPG/<name>.png at <sec>, then quit
 *   -RPGShotName=<name>     file name for -RPGShot (default "shot")
 *   -RPGCam=X,Y,Z,Pitch,Yaw view from a fixed camera instead of the player's
 *   -RPGClass=<id> -RPGSex=<male|female>   skip character select with this hero
 *   -RPGProbe               log the parameters exposed by the materials we tint at runtime
 *   -RPGQuitAfter=<sec>     exit after <sec> (logic-only runs with -nullrhi)
 */
UCLASS()
class ACTIONRPG_API ARPGGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARPGGameMode();

	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override;

	UPROPERTY() TObjectPtr<ARPGWorldBuilder> WorldBuilder;

private:
	void SpawnCharacters();
	void RunSelfTests();
	void Probe();
};
