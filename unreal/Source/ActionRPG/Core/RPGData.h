#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RPGJson.h"
#include "RPGData.generated.h"

/** One spawn marker from the map ("P", "E", "s", ...), already resolved to its kind and id. */
struct FRPGSpawn
{
	FString Kind;   // "player", "npc", "enemy"
	FString Id;     // e.g. "elder", "slime"
	int32 X = 0;
	int32 Y = 0;
};

/**
 * The game's rules, loaded once from Content/Data/game-data.json — the same file the HTML prototype
 * inlines (see tools/sync-data.js).
 *
 * Gameplay numbers in that file are in prototype pixels. `Px()` converts them to Unreal units using
 * world3d.unitsPerPx, so ranges, speeds and knockback keep their tuned proportions in 3D.
 * Map positions use world3d.tileSize instead (the 3D world is roomier than the 2D one).
 *
 * Unreal: UGameInstanceSubsystem so it lives for the whole session.
 */
UCLASS()
class ACTIONRPG_API URPGData : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	static URPGData& Get(const UObject* WorldContext);

	RPGJson::FObj Root;

	RPGJson::FObj Section(const FString& Name) const { return RPGJson::Obj(Root, Name); }
	/** e.g. Entry("enemies", "slime") */
	RPGJson::FObj Entry(const FString& SectionName, const FString& Id) const { return RPGJson::Obj(Section(SectionName), Id); }
	double Tuning(const FString& Key, double Default = 0.0) const { return RPGJson::Num(Section(TEXT("tuning")), Key, Default); }
	RPGJson::FObj World3D() const { return Section(TEXT("world3d")); }

	/** Prototype pixels -> Unreal units. */
	float Px(double Pixels) const { return float(Pixels) * UnitsPerPx; }

	float UnitsPerPx = 3.5f;
	float TileSize = 300.f;

	// ---- map ----
	TArray<FString> Rows;          // spawn markers already replaced by floor
	int32 MapW = 0, MapH = 0;
	TArray<FRPGSpawn> Spawns;

	TCHAR TileAt(int32 X, int32 Y) const;
	/** World position of a tile's centre on the ground plane. */
	FVector TileCenter(int32 X, int32 Y) const { return FVector((X + 0.5f) * TileSize, (Y + 0.5f) * TileSize, 0.f); }
	FIntPoint TileOf(const FVector& P) const { return FIntPoint(FMath::FloorToInt(P.X / TileSize), FMath::FloorToInt(P.Y / TileSize)); }

	static bool IsWater(TCHAR C) { return C == TEXT('~'); }
	static bool IsBridge(TCHAR C) { return C == TEXT('='); }

	/** Map region ("north" / "south") for a world Y. Enemies only engage the player inside their own region. */
	FString RegionAt(float WorldY) const;

	bool IsLoaded() const { return Root.IsValid(); }

private:
	void ParseMap();
};
