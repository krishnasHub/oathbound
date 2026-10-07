#include "RPGCharacterBase.h"
#include "TSData.h"
#include "RPGAssets.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

ARPGCharacterBase::ARPGCharacterBase()
{
	Blob = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Blob"));
	Blob->SetupAttachment(GetCapsuleComponent());
	Blob->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Blob->SetVisibility(false);
}

void ARPGCharacterBase::SetLookFromData(const FString& LookId)
{
	const TSJson::FObj Look = TSJson::Obj(TSJson::Obj(UTSData::Get(this).World(), TEXT("looks")), LookId);
	SetLook(TSJson::Str(Look, TEXT("mesh"), TEXT("manny")), TSJson::Str(Look, TEXT("tint")), float(TSJson::Num(Look, TEXT("scale"), 1.0)));
	const FString Sheet = TSJson::Str(TSJson::Obj(TSJson::Obj(UTSData::Get(this).World(), TEXT("looks2d")), TEXT("sheets")), LookId, LookId);
	UseSprite(Sheet);
}

void ARPGCharacterBase::SetLook(const FString& InMeshKind, const FString& TintHex, float Scale)
{
	MeshKind = InMeshKind;
	SetActorScale3D(FVector(Scale));
	BodyMaterials.Reset();

	if (MeshKind == TEXT("slime"))
	{
		// A glossy jelly blob instead of a skeleton. Squashes and stretches in Tick.
		GetMesh()->SetVisibility(false);
		GetMesh()->SetComponentTickEnabled(false);
		Blob->SetStaticMesh(TSAssets::Shape(TEXT("Sphere")));
		UMaterialInstanceDynamic* M = TSAssets::Color(this, TSJson::Color(TintHex, FLinearColor(0.3f, 0.8f, 0.3f)));
		M->SetScalarParameterValue(TEXT("Roughness"), 0.15f);
		Blob->SetMaterial(0, M);
		BodyMaterials.Add(M);
		Blob->SetRelativeLocation(FVector(0, 0, -50.f));
		Blob->SetRelativeScale3D(FVector(0.9f, 0.9f, 0.75f));
		Blob->SetVisibility(true);
		GetCapsuleComponent()->SetCapsuleSize(40.f, 50.f);
		GetCharacterMovement()->bOrientRotationToMovement = true;
		return;
	}

	USkeletalMesh* SkMesh = TSAssets::Load<USkeletalMesh>(MeshKind == TEXT("quinn") ? RPGAssets::QuinnMesh : RPGAssets::MannyMesh);
	GetMesh()->SetSkeletalMesh(SkMesh);
	if (UClass* AnimClass = LoadClass<UAnimInstance>(nullptr, RPGAssets::CombatAnimBP))
	{
		GetMesh()->SetAnimInstanceClass(AnimClass);
	}
	GetMesh()->SetVisibility(true);
	Blob->SetVisibility(false);

	if (!TintHex.IsEmpty())
	{
		const FLinearColor Tint = TSJson::Color(TintHex);
		for (int32 I = 0; I < GetMesh()->GetNumMaterials(); ++I)
		{
			UMaterialInstanceDynamic* MID = GetMesh()->CreateDynamicMaterialInstance(I);
			if (!MID) continue;
			// The mannequin material instances expose their colour as "Paint Tint" (found with -RPGProbe).
			MID->SetVectorParameterValue(TEXT("Paint Tint"), Tint);
			BodyMaterials.Add(MID);
		}
	}
}

void ARPGCharacterBase::Flash()
{
	Super::Flash();
	if (MeshKind == TEXT("slime")) Blob->SetOverlayMaterial(FlashMaterial());
}

void ARPGCharacterBase::ClearFlash()
{
	Super::ClearFlash();
	Blob->SetOverlayMaterial(nullptr);
}

void ARPGCharacterBase::Die(AActor* Killer)
{
	if (bDead) return;
	Super::Die(Killer);
	if (MeshKind != TEXT("slime"))
	{
		if (UAnimSequenceBase* Death = TSAssets::Load<UAnimSequenceBase>(RPGAssets::DeathAnim(FMath::Rand())))
		{
			GetMesh()->PlayAnimation(Death, false);
		}
	}
}

void ARPGCharacterBase::HideBody()
{
	Super::HideBody();
	if (Blob) Blob->SetVisibility(false, true);
}

void ARPGCharacterBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (MeshKind != TEXT("slime")) return;
	if (bDead)
	{
		Blob->SetRelativeScale3D(FVector(0.9f, 0.9f, 0.75f) * FMath::Max(0.f, 1.f - DeathTime * 2.5f));
		return;
	}
	// Jelly wobble, faster when moving.
	const float Speed = GetVelocity().Size2D();
	BlobPhase += DeltaSeconds * (4.f + Speed * 0.03f);
	const float S = FMath::Sin(BlobPhase) * (0.05f + FMath::Min(Speed, 300.f) * 0.0003f);
	Blob->SetRelativeScale3D(FVector(0.9f - S * 0.6f, 0.9f - S * 0.6f, 0.75f + S));
}
