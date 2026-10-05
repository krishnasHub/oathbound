#include "RPGWorldBuilder.h"
#include "ActionRPG.h"
#include "RPGData.h"
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

#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/PostProcessVolume.h"

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

ARPGWorldBuilder::ARPGWorldBuilder()
{
	PrimaryActorTick.bCanEverTick = true;
	Terrain = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Terrain"));
	RootComponent = Terrain;
}

void ARPGWorldBuilder::Build()
{
	const URPGData& D = URPGData::Get(this);
	if (!D.IsLoaded()) return;

	Tile = D.TileSize;
	Rows = D.Rows;
	MapW = D.MapW;
	MapH = D.MapH;

	const double T0 = FPlatformTime::Seconds();
	BuildTerrain(D);
	BuildWater(D);
	BuildBlockers(D);
	BuildRuins(D);
	BuildHouses(D);
	BuildBridge(D);
	BuildTreesAndScatter(D);
	BuildProps(D);
	BuildSky();
	UE_LOG(LogRPG, Display, TEXT("World built from map (%dx%d tiles, %.0fuu per tile) in %.2fs."), MapW, MapH, Tile, FPlatformTime::Seconds() - T0);
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

void ARPGWorldBuilder::BuildTerrain(const URPGData& D)
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

float ARPGWorldBuilder::GroundZ(float X, float Y) const
{
	if (Heights.IsEmpty()) return 0.f;
	const float FI = (X - OriginX) / Step, FJ = (Y - OriginY) / Step;
	const int32 I = FMath::Clamp(FMath::FloorToInt(FI), 0, GridW - 2), J = FMath::Clamp(FMath::FloorToInt(FJ), 0, GridH - 2);
	const float U = FMath::Clamp(FI - I, 0.f, 1.f), V = FMath::Clamp(FJ - J, 0.f, 1.f);
	const float H00 = Heights[J * GridW + I], H10 = Heights[J * GridW + I + 1];
	const float H01 = Heights[(J + 1) * GridW + I], H11 = Heights[(J + 1) * GridW + I + 1];
	return FMath::Lerp(FMath::Lerp(H00, H10, U), FMath::Lerp(H01, H11, U), V);
}

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

UHierarchicalInstancedStaticMeshComponent* ARPGWorldBuilder::MakeInstances(UStaticMesh* Mesh, UMaterialInterface* Material, bool bCollide, int32 CullDistance)
{
	UHierarchicalInstancedStaticMeshComponent* H = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
	H->SetStaticMesh(Mesh);
	if (Material) H->SetMaterial(0, Material);
	H->SetCollisionProfileName(bCollide ? TEXT("BlockAll") : TEXT("NoCollision"));
	if (CullDistance > 0) H->SetCullDistances(CullDistance * 3 / 4, CullDistance);
	H->SetupAttachment(RootComponent);
	H->RegisterComponent();
	return H;
}

UStaticMeshComponent* ARPGWorldBuilder::AddMesh(UStaticMesh* Mesh, UMaterialInterface* Material, const FTransform& T, bool bCollide)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(Mesh);
	if (Material) C->SetMaterial(0, Material);
	C->SetCollisionProfileName(bCollide ? TEXT("BlockAll") : TEXT("NoCollision"));
	C->SetupAttachment(RootComponent);
	C->SetWorldTransform(T);
	C->RegisterComponent();
	return C;
}

UStaticMeshComponent* ARPGWorldBuilder::AddBox(const FVector& Center, const FVector& Size, UMaterialInterface* Material, bool bCollide, const FRotator& Rot)
{
	static UStaticMesh* Cube = nullptr;
	if (!Cube) Cube = RPGAssets::Shape(TEXT("Cube"));
	// The engine cube is 100uu and centred on its pivot.
	return AddMesh(Cube, Material, FTransform(Rot, Center, Size / 100.f), bCollide);
}

void ARPGWorldBuilder::AddBlocker(const FVector& Center, const FVector& HalfExtent)
{
	UBoxComponent* B = NewObject<UBoxComponent>(this);
	B->SetBoxExtent(HalfExtent);
	B->SetCollisionProfileName(TEXT("InvisibleWall"));   // blocks pawns, not sight
	B->SetupAttachment(RootComponent);
	B->SetWorldLocation(Center);
	B->RegisterComponent();
}

// ---------------------------------------------------------------------------------------------
// Water, blockers
// ---------------------------------------------------------------------------------------------

void ARPGWorldBuilder::BuildWater(const URPGData& D)
{
	// One big lake surface. The terrain sits above it everywhere except the carved river and lake.
	const float W = (MapW + 2 * Pad) * Tile, H = (MapH + 2 * Pad) * Tile;
	UStaticMeshComponent* Water = AddMesh(RPGAssets::Shape(TEXT("Plane")), RPGAssets::StarterMat(TEXT("M_Water_Lake")),
		FTransform(FRotator::ZeroRotator, FVector(OriginX + W * 0.5f, OriginY + H * 0.5f, WaterZ), FVector(W / 100.f, H / 100.f, 1.f)), false);
	Water->SetCastShadow(false);
}

void ARPGWorldBuilder::BuildBlockers(const URPGData& D)
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

void ARPGWorldBuilder::BuildRuins(const URPGData& D)
{
	UMaterialInterface* Stone = RPGAssets::StarterMat(TEXT("M_Brick_Hewn_Stone"));
	UMaterialInterface* Cap = RPGAssets::StarterMat(TEXT("M_Rock_Slate"));
	const float WallH = float(RPGJson::Num(D.World3D(), TEXT("wallHeight"), 380.0));

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

void ARPGWorldBuilder::BuildHouses(const URPGData& D)
{
	UMaterialInterface* Footing = RPGAssets::StarterMat(TEXT("M_Brick_Cut_Stone"));
	UMaterialInterface* Plaster = RPGAssets::StarterMat(TEXT("M_Concrete_Poured"));
	UMaterialInterface* Timber = RPGAssets::StarterMat(TEXT("M_Wood_Walnut"));
	UMaterialInterface* Roof = RPGAssets::StarterMat(TEXT("M_Brick_Clay_Old"));
	UMaterialInterface* DoorMat = RPGAssets::StarterMat(TEXT("M_Wood_Oak"));
	UMaterialInterface* Glass = RPGAssets::Color(this, FLinearColor(0.02f, 0.03f, 0.05f));

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
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Bridge
// ---------------------------------------------------------------------------------------------

void ARPGWorldBuilder::BuildBridge(const URPGData& D)
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

	UHierarchicalInstancedStaticMeshComponent* PlankSet = MakeInstances(RPGAssets::Shape(TEXT("Cube")), Planks, false);
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

void ARPGWorldBuilder::BuildTreesAndScatter(const URPGData& D)
{
	UStaticMesh* Bush = RPGAssets::StarterProp(TEXT("SM_Bush"));
	UStaticMesh* Rock = RPGAssets::StarterProp(TEXT("SM_Rock"));
	UHierarchicalInstancedStaticMeshComponent* Trunks = MakeInstances(RPGAssets::Shape(TEXT("Cylinder")), RPGAssets::StarterMat(TEXT("M_Wood_Walnut")), true);
	UHierarchicalInstancedStaticMeshComponent* Crowns = MakeInstances(Bush, nullptr, false);
	UHierarchicalInstancedStaticMeshComponent* Shrubs = MakeInstances(Bush, nullptr, false, 9000);
	UHierarchicalInstancedStaticMeshComponent* Rocks = MakeInstances(Rock, nullptr, true);
	UHierarchicalInstancedStaticMeshComponent* Pebbles = MakeInstances(Rock, nullptr, false, 7000);

	auto AddTree = [&](const FVector& At, FRandomStream& R, float SizeMul)
	{
		const float Height = R.FRandRange(240.f, 330.f) * SizeMul, Radius = R.FRandRange(20.f, 28.f) * SizeMul;
		Trunks->AddInstance(FTransform(FRotator(R.FRandRange(-3, 3), R.FRandRange(0, 360), R.FRandRange(-3, 3)),
			FVector(At.X, At.Y, At.Z + Height * 0.5f - 20.f), FVector(Radius / 50.f, Radius / 50.f, Height / 100.f)), true);
		const int32 Clumps = R.RandRange(3, 4);
		for (int32 I = 0; I < Clumps; ++I)
		{
			const float S = R.FRandRange(2.4f, 3.3f) * SizeMul;
			const FVector Off(R.FRandRange(-70, 70) * SizeMul, R.FRandRange(-70, 70) * SizeMul, Height - 40.f + I * 45.f * SizeMul);
			Crowns->AddInstance(FTransform(FRotator(0, R.FRandRange(0, 360), 0), At + Off, FVector(S, S, S * R.FRandRange(0.85f, 1.1f))), true);
		}
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
					Shrubs->AddInstance(FTransform(FRotator(0, R.FRandRange(0, 360), 0), Jitter(130.f) - FVector(0, 0, 8), FVector(S, S, S * 0.8f)), true);
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
	if (UParticleSystem* Fire = RPGAssets::Load<UParticleSystem>(TEXT("/Game/StarterContent/Particles/P_Fire.P_Fire")))
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
	Flickers.Add({ L, LightIntensity, FMath::FRand() * 10.f });
}

void ARPGWorldBuilder::BuildProps(const URPGData& D)
{
	// Campfire beside the elder.
	for (const FRPGSpawn& S : D.Spawns)
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
		AddMesh(RPGAssets::Shape(TEXT("Cylinder")), Log, FTransform(FRotator(90, 30, 0), At + FVector(0, 0, 10), FVector(0.16f, 0.16f, 0.8f)), false);
		AddMesh(RPGAssets::Shape(TEXT("Cylinder")), Log, FTransform(FRotator(90, -40, 0), At + FVector(0, 0, 14), FVector(0.16f, 0.16f, 0.8f)), false);
		AddFire(At + FVector(0, 0, 8), 0.6f, 90.f);

		if (USoundBase* Crackle = RPGAssets::Load<USoundBase>(TEXT("/Game/StarterContent/Audio/Fire01_Cue.Fire01_Cue")))
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
	if (USoundBase* Birds = RPGAssets::Load<USoundBase>(TEXT("/Game/StarterContent/Audio/Starter_Birds01.Starter_Birds01")))
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

void ARPGWorldBuilder::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float T = GetWorld()->GetTimeSeconds();
	for (FFlicker& F : Flickers)
	{
		const float N = FMath::Sin(T * 13.f + F.Phase) * 0.08f + FMath::Sin(T * 7.3f + F.Phase * 2.f) * 0.1f + FMath::PerlinNoise1D(T * 4.f + F.Phase) * 0.15f;
		F.Light->SetIntensity(F.Base * (1.f + N));
	}
}

// ---------------------------------------------------------------------------------------------
// Sky and lighting — all dynamic (Lumen GI + reflections, virtual shadow maps).
// ---------------------------------------------------------------------------------------------

void ARPGWorldBuilder::BuildSky()
{
	UWorld* W = GetWorld();
	const URPGData& D = URPGData::Get(this);
	const RPGJson::FObj Sun = RPGJson::Obj(D.World3D(), TEXT("sun"));
	const FRotator SunRot(float(RPGJson::Num(Sun, TEXT("pitch"), -36)), float(RPGJson::Num(Sun, TEXT("yaw"), 125)), 0.f);

	// Sun. Spawned deferred so mobility is set before the components register.
	ADirectionalLight* SunActor = W->SpawnActorDeferred<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform(SunRot));
	UDirectionalLightComponent* SunLight = CastChecked<UDirectionalLightComponent>(SunActor->GetLightComponent());
	SunLight->SetMobility(EComponentMobility::Movable);
	SunLight->SetAtmosphereSunLight(true);
	SunLight->Intensity = float(RPGJson::Num(Sun, TEXT("intensityLux"), 9.0));
	SunLight->LightSourceAngle = 1.2f;
	SunLight->SetLightColor(FLinearColor(1.f, 0.96f, 0.9f));
	SunActor->FinishSpawning(FTransform(SunRot));

	W->SpawnActor<ASkyAtmosphere>();

	ASkyLight* Sky = W->SpawnActorDeferred<ASkyLight>(ASkyLight::StaticClass(), FTransform::Identity);
	USkyLightComponent* SkyC = Sky->GetLightComponent();
	SkyC->SetMobility(EComponentMobility::Movable);
	SkyC->bRealTimeCapture = true;
	SkyC->SourceType = SLS_CapturedScene;
	SkyC->Intensity = 1.f;
	Sky->FinishSpawning(FTransform::Identity);

	if (AVolumetricCloud* Clouds = W->SpawnActor<AVolumetricCloud>())
	{
		if (UMaterialInterface* CloudMat = RPGAssets::Load<UMaterialInterface>(TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst")))
		{
			Clouds->GetComponentByClass<UVolumetricCloudComponent>()->SetMaterial(CloudMat);
		}
	}

	if (AExponentialHeightFog* Fog = W->SpawnActor<AExponentialHeightFog>(FVector(0, 0, -200), FRotator::ZeroRotator))
	{
		UExponentialHeightFogComponent* F = Fog->GetComponent();
		F->SetFogDensity(0.012f);
		F->SetFogHeightFalloff(0.15f);
		F->SetVolumetricFog(true);
		F->SetVolumetricFogScatteringDistribution(0.5f);
		F->SetVolumetricFogExtinctionScale(0.6f);
	}

	APostProcessVolume* PP = W->SpawnActor<APostProcessVolume>();
	PP->bUnbound = true;
	FPostProcessSettings& S = PP->Settings;
	S.bOverride_BloomIntensity = true;            S.BloomIntensity = 0.45f;
	S.bOverride_VignetteIntensity = true;         S.VignetteIntensity = 0.3f;
	S.bOverride_AmbientOcclusionIntensity = true; S.AmbientOcclusionIntensity = 0.55f;
	S.bOverride_AutoExposureBias = true;          S.AutoExposureBias = -0.5f;
	S.bOverride_ColorSaturation = true;           S.ColorSaturation = FVector4(1.06f, 1.06f, 1.06f, 1.f);
	S.bOverride_SceneColorTint = true;            S.SceneColorTint = FLinearColor(1.02f, 1.0f, 0.97f);
}
