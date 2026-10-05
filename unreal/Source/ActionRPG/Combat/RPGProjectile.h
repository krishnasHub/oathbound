#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGCombat.h"
#include "RPGProjectile.generated.h"

class ARPGCharacterBase;
class UStaticMeshComponent;
class UPointLightComponent;

/**
 * Arrow / bolt / fireball. Moves itself each tick: a line trace on the Visibility channel stops it on
 * terrain, trees, walls and houses (the invisible river walls ignore Visibility, so shots fly over
 * water); characters are hit by distance to their capsule. A dodging (invulnerable) target lets it
 * pass through, like the prototype.
 */
UCLASS()
class ACTIONRPG_API ARPGProjectile : public AActor
{
	GENERATED_BODY()

public:
	ARPGProjectile();

	/** Spawn and launch. Speed / range / radius are in Unreal units. */
	static ARPGProjectile* Fire(ARPGCharacterBase* Owner, const FVector& From, const FVector& Dir, float Speed, float Range,
		float Radius, const FLinearColor& Color, bool bArrow, const FRPGHit& Hit);

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY() TObjectPtr<UPointLightComponent> Glow;

	TWeakObjectPtr<ARPGCharacterBase> Shooter;
	bool bFromPlayer = false;
	FVector Velocity;
	float Life = 1.f, HitRadius = 10.f;
	FRPGHit Hit;
	FLinearColor Color;
	TSet<TWeakObjectPtr<AActor>> PassedThrough;

	void Burst();
};
