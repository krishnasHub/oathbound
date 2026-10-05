#include "RPGLoot.h"
#include "RPGData.h"
#include "RPGAssets.h"
#include "RPGEnemy.h"
#include "RPGStory.h"
#include "RPGPlayerCharacter.h"
#include "RPGWorldBuilder.h"
#include "RPGGameMode.h"

#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"

ARPGPickup::ARPGPickup()
{
	PrimaryActorTick.bCanEverTick = true;
	Gem = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Gem"));
	Gem->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = Gem;
	Beam = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Beam"));
	Beam->SetupAttachment(Gem);
	Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beam->SetCastShadow(false);
	Beam->SetVisibility(false);
	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Gem);
	Light->SetCastShadows(false);
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetIntensity(12.f);
	Light->SetAttenuationRadius(300.f);
}

void ARPGPickup::InitGold(int32 InAmount)
{
	bGold = true;
	Amount = InAmount;
	Gem->SetStaticMesh(RPGAssets::Shape(TEXT("Cylinder")));
	Gem->SetMaterial(0, RPGAssets::StarterMat(TEXT("M_Metal_Gold")));
	Gem->SetWorldScale3D(FVector(0.22f, 0.22f, 0.06f));
	Light->SetLightColor(FLinearColor(1.f, 0.8f, 0.3f));
	Light->SetIntensity(4.f);
	BaseZ = GetActorLocation().Z;
}

void ARPGPickup::InitItem(const FRPGItem& InItem)
{
	Item = InItem;
	const FLinearColor C = URPGInventoryComponent::RarityColor(this, Item.Rarity);
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(RPGAssets::Load<UMaterialInterface>(TEXT("/Game/RPG/Materials/M_RPG_Glow.M_RPG_Glow")), this);
	M->SetVectorParameterValue(TEXT("Color"), C);
	M->SetScalarParameterValue(TEXT("Intensity"), 4.f);
	Gem->SetStaticMesh(RPGAssets::Shape(TEXT("Cone")));
	Gem->SetMaterial(0, M);
	Gem->SetWorldScale3D(FVector(0.18f, 0.18f, 0.28f));
	Light->SetLightColor(C);
	if (Item.Rarity == TEXT("rare") || Item.Rarity == TEXT("quest"))
	{
		UMaterialInstanceDynamic* B = UMaterialInstanceDynamic::Create(RPGAssets::Load<UMaterialInterface>(TEXT("/Game/RPG/Materials/M_RPG_Telegraph.M_RPG_Telegraph")), this);
		B->SetVectorParameterValue(TEXT("Color"), C);
		B->SetScalarParameterValue(TEXT("Opacity"), 0.25f);
		Beam->SetStaticMesh(RPGAssets::Shape(TEXT("Cylinder")));
		Beam->SetMaterial(0, B);
		Beam->SetWorldScale3D(FVector(0.08f, 0.08f, 4.f));
		Beam->SetRelativeLocation(FVector(0, 0, 900.f));
		Beam->SetVisibility(true);
	}
	BaseZ = GetActorLocation().Z;
}

void ARPGPickup::Tick(float Dt)
{
	Super::Tick(Dt);
	Age += Dt;
	Warned -= Dt;
	FVector L = GetActorLocation();
	L.Z = BaseZ + 12.f + FMath::Sin(Age * 3.f) * 6.f;
	SetActorLocation(L);
	AddActorWorldRotation(FRotator(0, 90.f * Dt, 0));

	URPGStory* Story = URPGStory::Get(this);
	ARPGPlayerCharacter* P = Story ? Story->Player() : nullptr;
	if (!P || P->IsDead() || Age < 0.4f) return;
	const float D = FVector::Dist2D(P->GetActorLocation(), L);
	if (bGold && D < 260.f)
	{
		const FVector To = (P->GetActorLocation() - L).GetSafeNormal2D();
		SetActorLocation(L + To * 900.f * Dt);
	}
	if (D > P->Radius() + 45.f) return;

	if (bGold)
	{
		P->Inventory->Gold += Amount;
		P->Inventory->OnChanged.Broadcast();
		Story->Float(P->Head(), FString::Printf(TEXT("+%dg"), Amount), FLinearColor(1.f, 0.83f, 0.3f), 0.9f);
		Destroy();
	}
	else if (P->Inventory->Add(Item))
	{
		Story->Toast(TEXT("Picked up ") + Item.Name, URPGInventoryComponent::RarityColor(this, Item.Rarity));
		Destroy();
	}
	else if (Warned <= 0.f)
	{
		Story->Toast(TEXT("Bag is full — salvage something (I, Shift+Click)"));
		Warned = 3.f;
	}
}

// ---------------------------------------------------------------------------------------------

void RPGLoot::Spawn(UWorld* World, const FVector& At, const FRPGItem* Item, int32 Gold)
{
	const float A = FMath::FRandRange(0.f, UE_TWO_PI), R = FMath::FRandRange(20.f, 80.f);
	FVector P = At + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 0);
	if (const ARPGGameMode* GM = World->GetAuthGameMode<ARPGGameMode>()) if (GM->WorldBuilder) P.Z = GM->WorldBuilder->GroundZ(P.X, P.Y);
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARPGPickup* Pk = World->SpawnActor<ARPGPickup>(P, FRotator::ZeroRotator, SP);
	if (Item) Pk->InitItem(*Item); else Pk->InitGold(Gold);
}

void RPGLoot::DropTable(UWorld* World, const FVector& At, const FString& TableId)
{
	const URPGData& D = URPGData::Get(World);
	const RPGJson::FObj Table = D.Entry(TEXT("lootTables"), TableId);
	if (!Table) return;
	const TArray<TSharedPtr<FJsonValue>> Gold = RPGJson::Arr(Table, TEXT("gold"));
	if (Gold.Num() == 2) Spawn(World, At, nullptr, FMath::RandRange(int32(Gold[0]->AsNumber()), int32(Gold[1]->AsNumber())));

	const TArray<TSharedPtr<FJsonValue>> Entries = RPGJson::Arr(Table, TEXT("entries"));
	const int32 Rolls = int32(RPGJson::Num(Table, TEXT("rolls"), 1));
	for (int32 I = 0; I < Rolls && Entries.Num(); ++I)
	{
		if (FMath::FRand() >= RPGJson::Num(Table, TEXT("dropChance"), 0.4)) continue;
		double Total = 0;
		for (const auto& E : Entries) Total += RPGJson::Num(E->AsObject(), TEXT("weight"), 1);
		double R = FMath::FRand() * Total;
		FString Pick;
		for (const auto& E : Entries) { R -= RPGJson::Num(E->AsObject(), TEXT("weight"), 1); if (R <= 0) { Pick = RPGJson::Str(E->AsObject(), TEXT("item")); break; } }
		if (Pick.IsEmpty()) Pick = RPGJson::Str(Entries.Last()->AsObject(), TEXT("item"));
		const FRPGItem It = URPGInventoryComponent::MakeItem(World, Pick, RPGJson::Obj(Table, TEXT("rarity")));
		Spawn(World, At, &It, 0);
	}
}

void RPGLoot::Drop(ARPGEnemy* E)
{
	UWorld* W = E->GetWorld();
	const FVector At = E->GetActorLocation();
	const TArray<TSharedPtr<FJsonValue>> Gold = RPGJson::Arr(E->Def, TEXT("gold"));
	if (Gold.Num() == 2) Spawn(W, At, nullptr, FMath::RandRange(int32(Gold[0]->AsNumber()), int32(Gold[1]->AsNumber())));
	DropTable(W, At, RPGJson::Str(E->Def, TEXT("loot")));

	// Quest drop (the relic): only while the quest isn't turned in and you don't already have it.
	const RPGJson::FObj Q = RPGJson::Obj(E->Def, TEXT("questDrop"));
	URPGStory* Story = URPGStory::Get(E);
	const ARPGPlayerCharacter* P = Story->Player();
	if (Q && P && Story->QuestStatus(RPGJson::Str(Q, TEXT("quest"))) != TEXT("turnedIn") && P->Inventory->Count(RPGJson::Str(Q, TEXT("item"))) == 0)
	{
		const FRPGItem It = URPGInventoryComponent::MakeItem(W, RPGJson::Str(Q, TEXT("item")));
		Spawn(W, At, &It, 0);
	}
}
