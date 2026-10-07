#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGAmbient.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class ARPGWorldBuilder;
class ATSAmbientLife;

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
	/** Tessera's ambient life (children, puppies, wolves...), on this map's areas: grass, village, road, wild. */
	ATSAmbientLife* GetLife() const { return Life; }
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
		float Frozen = 0.f;                  // seconds left frozen in place (a frost nova)
		FVector Thrown = FVector::ZeroVector;   // thrown by a barrier: sliding outward, slowing down
		float Size = 100.f;                  // as last placed, to keep drawing it while it's thrown
		int32 Col = 0, Row = 0;
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
	/** An area effect (UTSAreaEvents): a "Frozen" area freezes the animals in it, tinted like frozen characters. */
	void OnAreaStatus(const FVector& Center, float Radius, FName Tag, float Duration);
	/** Count down a freeze; true while the card must stay put. */
	static bool StayFrozen(FCard& C, float Dt);
	/** A barrier went up (UTSAreaEvents::OnPush): birds inside burst into the air, geese and the prowler are thrown. */
	void OnAreaPush(const FVector& Center, float Radius);
	/** Slide a thrown card outward until it stops; true while it's still sliding. */
	bool StayThrown(FCard& C, float Dt);

	TWeakObjectPtr<ARPGWorldBuilder> World;
	UPROPERTY() TObjectPtr<ATSAmbientLife> Life;
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
