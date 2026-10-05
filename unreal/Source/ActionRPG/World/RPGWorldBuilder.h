#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGWorldBuilder.generated.h"

class UProceduralMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UPointLightComponent;
class URPGData;

/**
 * Builds the playable level from the map rows in game-data.json — the same map the HTML prototype uses.
 *
 *   terrain   one procedural mesh (sections: grass / gravel path / cobbled ruins / riverbed), gently rolling,
 *             with the river carved in and hills rising beyond the map edge
 *   water     a single lake surface at water level; the terrain hides it everywhere except the river
 *   '#'       broken stone ruin walls (the outer border is open hills + invisible walls instead)
 *   'T'       trees: wooden trunk + foliage crown
 *   'H'       each block of house tiles becomes one cottage (stone footing, plaster walls, timber frame, gabled roof)
 *   '='       a plank bridge with rails
 *   + scattered bushes and rocks, a campfire in the village, torches at the bridge
 *
 * Also spawns the sky: sun, sky atmosphere, real-time sky light, volumetric clouds, height fog, post process.
 *
 * Unreal: an actor that owns instanced components. Everything is generated, so the project ships no .umap.
 */
UCLASS()
class ACTIONRPG_API ARPGWorldBuilder : public AActor
{
	GENERATED_BODY()

public:
	ARPGWorldBuilder();

	void Build();

	virtual void Tick(float DeltaSeconds) override;

	/** Ground height at a world XY (follows the generated terrain). */
	float GroundZ(float X, float Y) const;

private:
	void BuildTerrain(const URPGData& D);
	void BuildWater(const URPGData& D);
	void BuildBlockers(const URPGData& D);
	void BuildTreesAndScatter(const URPGData& D);
	void BuildRuins(const URPGData& D);
	void BuildHouses(const URPGData& D);
	void BuildBridge(const URPGData& D);
	void BuildProps(const URPGData& D);
	void BuildSky();

	float BaseHeight(int32 TX, int32 TY) const;   // per-tile target height
	float NoiseAt(float X, float Y) const;

	UHierarchicalInstancedStaticMeshComponent* MakeInstances(UStaticMesh* Mesh, UMaterialInterface* Material, bool bCollide, int32 CullDistance = 0);
	UStaticMeshComponent* AddMesh(UStaticMesh* Mesh, UMaterialInterface* Material, const FTransform& T, bool bCollide);
	/** A box built from the 100uu engine cube: Center/Size in world units, rotation optional. */
	UStaticMeshComponent* AddBox(const FVector& Center, const FVector& Size, UMaterialInterface* Material, bool bCollide = true, const FRotator& Rot = FRotator::ZeroRotator);
	void AddBlocker(const FVector& Center, const FVector& HalfExtent);
	void AddFire(const FVector& Location, float Scale, float LightIntensity);

	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Terrain;

	struct FFlicker { TObjectPtr<UPointLightComponent> Light; float Base = 0; float Phase = 0; };
	TArray<FFlicker> Flickers;

	// cached height grid (for GroundZ)
	TArray<float> Heights;
	int32 GridW = 0, GridH = 0, Sub = 2, Pad = 8;
	float Step = 150.f, OriginX = 0, OriginY = 0;
	float Tile = 300.f;
	TArray<FString> Rows;
	int32 MapW = 0, MapH = 0;
};
