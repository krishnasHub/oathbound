#include "RPGInventoryComponent.h"
#include "TSData.h"
#include "RPGCharacterBase.h"
#include "RPGCombat.h"

namespace
{
	int32 NextUid = 1;

	FString WeightedKey(const TSJson::FObj& Weights)
	{
		double Total = 0;
		for (const auto& KV : Weights->Values) Total += KV.Value->AsNumber();
		double R = FMath::FRand() * Total;
		for (const auto& KV : Weights->Values) { R -= KV.Value->AsNumber(); if (R <= 0) return FString(*KV.Key); }
		return FString(*Weights->Values.CreateConstIterator().Key());
	}
}

FRPGItem URPGInventoryComponent::MakeItem(const UObject* Ctx, const FString& Id, const TSJson::FObj& RarityWeights)
{
	const UTSData& D = UTSData::Get(Ctx);
	const TSJson::FObj B = D.Entry(TEXT("items"), Id);
	FRPGItem It;
	It.Uid = NextUid++;
	It.Id = Id;
	It.Name = TSJson::Str(B, TEXT("name"), Id);
	It.Type = TSJson::Str(B, TEXT("type"));
	It.Slot = TSJson::Str(B, TEXT("slot"));
	It.bStackable = TSJson::Bool(B, TEXT("stackable"));
	It.Rarity = It.Type == TEXT("quest") ? TEXT("quest") : TEXT("common");
	if (const TSJson::FObj Mods = TSJson::Obj(B, TEXT("mods"))) for (const auto& KV : Mods->Values) It.Mods.Add(FName(*KV.Key), float(KV.Value->AsNumber()));

	if (It.Type == TEXT("equipment"))
	{
		FString Rarity = RarityWeights.IsValid() && RarityWeights->Values.Num() ? WeightedKey(RarityWeights) : TEXT("common");
		int32 N = int32(TSJson::Num(D.Entry(TEXT("rarities"), Rarity), TEXT("affixes"), 0));
		const int32 MinAffixes = int32(TSJson::Num(B, TEXT("minAffixes"), 0));
		if (N < MinAffixes) { N = MinAffixes; Rarity = N >= 2 ? TEXT("rare") : TEXT("magic"); }
		It.Rarity = Rarity;

		TArray<TSharedPtr<FJsonValue>> Pool = TSJson::Arr(D.Root, TEXT("affixes"));
		FString FirstLabel;
		for (int32 I = 0; I < N && Pool.Num(); ++I)
		{
			const TSJson::FObj A = Pool[FMath::RandRange(0, Pool.Num() - 1)]->AsObject();
			Pool.RemoveAll([&](const TSharedPtr<FJsonValue>& V) { return V->AsObject() == A; });
			It.Mods.FindOrAdd(FName(TSJson::Str(A, TEXT("stat")))) += float(FMath::RandRange(int32(TSJson::Num(A, TEXT("min"))), int32(TSJson::Num(A, TEXT("max")))));
			if (FirstLabel.IsEmpty()) FirstLabel = TSJson::Str(A, TEXT("label"));
		}
		if (!FirstLabel.IsEmpty()) It.Name += TEXT(" ") + FirstLabel;
		if (TSJson::Has(B, TEXT("rarity"))) It.Rarity = TSJson::Str(B, TEXT("rarity"));   // uniques
	}
	return It;
}

FLinearColor URPGInventoryComponent::RarityColor(const UObject* Ctx, const FString& Rarity)
{
	if (Rarity == TEXT("quest")) return TSJson::Color(TEXT("#ff9a3d"));
	return TSJson::Color(TSJson::Str(UTSData::Get(Ctx).Entry(TEXT("rarities"), Rarity), TEXT("color"), TEXT("#dddddd")));
}

bool URPGInventoryComponent::Add(const FRPGItem& Item)
{
	if (Item.bStackable)
	{
		for (FRPGItem& I : Items) if (I.Id == Item.Id) { I.Qty += Item.Qty; Changed(); return true; }
	}
	if (Items.Num() >= Capacity) return false;
	Items.Add(Item);
	Changed();
	return true;
}

int32 URPGInventoryComponent::Count(const FString& Id) const
{
	int32 N = 0;
	for (const FRPGItem& I : Items) if (I.Id == Id) N += I.Qty;
	return N;
}

void URPGInventoryComponent::Remove(const FString& Id, int32 Qty)
{
	for (int32 I = Items.Num() - 1; I >= 0 && Qty > 0; --I)
	{
		if (Items[I].Id != Id) continue;
		const int32 Take = FMath::Min(Qty, Items[I].Qty);
		Items[I].Qty -= Take;
		Qty -= Take;
		if (Items[I].Qty <= 0) Items.RemoveAt(I);
	}
	Changed();
}

FRPGItem* URPGInventoryComponent::Find(int32 Uid)
{
	return Items.FindByPredicate([Uid](const FRPGItem& I) { return I.Uid == Uid; });
}

void URPGInventoryComponent::Equip(int32 Uid)
{
	const int32 Index = Items.IndexOfByPredicate([Uid](const FRPGItem& I) { return I.Uid == Uid; });
	if (Index == INDEX_NONE) return;
	const FRPGItem It = Items[Index];
	Items.RemoveAt(Index);
	ARPGCharacterBase* Owner = Cast<ARPGCharacterBase>(GetOwner());
	if (FRPGItem* Prev = Equipment.Find(It.Slot))
	{
		Items.Add(*Prev);
		if (Owner) Owner->Stats->RemoveModifiers(FName(It.Slot));
	}
	Equipment.Add(It.Slot, It);
	if (Owner) Owner->Stats->AddModifiers(FName(It.Slot), It.Mods);
	Changed();
}

void URPGInventoryComponent::Unequip(const FString& Slot)
{
	FRPGItem* It = Equipment.Find(Slot);
	if (!It || Items.Num() >= Capacity) return;
	Items.Add(*It);
	Equipment.Remove(Slot);
	if (ARPGCharacterBase* Owner = Cast<ARPGCharacterBase>(GetOwner())) Owner->Stats->RemoveModifiers(FName(Slot));
	Changed();
}

bool URPGInventoryComponent::Use(int32 Uid)
{
	FRPGItem* It = Find(Uid);
	if (!It) return false;
	if (It->Type == TEXT("equipment")) { Equip(Uid); return true; }
	if (It->Type == TEXT("consumable"))
	{
		ARPGCharacterBase* Owner = Cast<ARPGCharacterBase>(GetOwner());
		const double HealAmt = TSJson::Num(UTSData::Get(this).Entry(TEXT("items"), It->Id), TEXT("heal"), 0);
		if (Owner && HealAmt > 0)
		{
			if (Owner->Stats->HP >= Owner->Stats->MaxHP()) return false;
			RPGCombat::Heal(Owner, float(HealAmt));
		}
		Remove(It->Id, 1);
		return true;
	}
	return false;
}

void URPGInventoryComponent::Salvage(int32 Uid)
{
	FRPGItem* It = Find(Uid);
	if (!It || It->Type == TEXT("quest")) return;
	Gold += int32(TSJson::Num(UTSData::Get(this).Entry(TEXT("rarities"), It->Rarity), TEXT("salvage"), 2)) * It->Qty;
	Items.RemoveAll([Uid](const FRPGItem& I) { return I.Uid == Uid; });
	Changed();
}
