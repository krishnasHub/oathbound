#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGInventoryComponent.h"
#include "RPGLoot.generated.h"

class ARPGEnemy;
class UStaticMeshComponent;
class UPointLightComponent;

/**
 * Loot on the ground: gold coins (pulled toward you when close) or an item gem coloured by rarity,
 * with a light beam for rare and quest items. Walk over it to pick it up.
 */
UCLASS()
class ACTIONRPG_API ARPGPickup : public AActor
{
	GENERATED_BODY()

public:
	ARPGPickup();
	void InitGold(int32 Amount);
	void InitItem(const FRPGItem& Item);
	virtual void Tick(float DeltaSeconds) override;

	bool bGold = false;
	int32 Amount = 0;
	FRPGItem Item;

private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Gem;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Beam;
	UPROPERTY() TObjectPtr<UPointLightComponent> Light;
	float Age = 0.f, BaseZ = 0.f, Warned = 0.f;
};

/** Loot tables (prototype: Loot.drop / Loot.dropTable). */
namespace RPGLoot
{
	void DropTable(UWorld* World, const FVector& At, const FString& TableId);
	void Drop(ARPGEnemy* Enemy);
	void Spawn(UWorld* World, const FVector& At, const FRPGItem* Item, int32 Gold);
}
