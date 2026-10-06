#include "RPGLook.h"
#include "RPGData.h"
#include "RPGAssets.h"
#include "ActionRPG.h"

#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	RPGLook::EMode GMode = RPGLook::EMode::Mesh3D;
	RPGJson::FObj GCfg;            // world3d.looks2d
	RPGJson::FObj GTopDown;        // world3d.camera.topdown
	TMap<FString, TStrongObjectPtr<UMaterialInstanceDynamic>> GCache;

	const TCHAR* SpriteMat = TEXT("/Game/RPG/Materials/M_RPG_Sprite.M_RPG_Sprite");
	const TCHAR* PixelMat = TEXT("/Game/RPG/Materials/M_RPG_PixelWorld.M_RPG_PixelWorld");

	UTexture2D* PixelTex(const FString& Name) { return RPGAssets::Load<UTexture2D>(RPGAssets::ObjPath(TEXT("/Game/RPG/Pixel"), Name)); }
}

void RPGLook::Init(const UObject* WorldContext)
{
	const URPGData& D = URPGData::Get(WorldContext);
	GCfg = RPGJson::Obj(D.World3D(), TEXT("looks2d"));
	GTopDown = RPGJson::Obj(RPGJson::Obj(D.World3D(), TEXT("camera")), TEXT("topdown"));
	FString Name = RPGJson::Str(D.World3D(), TEXT("look"), TEXT("mesh3d"));
	FParse::Value(FCommandLine::Get(), TEXT("RPGLook="), Name);
	GMode = Name == TEXT("hd2d") ? EMode::HD2D : Name == TEXT("flat2d") ? EMode::Flat2D : EMode::Mesh3D;
	GCache.Reset();
	UE_LOG(LogRPG, Display, TEXT("Look: %s"), ModeName());
}

RPGLook::EMode RPGLook::Mode() { return GMode; }
const TCHAR* RPGLook::ModeName() { return GMode == EMode::HD2D ? TEXT("hd2d") : GMode == EMode::Flat2D ? TEXT("flat2d") : TEXT("mesh3d"); }

float RPGLook::SpriteUnits()
{
	return float(RPGJson::Num(RPGJson::Obj(GCfg, TEXT("spriteUnits")), ModeName(), 7.0));
}

FRotator RPGLook::CameraRotation()
{
	const float Yaw = float(RPGJson::Num(GTopDown, TEXT("yaw"), -90));
	if (GMode == EMode::Flat2D) return FRotator(-90.f, Yaw, 0.f);
	if (GMode == EMode::HD2D) return FRotator(float(RPGJson::Num(RPGJson::Obj(GCfg, TEXT("hd2dCamera")), TEXT("pitch"), -40)), Yaw, 0.f);
	return FRotator(float(RPGJson::Num(GTopDown, TEXT("pitch"), -58)), Yaw, 0.f);
}

FRotator RPGLook::CardRotation()
{
	// The engine Plane lies in its local XY (normal +Z, U along +X, V along +Y). Lay U along screen-right,
	// V down the screen, and the normal toward the camera.
	const FRotationMatrix Cam(CameraRotation());
	const FVector Right = Cam.GetUnitAxis(EAxis::Y);
	FVector Up = Cam.GetUnitAxis(EAxis::Z);
	if (GMode == EMode::Flat2D) Up = FRotationMatrix(FRotator(0, CameraRotation().Yaw, 0)).GetUnitAxis(EAxis::X);   // the picture's top points north
	return FRotationMatrix::MakeFromXY(Right, -Up).Rotator();
}

float RPGLook::FlatSortZ(float WorldY)
{
	return 30.f + WorldY * 0.02f;
}

UMaterialInterface* RPGLook::PixelTexture(const FString& TexName)
{
	const FString Key = TEXT("world:") + TexName;
	if (const TStrongObjectPtr<UMaterialInstanceDynamic>* Hit = GCache.Find(Key)) return Hit->Get();
	UMaterialInterface* Base = RPGAssets::Load<UMaterialInterface>(PixelMat);
	UTexture2D* Tex = PixelTex(TEXT("TX_") + TexName);
	if (!Base || !Tex) return nullptr;
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, GetTransientPackage());
	MID->SetTextureParameterValue(TEXT("Tex"), Tex);
	MID->SetScalarParameterValue(TEXT("Size"), float(RPGJson::Num(GCfg, TEXT("pixelSize"), 200)));
	GCache.Add(Key, TStrongObjectPtr<UMaterialInstanceDynamic>(MID));
	return MID;
}

UMaterialInterface* RPGLook::PixelMaterial(const FString& StarterName)
{
	if (GMode != EMode::HD2D) return nullptr;
	const FString Tex = RPGJson::Str(RPGJson::Obj(GCfg, TEXT("pixelMaterials")), StarterName);
	return Tex.IsEmpty() ? nullptr : PixelTexture(Tex);
}

UMaterialInstanceDynamic* RPGLook::SpriteMaterial(UObject* Outer, const FString& Texture, int32 Cols, int32 Rows)
{
	UMaterialInterface* Base = RPGAssets::Load<UMaterialInterface>(SpriteMat);
	UTexture2D* Tex = PixelTex(Texture);
	if (!Base || !Tex) return nullptr;
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer);
	MID->SetTextureParameterValue(TEXT("Tex"), Tex);
	MID->SetScalarParameterValue(TEXT("Cols"), float(Cols));
	MID->SetScalarParameterValue(TEXT("Rows"), float(Rows));
	return MID;
}

UMaterialInterface* RPGLook::PropMaterial(const FString& Texture)
{
	const FString Key = TEXT("prop:") + Texture;
	if (const TStrongObjectPtr<UMaterialInstanceDynamic>* Hit = GCache.Find(Key)) return Hit->Get();
	UMaterialInstanceDynamic* MID = SpriteMaterial(GetTransientPackage(), Texture);
	if (MID) GCache.Add(Key, TStrongObjectPtr<UMaterialInstanceDynamic>(MID));
	return MID;
}
