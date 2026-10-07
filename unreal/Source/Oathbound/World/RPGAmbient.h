#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGAmbient.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class ARPGWorldBuilder;

/**
 * Life that isn't part of the game: it just lives in the world and reacts to the hero.
 *
 *   by day    flocks of birds pecking in the grass: they burst into the air when you come close, and settle
 *             somewhere else later; geese waddling and grazing, honking and running off if you crowd them
 *   by night  a hooded prowler slinking between the cottages: freezes when you come near, then bolts into the
 *             dark; fireflies over the grass that drift away from you
 *
 * Everything is a sprite card (or a glowing speck) driven here; nothing collides or can be fought.
 */
UCLASS()
class OATHBOUND_API ARPGAmbient : public AActor
{
	GENERATED_BODY()

public:
	ARPGAmbient();
	void Init(ARPGWorldBuilder* InWorld);
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FCard
	{
		TObjectPtr<UStaticMeshComponent> Mesh;
		TObjectPtr<UMaterialInstanceDynamic> Mat;
		FVector Pos = FVector::ZeroVector;   // feet on the ground (Z = ground)
		FVector Vel = FVector::ZeroVector;
		float Lift = 0.f;                    // height above the ground (flying)
		float Timer = 0.f, Anim = 0.f;
		int32 State = 0;
		bool bFacingLeft = false;
	};
	struct FBird : FCard {};
	struct FGoose : FCard { FVector Target = FVector::ZeroVector; };
	struct FFly { TObjectPtr<UStaticMeshComponent> Mesh; TObjectPtr<UMaterialInstanceDynamic> Mat; FVector Home, Offset; float Phase; };

	FCard MakeCard(const FString& Sheet, int32 Cols, int32 Rows, float SizeUU);
	void Place(FCard& C, float SizeUU, int32 Col, int32 Row, bool bVisible);
	FVector RandomGrass(const FVector& AwayFrom, float MinDist) const;
	float Ground(const FVector& P) const;

	void TickBirds(float Dt, const FVector& Hero, bool bDay);
	void TickGeese(float Dt, const FVector& Hero, bool bDay);
	void TickProwler(float Dt, const FVector& Hero, bool bNight);
	void TickFireflies(float Dt, const FVector& Hero, float Night);

	TWeakObjectPtr<ARPGWorldBuilder> World;
	TArray<FVector> Grass, Village;
	TArray<FBird> Birds;
	TArray<FGoose> Geese;
	FCard Prowler;
	TArray<FVector> ProwlRoute;
	int32 ProwlLeg = 0;
	TArray<FFly> Flies;
	FRandomStream Rand;
	float Clock = 0.f;
	float BirdSize = 100.f, GooseSize = 150.f, ManSize = 210.f;
};
