#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TSJson.h"
#include "RPGInventoryComponent.generated.h"

/** One item instance (prototype: makeItem()). */
struct FRPGItem
{
	int32 Uid = 0;
	FString Id, Name, Type, Slot, Rarity = TEXT("common");
	TMap<FName, float> Mods;
	int32 Qty = 1;
	bool bStackable = false;
};

DECLARE_MULTICAST_DELEGATE(FRPGInventoryChanged);

/**
 * Bag, equipment slots and gold. Equipping pushes the item's mods into the owner's stats.
 * Prototype equivalent: InventoryComponent.
 */
UCLASS(ClassGroup = (RPG))
class ACTIONRPG_API URPGInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	TArray<FRPGItem> Items;
	TMap<FString, FRPGItem> Equipment;   // "weapon" | "armor" | "trinket"
	int32 Gold = 0;
	int32 Capacity = 16;

	FRPGInventoryChanged OnChanged;

	/** Rolls an item: rarity from weights (or common), random affixes per rarity. */
	static FRPGItem MakeItem(const UObject* WorldContext, const FString& Id, const TSJson::FObj& RarityWeights = nullptr);
	static FLinearColor RarityColor(const UObject* WorldContext, const FString& Rarity);

	bool Add(const FRPGItem& Item);
	int32 Count(const FString& Id) const;
	void Remove(const FString& Id, int32 Qty);
	FRPGItem* Find(int32 Uid);
	void Equip(int32 Uid);
	void Unequip(const FString& Slot);
	/** Equipment -> equip, potion -> drink. Returns false if it could not be used. */
	bool Use(int32 Uid);
	void Salvage(int32 Uid);

private:
	void Changed() { OnChanged.Broadcast(); }
};
