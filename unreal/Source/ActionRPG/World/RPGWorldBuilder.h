#pragma once

#include "CoreMinimal.h"
#include "TSWorldBuilder.h"
#include "RPGWorldBuilder.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UTSData;

/**
 * Builds this game's level from the map rows in game-data.json — the same map the HTML prototype uses — on top of
 * Tessera's ATSWorldBuilder (helpers, cutaways, height field, sky and day/night, navmesh).
 *
 *   terrain   one procedural mesh (sections: grass / gravel path / cobbled ruins / riverbed), gently rolling,
 *             with the river carved in and hills rising beyond the map edge
 *   water     a single lake surface at water level; the terrain hides it everywhere except the river
 *   '#'       broken stone ruin walls (the outer border is open hills + invisible walls instead)
 *   'T'       trees: wooden trunk + foliage crown
 *   'H'       each block of house tiles becomes one cottage (stone footing, plaster walls, timber frame, gabled roof)
 *   '='       a plank bridge with rails
 *   + scattered bushes and rocks, a campfire in the village, torches at the bridge
 */
UCLASS()
class ACTIONRPG_API ARPGWorldBuilder : public ATSWorldBuilder
{
	GENERATED_BODY()

public:
	virtual void Build() override;

	/** "14:30  Afternoon" for the HUD clock. */
	static FString ClockText();

private:
	void BuildTerrain(const UTSData& D);
	void BuildWater(const UTSData& D);
	void BuildBlockers(const UTSData& D);
	void BuildTreesAndScatter(const UTSData& D);
	void BuildRuins(const UTSData& D);
	void BuildHouses(const UTSData& D);
	void BuildBridge(const UTSData& D);
	void BuildProps(const UTSData& D);
	void BuildFlat2D();

	float BaseHeight(int32 TX, int32 TY) const;   // per-tile target height
	float NoiseAt(float X, float Y) const;
	void AddFire(const FVector& Location, float Scale, float LightIntensity);

	UPROPERTY() TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Crowns;   // tree crowns (cut away when they hide the hero)

	int32 Sub = 2, Pad = 8;   // terrain cells per tile, tiles of hills beyond the map
	float Tile = 300.f;
	TArray<FString> Rows;
	int32 MapW = 0, MapH = 0;
};
