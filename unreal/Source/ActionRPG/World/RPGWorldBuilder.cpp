#include "RPGWorldBuilder.h"
#include "ActionRPG.h"
#include "TSData.h"
#include "TSLook.h"
#include "TSSky.h"
#include "RPGAssets.h"

#include "ProceduralMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/AudioComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	// Deterministic per-tile randomness so the world looks the same every run.
	FRandomStream TileRand(int32 X, int32 Y, int32 Salt = 0)
	{
		return FRandomStream(int32(uint32(X * 73856093) ^ uint32(Y * 19349663) ^ uint32(Salt * 83492791)));
	}

	constexpr float RiverBedZ = -170.f;
	constexpr float WaterZ = -55.f;
	constexpr float DeckZ = 18.f;
}

void ARPGWorldBuilder::Build()
{
	const UTSData& D = UTSData::Get(this);
	if (!D.IsLoaded()) return;

	Tile = D.TileSize;
	Rows = D.Rows;
	MapW = D.MapW;
	MapH = D.MapH;

	const double T0 = FPlatformTime::Seconds();
	BeginBuild();
	BuildTerrain(D);
	BuildWater(D);
	BuildBlockers(D);
	BuildRuins(D);
	BuildHouses(D);
	BuildBridge(D);
	BuildTreesAndScatter(D);
	BuildProps(D);
	if (TSLook::Mode() == TSLook::EMode::Flat2D) BuildFlat2D();
	// Sky, day/night and the navmesh over the map (plus room above and below for the terrain).
	FinishBuild(FBox(FVector(0.f, 0.f, -1500.f), FVector(MapW * Tile, MapH * Tile, 3000.f)));
	UE_LOG(LogRPG, Display, TEXT("World built from map (%dx%d tiles, %.0fuu per tile) in %.2fs."), MapW, MapH, Tile, FPlatformTime::Seconds() - T0);
}

// ---------------------------------------------------------------------------------------------
// Flat 2D look: the 3D world stays (invisible) for collision and the navmesh; what you see is the map baked
// as one pixel-art ground image (tools/pixelart, MAP_Ground) plus flat, y-sorted sprite props.
// ---------------------------------------------------------------------------------------------

void ARPGWorldBuilder::BuildFlat2D()
{
	TArray<UPrimitiveComponent*> Prims;
	GetComponents(Prims);
	for (UPrimitiveComponent* P : Prims)
	{
		// Invisible, and out of the lighting too (distance-field shadows / GI would still see it).
		P->SetVisibility(false);
		P->SetCastShadow(false);
		P->bAffectDistanceFieldLighting = false;
		P->SetAffectDynamicIndirectLighting(false);
	}
	Cutaways.Reset();
	InstanceCutaways.Reset();

	const int32 PadTiles = 6;   // build_all.py PAD
	const float U = TSLook::SpriteUnits();
	const FRotator R = TSLook::CardRotation();
	const FVector North = -FRotationMatrix(R).GetUnitAxis(EAxis::Y);
	UStaticMesh* Plane = TSAssets::Shape(TEXT("Plane"));

	// Ground.
	const FVector Mid(MapW * Tile * 0.5f, MapH * Tile * 0.5f, 0.f);
	UStaticMeshComponent* Ground = AddMesh(Plane, TSLook::PropMaterial(TEXT("MAP_Ground")),
		FTransform(R, Mid, FVector((MapW + 2 * PadTiles) * Tile / 100.f, (MapH + 2 * PadTiles) * Tile / 100.f, 1.f)), false);
	Ground->SetCastShadow(false);

	// Props: a card whose bottom edge sits at Anchor and whose picture extends north; south draws over north.
	TMap<FString, UHierarchicalInstancedStaticMeshComponent*> Sets;
	auto Prop = [&](const FString& Tex, const FVector2D& Anchor, float Wpx, float Hpx, float Scale = 1.f)
	{
		auto Set = [&](const FString& T) -> UHierarchicalInstancedStaticMeshComponent*
		{
			UHierarchicalInstancedStaticMeshComponent*& H = Sets.FindOrAdd(T);
			if (!H) { H = MakeInstances(Plane, TSLook::PropMaterial(T), false); H->SetCastShadow(false); }   // flat cards: drawn shadows only
			return H;
		};
		const float W = Wpx * U * Scale, Ht = Hpx * U * Scale;
		// (+ a hair of x so neighbours in one row never share a depth and z-fight)
		const FVector At = FVector(Anchor, TSLook::FlatSortZ(Anchor.Y) + Anchor.X * 0.00003f) + North * (Ht * 0.5f);
		Set(Tex)->AddInstance(FTransform(R, At, FVector(W / 100.f, Ht / 100.f, 1.f)), true);
		// A dithered blob shadow at the base of anything that stands up (under every other card).
		if (!Tex.StartsWith(TEXT("PR_Wall")) && !Tex.StartsWith(TEXT("PR_House")))
			Set(TEXT("PR_Shadow"))->AddInstance(FTransform(R, FVector(Anchor, 2.f) + North * (2.f * U * Scale), FVector(W * 0.7f / 100.f, W * 0.25f / 100.f, 1.f)), true);
	};

	FRandomStream Rand(42);
	for (int32 Y = -PadTiles; Y < MapH + PadTiles; ++Y)
	{
		for (int32 X = -PadTiles; X < MapW + PadTiles; ++X)
		{
			const bool bInside = X >= 0 && Y >= 0 && X < MapW && Y < MapH;
			const TCHAR C = bInside ? Rows[Y][X] : TEXT('o');
			const bool bBorder = bInside && (X == 0 || Y == 0 || X == MapW - 1 || Y == MapH - 1);
			const FVector2D Foot((X + 0.5f + Rand.FRandRange(-0.15f, 0.15f)) * Tile, (Y + 0.9f) * Tile);
			if (C == TEXT('T') || (C == TEXT('o') && Rand.FRand() < 0.55f) || (bBorder && C == TEXT('#') && Rand.FRand() < 0.85f))
				Prop(Rand.FRand() < 0.5f ? TEXT("PR_Tree1") : TEXT("PR_Tree2"), Foot, 56.f, 72.f, Rand.FRandRange(0.9f, 1.15f));
			else if (C == TEXT('#') && !bBorder)
				Prop(TEXT("PR_Wall"), FVector2D((X + 0.5f) * Tile, (Y + 1.f) * Tile), 32.f, 40.f);
			else if (C == TEXT('.') && Rand.FRand() < 0.06f)
				Prop(Rand.FRand() < 0.7f ? TEXT("PR_Bush") : TEXT("PR_Rock"), Foot, 24.f, 18.f, Rand.FRandRange(0.8f, 1.2f));
		}
	}

	// Houses: one sprite per block of 'H' tiles (PR_House_<w>x<h>, roof over the footprint, front wall below).
	TSet<FIntPoint> Seen;
	for (int32 Y = 0; Y < MapH; ++Y)
		for (int32 X = 0; X < MapW; ++X)
		{
			if (Rows[Y][X] != TEXT('H') || Seen.Contains(FIntPoint(X, Y))) continue;
			int32 W = 0, H = 0;
			while (X + W < MapW && Rows[Y][X + W] == TEXT('H')) ++W;
			while (Y + H < MapH && Rows[Y + H][X] == TEXT('H')) ++H;
			for (int32 YY = Y; YY < Y + H; ++YY) for (int32 XX = X; XX < X + W; ++XX) Seen.Add(FIntPoint(XX, YY));
			Prop(FString::Printf(TEXT("PR_House_%dx%d"), W, H), FVector2D((X + W * 0.5f) * Tile, (Y + H) * Tile + 8.f * U), W * 32.f, H * 32.f + 32.f);
		}
	UE_LOG(LogRPG, Display, TEXT("Flat 2D: ground + %d prop sets"), Sets.Num());
}

// ---------------------------------------------------------------------------------------------
// Terrain
// ---------------------------------------------------------------------------------------------

float ARPGWorldBuilder::BaseHeight(int32 TX, int32 TY) const
{
	if (TX >= 0 && TY >= 0 && TX < MapW && TY < MapH)
	{
		const TCHAR C = Rows[TY][TX];
		if (C == TEXT('~') || C == TEXT('=')) return RiverBedZ;
		return 0.f;
	}
	// Outside the playable map: hills that rise with distance, framing the valley.
	const int32 DX = TX < 0 ? -TX : (TX >= MapW ? TX - MapW + 1 : 0);
	const int32 DY = TY < 0 ? -TY : (TY >= MapH ? TY - MapH + 1 : 0);
	const float Dist = float(FMath::Max(DX, DY));
	return 70.f * Dist + 22.f * Dist * Dist;
}

float ARPGWorldBuilder::NoiseAt(float X, float Y) const
{
	return FMath::PerlinNoise2D(FVector2D(X, Y) * 0.00065f) * 38.f
	     + FMath::PerlinNoise2D(FVector2D(X + 911.f, Y - 377.f) * 0.0028f) * 9.f;
}

void ARPGWorldBuilder::BuildTerrain(const UTSData& D)
{
	Step = Tile / Sub;
	OriginX = -Pad * Tile;
	OriginY = -Pad * Tile;
	const int32 CellsX = (MapW + 2 * Pad) * Sub;
	const int32 CellsY = (MapH + 2 * Pad) * Sub;
	GridW = CellsX + 1;
	GridH = CellsY + 1;

	auto CellTile = [&](int32 CX, int32 CY) { return FIntPoint(FMath::FloorToInt(float(CX) / Sub) - Pad, FMath::FloorToInt(float(CY) / Sub) - Pad); };

	// Vertex heights: average of the (up to) four cells that share the vertex, so the river banks slope
	// over half a tile, plus rolling noise on land (stronger out in the hills).
	Heights.SetNumZeroed(GridW * GridH);
	for (int32 J = 0; J < GridH; ++J)
	{
		for (int32 I = 0; I < GridW; ++I)
		{
			float Sum = 0.f, MinH = 1e9f;
			int32 N = 0;
			bool bOutside = false;
			for (int32 DJ = -1; DJ <= 0; ++DJ)
			{
				for (int32 DI = -1; DI <= 0; ++DI)
				{
					const int32 CX = FMath::Clamp(I + DI, 0, CellsX - 1), CY = FMath::Clamp(J + DJ, 0, CellsY - 1);
					const FIntPoint T = CellTile(CX, CY);
					const float H = BaseHeight(T.X, T.Y);
					Sum += H; MinH = FMath::Min(MinH, H); ++N;
					bOutside |= T.X < 0 || T.Y < 0 || T.X >= MapW || T.Y >= MapH;
				}
			}
			const float X = OriginX + I * Step, Y = OriginY + J * Step;
			float H = Sum / N;
			if (MinH > RiverBedZ + 1.f) H += NoiseAt(X, Y) * (bOutside ? 2.5f : 1.f);
			else H += NoiseAt(X, Y) * 0.15f;
			Heights[J * GridW + I] = H;
		}
	}

	auto HAt = [&](int32 I, int32 J) { return Heights[FMath::Clamp(J, 0, GridH - 1) * GridW + FMath::Clamp(I, 0, GridW - 1)]; };

	// One mesh section per surface.
	enum ESection { Grass, Path, Ruins, Bed, NumSections };
	struct FSection { TArray<FVector> V; TArray<int32> Tri; TArray<FVector> N; TArray<FVector2D> UV; TArray<FProcMeshTangent> Tan; };
	FSection Sections[NumSections];

	for (int32 CY = 0; CY < CellsY; ++CY)
	{
		for (int32 CX = 0; CX < CellsX; ++CX)
		{
			const FIntPoint T = CellTile(CX, CY);
			const TCHAR C = (T.X >= 0 && T.Y >= 0 && T.X < MapW && T.Y < MapH) ? Rows[T.Y][T.X] : TEXT('.');
			ESection S = Grass;
			if (C == TEXT(',')) S = Path;
			else if (C == TEXT('r')) S = Ruins;
			else if (C == TEXT('~') || C == TEXT('=')) S = Bed;
			else if (C == TEXT('#') && T.X > 0 && T.Y > 0 && T.X < MapW - 1 && T.Y < MapH - 1) S = Ruins;
			FSection& Sec = Sections[S];

			// Quad corners A(x,y) B(x,y+1) C(x+1,y+1) D(x+1,y). In Unreal's left-handed space the
			// triangles (A,B,D) and (B,C,D) face +Z.
			const int32 Base = Sec.V.Num();
			const FIntPoint Corners[4] = { {CX, CY}, {CX, CY + 1}, {CX + 1, CY + 1}, {CX + 1, CY} };
			for (const FIntPoint& P : Corners)
			{
				const float X = OriginX + P.X * Step, Y = OriginY + P.Y * Step;
				Sec.V.Add(FVector(X, Y, HAt(P.X, P.Y)));
				const float DX = (HAt(P.X + 1, P.Y) - HAt(P.X - 1, P.Y)) / (2.f * Step);
				const float DY = (HAt(P.X, P.Y + 1) - HAt(P.X, P.Y - 1)) / (2.f * Step);
				Sec.N.Add(FVector(-DX, -DY, 1.f).GetSafeNormal());
				Sec.Tan.Add(FProcMeshTangent(FVector(1.f, 0.f, DX).GetSafeNormal(), false));
				const float UVScale = (S == Ruins) ? 300.f : 420.f;
				Sec.UV.Add(FVector2D(X / UVScale, Y / UVScale));
			}
			Sec.Tri.Append({ Base + 0, Base + 1, Base + 3, Base + 1, Base + 2, Base + 3 });
		}
	}

	UMaterialInterface* Mats[NumSections] = {
		RPGAssets::StarterMat(TEXT("M_Ground_Grass")),
		RPGAssets::StarterMat(TEXT("M_Ground_Gravel")),
		RPGAssets::StarterMat(TEXT("M_CobbleStone_Rough")),
		RPGAssets::StarterMat(TEXT("M_CobbleStone_Pebble")),
	};

	Terrain->bUseComplexAsSimpleCollision = true;
	Terrain->SetCollisionProfileName(TEXT("BlockAll"));
	for (int32 S = 0; S < NumSections; ++S)
	{
		FSection& Sec = Sections[S];
		if (Sec.V.IsEmpty()) continue;
		Terrain->CreateMeshSection(S, Sec.V, Sec.Tri, Sec.N, Sec.UV, TArray<FColor>(), Sec.Tan, /*bCreateCollision*/ true);
		Terrain->SetMaterial(S, Mats[S]);
	}
}

// ---------------------------------------------------------------------------------------------
// Water, blockers
// ---------------------------------------------------------------------------------------------

void ARPGWorldBuilder::BuildWater(const UTSData& D)
{
	// One big lake surface. The terrain sits above it everywhere except the carved river and lake.
	const float W = (MapW + 2 * Pad) * Tile, H = (MapH + 2 * Pad) * Tile;
	UStaticMeshComponent* Water = AddMesh(TSAssets::Shape(TEXT("Plane")), RPGAssets::StarterMat(TEXT("M_Water_Lake")),
		FTransform(FRotator::ZeroRotator, FVector(OriginX + W * 0.5f, OriginY + H * 0.5f, WaterZ), FVector(W / 100.f, H / 100.f, 1.f)), false);
	Water->SetCastShadow(false);
}

void ARPGWorldBuilder::BuildBlockers(const UTSData& D)
{
	// You can't wade into the river: merge each row's run of water tiles into one invisible wall.
	for (int32 Y = 0; Y < MapH; ++Y)
	{
		int32 X = 0;
		while (X < MapW)
		{
			if (Rows[Y][X] != TEXT('~')) { ++X; continue; }
			const int32 Start = X;
			while (X < MapW && Rows[Y][X] == TEXT('~')) ++X;
			const float CX = (Start + X) * 0.5f * Tile, CY = (Y + 0.5f) * Tile;
			AddBlocker(FVector(CX, CY, 0.f), FVector((X - Start) * Tile * 0.5f - 20.f, Tile * 0.5f - 20.f, 400.f));
		}
	}

	// Map edge.
	const float W = MapW * Tile, H = MapH * Tile, T = Tile;
	AddBlocker(FVector(W * 0.5f, T * 0.5f, 0), FVector(W * 0.5f, T * 0.5f, 800));
	AddBlocker(FVector(W * 0.5f, H - T * 0.5f, 0), FVector(W * 0.5f, T * 0.5f, 800));
	AddBlocker(FVector(T * 0.5f, H * 0.5f, 0), FVector(T * 0.5f, H * 0.5f, 800));
	AddBlocker(FVector(W - T * 0.5f, H * 0.5f, 0), FVector(T * 0.5f, H * 0.5f, 800));
}

// ---------------------------------------------------------------------------------------------
// Ruins
// ---------------------------------------------------------------------------------------------

void ARPGWorldBuilder::BuildRuins(const UTSData& D)
{
	UMaterialInterface* Stone = RPGAssets::StarterMat(TEXT("M_Brick_Hewn_Stone"));
	UMaterialInterface* Cap = RPGAssets::StarterMat(TEXT("M_Rock_Slate"));
	const float WallH = float(TSJson::Num(D.World(), TEXT("wallHeight"), 380.0));

	for (int32 Y = 1; Y < MapH - 1; ++Y)
	{
		for (int32 X = 1; X < MapW - 1; ++X)
		{
			if (Rows[Y][X] != TEXT('#')) continue;
			FRandomStream R = TileRand(X, Y, 1);
			const FVector C = D.TileCenter(X, Y);
			const float Z0 = GroundZ(C.X, C.Y) - 40.f;
			// Weathered: each block a different height, some broken low.
			const float H = R.FRand() < 0.18f ? R.FRandRange(90.f, 160.f) : R.FRandRange(WallH * 0.65f, WallH);
			AddBox(FVector(C.X, C.Y, Z0 + H * 0.5f), FVector(Tile + 2.f, Tile + 2.f, H), Stone);
			if (R.FRand() < 0.5f)
			{
				const float CapH = 26.f;
				AddBox(FVector(C.X + R.FRandRange(-12, 12), C.Y + R.FRandRange(-12, 12), Z0 + H + CapH * 0.5f),
					FVector(Tile * 0.9f, Tile * 0.9f, CapH), Cap, true, FRotator(R.FRandRange(-3, 3), R.FRandRange(-6, 6), 0));
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Houses: each 4-connected block of 'H' tiles is one cottage.
// ---------------------------------------------------------------------------------------------

void ARPGWorldBuilder::BuildHouses(const UTSData& D)
{
	UMaterialInterface* Footing = RPGAssets::StarterMat(TEXT("M_Brick_Cut_Stone"));
	UMaterialInterface* Plaster = RPGAssets::StarterMat(TEXT("M_Concrete_Poured"));
	UMaterialInterface* Timber = RPGAssets::StarterMat(TEXT("M_Wood_Walnut"));
	UMaterialInterface* Roof = RPGAssets::StarterMat(TEXT("M_Brick_Clay_Old"));
	UMaterialInterface* DoorMat = RPGAssets::StarterMat(TEXT("M_Wood_Oak"));
	UMaterialInterface* Glass = TSAssets::Color(this, FLinearColor(0.02f, 0.03f, 0.05f));

	TArray<bool> Seen;
	Seen.SetNumZeroed(MapW * MapH);
	for (int32 Y = 0; Y < MapH; ++Y)
	{
		for (int32 X = 0; X < MapW; ++X)
		{
			if (Rows[Y][X] != TEXT('H') || Seen[Y * MapW + X]) continue;

			// Flood fill -> bounding rectangle.
			FIntPoint Min(X, Y), Max(X, Y);
			TArray<FIntPoint> Stack = { FIntPoint(X, Y) };
			Seen[Y * MapW + X] = true;
			while (Stack.Num())
			{
				const FIntPoint P = Stack.Pop();
				Min = Min.ComponentMin(P); Max = Max.ComponentMax(P);
				const FIntPoint Next[4] = { {P.X + 1, P.Y}, {P.X - 1, P.Y}, {P.X, P.Y + 1}, {P.X, P.Y - 1} };
				for (const FIntPoint& Q : Next)
				{
					if (Q.X < 0 || Q.Y < 0 || Q.X >= MapW || Q.Y >= MapH || Seen[Q.Y * MapW + Q.X] || Rows[Q.Y][Q.X] != TEXT('H')) continue;
					Seen[Q.Y * MapW + Q.X] = true;
					Stack.Add(Q);
				}
			}

			const float Inset = 18.f;
			const FVector2D A(Min.X * Tile + Inset, Min.Y * Tile + Inset), B((Max.X + 1) * Tile - Inset, (Max.Y + 1) * Tile - Inset);
			const FVector2D Ctr = (A + B) * 0.5f, Size = B - A;
			const float Ground = FMath::Min(FMath::Min(GroundZ(A.X, A.Y), GroundZ(B.X, B.Y)), FMath::Min(GroundZ(A.X, B.Y), GroundZ(B.X, A.Y)));
			const float FootTop = Ground + 25.f, WallH = 290.f, WallTop = FootTop + WallH;

			AddBox(FVector(Ctr, Ground - 40.f + 32.5f), FVector(Size.X + 24.f, Size.Y + 24.f, 65.f + 40.f), Footing);

			// Everything above the footing can be cut away (see UpdateCutaways).
			FTSCutaway& House = Cutaways.AddDefaulted_GetRef();
			Collect = &House.Full;
			AddBox(FVector(Ctr, FootTop + WallH * 0.5f), FVector(Size.X, Size.Y, WallH), Plaster);

			// Timber frame: corner posts, intermediate posts, a sill and a top beam on every face.
			const float Beam = 22.f, Proud = 6.f;
			auto Post = [&](float PX, float PY) { AddBox(FVector(PX, PY, FootTop + WallH * 0.5f), FVector(Beam, Beam, WallH), Timber, false); };
			for (float PX = A.X; PX <= B.X + 1.f; PX += Size.X / FMath::Max(1, FMath::RoundToInt(Size.X / 230.f)))
			{
				Post(PX, A.Y - Proud); Post(PX, B.Y + Proud);
			}
			for (float PY = A.Y; PY <= B.Y + 1.f; PY += Size.Y / FMath::Max(1, FMath::RoundToInt(Size.Y / 230.f)))
			{
				Post(A.X - Proud, PY); Post(B.X + Proud, PY);
			}
			for (const float Z : { FootTop + 8.f, FootTop + WallH * 0.55f, WallTop - 8.f })
			{
				AddBox(FVector(Ctr.X, A.Y - Proud, Z), FVector(Size.X + Beam, Beam, 16.f), Timber, false);
				AddBox(FVector(Ctr.X, B.Y + Proud, Z), FVector(Size.X + Beam, Beam, 16.f), Timber, false);
				AddBox(FVector(A.X - Proud, Ctr.Y, Z), FVector(Beam, Size.Y + Beam, 16.f), Timber, false);
				AddBox(FVector(B.X + Proud, Ctr.Y, Z), FVector(Beam, Size.Y + Beam, 16.f), Timber, false);
			}

			// Gabled roof: a cube rotated 45 degrees about the long axis. Its upper half is the roof,
			// its end faces are the gable triangles, the lower half sits hidden inside the walls.
			const bool bLongX = Size.X >= Size.Y;
			const float Long = bLongX ? Size.X : Size.Y, Short = bLongX ? Size.Y : Size.X;
			const float Diag = Short + 110.f;                     // eave-to-eave width incl. overhang
			const float Side = Diag / UE_SQRT_2;
			const FRotator RoofRot = bLongX ? FRotator(0, 0, 45.f) : FRotator(45.f, 0, 0);
			const FVector RoofScale = bLongX ? FVector(Long - 4.f, Side, Side) : FVector(Side, Long - 4.f, Side);
			AddBox(FVector(Ctr, WallTop), RoofScale, Roof, true, RoofRot);

			// Chimney.
			const FVector2D ChimneyAt = bLongX ? FVector2D(B.X - 70.f, Ctr.Y - Short * 0.18f) : FVector2D(Ctr.X - Short * 0.18f, B.Y - 70.f);
			AddBox(FVector(ChimneyAt, WallTop + Diag * 0.35f), FVector(55.f, 55.f, Diag * 0.5f + 60.f), Footing);

			// Door on the south face (all village houses face the path), windows either side.
			AddBox(FVector(Ctr.X, B.Y + 10.f, FootTop + 105.f), FVector(110.f, 14.f, 210.f), DoorMat);
			AddBox(FVector(Ctr.X, B.Y + 14.f, FootTop + 215.f), FVector(140.f, 12.f, 16.f), Timber, false);
			for (const float WX : { A.X + Size.X * 0.2f, B.X - Size.X * 0.2f })
			{
				AddBox(FVector(WX, B.Y + 8.f, FootTop + 165.f), FVector(70.f, 10.f, 70.f), Glass, false);
				AddBox(FVector(WX, A.Y - 8.f, FootTop + 165.f), FVector(70.f, 10.f, 70.f), Glass, false);
			}

			// The cut-away version: a plank floor inside walls cut off at knee height.
			Collect = &House.Cut;
			const float CutH = 80.f, Thick = 26.f;
			AddBox(FVector(Ctr, FootTop + 2.f), FVector(Size.X - Thick, Size.Y - Thick, 4.f), DoorMat, false);
			AddBox(FVector(Ctr.X, A.Y + Thick * 0.5f, FootTop + CutH * 0.5f), FVector(Size.X, Thick, CutH), Plaster, false);
			AddBox(FVector(Ctr.X, B.Y - Thick * 0.5f, FootTop + CutH * 0.5f), FVector(Size.X, Thick, CutH), Plaster, false);
			AddBox(FVector(A.X + Thick * 0.5f, Ctr.Y, FootTop + CutH * 0.5f), FVector(Thick, Size.Y, CutH), Plaster, false);
			AddBox(FVector(B.X - Thick * 0.5f, Ctr.Y, FootTop + CutH * 0.5f), FVector(Thick, Size.Y, CutH), Plaster, false);
			Collect = nullptr;
			for (UStaticMeshComponent* C : House.Cut) C->SetVisibility(false);
			House.Bounds = FBox(FVector(A.X - 70.f, A.Y - 70.f, Ground), FVector(B.X + 70.f, B.Y + 70.f, WallTop + Diag * 0.5f + 80.f));
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Bridge
// ---------------------------------------------------------------------------------------------

void ARPGWorldBuilder::BuildBridge(const UTSData& D)
{
	FIntPoint Min(INT_MAX, INT_MAX), Max(INT_MIN, INT_MIN);
	for (int32 Y = 0; Y < MapH; ++Y)
		for (int32 X = 0; X < MapW; ++X)
			if (Rows[Y][X] == TEXT('=')) { Min = Min.ComponentMin({X, Y}); Max = Max.ComponentMax({X, Y}); }
	if (Min.X == INT_MAX) return;

	UMaterialInterface* Planks = RPGAssets::StarterMat(TEXT("M_Wood_Floor_Walnut_Worn"));
	UMaterialInterface* Frame = RPGAssets::StarterMat(TEXT("M_Wood_Walnut"));

	// Travel runs along Y (north-south). The deck reaches half a tile onto each bank.
	const float X0 = Min.X * Tile + 30.f, X1 = (Max.X + 1) * Tile - 30.f;
	const float Y0 = Min.Y * Tile - Tile * 0.5f, Y1 = (Max.Y + 1) * Tile + Tile * 0.5f;
	const float CX = (X0 + X1) * 0.5f, Width = X1 - X0, Length = Y1 - Y0;

	UHierarchicalInstancedStaticMeshComponent* PlankSet = MakeInstances(TSAssets::Shape(TEXT("Cube")), Planks, false);
	FRandomStream R(1234);
	for (float Y = Y0 + 16.f; Y < Y1; Y += 31.f)
	{
		PlankSet->AddInstance(FTransform(FRotator(0, R.FRandRange(-1.2f, 1.2f), R.FRandRange(-1.f, 1.f)),
			FVector(CX + R.FRandRange(-6, 6), Y, DeckZ - 5.f + R.FRandRange(-1.5f, 1.5f)), FVector(Width / 100.f, 0.27f, 0.1f)), true);
	}

	// Walkable deck (one invisible slab — the planks themselves have no collision).
	UBoxComponent* Deck = NewObject<UBoxComponent>(this);
	Deck->SetBoxExtent(FVector(Width * 0.5f, Length * 0.5f, 10.f));
	Deck->SetCollisionProfileName(TEXT("BlockAll"));
	Deck->SetupAttachment(RootComponent);
	Deck->SetWorldLocation(FVector(CX, (Y0 + Y1) * 0.5f, DeckZ - 10.f));
	Deck->RegisterComponent();

	// Stringers underneath, posts and rails on both sides.
	for (const float BX : { X0 + 40.f, CX, X1 - 40.f })
		AddBox(FVector(BX, (Y0 + Y1) * 0.5f, DeckZ - 32.f), FVector(28.f, Length, 34.f), Frame, false);
	for (const float SX : { X0 + 10.f, X1 - 10.f })
	{
		for (float Y = Y0 + 10.f; Y <= Y1 - 9.f; Y += Length / 6.f)
			AddBox(FVector(SX, Y, DeckZ + 20.f), FVector(20.f, 20.f, 180.f), Frame);
		AddBox(FVector(SX, (Y0 + Y1) * 0.5f, DeckZ + 100.f), FVector(14.f, Length, 12.f), Frame);
		AddBox(FVector(SX, (Y0 + Y1) * 0.5f, DeckZ + 55.f), FVector(10.f, Length, 8.f), Frame, false);
	}

	// Torches at the north end, where the bandits camp.
	for (const float SX : { X0 - 40.f, X1 + 40.f })
	{
		const FVector Base(SX, Y0 - 30.f, GroundZ(SX, Y0 - 30.f));
		AddBox(Base + FVector(0, 0, 90), FVector(16, 16, 180), Frame);
		AddFire(Base + FVector(0, 0, 186), 0.25f, 30.f);
	}
}

// ---------------------------------------------------------------------------------------------
// Trees, bushes, rocks
// ---------------------------------------------------------------------------------------------

void ARPGWorldBuilder::BuildTreesAndScatter(const UTSData& D)
{
	UStaticMesh* Bush = RPGAssets::StarterProp(TEXT("SM_Bush"));
	UStaticMesh* Rock = RPGAssets::StarterProp(TEXT("SM_Rock"));
	UHierarchicalInstancedStaticMeshComponent* Trunks = MakeInstances(TSAssets::Shape(TEXT("Cylinder")), RPGAssets::StarterMat(TEXT("M_Wood_Walnut")), true);
	Crowns = MakeInstances(Bush, nullptr, false);
	UHierarchicalInstancedStaticMeshComponent* Shrubs = MakeInstances(Bush, nullptr, false, 9000);
	UHierarchicalInstancedStaticMeshComponent* Rocks = MakeInstances(Rock, nullptr, true);
	UHierarchicalInstancedStaticMeshComponent* Pebbles = MakeInstances(Rock, nullptr, false, 7000);

	// HD-2D look: trees and bushes are pixel-art cards facing the camera (trunks stay, invisible, for
	// collision); rocks get the pixel stone texture.
	const bool bCards = TSLook::Mode() == TSLook::EMode::HD2D;
	auto Card = [](const FVector& At, float W, float H)
	{
		const FRotator R = TSLook::CardRotation();
		return FTransform(R, At - FRotationMatrix(R).GetUnitAxis(EAxis::Y) * (H * 0.5f), FVector(W / 100.f, H / 100.f, 1.f));
	};
	if (bCards)
	{
		UStaticMesh* Plane = TSAssets::Shape(TEXT("Plane"));
		Trunks->SetVisibility(false);
		Crowns->DestroyComponent();
		Crowns = MakeInstances(Plane, TSLook::PropMaterial(TEXT("PR_Tree1")), false);
		Shrubs->DestroyComponent();
		Shrubs = MakeInstances(Plane, TSLook::PropMaterial(TEXT("PR_Bush")), false, 9000);
		Rocks->SetMaterial(0, TSLook::PixelTexture(TEXT("rock")));
		Pebbles->SetMaterial(0, TSLook::PixelTexture(TEXT("rock")));
	}

	auto AddTree = [&](const FVector& At, FRandomStream& R, float SizeMul)
	{
		const float Height = R.FRandRange(240.f, 330.f) * SizeMul, Radius = R.FRandRange(20.f, 28.f) * SizeMul;
		Trunks->AddInstance(FTransform(FRotator(R.FRandRange(-3, 3), R.FRandRange(0, 360), R.FRandRange(-3, 3)),
			FVector(At.X, At.Y, At.Z + Height * 0.5f - 20.f), FVector(Radius / 50.f, Radius / 50.f, Height / 100.f)), true);
		if (bCards)
		{
			const float U = TSLook::SpriteUnits() * SizeMul * R.FRandRange(0.9f, 1.15f);
			const float W = 56.f * U, H = 72.f * U;
			FTSInstanceCutaway& Tree = InstanceCutaways.AddDefaulted_GetRef();
			Tree.Set = Crowns;
			const FTransform T = Card(At - FVector(0, 0, 12), W, H);
			Tree.Instances.Add(Crowns->AddInstance(T, true));
			Tree.Transforms.Add(T);
			Tree.Bounds = FBox(At + FVector(-W * 0.4f, -W * 0.4f, 80.f), At + FVector(W * 0.4f, W * 0.4f, H * 0.85f));
			return;
		}
		const int32 Clumps = R.RandRange(3, 4);
		FTSInstanceCutaway& Tree = InstanceCutaways.AddDefaulted_GetRef();
		Tree.Set = Crowns;
		for (int32 I = 0; I < Clumps; ++I)
		{
			const float S = R.FRandRange(2.4f, 3.3f) * SizeMul;
			const FVector Off(R.FRandRange(-70, 70) * SizeMul, R.FRandRange(-70, 70) * SizeMul, Height - 40.f + I * 45.f * SizeMul);
			const FTransform T(FRotator(0, R.FRandRange(0, 360), 0), At + Off, FVector(S, S, S * R.FRandRange(0.85f, 1.1f)));
			Tree.Instances.Add(Crowns->AddInstance(T, true));
			Tree.Transforms.Add(T);
		}
		// The crown's rough extent (the bush mesh is ~100uu across at scale 1).
		const float Reach = 70.f * SizeMul + 3.3f * SizeMul * 55.f;
		Tree.Bounds = FBox(At + FVector(-Reach, -Reach, Height - 140.f * SizeMul), At + FVector(Reach, Reach, Height + 300.f * SizeMul));
	};

	for (int32 Y = -Pad; Y < MapH + Pad; ++Y)
	{
		for (int32 X = -Pad; X < MapW + Pad; ++X)
		{
			FRandomStream R = TileRand(X, Y, 7);
			const bool bInside = X >= 0 && Y >= 0 && X < MapW && Y < MapH;
			const TCHAR C = bInside ? Rows[Y][X] : TEXT('o');
			const FVector Ctr = D.TileCenter(X, Y);
			auto Jitter = [&](float Amount) { const float JX = Ctr.X + R.FRandRange(-Amount, Amount), JY = Ctr.Y + R.FRandRange(-Amount, Amount); return FVector(JX, JY, GroundZ(JX, JY)); };

			if (C == TEXT('T')) { AddTree(Jitter(55.f), R, 1.f); continue; }

			if (C == TEXT('o'))
			{
				// Forest on the surrounding hills, thinning further out.
				if (R.FRand() < 0.62f) AddTree(Jitter(110.f), R, R.FRandRange(1.0f, 1.35f));
				if (R.FRand() < 0.3f) { const FVector P = Jitter(140.f); const float S = R.FRandRange(0.8f, 1.6f); Rocks->AddInstance(FTransform(FRotator(R.FRandRange(-20, 20), R.FRandRange(0, 360), 0), P - FVector(0, 0, 20), FVector(S)), true); }
				continue;
			}

			// Edge of the map ('#' border): rocky, wooded.
			if (C == TEXT('#') && (X == 0 || Y == 0 || X == MapW - 1 || Y == MapH - 1))
			{
				if (R.FRand() < 0.5f) AddTree(Jitter(80.f), R, 1.1f);
				else { const FVector P = Jitter(60.f); const float S = R.FRandRange(1.0f, 1.8f); Rocks->AddInstance(FTransform(FRotator(R.FRandRange(-15, 15), R.FRandRange(0, 360), 0), P - FVector(0, 0, 20), FVector(S)), true); }
				continue;
			}

			// Ground cover on open grass.
			if (C == TEXT('.'))
			{
				const int32 N = R.RandRange(0, 2);
				for (int32 I = 0; I < N; ++I)
				{
					const float S = R.FRandRange(0.35f, 0.75f);
					if (bCards) { const float U = TSLook::SpriteUnits() * S * 1.2f; Shrubs->AddInstance(Card(Jitter(130.f) - FVector(0, 0, 6), 24.f * U, 18.f * U), true); }
					else Shrubs->AddInstance(FTransform(FRotator(0, R.FRandRange(0, 360), 0), Jitter(130.f) - FVector(0, 0, 8), FVector(S, S, S * 0.8f)), true);
				}
				if (R.FRand() < 0.07f)
				{
					const float S = R.FRandRange(0.25f, 0.55f);
					Pebbles->AddInstance(FTransform(FRotator(R.FRandRange(-20, 20), R.FRandRange(0, 360), 0), Jitter(120.f) - FVector(0, 0, 10), FVector(S)), true);
				}
			}
			if (C == TEXT('r') && R.FRand() < 0.18f)
			{
				const float S = R.FRandRange(0.2f, 0.45f);
				Pebbles->AddInstance(FTransform(FRotator(R.FRandRange(-30, 30), R.FRandRange(0, 360), 0), Jitter(120.f) - FVector(0, 0, 6), FVector(S)), true);
			}
		}
	}

	UE_LOG(LogRPG, Display, TEXT("Vegetation: %d trees, %d shrubs, %d rocks, %d pebbles."),
		Trunks->GetInstanceCount(), Shrubs->GetInstanceCount(), Rocks->GetInstanceCount(), Pebbles->GetInstanceCount());
}

// ---------------------------------------------------------------------------------------------
// Props: campfire in the village, ambience
// ---------------------------------------------------------------------------------------------

void ARPGWorldBuilder::AddFire(const FVector& Location, float Scale, float LightIntensity)
{
	if (UParticleSystem* Fire = TSAssets::Load<UParticleSystem>(TEXT("/Game/StarterContent/Particles/P_Fire.P_Fire")))
	{
		UParticleSystemComponent* PS = NewObject<UParticleSystemComponent>(this);
		PS->SetTemplate(Fire);
		PS->SetupAttachment(RootComponent);
		PS->SetWorldLocation(Location);
		PS->SetWorldScale3D(FVector(Scale));
		PS->RegisterComponent();
		PS->ActivateSystem();
	}

	UPointLightComponent* L = NewObject<UPointLightComponent>(this);
	L->SetupAttachment(RootComponent);
	L->SetWorldLocation(Location + FVector(0, 0, 40));
	L->SetIntensityUnits(ELightUnits::Candelas);
	L->SetIntensity(LightIntensity);
	L->SetLightColor(FLinearColor(1.f, 0.55f, 0.22f));
	L->SetAttenuationRadius(900.f);
	L->SetCastShadows(false);
	L->RegisterComponent();
	AddFlicker(L, LightIntensity);
	ATSSky::AddNightLight(Location.X, Location.Y, L->AttenuationRadius);   // a fire keeps the dark back
}

void ARPGWorldBuilder::BuildProps(const UTSData& D)
{
	// Campfire beside the elder.
	for (const FTSSpawn& S : D.Spawns)
	{
		if (S.Kind != TEXT("npc") || S.Id != TEXT("elder")) continue;
		FVector At = D.TileCenter(S.X, S.Y) + FVector(Tile * 1.1f, Tile * 0.9f, 0);
		At.Z = GroundZ(At.X, At.Y);
		UStaticMesh* Rock = RPGAssets::StarterProp(TEXT("SM_Rock"));
		for (int32 I = 0; I < 8; ++I)
		{
			const float A = I * UE_TWO_PI / 8.f;
			AddMesh(Rock, nullptr, FTransform(FRotator(0, I * 47.f, 0), At + FVector(FMath::Cos(A) * 55.f, FMath::Sin(A) * 55.f, -6.f), FVector(0.16f)), false);
		}
		UMaterialInterface* Log = RPGAssets::StarterMat(TEXT("M_Wood_Walnut"));
		AddMesh(TSAssets::Shape(TEXT("Cylinder")), Log, FTransform(FRotator(90, 30, 0), At + FVector(0, 0, 10), FVector(0.16f, 0.16f, 0.8f)), false);
		AddMesh(TSAssets::Shape(TEXT("Cylinder")), Log, FTransform(FRotator(90, -40, 0), At + FVector(0, 0, 14), FVector(0.16f, 0.16f, 0.8f)), false);
		AddFire(At + FVector(0, 0, 8), 0.6f, 90.f);

		if (USoundBase* Crackle = TSAssets::Load<USoundBase>(TEXT("/Game/StarterContent/Audio/Fire01_Cue.Fire01_Cue")))
		{
			UAudioComponent* A = NewObject<UAudioComponent>(this);
			A->SetSound(Crackle);
			A->SetupAttachment(RootComponent);
			A->SetWorldLocation(At);
			A->bOverrideAttenuation = true;
			A->AttenuationOverrides.bAttenuate = true;
			A->AttenuationOverrides.FalloffDistance = 1500.f;
			A->SetVolumeMultiplier(0.6f);
			A->RegisterComponent();
			A->Play();
		}
	}

	// Gentle outdoor ambience.
	if (USoundBase* Birds = TSAssets::Load<USoundBase>(TEXT("/Game/StarterContent/Audio/Starter_Birds01.Starter_Birds01")))
	{
		UAudioComponent* A = NewObject<UAudioComponent>(this);
		A->SetSound(Birds);
		A->bAllowSpatialization = false;
		A->SetVolumeMultiplier(0.25f);
		A->SetupAttachment(RootComponent);
		A->RegisterComponent();
		A->Play();
	}
}


FString ARPGWorldBuilder::ClockText()
{
	const float GHour = ATSSky::Hour();
	const int32 H = FMath::FloorToInt(GHour), M = FMath::FloorToInt(FMath::Fmod(GHour, 1.f) * 60.f) / 10 * 10;
	const TCHAR* Part = GHour < 5.f ? TEXT("Night") : GHour < 7.f ? TEXT("Dawn") : GHour < 12.f ? TEXT("Morning") : GHour < 17.5f ? TEXT("Afternoon")
		: GHour < 20.5f ? TEXT("Dusk") : TEXT("Night");
	return FString::Printf(TEXT("%02d:%02d  %s"), H, M, Part);
}
