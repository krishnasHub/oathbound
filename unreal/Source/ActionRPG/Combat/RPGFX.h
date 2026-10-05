#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGFX.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;

/**
 * Short-lived visual effects built from engine shapes and our generated materials:
 *   Ring      expanding ground ring (Cleave, Frost Nova, level-up, Mend)
 *   Bolt      glowing segments between points (Chain Lightning, Silver Words)
 *   Burst     brief glow sphere + light (impacts, Blink)
 *   Smoke     Starter Content smoke particles (Smoke Bomb)
 */
UCLASS()
class ACTIONRPG_API ARPGFX : public AActor
{
	GENERATED_BODY()

public:
	ARPGFX();
	virtual void Tick(float DeltaSeconds) override;

	static void Ring(UWorld* W, const FVector& At, float Radius, const FLinearColor& Color, float Life = 0.4f);
	static void Bolt(UWorld* W, const TArray<FVector>& Points, const FLinearColor& Color, float Life = 0.25f);
	static void Burst(UWorld* W, const FVector& At, float Radius, const FLinearColor& Color, float Life = 0.3f);
	static void Smoke(UWorld* W, const FVector& At, float Radius);

private:
	enum class EKind : uint8 { Ring, Bolt, Burst, Smoke } Kind = EKind::Ring;
	float Age = 0.f, Life = 0.4f, Radius = 100.f;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Mats;
	UPROPERTY() TObjectPtr<UPointLightComponent> Light;
	float LightBase = 0.f;

	static ARPGFX* Make(UWorld* W, const FVector& At, EKind Kind, float Life);
	UStaticMeshComponent* AddPart(const TCHAR* Shape, const TCHAR* Material, const FLinearColor& Color, float Intensity);
};
