#pragma once

#include "CoreMinimal.h"
#include "TSAssets.h"

class UStaticMesh;
class UMaterialInterface;

/**
 * This game's asset paths: Third Person template mannequins and montages, Starter Content. Loading and engine
 * shapes come from Tessera (TSAssets). The world is built at runtime, so nothing holds hard references to these;
 * DefaultGame.ini lists the folders so the cooker still packages them.
 */
namespace RPGAssets
{
	/** Starter Content materials, e.g. "M_Ground_Grass". */
	UMaterialInterface* StarterMat(const FString& Name);   // (HD-2D look: its pixel-art replacement)
	/** Starter Content props, e.g. "SM_Bush". */
	inline UStaticMesh* StarterProp(const FString& Name) { return TSAssets::Load<UStaticMesh>(TSAssets::ObjPath(TEXT("/Game/StarterContent/Props"), Name)); }

	// Characters (Third Person template mannequins)
	inline const TCHAR* MannyMesh = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
	inline const TCHAR* QuinnMesh = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple");
	inline const TCHAR* CombatAnimBP = TEXT("/Game/Variant_Combat/Anims/ABP_Manny_Combat.ABP_Manny_Combat_C");
	inline const TCHAR* ComboMontage = TEXT("/Game/Variant_Combat/Anims/AM_ComboAttack.AM_ComboAttack");
	inline const TCHAR* ChargedMontage = TEXT("/Game/Variant_Combat/Anims/AM_ChargedAttack.AM_ChargedAttack");
	inline const TCHAR* DashAnim = TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Dash.MM_Dash");
	inline const TCHAR* DamageFX = TEXT("/Game/Variant_Combat/VFX/NS_Damage.NS_Damage");

	inline FString DeathAnim(int32 Index)
	{
		static const TCHAR* Names[] = { TEXT("MM_Death_Front_01"), TEXT("MM_Death_Front_02"), TEXT("MM_Death_Back_01"), TEXT("MM_Death_Left_01"), TEXT("MM_Death_Right_01") };
		const FString N = Names[FMath::Abs(Index) % UE_ARRAY_COUNT(Names)];
		return TSAssets::ObjPath(TEXT("/Game/Characters/Mannequins/Anims/Death"), N);
	}
}
