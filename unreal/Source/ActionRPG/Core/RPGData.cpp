#include "RPGData.h"
#include "ActionRPG.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"

void URPGData::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const FString Path = FPaths::ProjectContentDir() / TEXT("Data/game-data.json");
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogRPG, Error, TEXT("Could not read %s — run tools/sync-data.js"), *Path);
		return;
	}

	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogRPG, Error, TEXT("game-data.json is not valid JSON: %s"), *Reader->GetErrorMessage());
		return;
	}

	UnitsPerPx = float(RPGJson::Num(World3D(), TEXT("unitsPerPx"), 3.5));
	TileSize = float(RPGJson::Num(World3D(), TEXT("tileSize"), 300.0));
	ParseMap();

	UE_LOG(LogRPG, Display, TEXT("Game data loaded: %d enemies, %d abilities, %d dialogue nodes, map %dx%d (%d spawns)."),
		Section(TEXT("enemies"))->Values.Num(), Section(TEXT("abilities"))->Values.Num(),
		Section(TEXT("dialogue"))->Values.Num(), MapW, MapH, Spawns.Num());
}

URPGData& URPGData::Get(const UObject* WorldContext)
{
	const UGameInstance* GI = UGameplayStatics::GetGameInstance(WorldContext);
	check(GI);
	return *GI->GetSubsystem<URPGData>();
}

void URPGData::ParseMap()
{
	const RPGJson::FObj Map = Section(TEXT("map"));
	const RPGJson::FObj SpawnDefs = RPGJson::Obj(Map, TEXT("spawns"));

	for (const TSharedPtr<FJsonValue>& V : RPGJson::Arr(Map, TEXT("rows")))
	{
		Rows.Add(V->AsString());
	}
	MapH = Rows.Num();
	for (const FString& R : Rows) MapW = FMath::Max(MapW, R.Len());

	for (int32 Y = 0; Y < MapH; ++Y)
	{
		FString& Row = Rows[Y];
		while (Row.Len() < MapW) Row.AppendChar(TEXT('.'));
		for (int32 X = 0; X < MapW; ++X)
		{
			const FString Key = Row.Mid(X, 1);
			FString Def;
			if (!SpawnDefs.IsValid() || !SpawnDefs->TryGetStringField(Key, Def)) continue;

			FRPGSpawn S;
			S.X = X; S.Y = Y;
			if (!Def.Split(TEXT(":"), &S.Kind, &S.Id)) { S.Kind = Def; }
			Spawns.Add(S);

			// The marker tile becomes whatever floor is to its left (same rule as the prototype).
			const TCHAR Left = X > 0 ? Row[X - 1] : TEXT('.');
			const bool bLeftWalkable = Left != TEXT('#') && Left != TEXT('T') && Left != TEXT('H') && Left != TEXT('~');
			Row[X] = bLeftWalkable ? Left : TEXT('.');
		}
	}
}

TCHAR URPGData::TileAt(int32 X, int32 Y) const
{
	if (X < 0 || Y < 0 || Y >= MapH || X >= MapW) return TEXT('#');
	return Rows[Y][X];
}

FString URPGData::RegionAt(float WorldY) const
{
	const int32 Row = FMath::FloorToInt(WorldY / TileSize);
	for (const TSharedPtr<FJsonValue>& V : RPGJson::Arr(Section(TEXT("map")), TEXT("regions")))
	{
		const RPGJson::FObj R = V->AsObject();
		if (Row <= int32(RPGJson::Num(R, TEXT("maxRow"), 9999))) return RPGJson::Str(R, TEXT("id"));
	}
	return TEXT("north");
}
