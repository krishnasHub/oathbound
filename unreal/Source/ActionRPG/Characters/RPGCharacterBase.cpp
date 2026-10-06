#include "RPGCharacterBase.h"
#include "RPGSprite.h"
#include "RPGLook.h"
#include "ActionRPG.h"
#include "RPGData.h"
#include "RPGAssets.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/PointLightComponent.h"

ARPGCharacterBase::ARPGCharacterBase()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(36.f, 90.f);

	// The mannequin's root is at its feet and it faces +Y; the capsule's origin is its centre.
	GetMesh()->SetRelativeLocationAndRotation(FVector(0, 0, -90.f), FRotator(0, -90.f, 0));
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = true;
	Move->RotationRate = FRotator(0, 720.f, 0);
	Move->MaxAcceleration = 2600.f;
	Move->BrakingDecelerationWalking = 2400.f;
	Move->GroundFriction = 8.f;
	Move->JumpZVelocity = 520.f;
	Move->AirControl = 0.35f;
	bUseControllerRotationYaw = false;

	Stats = CreateDefaultSubobject<URPGStatsComponent>(TEXT("Stats"));

	Blob = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Blob"));
	Blob->SetupAttachment(GetCapsuleComponent());
	Blob->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Blob->SetVisibility(false);
}

void ARPGCharacterBase::BeginPlay()
{
	Super::BeginPlay();
}

float ARPGCharacterBase::Radius() const
{
	return GetCapsuleComponent()->GetScaledCapsuleRadius();
}

UAnimInstance* ARPGCharacterBase::Anim() const
{
	return GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
}

float ARPGCharacterBase::PlayMontage(UAnimMontage* Montage, float Rate, FName Section)
{
	UAnimInstance* A = Anim();
	if (!A || !Montage) return 0.f;
	const float Len = A->Montage_Play(Montage, Rate);
	if (Section != NAME_None) A->Montage_JumpToSection(Section, Montage);
	return Len;
}

void ARPGCharacterBase::SetLookFromData(const FString& LookId)
{
	const RPGJson::FObj Look = RPGJson::Obj(RPGJson::Obj(URPGData::Get(this).World3D(), TEXT("looks")), LookId);
	SetLook(RPGJson::Str(Look, TEXT("mesh"), TEXT("manny")), RPGJson::Str(Look, TEXT("tint")), float(RPGJson::Num(Look, TEXT("scale"), 1.0)));
	const FString Sheet = RPGJson::Str(RPGJson::Obj(RPGJson::Obj(URPGData::Get(this).World3D(), TEXT("looks2d")), TEXT("sheets")), LookId, LookId);
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
		Blob->SetStaticMesh(RPGAssets::Shape(TEXT("Sphere")));
		UMaterialInstanceDynamic* M = RPGAssets::Color(this, RPGJson::Color(TintHex, FLinearColor(0.3f, 0.8f, 0.3f)));
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

	USkeletalMesh* SkMesh = RPGAssets::Load<USkeletalMesh>(MeshKind == TEXT("quinn") ? RPGAssets::QuinnMesh : RPGAssets::MannyMesh);
	GetMesh()->SetSkeletalMesh(SkMesh);
	if (UClass* AnimClass = LoadClass<UAnimInstance>(nullptr, RPGAssets::CombatAnimBP))
	{
		GetMesh()->SetAnimInstanceClass(AnimClass);
	}
	GetMesh()->SetVisibility(true);
	Blob->SetVisibility(false);

	if (!TintHex.IsEmpty())
	{
		const FLinearColor Tint = RPGJson::Color(TintHex);
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

void ARPGCharacterBase::SetWeaponKits(const TArray<FString>& KitIds)
{
	for (USceneComponent* C : WeaponParts) if (C) C->DestroyComponent();
	WeaponParts.Reset();
	KitMounts.Reset();
	KitGlows.Reset();
	KitParts.Reset();
	if (MeshKind == TEXT("slime")) return;

	const RPGJson::FObj W3 = URPGData::Get(this).World3D();
	auto Vec = [](const RPGJson::FObj& O, const TCHAR* K, const FVector& Def)
	{
		const TArray<TSharedPtr<FJsonValue>> A = RPGJson::Arr(O, K);
		return A.Num() == 3 ? FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber()) : Def;
	};
	UMaterialInterface* Glow = RPGAssets::Load<UMaterialInterface>(TEXT("/Game/RPG/Materials/M_RPG_Glow.M_RPG_Glow"));

	for (const FString& KitId : KitIds)
	{
		const RPGJson::FObj Kit = RPGJson::Obj(RPGJson::Obj(W3, TEXT("kits")), KitId);
		if (!Kit) continue;
		const FString Bone = RPGJson::Str(Kit, TEXT("bone"), TEXT("hand_r"));
		const RPGJson::FObj Mount = RPGJson::Obj(RPGJson::Obj(W3, TEXT("mounts")), Bone);

		// A mount per kit: positions the kit's "weapon space" (+Z out of the fist) on the bone.
		USceneComponent* Root = NewObject<USceneComponent>(this);
		Root->SetupAttachment(BodyMesh(), FName(Bone));
		// A kit can override its bone's mount (e.g. the staff stands upright instead of pointing forward).
		const FVector R = Vec(Kit, TEXT("mountRot"), Vec(Mount, TEXT("rot"), FVector::ZeroVector));
		Root->SetRelativeLocationAndRotation(Vec(Kit, TEXT("mountLoc"), Vec(Mount, TEXT("loc"), FVector::ZeroVector)), FRotator(R.X, R.Y, R.Z));
		Root->RegisterComponent();
		WeaponParts.Add(Root);
		FKitMount& KM = KitMounts.Add(KitId, { Root, FName(Bone), Root->GetRelativeTransform(), false });
		if (const RPGJson::FObj H = RPGJson::Obj(Kit, TEXT("holster")))
		{
			// Holster given as the kit's X and Z axes in the holster bone's space (easier to reason about than angles).
			KM.bHasHolster = true;
			KM.HolsterBone = FName(RPGJson::Str(H, TEXT("bone"), TEXT("pelvis")));
			KM.Holster = FTransform(FRotationMatrix::MakeFromXZ(Vec(H, TEXT("x"), FVector::ForwardVector), Vec(H, TEXT("z"), FVector::UpVector)).ToQuat(), Vec(H, TEXT("loc"), FVector::ZeroVector));
		}

		for (const TSharedPtr<FJsonValue>& V : RPGJson::Arr(Kit, TEXT("parts")))
		{
			const RPGJson::FObj Part = V->AsObject();
			UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(this);
			M->SetStaticMesh(RPGAssets::Shape(RPGJson::Str(Part, TEXT("shape"), TEXT("Cube"))));
			M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			if (RPGJson::Has(Part, TEXT("glow")))
			{
				UMaterialInstanceDynamic* G = UMaterialInstanceDynamic::Create(Glow, this);
				G->SetVectorParameterValue(TEXT("Color"), RPGJson::Color(RPGJson::Str(Part, TEXT("glow"))));
				G->SetScalarParameterValue(TEXT("Intensity"), 10.f);
				M->SetMaterial(0, G);
				UPointLightComponent* L = NewObject<UPointLightComponent>(this);
				L->SetupAttachment(M);
				L->SetIntensityUnits(ELightUnits::Candelas);
				L->SetIntensity(4.f);
				L->SetAttenuationRadius(250.f);
				L->SetLightColor(RPGJson::Color(RPGJson::Str(Part, TEXT("glow"))));
				L->SetCastShadows(false);
				L->RegisterComponent();
				WeaponParts.Add(L);
				KitGlows.Add(KitId, { M, G, L, Vec(Part, TEXT("size"), FVector(10)) / 100.f });
			}
			else M->SetMaterial(0, RPGAssets::StarterMat(RPGJson::Str(Part, TEXT("mat"), TEXT("M_Metal_Steel"))));
			M->SetupAttachment(Root);
			const FVector PR = Vec(Part, TEXT("rot"), FVector::ZeroVector);
			M->SetRelativeLocationAndRotation(Vec(Part, TEXT("loc"), FVector::ZeroVector), FRotator(PR.X, PR.Y, PR.Z));
			M->SetRelativeScale3D(Vec(Part, TEXT("size"), FVector(10)) / 100.f);
			M->RegisterComponent();
			WeaponParts.Add(M);
			if (RPGJson::Has(Part, TEXT("id"))) KitParts.Add(KitId + TEXT("/") + RPGJson::Str(Part, TEXT("id")), M);
		}
	}
}

void ARPGCharacterBase::PoseKit(const FString& KitId, bool bOffBone, const FTransform& BodyRelative)
{
	FKitMount* K = KitMounts.Find(KitId);
	if (!K || !K->Root || K->bOffBone == bOffBone) return;
	K->bOffBone = bOffBone;
	if (bOffBone)
	{
		K->Root->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		K->Root->SetRelativeTransform(BodyRelative);
	}
	else
	{
		K->Root->AttachToComponent(BodyMesh(), FAttachmentTransformRules::KeepRelativeTransform, K->Bone);
		K->Root->SetRelativeTransform(K->OnBone);
	}
}

void ARPGCharacterBase::SetKitHolstered(const FString& KitId, bool bHolstered)
{
	FKitMount* K = KitMounts.Find(KitId);
	if (!K || !K->Root || !K->bHasHolster || K->bHolstered == bHolstered) return;
	K->bHolstered = bHolstered;
	K->Root->AttachToComponent(BodyMesh(), FAttachmentTransformRules::KeepRelativeTransform, bHolstered ? K->HolsterBone : K->Bone);
	K->Root->SetRelativeTransform(bHolstered ? K->Holster : K->OnBone);
}

UStaticMeshComponent* ARPGCharacterBase::KitGlow(const FString& KitId) const
{
	const FKitGlow* G = KitGlows.Find(KitId);
	return G ? G->Mesh.Get() : nullptr;
}

void ARPGCharacterBase::SetKitGlow(const FString& KitId, float Flash)
{
	if (FKitGlow* G = KitGlows.Find(KitId))
	{
		G->Mat->SetScalarParameterValue(TEXT("Intensity"), 10.f + 30.f * Flash);
		G->Light->SetIntensity(4.f + 260.f * Flash);
		G->Mesh->SetRelativeScale3D(G->BaseScale * (1.f + 0.4f * Flash));
	}
}

void ARPGCharacterBase::Knock(const FVector& Velocity)
{
	KnockVelocity += FVector(Velocity.X, Velocity.Y, 0.f);
}

void ARPGCharacterBase::Flash()
{
	FlashTime = 0.1f;
	static UMaterialInterface* White = nullptr;
	if (!White)
	{
		White = RPGAssets::Load<UMaterialInterface>(TEXT("/Game/RPG/Materials/M_RPG_Flash.M_RPG_Flash"));
		if (White) White->AddToRoot();
	}
	if (MeshKind == TEXT("slime")) Blob->SetOverlayMaterial(White);
	else BodyMesh()->SetOverlayMaterial(White);
}

void ARPGCharacterBase::Die(AActor* Killer)
{
	if (bDead) return;
	bDead = true;
	DeathTime = 0.f;
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (MeshKind != TEXT("slime"))
	{
		if (UAnimSequenceBase* Death = RPGAssets::Load<UAnimSequenceBase>(RPGAssets::DeathAnim(FMath::Rand())))
		{
			GetMesh()->PlayAnimation(Death, false);
		}
	}
}

void ARPGCharacterBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (FlashTime > 0.f)
	{
		FlashTime -= DeltaSeconds;
		if (FlashTime <= 0.f) { BodyMesh()->SetOverlayMaterial(nullptr); Blob->SetOverlayMaterial(nullptr); }
	}

	if (bDead)
	{
		DeathTime += DeltaSeconds;
		if (MeshKind == TEXT("slime")) Blob->SetRelativeScale3D(FVector(0.9f, 0.9f, 0.75f) * FMath::Max(0.f, 1.f - DeathTime * 2.5f));
		return;
	}

	Tags.Tick(DeltaSeconds);

	// Knockback decays fast (same curve as the prototype: x0.002 per second).
	if (KnockVelocity.SizeSquared() > 25.f)
	{
		AddMovementInput(FVector::ZeroVector);
		GetCharacterMovement()->Velocity.X = KnockVelocity.X;
		GetCharacterMovement()->Velocity.Y = KnockVelocity.Y;
		KnockVelocity *= FMath::Pow(0.002f, DeltaSeconds);
	}
	else KnockVelocity = FVector::ZeroVector;

	PoiseTimer -= DeltaSeconds;
	if (PoiseTimer <= 0.f) Poise = MaxPoise;

	if (MeshKind == TEXT("slime"))
	{
		// Jelly wobble, faster when moving.
		const float Speed = GetVelocity().Size2D();
		BlobPhase += DeltaSeconds * (4.f + Speed * 0.03f);
		const float S = FMath::Sin(BlobPhase) * (0.05f + FMath::Min(Speed, 300.f) * 0.0003f);
		Blob->SetRelativeScale3D(FVector(0.9f - S * 0.6f, 0.9f - S * 0.6f, 0.75f + S));
	}
}

// ---------------------------------------------------------------------------------------------
// 2D looks
// ---------------------------------------------------------------------------------------------

void ARPGCharacterBase::UseSprite(const FString& Sheet)
{
	if (!RPGLook::IsSprite() || Sheet.IsEmpty()) return;
	if (!Sprite)
	{
		Sprite = NewObject<URPGSpriteComponent>(this, TEXT("Sprite"));
		Sprite->SetupAttachment(RootComponent);
		Sprite->RegisterComponent();
	}
	Sprite->Setup(Sheet);
	HeadZ = 100.f;
	HideBody();
}

void ARPGCharacterBase::HideBody()
{
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	GetMesh()->SetVisibility(false, true);
	if (BodyMesh() && BodyMesh() != GetMesh()) BodyMesh()->SetVisibility(false, true);
	if (Blob) Blob->SetVisibility(false, true);
}
