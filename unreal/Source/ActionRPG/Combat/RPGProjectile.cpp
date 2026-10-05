#include "RPGProjectile.h"
#include "RPGAssets.h"
#include "RPGCharacterBase.h"
#include "RPGPlayerCharacter.h"

#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/CapsuleComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Engine/World.h"

ARPGProjectile::ARPGProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastShadow(false);
	RootComponent = Body;
	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Body);
	Glow->SetCastShadows(false);
	Glow->SetIntensityUnits(ELightUnits::Candelas);
}

ARPGProjectile* ARPGProjectile::Fire(ARPGCharacterBase* Owner, const FVector& From, const FVector& Dir, float Speed, float Range,
	float Radius, const FLinearColor& InColor, bool bArrow, const FRPGHit& InHit)
{
	UWorld* W = Owner->GetWorld();
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARPGProjectile* Pr = W->SpawnActor<ARPGProjectile>(From, Dir.Rotation(), P);
	Pr->Shooter = Owner;
	Pr->bFromPlayer = Owner->Team == ERPGTeam::Player;
	Pr->Velocity = Dir.GetSafeNormal() * Speed;
	Pr->Life = Range / FMath::Max(Speed, 1.f);
	Pr->HitRadius = FMath::Max(Radius, 8.f);
	Pr->Hit = InHit;
	Pr->Color = InColor;

	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(RPGAssets::Load<UMaterialInterface>(TEXT("/Game/RPG/Materials/M_RPG_Glow.M_RPG_Glow")), Pr);
	M->SetVectorParameterValue(TEXT("Color"), InColor);
	if (bArrow)
	{
		// Shaft: a thin cylinder lying along the flight direction, faint glow so it reads against grass.
		Pr->Body->SetStaticMesh(RPGAssets::Shape(TEXT("Cylinder")));
		Pr->Body->SetRelativeScale3D(FVector(0.03f, 0.03f, 0.75f));
		Pr->Body->SetWorldRotation(FRotationMatrix::MakeFromZ(Dir).Rotator());
		M->SetScalarParameterValue(TEXT("Intensity"), 1.5f);
		Pr->Glow->SetIntensity(0.f);
	}
	else
	{
		Pr->Body->SetStaticMesh(RPGAssets::Shape(TEXT("Sphere")));
		Pr->Body->SetRelativeScale3D(FVector(Radius * 2.f / 100.f));
		M->SetScalarParameterValue(TEXT("Intensity"), 14.f);
		Pr->Glow->SetLightColor(InColor);
		Pr->Glow->SetIntensity(60.f);
		Pr->Glow->SetAttenuationRadius(500.f);
	}
	Pr->Body->SetMaterial(0, M);
	return Pr;
}

void ARPGProjectile::Burst()
{
	if (UNiagaraSystem* FX = RPGAssets::Load<UNiagaraSystem>(RPGAssets::DamageFX))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), FX, GetActorLocation(), FRotator::ZeroRotator, FVector(0.5f));
	}
	Destroy();
}

void ARPGProjectile::Tick(float Dt)
{
	Super::Tick(Dt);
	Life -= Dt;
	const FVector Prev = GetActorLocation();
	const FVector Next = Prev + Velocity * Dt;

	FHitResult WorldHit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(RPGProjectile), false, this);
	if (Life <= 0.f || GetWorld()->LineTraceSingleByChannel(WorldHit, Prev, Next, ECC_Visibility, Q))
	{
		if (WorldHit.bBlockingHit) SetActorLocation(WorldHit.ImpactPoint);
		Burst();
		return;
	}
	SetActorLocation(Next);

	ARPGCharacterBase* Src = Shooter.Get();
	if (!Src) { Destroy(); return; }
	for (ARPGCharacterBase* T : RPGCombat::Opponents(Src))
	{
		if (PassedThrough.Contains(T)) continue;
		// Distance from the projectile to the target's capsule axis.
		const UCapsuleComponent* C = T->GetCapsuleComponent();
		const float Half = C->GetScaledCapsuleHalfHeight() - C->GetScaledCapsuleRadius();
		const FVector A = T->GetActorLocation() - FVector(0, 0, Half), B = T->GetActorLocation() + FVector(0, 0, Half);
		if (FMath::PointDistToSegment(Next, A, B) > C->GetScaledCapsuleRadius() + HitRadius) continue;

		FRPGHit H = Hit;
		H.Dir = Velocity.GetSafeNormal2D();
		H.From = Prev - Velocity * 0.05f;
		if (RPGCombat::Deal(Src, T, H)) { Burst(); return; }
		PassedThrough.Add(T);   // dodged through it
	}
}
