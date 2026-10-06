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

	/** Day/night: the hour of the day (0-24) and "14:30  Afternoon" for the HUD clock. */
	static float Hour();
	static FString ClockText();
	/** 0 by day, 1 at night (follows the sun). */
	static float Night();

	// Deep night: only what's near the hero or near a fire / torch can be seen (world3d.dayNight.nightVision).
	/** How strongly the dark swallows everything out of reach (0 by day, up to ~1 in deep night). */
	static float Darkness();
	/** How far the hero sees in the dark (uu). */
	static float HeroSight();
	/** The fires and torches: (x, y, radius it lights). */
	static const TArray<FVector> & NightLights();
	/** Can a point be seen right now (daylight, or near the hero, or near a light)? */
	static bool IsLit(const FVector& At, const FVector& Hero);

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
	void BuildNavigation();
	void BuildFlat2D();

	float BaseHeight(int32 TX, int32 TY) const;   // per-tile target height
	float NoiseAt(float X, float Y) const;

	UHierarchicalInstancedStaticMeshComponent* MakeInstances(UStaticMesh* Mesh, UMaterialInterface* Material, bool bCollide, int32 CullDistance = 0);
	UStaticMeshComponent* AddMesh(UStaticMesh* Mesh, UMaterialInterface* Material, const FTransform& T, bool bCollide);
	/** A box built from the 100uu engine cube: Center/Size in world units, rotation optional. */
	UStaticMeshComponent* AddBox(const FVector& Center, const FVector& Size, UMaterialInterface* Material, bool bCollide = true, const FRotator& Rot = FRotator::ZeroRotator);
	void AddBlocker(const FVector& Center, const FVector& HalfExtent);
	void AddFire(const FVector& Location, float Scale, float LightIntensity);

	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Terrain;

	// Day/night cycle (world3d.dayNight): the sun crosses the sky, dusk warms it, the moon lights the night.
	UPROPERTY() TObjectPtr<class UDirectionalLightComponent> SunLight;
	UPROPERTY() TObjectPtr<class UDirectionalLightComponent> MoonLight;
	UPROPERTY() TObjectPtr<class APostProcessVolume> Grade;   // time-of-day exposure and colour
	UPROPERTY() TObjectPtr<class UExponentialHeightFogComponent> Fog;
	UPROPERTY() TObjectPtr<class USkyLightComponent> SkyFill;
	UPROPERTY() TObjectPtr<class UDirectionalLightComponent> FillLight;   // shadowless fill from above: dusk shadows aren't black
	float FogDay = 0.012f, FogDusk = 0.03f, FogNight = 0.05f;
	bool bFog = true;   // dayNight.fog.enabled
	bool bDayCycle = false;
	float NightExposure = -2.2f, ExposureMinEV = 2.f, ExposureMaxEV = 5.f;
	float SecondsPerHour = 30.f, SunLux = 9.f, MoonLux = 0.6f;
	void UpdateSky();

	struct FFlicker { TObjectPtr<UPointLightComponent> Light; float Base = 0; float Phase = 0; };
	TArray<FFlicker> Flickers;

	/** A cottage that cuts away (walls + roof hidden, low cut walls shown) while it hides the player from the camera. */
	struct FHouse { FBox Bounds; TArray<TObjectPtr<UStaticMeshComponent>> Full, Cut; float Hold = 0; bool bCut = false; };
	TArray<FHouse> Houses;
	/** A tree's crown instances: hidden (shrunk away) while they hide the player. */
	struct FTreeCrown { FBox Bounds; TArray<int32> Instances; TArray<FTransform> Transforms; float Hold = 0; bool bCut = false; };
	TArray<FTreeCrown> TreeCrowns;
	UPROPERTY() TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Crowns;
	void UpdateCutaways(float Dt);
	/** While set, AddMesh also appends what it creates here (to group a house's parts). */
	TArray<TObjectPtr<UStaticMeshComponent>>* Collect = nullptr;

	// cached height grid (for GroundZ)
	TArray<float> Heights;
	int32 GridW = 0, GridH = 0, Sub = 2, Pad = 8;
	float Step = 150.f, OriginX = 0, OriginY = 0;
	float Tile = 300.f;
	TArray<FString> Rows;
	int32 MapW = 0, MapH = 0;
};
