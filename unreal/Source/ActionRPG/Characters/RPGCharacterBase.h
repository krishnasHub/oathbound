#pragma once

#include "CoreMinimal.h"
#include "TSCharacter.h"
#include "RPGCharacterBase.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/** This game's stat pools (data: stats.pools). Health is Stats->Health(). */
namespace RPGStat
{
	inline const FName Stamina = TEXT("stamina");
	inline const FName Mana = TEXT("mana");
}

/**
 * This game's characters on top of Tessera's ATSCharacter: their look (world3d.looks): the Third Person template's
 * Manny / Quinn mannequins (combat anim blueprint, optional tint, scale) or a jelly slime blob.
 */
UCLASS(Abstract)
class ACTIONRPG_API ARPGCharacterBase : public ATSCharacter
{
	GENERATED_BODY()

public:
	ARPGCharacterBase();

	/** Appearance: "manny" | "quinn" | "slime", tint (#rrggbb, empty = default look), uniform scale. */
	void SetLook(const FString& MeshKind, const FString& TintHex, float Scale);
	/** Reads { "mesh", "tint", "scale" } from world3d.looks[Id], and the sprite sheet from looks2d.sheets. */
	void SetLookFromData(const FString& LookId);
	FString MeshKind;

	virtual bool CanHoldKits() const override { return MeshKind != TEXT("slime"); }
	virtual void Flash() override;
	virtual void Die(AActor* Killer) override;
	virtual void HideBody() override;

protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual void ClearFlash() override;

	UPROPERTY() TObjectPtr<UStaticMeshComponent> Blob;   // slime body
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> BodyMaterials;
	float BlobPhase = 0.f;
};
