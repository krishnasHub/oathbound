#pragma once

#include "CoreMinimal.h"
#include "RPGCharacterBase.h"
#include "RPGNPC.generated.h"

class UTSSleep;

/**
 * A villager you can talk to (game-data.json "npcs"). Turns to face the player when they come near.
 * Hours ("sleeps", "house"): at bedtime it walks to its cottage door and goes in to bed (Tessera UTSSleep); while it
 * sleeps the house can be peeked into (cut away as the hero passes), and only someone inside can reach it. In the
 * morning it comes out and walks back to its spot.
 */
UCLASS()
class OATHBOUND_API ARPGNPC : public ARPGCharacterBase
{
	GENERATED_BODY()

public:
	ARPGNPC();
	void Init(const FString& NpcId);
	FString NpcId;
	UTSSleep* GetSleep() const { return Sleep; }

protected:
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY() TObjectPtr<UTSSleep> Sleep;
	FVector Home = FVector::ZeroVector;
	int32 HouseCutaway = INDEX_NONE;
};
