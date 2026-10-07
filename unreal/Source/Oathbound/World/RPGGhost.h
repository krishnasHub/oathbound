#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGGhost.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class ATSCharacter;

/**
 * A fallen foe's ghost: a little see-through puff of gas with two big eyes (SPR_Ghost) that rises out of the body,
 * wiggles side to side on the way up, and fades away. Purely for fun; nothing touches it.
 *
 * Sometimes (scaredChance), when it was the last foe around (none alive within scaredVicinity), the ghost spots the
 * hero on the way up, freezes with a "!" and a frightened face looking at them, then flees the other way.
 *
 * Spawned by URPGSession when Tessera announces a death (UTSCharacterEvents::OnDied) and the dead was an enemy.
 * Data: world3d.deathGhost { sheet, frames, size, rise, life, delay, wiggle, wiggleSpeed, opacity, emissive,
 * scaredChance, scaredVicinity, fleeDistance, scaredLife }. -RPGGhostScared forces a scared ghost (tests).
 */
UCLASS()
class OATHBOUND_API ARPGGhost : public AActor
{
	GENERATED_BODY()

public:
	ARPGGhost();
	/** A ghost rising from Body; bCalm: laid to rest (never scared, rises slow and gentle). */
	static ARPGGhost* Rise(ATSCharacter* Body, bool bCalm = false);
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Card;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Mat;
	FVector Feet = FVector::ZeroVector;
	float Age = 0.f, Life = 2.6f, Delay = 0.4f, Size = 120.f, RiseBy = 260.f;
	float Wiggle = 22.f, WiggleSpeed = 1.6f, Opacity = 0.65f, Phase = 0.f;
	int32 Frames = 4;                 // frames per face: calm, then the scared face looking each way
	/** The scared face's eye directions in SPR_Ghost (tools/pixelart GHOST_LOOKS): right, up-right, up, down-right, down. */
	static constexpr int32 LookDirections = 5;
	bool bScared = false, bSpotted = false;
	float FleeDistance = 900.f;
	FVector FleeDir = FVector::ForwardVector;
	FVector PeakAt = FVector::ZeroVector;   // where it was when it spotted the hero
public:
	bool IsScared() const { return bScared; }
	/** How far it has drifted from the body (flat), for tests. */
	float Drift() const { return FVector::Dist2D(Card->GetComponentLocation(), Feet); }
};
