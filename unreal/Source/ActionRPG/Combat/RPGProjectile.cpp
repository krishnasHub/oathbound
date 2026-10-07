#include "RPGProjectile.h"
#include "RPGAssets.h"
#include "RPGCharacterBase.h"
#include "RPGPlayerCharacter.h"
#include "TSData.h"

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
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastShadow(false);
	Body->SetupAttachment(Root);
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

	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(TSAssets::Load<UMaterialInterface>(TEXT("/Game/RPG/Materials/M_RPG_Glow.M_RPG_Glow")), Pr);
	M->SetVectorParameterValue(TEXT("Color"), InColor);
	if (bArrow)
	{
		// Shaft: a thin cylinder lying along the flight direction, faint glow so it reads against grass.
		Pr->Body->SetStaticMesh(TSAssets::Shape(TEXT("Cylinder")));
		Pr->Body->SetRelativeScale3D(FVector(0.03f, 0.03f, 0.75f));
		Pr->Body->SetWorldRotation(FRotationMatrix::MakeFromZ(Dir).Rotator());
		M->SetScalarParameterValue(TEXT("Intensity"), 1.5f);
		Pr->Glow->SetIntensity(0.f);
	}
	else
	{
		Pr->Body->SetStaticMesh(TSAssets::Shape(TEXT("Sphere")));
		Pr->Body->SetRelativeScale3D(FVector(Radius * 2.f / 100.f));
		M->SetScalarParameterValue(TEXT("Intensity"), 14.f);
		Pr->Glow->SetLightColor(InColor);
		Pr->Glow->SetIntensity(60.f);
		Pr->Glow->SetAttenuationRadius(500.f);
	}
	Pr->Body->SetMaterial(0, M);
	return Pr;
}

void ARPGProjectile::ArcLaunch(const UObject* Ctx, const FVector& From, const FVector& To, float Speed, FVector& OutVel, float& OutG, float& OutT)
{
	// Horizontal speed is constant, so the flight time is distance / speed. Gravity is picked so the arc rises
	// Height above the straight line at mid-flight (g T^2 / 8), and the vertical launch so it ends at To.
	const TSJson::FObj A = TSJson::Obj(UTSData::Get(Ctx).World(), TEXT("arrows"));
	const float D2 = FVector::Dist2D(From, To);
	OutT = FMath::Max(D2 / FMath::Max(Speed, 1.f), 0.12f);
	const float Height = FMath::Clamp(D2 * float(TSJson::Num(A, TEXT("heightPerDistance"), 0.16)),
		float(TSJson::Num(A, TEXT("minHeight"), 30)), float(TSJson::Num(A, TEXT("maxHeight"), 320)));
	OutG = 8.f * Height / (OutT * OutT);
	const FVector Flat = FVector(To.X - From.X, To.Y - From.Y, 0.f) / OutT;
	OutVel = FVector(Flat.X, Flat.Y, (To.Z - From.Z) / OutT + 0.5f * OutG * OutT);
}

UStaticMeshComponent* ARPGProjectile::AddArrowPart(const TCHAR* Shape, const FLinearColor& Col, const FVector& Loc, const FRotator& Rot, const FVector& Scale)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(TSAssets::Shape(Shape));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetMaterial(0, TSAssets::Color(this, Col));
	C->SetupAttachment(Root);
	C->SetRelativeLocationAndRotation(Loc, Rot);
	C->SetRelativeScale3D(Scale);
	C->RegisterComponent();
	ArrowParts.Add(C);
	return C;
}

ARPGProjectile* ARPGProjectile::FireArrow(ARPGCharacterBase* Owner, const FVector& From, const FVector& To, float Speed,
	float Radius, const FLinearColor& InColor, const FRPGHit& InHit)
{
	FVector V;
	float G = 0.f, T = 1.f;
	ArcLaunch(Owner, From, To, Speed, V, G, T);
	UWorld* W = Owner->GetWorld();
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARPGProjectile* Pr = W->SpawnActor<ARPGProjectile>(From, V.Rotation(), P);
	Pr->Shooter = Owner;
	Pr->bFromPlayer = Owner->Team == ERPGTeam::Player;
	Pr->bArrow = true;
	Pr->Velocity = V;
	Pr->Gravity = G;
	Pr->Life = T * 2.5f + 1.f;   // past the target it keeps falling until it hits something
	Pr->HitRadius = FMath::Max(Radius, 8.f);
	Pr->Hit = InHit;
	Pr->Color = InColor;
	Pr->Body->SetVisibility(false);
	Pr->Glow->SetIntensity(0.f);

	// A chunky, readable arrow along the actor's X (it turns to follow the flight): shaft, head, fletching.
	// Exaggerated so it reads from the game camera; it casts a real shadow, which shows how high it is.
	const FRotator AlongX(-90.f, 0.f, 0.f);   // the engine cylinder and cone run along Z
	Pr->AddArrowPart(TEXT("Cylinder"), FLinearColor(0.42f, 0.26f, 0.13f), FVector::ZeroVector, AlongX, FVector(0.08f, 0.08f, 1.45f));
	Pr->AddArrowPart(TEXT("Cone"), FLinearColor(0.82f, 0.84f, 0.88f), FVector(82.f, 0, 0), AlongX, FVector(0.2f, 0.2f, 0.26f));
	Pr->AddArrowPart(TEXT("Cube"), InColor, FVector(-60.f, 0, 0), FRotator::ZeroRotator, FVector(0.32f, 0.025f, 0.16f));
	Pr->AddArrowPart(TEXT("Cube"), InColor, FVector(-60.f, 0, 0), FRotator(0, 0, 90.f), FVector(0.32f, 0.025f, 0.16f));
	// A faint streak behind it, so a shot reads as a shot.
	Pr->Trail = NewObject<UStaticMeshComponent>(Pr);
	Pr->Trail->SetStaticMesh(TSAssets::Shape(TEXT("Cylinder")));
	Pr->Trail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Pr->Trail->SetCastShadow(false);
	UMaterialInstanceDynamic* TM = UMaterialInstanceDynamic::Create(TSAssets::Load<UMaterialInterface>(TEXT("/Game/RPG/Materials/M_RPG_Telegraph.M_RPG_Telegraph")), Pr);
	TM->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(InColor, FLinearColor::White, 0.5f));
	TM->SetScalarParameterValue(TEXT("Intensity"), 1.2f);
	TM->SetScalarParameterValue(TEXT("Opacity"), 0.28f);
	Pr->Trail->SetMaterial(0, TM);
	Pr->Trail->SetupAttachment(Pr->Root);
	Pr->Trail->SetRelativeLocationAndRotation(FVector(-190.f, 0, 0), AlongX);
	Pr->Trail->SetRelativeScale3D(FVector(0.035f, 0.035f, 2.2f));
	Pr->Trail->RegisterComponent();
	return Pr;
}

void ARPGProjectile::Burst()
{
	if (UNiagaraSystem* FX = TSAssets::Load<UNiagaraSystem>(RPGAssets::DamageFX))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), FX, GetActorLocation(), FRotator::ZeroRotator, FVector(0.5f));
	}
	Destroy();
}

void ARPGProjectile::Tick(float Dt)
{
	Super::Tick(Dt);
	if (bStuck)
	{
		StuckTime -= Dt;
		if (StuckTime <= 0.f) Destroy();
		return;
	}
	Life -= Dt;
	const FVector Prev = GetActorLocation();
	Velocity.Z -= Gravity * Dt;
	const FVector Next = Prev + Velocity * Dt;
	if (bArrow) SetActorRotation(Velocity.Rotation());   // nose follows the arc

	FHitResult WorldHit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(RPGProjectile), false, this);
	const bool bWorld = GetWorld()->LineTraceSingleByChannel(WorldHit, Prev, Next, ECC_Visibility, Q);
	const FVector End = bWorld ? WorldHit.ImpactPoint : Next;

	// Characters along this step (before the world: a falling arrow can graze the ground just short of a
	// small target's body).
	ARPGCharacterBase* Src = Shooter.Get();
	if (!Src) { Destroy(); return; }
	for (ARPGCharacterBase* T : RPGCombat::Opponents(Src))
	{
		if (PassedThrough.Contains(T)) continue;
		// Closest approach between this step and the target's capsule axis.
		const UCapsuleComponent* C = T->GetCapsuleComponent();
		const float Half = C->GetScaledCapsuleHalfHeight() - C->GetScaledCapsuleRadius();
		const FVector A = T->GetActorLocation() - FVector(0, 0, Half), B = T->GetActorLocation() + FVector(0, 0, Half);
		FVector OnStep, OnAxis;
		FMath::SegmentDistToSegmentSafe(Prev, End, A, B, OnStep, OnAxis);
		if (FVector::Dist(OnStep, OnAxis) > C->GetScaledCapsuleRadius() + HitRadius) continue;

		SetActorLocation(OnStep);
		FRPGHit H = Hit;
		H.Dir = Velocity.GetSafeNormal2D();
		H.From = Prev - Velocity * 0.05f;
		if (RPGCombat::Deal(Src, T, H)) { Burst(); return; }
		PassedThrough.Add(T);   // dodged through it
	}

	if (Life <= 0.f || bWorld)
	{
		if (bArrow && bWorld)
		{
			// A miss sticks where it lands (tip buried), then disappears.
			SetActorLocation(WorldHit.ImpactPoint - Velocity.GetSafeNormal() * 25.f);
			bStuck = true;
			StuckTime = 2.5f;
			if (Trail) Trail->SetVisibility(false);
			return;
		}
		if (bWorld) SetActorLocation(WorldHit.ImpactPoint);
		Burst();
		return;
	}
	SetActorLocation(Next);
}
