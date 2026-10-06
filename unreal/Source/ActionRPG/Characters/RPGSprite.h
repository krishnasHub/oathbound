#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "RPGSprite.generated.h"

class UMaterialInstanceDynamic;

/** Character sprite sheet layout (tools/pixelart/characters.py): 4 columns x 13 rows of 32-unit frames. */
namespace RPGSpriteSheet { constexpr int32 Cols = 4, Rows = 13, Frame = 32; }

/**
 * A pixel-art character for the 2D looks (RPGLook HD2D / Flat2D): a card showing one frame of a sprite sheet
 * (tools/pixelart/characters.py layout: 3 directions x idle / walk / attack / hurt, plus dead).
 *
 * Every frame it reads the owner's state (facing, speed, attack timing, damage taken, death), picks the
 * direction row and frame, and places the card at the owner's feet facing the camera (or lying flat,
 * y-sorted, in Flat 2D). The owner's 3D body stays hidden but keeps animating, so montage-driven hit
 * timing works exactly as before.
 */
UCLASS()
class ACTIONRPG_API URPGSpriteComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	URPGSpriteComponent();
	void Setup(const FString& Sheet);
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	FString SheetName;

private:
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Mat;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Shadow;   // Flat 2D: a drawn blob shadow (cards cast none)
	float Clock = 0.f, LastHP = -1.f, HurtT = 0.f, HideCheck = 0.f;
};
