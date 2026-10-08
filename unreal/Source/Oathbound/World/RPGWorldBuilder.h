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
 *   '+'       a gate in a ruin wall: a wall block that opens (an enclosure's lock: restPlaces.<id>.enclosure)
 *   'T'       trees: wooden trunk + foliage crown
 *   'H'       each block of house tiles becomes one cottage (stone footing, plaster walls, timber frame, gabled roof);
 *             cut away it shows a plank floor, a bed, and a door gap in its knee-high walls (Houses)
 *   '='       a plank bridge with rails
 *   areas     (map.areas) small separate maps off to the side: a dirt floor, rock walls ('#'), torches ("lights")
 *   + scattered bushes and rocks, a campfire in the village, torches at the bridge
 */
UCLASS()
class OATHBOUND_API ARPGWorldBuilder : public ATSWorldBuilder
{
	GENERATED_BODY()

public:
	virtual void Build() override;

	/** "14:30  Afternoon" for the HUD clock. */
	static FString ClockText();

	/** A cottage: its cutaway (ATSWorldBuilder), where its bed is (the mattress top) and its front door (outside, on the
	 *  ground). */
	struct FHouse { int32 Cutaway = INDEX_NONE; FVector Bed = FVector::ZeroVector; float BedYaw = 0.f; FVector Door = FVector::ZeroVector; };
	TArray<FHouse> Houses;
	/** The house standing on Point (flat), or null. */
	const FHouse* HouseAt(const FVector& Point) const;
	/** Light a camp fire (stone ring, logs, flame, crackle) on the ground at XY. */
	void AddCampfire(const FVector2D& At);
	/** Walled places with a gate (restPlaces.<id>.enclosure [x0, y0, x1, y1] tiles): id -> cutaway index (no mesh to
	 *  cut; its "full" parts are the gate's wall blocks, which stop colliding when it opens). */
	TMap<FString, int32> Enclosures;
	/** Open a lock: a house (its cut walls start colliding) or an enclosure (its gate blocks vanish). */
	void OpenLock(int32 Cutaway);

private:
	void BuildTerrain(const UTSData& D);
	void BuildWater(const UTSData& D);
	void BuildBlockers(const UTSData& D);
	void BuildTreesAndScatter(const UTSData& D);
	void BuildRuins(const UTSData& D);
	void BuildEnclosures(const UTSData& D);
	TMap<FIntPoint, TArray<TObjectPtr<UStaticMeshComponent>>> GateParts;   // '+' tiles' wall blocks
	void BuildHouses(const UTSData& D);
	void BuildBridge(const UTSData& D);
	void BuildProps(const UTSData& D);
	void BuildFlat2D();
	/** Separate small maps (map.areas: the brute's cave): rock walls, an earth floor, torches, darkness all round. */
	void BuildAreas(const UTSData& D);

	float BaseHeight(int32 TX, int32 TY) const;   // per-tile target height
	float NoiseAt(float X, float Y) const;
	void AddFire(const FVector& Location, float Scale, float LightIntensity);

	UPROPERTY() TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Crowns;   // tree crowns (cut away when they hide the hero)

	int32 Sub = 2, Pad = 8;   // terrain cells per tile, tiles of hills beyond the map
	float Tile = 300.f;
	TArray<FString> Rows;
	int32 MapW = 0, MapH = 0;
};
