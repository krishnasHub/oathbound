#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "RPGAnimNotifies.generated.h"

/**
 * Implemented by characters that play the combat montages. The montage notifies below call into it.
 */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class URPGAttacker : public UInterface { GENERATED_BODY() };

class IRPGAttacker
{
	GENERATED_BODY()
public:
	/** The swing's hit moment. */
	virtual void DoAttackTrace(FName SourceBone) = 0;
	/** Window where a buffered attack input continues the combo. */
	virtual void CheckCombo() = 0;
	virtual void CheckChargedAttack() {}
};

/*
 * The three notifies below keep the class and property names the Third Person template's montages
 * (AM_ComboAttack / AM_ChargedAttack) were saved with. Config/DefaultEngine.ini redirects
 * /Script/TP_ThirdPerson -> /Script/ActionRPG, so those montages instantiate these classes.
 */

UCLASS()
class ACTIONRPG_API UAnimNotify_DoAttackTrace : public UAnimNotify
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Attack")
	FName AttackBoneName;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("Do Attack Trace"); }
};

UCLASS()
class ACTIONRPG_API UAnimNotify_CheckCombo : public UAnimNotify
{
	GENERATED_BODY()
public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("Check Combo String"); }
};

UCLASS()
class ACTIONRPG_API UAnimNotify_CheckChargedAttack : public UAnimNotify
{
	GENERATED_BODY()
public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("Check Charged Attack"); }
};
