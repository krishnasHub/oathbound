#pragma once

#include "CoreMinimal.h"
#include "RPGCharacterBase.h"
#include "RPGNPC.generated.h"

/** A villager you can talk to (game-data.json "npcs"). Turns to face the player when they come near. */
UCLASS()
class OATHBOUND_API ARPGNPC : public ARPGCharacterBase
{
	GENERATED_BODY()

public:
	ARPGNPC();
	void Init(const FString& NpcId);
	FString NpcId;

protected:
	virtual void Tick(float DeltaSeconds) override;
};
