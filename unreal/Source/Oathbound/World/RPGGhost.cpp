#include "RPGGhost.h"
#include "TSCharacter.h"
#include "TSAssets.h"
#include "TSData.h"
#include "TSLook.h"
#include "TSFeedback.h"
#include "Tessera.h"
#include "RPGEnemy.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

ARPGGhost::ARPGGhost()
{
	PrimaryActorTick.bCanEverTick = true;
	Card = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Card"));
	RootComponent = Card;
	Card->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Card->SetCastShadow(false);
	Card->SetVisibility(false);
}

ARPGGhost* ARPGGhost::Rise(ATSCharacter* Body, bool bCalm)
{
	UWorld* W = Body ? Body->GetWorld() : nullptr;
	if (!W) return nullptr;
	const UTSData& D = UTSData::Get(Body);
	const TSJson::FObj G = TSJson::Obj(D.World(), TEXT("deathGhost"));
	UMaterialInterface* Base = TSAssets::Material(Body, TEXT("spriteSeeThrough"));
	const FString Folder = TSJson::Str(TSJson::Obj(D.World(), TEXT("looks2d")), TEXT("textureFolder"));
	UTexture2D* Tex = TSAssets::Load<UTexture2D>(TSAssets::ObjPath(Folder, TSJson::Str(G, TEXT("sheet"), TEXT("SPR_Ghost"))));
	if (!Base || !Tex) return nullptr;

	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Feet = Body->GetActorLocation() - FVector(0, 0, Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	ARPGGhost* Gh = W->SpawnActor<ARPGGhost>(Feet, FRotator::ZeroRotator, P);
	if (!Gh) return nullptr;
	Gh->Feet = Feet;
	Gh->Frames = int32(TSJson::Num(G, TEXT("frames"), 4));
	Gh->Size = float(TSJson::Num(G, TEXT("size"), 120)) * FMath::Max(1.f, Body->GetActorScale3D().Z);   // big foes, bigger ghosts (never smaller than the art's own scale)
	Gh->RiseBy = float(TSJson::Num(G, TEXT("rise"), 260));
	Gh->Life = float(TSJson::Num(G, TEXT("life"), 2.6));
	Gh->Delay = float(TSJson::Num(G, TEXT("delay"), 0.4));
	Gh->Wiggle = float(TSJson::Num(G, TEXT("wiggle"), 22));
	Gh->WiggleSpeed = float(TSJson::Num(G, TEXT("wiggleSpeed"), 1.6));
	Gh->Opacity = float(TSJson::Num(G, TEXT("opacity"), 0.65));
	Gh->Phase = FMath::FRandRange(0.f, UE_TWO_PI);   // each ghost wiggles its own way

	// The last foe around, and on a whim: it gets a fright on the way up and flees from the hero.
	bool bLast = true;
	const float Vicinity = float(TSJson::Num(G, TEXT("scaredVicinity"), 1800));
	for (TActorIterator<ARPGEnemy> It(W); It && bLast; ++It)
		if (*It != Body && !It->IsDead() && !It->IsLeaving() && !It->IsPassive() && FVector::Dist2D(It->GetActorLocation(), Feet) < Vicinity) bLast = false;
	Gh->bScared = !bCalm && (TSCmd::Has(TEXT("GhostScared")) || (bLast && FMath::FRand() < TSJson::Num(G, TEXT("scaredChance"), 0.5)));
	if (bCalm) { Gh->Life *= 1.8f; Gh->RiseBy *= 1.6f; Gh->Wiggle *= 0.4f; Gh->Delay = 0.6f; }   // at peace: a slow, straight climb
	if (Gh->bScared)
	{
		Gh->Life = float(TSJson::Num(G, TEXT("scaredLife"), 3.4));
		Gh->FleeDistance = float(TSJson::Num(G, TEXT("fleeDistance"), 900));
	}
	Gh->Card->SetStaticMesh(TSAssets::Shape(TEXT("Plane")));
	Gh->Mat = UMaterialInstanceDynamic::Create(Base, Gh);
	Gh->Mat->SetTextureParameterValue(TEXT("Tex"), Tex);
	Gh->Mat->SetScalarParameterValue(TEXT("Cols"), float(Gh->Frames * (1 + LookDirections)));   // calm, then scared looking each way
	Gh->Mat->SetScalarParameterValue(TEXT("Rows"), 1.f);
	Gh->Mat->SetScalarParameterValue(TEXT("Emissive"), float(TSJson::Num(G, TEXT("emissive"), 0.35)));   // a faint glow, so it reads at night
	Gh->Mat->SetScalarParameterValue(TEXT("Opacity"), 0.f);
	Gh->Card->SetMaterial(0, Gh->Mat);
	return Gh;
}

void ARPGGhost::Tick(float Dt)
{
	Super::Tick(Dt);
	Age += Dt;
	if (Age < Delay || !Mat) return;   // the body falls first
	const float T = FMath::Clamp((Age - Delay) / Life, 0.f, 1.f);
	if (T >= 1.f) { Destroy(); return; }

	const FRotator R = TSLook::CardRotation();
	const FVector Up = -FRotationMatrix(R).GetUnitAxis(EAxis::Y), Right = FRotationMatrix(R).GetUnitAxis(EAxis::X);
	const float S = Size * (0.75f + 0.35f * FMath::Min(T, 0.4f) / 0.4f);
	const float Frame = (Age - Delay) * 8.f;
	FVector At;
	int32 Col = int32(Frame) % FMath::Max(1, Frames);
	bool bFlip = false;
	float Fade = FMath::Clamp(FMath::Min(T / 0.15f, (1.f - T) / 0.35f), 0.f, 1.f);

	if (!bScared)
	{
		// Up, easing out; side to side, wider as it climbs; in over the first 15%, out over the last 35%.
		const float Height = RiseBy * (1.f - FMath::Square(1.f - T));
		const float Side = FMath::Sin(Phase + T * Life * WiggleSpeed * UE_TWO_PI) * Wiggle * (0.4f + 0.6f * T);
		At = Feet + FVector(0, 0, Height) + Right * Side;
		bFlip = Side < 0.f;   // leans the way it sways
	}
	else
	{
		// Rises a little (0-25%), spots the hero and freezes, trembling (25-45%), then flees the other way (45-100%).
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		const FVector Hero = PC && PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : Feet;
		if (T < 0.25f)
		{
			const float K = T / 0.25f;
			At = Feet + FVector(0, 0, RiseBy * 0.45f * (1.f - FMath::Square(1.f - K))) + Right * FMath::Sin(Phase + Age * 6.f) * Wiggle * 0.5f;
			PeakAt = At;
		}
		else
		{
			if (!bSpotted)
			{
				bSpotted = true;
				FleeDir = (PeakAt - Hero).GetSafeNormal2D();
				if (FleeDir.IsNearlyZero()) FleeDir = FVector(1, 0, 0);
				UTSFeedback::Get(this)->Float(PeakAt + Up * S, TEXT("!"), FLinearColor(1.f, 0.95f, 0.6f), 1.5f);
			}
			if (T < 0.45f)
				At = PeakAt + Right * FMath::Sin(Age * 70.f) * 4.f;       // trembling
			else
			{
				const float K = (T - 0.45f) / 0.55f;
				At = PeakAt + FleeDir * FleeDistance * K * K + FVector(0, 0, RiseBy * 0.5f * K) + Right * FMath::Sin(Age * 14.f) * Wiggle * 0.6f;
			}
			// The frightened face, eyes on the hero: where the hero is on screen from the ghost (right / up on the card),
			// to the nearest of the art's directions (right-facing; mirrored when the hero is to the left).
			const FVector To = Hero + FVector(0, 0, 60.f) - (At + Up * (S * 0.45f));
			const float SX = FVector::DotProduct(To, Right), SY = FVector::DotProduct(To, Up);
			bFlip = SX < 0.f;
			const float Deg = FMath::RadiansToDegrees(FMath::Atan2(SY, FMath::Abs(SX)));   // 90 up .. -90 down
			const int32 Look = Deg > 67.5f ? 2 : Deg > 22.5f ? 1 : Deg > -22.5f ? 0 : Deg > -67.5f ? 3 : 4;
			Col = Frames * (1 + Look) + int32(Frame * 0.5f) % FMath::Max(1, Frames);
			Fade = FMath::Clamp((1.f - T) / 0.3f, 0.f, 1.f);
		}
	}
	Card->SetWorldLocationAndRotation(At + Up * (S * 0.5f), R);
	Card->SetWorldScale3D(FVector(S / 100.f, S / 100.f, 1.f));
	Card->SetVisibility(true);
	Mat->SetScalarParameterValue(TEXT("Col"), float(Col));
	Mat->SetScalarParameterValue(TEXT("Flip"), bFlip ? 1.f : 0.f);
	Mat->SetScalarParameterValue(TEXT("Opacity"), Opacity * FMath::Min(Fade, FMath::Clamp(T / 0.15f, 0.f, 1.f)));
}
