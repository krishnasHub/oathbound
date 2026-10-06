#include "RPGFX.h"
#include "RPGAssets.h"

#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

namespace
{
	const TCHAR* GlowMat = TEXT("/Game/RPG/Materials/M_RPG_Glow.M_RPG_Glow");
	const TCHAR* SeeThroughMat = TEXT("/Game/RPG/Materials/M_RPG_Telegraph.M_RPG_Telegraph");
}

ARPGFX::ARPGFX()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

ARPGFX* ARPGFX::Make(UWorld* W, const FVector& At, EKind Kind, float Life)
{
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARPGFX* F = W->SpawnActor<ARPGFX>(At, FRotator::ZeroRotator, P);
	F->Kind = Kind;
	F->Life = Life;
	return F;
}

UStaticMeshComponent* ARPGFX::AddPart(const TCHAR* Shape, const TCHAR* Material, const FLinearColor& Color, float Intensity)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(RPGAssets::Shape(Shape));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(RPGAssets::Load<UMaterialInterface>(Material), this);
	M->SetVectorParameterValue(TEXT("Color"), Color);
	M->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	C->SetMaterial(0, M);
	C->SetupAttachment(RootComponent);
	C->RegisterComponent();
	Parts.Add(C);
	Mats.Add(M);
	return C;
}

void ARPGFX::Ring(UWorld* W, const FVector& At, float InRadius, const FLinearColor& Color, float InLife)
{
	ARPGFX* F = Make(W, At, EKind::Ring, InLife);
	F->Radius = InRadius;
	UStaticMeshComponent* Disc = F->AddPart(TEXT("Cylinder"), SeeThroughMat, Color, 4.f);
	Disc->SetRelativeScale3D(FVector(0.1f, 0.1f, 0.02f));
}

void ARPGFX::Bolt(UWorld* W, const TArray<FVector>& Points, const FLinearColor& Color, float InLife)
{
	if (Points.Num() < 2) return;
	ARPGFX* F = Make(W, Points[0], EKind::Bolt, InLife);
	for (int32 I = 1; I < Points.Num(); ++I)
	{
		// A few jagged sub-segments per hop.
		FVector A = Points[I - 1];
		for (int32 S = 1; S <= 4; ++S)
		{
			FVector B = FMath::Lerp(Points[I - 1], Points[I], S / 4.f);
			if (S < 4) B += FVector(FMath::FRandRange(-25.f, 25.f), FMath::FRandRange(-25.f, 25.f), FMath::FRandRange(-20.f, 20.f));
			UStaticMeshComponent* Seg = F->AddPart(TEXT("Cylinder"), GlowMat, Color, 25.f);
			const FVector Mid = (A + B) * 0.5f, Dir = B - A;
			Seg->SetWorldLocationAndRotation(Mid, FRotationMatrix::MakeFromZ(Dir.GetSafeNormal()).Rotator());
			Seg->SetWorldScale3D(FVector(0.035f, 0.035f, Dir.Size() / 100.f));
			A = B;
		}
	}
	F->Light = NewObject<UPointLightComponent>(F);
	F->Light->SetupAttachment(F->RootComponent);
	F->Light->SetWorldLocation(Points.Last());
	F->Light->SetIntensityUnits(ELightUnits::Candelas);
	F->Light->SetLightColor(Color);
	F->Light->SetIntensity(F->LightBase = 200.f);
	F->Light->SetAttenuationRadius(700.f);
	F->Light->SetCastShadows(false);
	F->Light->RegisterComponent();
}

void ARPGFX::Burst(UWorld* W, const FVector& At, float InRadius, const FLinearColor& Color, float InLife)
{
	ARPGFX* F = Make(W, At, EKind::Burst, InLife);
	F->Radius = InRadius;
	UStaticMeshComponent* S = F->AddPart(TEXT("Sphere"), SeeThroughMat, Color, 6.f);
	S->SetRelativeScale3D(FVector(InRadius / 50.f));
	F->Light = NewObject<UPointLightComponent>(F);
	F->Light->SetupAttachment(F->RootComponent);
	F->Light->SetIntensityUnits(ELightUnits::Candelas);
	F->Light->SetLightColor(Color);
	F->Light->SetIntensity(F->LightBase = 120.f);
	F->Light->SetAttenuationRadius(InRadius * 6.f);
	F->Light->SetCastShadows(false);
	F->Light->RegisterComponent();
}

void ARPGFX::Smoke(UWorld* W, const FVector& At, float InRadius, float Duration)
{
	ARPGFX* F = Make(W, At, EKind::Smoke, Duration + 2.f);   // + SmokeFade
	if (UParticleSystem* PS = RPGAssets::Load<UParticleSystem>(TEXT("/Game/StarterContent/Particles/P_Smoke.P_Smoke")))
	{
		for (int32 I = 0; I < 4; ++I)
		{
			const FVector Off(FMath::FRandRange(-InRadius, InRadius) * 0.4f, FMath::FRandRange(-InRadius, InRadius) * 0.4f, 0);
			if (UParticleSystemComponent* E = UGameplayStatics::SpawnEmitterAttached(PS, F->RootComponent, NAME_None, Off, FRotator::ZeroRotator,
				FVector(InRadius / 150.f), EAttachLocation::KeepRelativeOffset, /*bAutoDestroy*/ false))
				F->Emitters.Add(E);
		}
	}
	Ring(W, At, InRadius, FLinearColor(0.6f, 0.62f, 0.66f), 0.8f);
}

void ARPGFX::Tick(float Dt)
{
	Super::Tick(Dt);
	Age += Dt;
	const float K = FMath::Clamp(Age / Life, 0.f, 1.f);
	if (Kind == EKind::Ring && Parts.Num())
	{
		const float R = Radius * (0.3f + 0.7f * K);
		Parts[0]->SetRelativeScale3D(FVector(R / 50.f, R / 50.f, 0.02f));
		Mats[0]->SetScalarParameterValue(TEXT("Opacity"), 0.55f * (1.f - K));
	}
	else if (Kind == EKind::Burst && Parts.Num())
	{
		Parts[0]->SetRelativeScale3D(FVector(Radius / 50.f * (1.f + K * 0.6f)));
		Mats[0]->SetScalarParameterValue(TEXT("Opacity"), 0.5f * (1.f - K));
	}
	else if (Kind == EKind::Bolt)
	{
		for (UMaterialInstanceDynamic* M : Mats) M->SetScalarParameterValue(TEXT("Intensity"), 25.f * (1.f - K));
	}
	else if (Kind == EKind::Smoke && !bSmokeStopped && Age >= Life - SmokeFade)
	{
		// The cloud is done: stop making smoke and let what's in the air drift off before the actor goes.
		bSmokeStopped = true;
		for (UParticleSystemComponent* E : Emitters) if (E) E->Deactivate();
	}
	if (Light) Light->SetIntensity(LightBase * (1.f - K));
	if (Age >= Life) Destroy();
}
