#include "RPGAssets.h"
#include "TSLook.h"
#include "Materials/MaterialInterface.h"

UMaterialInterface* RPGAssets::StarterMat(const FString& Name)
{
	if (UMaterialInterface* Pixel = TSLook::PixelMaterial(Name)) return Pixel;
	return TSAssets::Load<UMaterialInterface>(TSAssets::ObjPath(TEXT("/Game/StarterContent/Materials"), Name));
}
