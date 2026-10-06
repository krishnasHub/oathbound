#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGCombat.h"
#include "RPGProjectile.generated.h"

class ARPGCharacterBase;
class UStaticMeshComponent;
class UPointLightComponent;

/**
 * Arrow / bolt / fireball. Arrows fly a parabola (FireArrow: launched up toward a target point, nose following the
 * flight, a real shadow on the ground; misses stick in the ground for a moment). Bolts and fireballs fly straight.
 * Moves itself each tick: a line trace on the Visibility channel stops it on
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

	/** Spawn an arrow that arcs from From to land on To (world3d.arrows: arc height grows with distance). Fletching is Color. */
	static ARPGProjectile* FireArrow(ARPGCharacterBase* Owner, const FVector& From, const FVector& To, float Speed,
		float Radius, const FLinearColor& Color, const FRPGHit& Hit);
	/** The launch for an arc from From to To at horizontal Speed: initial velocity, gravity, and time to reach To. */
	static void ArcLaunch(const UObject* WorldContext, const FVector& From, const FVector& To, float Speed, FVector& OutVelocity, float& OutGravity, float& OutTime);

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> ArrowParts;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Trail;
	bool bArrow = false, bStuck = false;
	float Gravity = 0.f, StuckTime = 0.f;
	UStaticMeshComponent* AddArrowPart(const TCHAR* Shape, const FLinearColor& Col, const FVector& Loc, const FRotator& Rot, const FVector& Scale);
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
