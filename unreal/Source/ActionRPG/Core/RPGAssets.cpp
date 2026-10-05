#include "RPGAssets.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

UMaterialInstanceDynamic* RPGAssets::Color(UObject* Outer, const FLinearColor& InColor)
{
	UMaterialInterface* Base = Load<UMaterialInterface>(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer);
	MID->SetVectorParameterValue(TEXT("Color"), InColor);
	return MID;
}
