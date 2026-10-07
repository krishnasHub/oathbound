#include "RPGAmbient.h"
#include "TSFeedback.h"
#include "TSSky.h"
#include "RPGWorldBuilder.h"
#include "TSLook.h"
#include "TSData.h"
#include "RPGAssets.h"
#include "RPGSession.h"
#include "LMStory.h"
#include "TSAreaEvents.h"
#include "TSCharacter.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

namespace
{
	// Bird sheet: 0 sit, 1 peck, 2 wings up, 3 wings down. Goose: 0/1 walk, 2 honk, 3 graze.
	enum { BirdGround, BirdFlying, BirdGone };
	enum { GooseWander, GooseGraze, GooseHonk, GooseFlee };
	enum { ProwlSneak, ProwlPause, ProwlStartled, ProwlBolt, ProwlGone };
	constexpr float Scare = 650.f;   // how close the hero can come before birds take off
}

ARPGAmbient::ARPGAmbient()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

ARPGAmbient::FCard ARPGAmbient::MakeCard(const FString& Sheet, int32 Cols, int32 Rows, float SizeUU)
{
	FCard C;
	C.Mesh = NewObject<UStaticMeshComponent>(this);
	C.Mesh->SetStaticMesh(TSAssets::Shape(TEXT("Plane")));
	C.Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C.Mesh->SetUsingAbsoluteLocation(true);
	C.Mesh->SetUsingAbsoluteRotation(true);
	C.Mesh->SetUsingAbsoluteScale(true);
	C.Mat = TSLook::SpriteMaterial(this, Sheet, Cols, Rows);
	if (C.Mat) C.Mesh->SetMaterial(0, C.Mat);
	C.Mesh->SetupAttachment(RootComponent);
	C.Mesh->RegisterComponent();
	C.Mesh->SetVisibility(false);
	return C;
}

float ARPGAmbient::Ground(const FVector& P) const
{
	return World.IsValid() ? World->GroundZ(P.X, P.Y) : P.Z;
}

void ARPGAmbient::Place(FCard& C, float SizeUU, int32 Col, int32 Row, bool bVisible)
{
	C.Mesh->SetVisibility(bVisible);
	C.Size = SizeUU; C.Col = Col; C.Row = Row;
	if (!bVisible || !C.Mat) return;
	const FRotator R = TSLook::CardRotation();
	const FVector Up = -FRotationMatrix(R).GetUnitAxis(EAxis::Y);
	const FVector Feet(C.Pos.X, C.Pos.Y, Ground(C.Pos) + C.Lift);
	C.Mesh->SetWorldLocationAndRotation(Feet + Up * (SizeUU * 0.5f - SizeUU * 0.06f), R);
	C.Mesh->SetWorldScale3D(FVector(SizeUU / 100.f, SizeUU / 100.f, 1.f));
	if (FMath::Abs(C.Vel.X) > 5.f) C.bFacingLeft = C.Vel.X < 0.f;
	C.Mat->SetScalarParameterValue(TEXT("Col"), float(Col));
	C.Mat->SetScalarParameterValue(TEXT("Row"), float(Row));
	C.Mat->SetScalarParameterValue(TEXT("Flip"), C.bFacingLeft ? 1.f : 0.f);
}

FVector ARPGAmbient::RandomGrass(const FVector& AwayFrom, float MinDist) const
{
	for (int32 Try = 0; Try < 40 && Grass.Num(); ++Try)
	{
		const FVector P = Grass[Rand.RandRange(0, Grass.Num() - 1)];
		if (FVector::Dist2D(P, AwayFrom) >= MinDist) return P;
	}
	return Grass.Num() ? Grass[0] : FVector::ZeroVector;
}

void ARPGAmbient::Init(ARPGWorldBuilder* InWorld)
{
	World = InWorld;
	if (UTSAreaEvents* Events = UTSAreaEvents::Get(this))
	{
		Events->OnStatus.AddUObject(this, &ARPGAmbient::OnAreaStatus);
		Events->OnPush.AddUObject(this, &ARPGAmbient::OnAreaPush);
	}
	Rand.Initialize(1234);
	const UTSData& D = UTSData::Get(this);
	const float U = TSLook::SpriteUnits();
	BirdSize = 16.f * U * 0.9f;
	GooseSize = 24.f * U;
	ManSize = 32.f * U;

	// Open grass to land and graze on; the village (north of the river) for the prowler.
	for (int32 Y = 1; Y < D.MapH - 1; ++Y)
		for (int32 X = 1; X < D.MapW - 1; ++X)
		{
			if (D.Rows[Y][X] != TEXT('.')) continue;
			const FVector C = D.TileCenter(X, Y) + FVector(Rand.FRandRange(-90, 90), Rand.FRandRange(-90, 90), 0);
			Grass.Add(C);
			if (Y < 11 && X < 16) Village.Add(C);
		}
	if (Grass.IsEmpty()) return;

	for (int32 F = 0; F < 3; ++F)   // flocks of five
	{
		const FVector Spot = RandomGrass(FVector(-1e6), 0.f);
		for (int32 I = 0; I < 5; ++I)
		{
			FBird B;
			static_cast<FCard&>(B) = MakeCard(TEXT("SPR_Bird"), 4, 1, BirdSize);
			B.Pos = Spot + FVector(Rand.FRandRange(-160, 160), Rand.FRandRange(-120, 120), 0);
			B.Timer = Rand.FRandRange(0.f, 2.f);
			B.Anim = Rand.FRand() * 10.f;
			Birds.Add(B);
		}
	}
	for (int32 I = 0; I < 2; ++I)
	{
		FGoose G;
		static_cast<FCard&>(G) = MakeCard(TEXT("SPR_Goose"), 4, 1, GooseSize);
		G.Pos = Village.Num() ? Village[Rand.RandRange(0, Village.Num() - 1)] : RandomGrass(FVector(-1e6), 0.f);
		G.Target = G.Pos;
		G.Timer = Rand.FRandRange(1.f, 3.f);
		Geese.Add(G);
	}

	// The prowler loops between spots around the cottages.
	Prowler = MakeCard(TEXT("SPR_sneak"), 4, 13, ManSize);
	for (int32 I = 0; I < 6 && Village.Num(); ++I) ProwlRoute.Add(Village[Rand.RandRange(0, Village.Num() - 1)]);
	if (ProwlRoute.Num()) Prowler.Pos = ProwlRoute[0];

	// Fireflies over the meadows.
	UMaterialInterface* Glow = TSAssets::Load<UMaterialInterface>(TEXT("/Game/RPG/Materials/M_RPG_Glow.M_RPG_Glow"));
	for (int32 I = 0; I < 40; ++I)
	{
		FFly F;
		F.Home = RandomGrass(FVector(-1e6), 0.f) + FVector(0, 0, Rand.FRandRange(40, 140));
		F.Offset = FVector::ZeroVector;
		F.Phase = Rand.FRand() * 10.f;
		F.Mesh = NewObject<UStaticMeshComponent>(this);
		F.Mesh->SetStaticMesh(TSAssets::Shape(TEXT("Sphere")));
		F.Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		F.Mesh->SetCastShadow(false);
		F.Mesh->SetUsingAbsoluteLocation(true);
		F.Mesh->SetUsingAbsoluteScale(true);
		F.Mesh->SetWorldScale3D(FVector(0.09f));
		if (Glow)
		{
			F.Mat = UMaterialInstanceDynamic::Create(Glow, this);
			F.Mat->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.75f, 1.f, 0.35f));
			F.Mesh->SetMaterial(0, F.Mat);
		}
		F.Mesh->SetupAttachment(RootComponent);
		F.Mesh->RegisterComponent();
		F.Mesh->SetVisibility(false);
		Flies.Add(F);
	}
}

void ARPGAmbient::Tick(float Dt)
{
	Super::Tick(Dt);
	Clock += Dt;
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APawn* P = PC ? PC->GetPawn() : nullptr;
	if (!P || P->IsHidden()) { for (FBird& B : Birds) B.Mesh->SetVisibility(false); return; }
	const FVector Hero = P->GetActorLocation();
	const float Night = ATSSky::Night();
	TickBirds(Dt, Hero, Night < 0.5f);
	TickGeese(Dt, Hero, Night < 0.5f);
	TickProwler(Dt, Hero, Night > 0.6f);
	TickFireflies(Dt, Hero, Night);
}

void ARPGAmbient::OnAreaStatus(const FVector& Center, float Radius, FName Tag, float Duration)
{
	FLinearColor Tint;
	if (Tag != TEXT("Frozen") || !ATSCharacter::TintForTag(this, Tag, Tint)) return;
	auto Freeze = [&](FCard& C)
	{
		if (!C.Mesh || !C.Mesh->IsVisible() || !C.Mat || FVector::Dist2D(C.Pos, Center) > Radius) return;
		C.Frozen = Duration;
		C.Mat->SetVectorParameterValue(TEXT("Tint"), Tint);
	};
	for (FBird& B : Birds) Freeze(B);
	for (FGoose& G : Geese) Freeze(G);
	Freeze(Prowler);
}

void ARPGAmbient::OnAreaPush(const FVector& Center, float Radius)
{
	auto Inside = [&](const FCard& C) { return C.Mesh && C.Mesh->IsVisible() && FVector::Dist2D(C.Pos, Center) < Radius; };
	auto Out = [&](const FCard& C) { const FVector D = (C.Pos - Center).GetSafeNormal2D(); return D.IsNearlyZero() ? FVector(1, 0, 0) : D; };
	for (FBird& B : Birds)
	{
		if (!Inside(B)) continue;
		B.Frozen = 0.f;
		B.State = BirdFlying;   // blasted into the air, away from the barrier
		B.Vel = Out(B) * 950.f;
		B.Timer = 2.5f;
	}
	auto Throw = [&](FCard& C)
	{
		if (!Inside(C)) return;
		// Knockback-style: it slides ~1/6.2 of its starting speed before stopping, so it lands past the edge.
		C.Thrown = Out(C) * (Radius - FVector::Dist2D(C.Pos, Center) + 120.f) * 6.2f;
		C.Vel = C.Thrown;
	};
	for (FGoose& G : Geese) Throw(G);
	Throw(Prowler);
}

bool ARPGAmbient::StayThrown(FCard& C, float Dt)
{
	if (C.Thrown.SizeSquared() < 400.f) { C.Thrown = FVector::ZeroVector; return false; }
	C.Pos += C.Thrown * Dt;
	C.Thrown *= FMath::Pow(0.002f, Dt);
	C.Lift = FMath::Min(40.f, C.Thrown.Size() * 0.04f);   // a little airborne while it flies
	Place(C, C.Size, C.Col, C.Row, true);
	if (C.Thrown.SizeSquared() < 400.f) C.Lift = 0.f;
	return true;
}

bool ARPGAmbient::StayFrozen(FCard& C, float Dt)
{
	if (C.Frozen <= 0.f) return false;
	C.Frozen -= Dt;
	if (C.Frozen <= 0.f && C.Mat) C.Mat->SetVectorParameterValue(TEXT("Tint"), FLinearColor::White);
	return C.Frozen > 0.f;
}

void ARPGAmbient::TickBirds(float Dt, const FVector& Hero, bool bDay)
{
	for (FBird& B : Birds)
	{
		if (StayFrozen(B, Dt)) continue;   // stuck where it was, even mid-air
		B.Anim += Dt;
		B.Timer -= Dt;
		if (!bDay && B.State == BirdGround) { B.State = BirdGone; B.Timer = 4.f; }
		switch (B.State)
		{
		case BirdGround:
		{
			// Peck, hop, look around; take off when the hero comes close.
			if (FVector::Dist2D(B.Pos, Hero) < Scare)
			{
				const FVector Away = (B.Pos - Hero).GetSafeNormal2D();
				B.Vel = (Away + FVector(Rand.FRandRange(-0.4f, 0.4f), Rand.FRandRange(-0.4f, 0.4f), 0)).GetSafeNormal2D() * Rand.FRandRange(520.f, 680.f);
				B.State = BirdFlying;
				B.Timer = Rand.FRandRange(2.5f, 3.5f);
				break;
			}
			if (B.Timer <= 0.f)
			{
				B.Timer = Rand.FRandRange(0.6f, 2.2f);
				if (Rand.FRand() < 0.4f) { B.Vel = FVector(Rand.FRandRange(-1, 1), Rand.FRandRange(-1, 1), 0).GetSafeNormal() * 120.f; B.Lift = 1.f; }
			}
			B.Pos += B.Vel * Dt;
			B.Vel *= FMath::Max(0.f, 1.f - Dt * 6.f);
			B.Lift = B.Vel.Size2D() > 30.f ? 14.f * FMath::Abs(FMath::Sin(B.Anim * 18.f)) : 0.f;   // little hops
			const int32 Col = FMath::Fmod(B.Anim, 1.6f) < 0.35f ? 1 : 0;                          // the odd peck
			Place(B, BirdSize, Col, 0, true);
			break;
		}
		case BirdFlying:
			B.Pos += B.Vel * Dt;
			B.Lift += 260.f * Dt;
			Place(B, BirdSize, 2 + (int32(B.Anim * 14.f) % 2), 0, true);
			if (B.Timer <= 0.f) { B.State = BirdGone; B.Timer = Rand.FRandRange(10.f, 20.f); }
			break;
		default:   // gone: settle somewhere else later (out of the hero's sight), as a group
			B.Mesh->SetVisibility(false);
			if (B.Timer <= 0.f && bDay)
			{
				B.Pos = RandomGrass(Hero, 1800.f) + FVector(Rand.FRandRange(-150, 150), Rand.FRandRange(-150, 150), 0);
				B.Lift = 0.f; B.Vel = FVector::ZeroVector; B.State = BirdGround; B.Timer = 1.f;
			}
		}
	}
}

void ARPGAmbient::TickGeese(float Dt, const FVector& Hero, bool bDay)
{
	URPGSession* Session = URPGSession::Get(this);
	for (FGoose& G : Geese)
	{
		if (StayFrozen(G, Dt)) continue;
		if (StayThrown(G, Dt)) { G.State = GooseFlee; G.Timer = 1.6f; G.Vel = G.Thrown.GetSafeNormal2D() * 300.f; continue; }   // then runs off
		G.Anim += Dt;
		G.Timer -= Dt;
		if (!bDay) { G.Mesh->SetVisibility(false); continue; }   // asleep somewhere
		const float Dist = FVector::Dist2D(G.Pos, Hero);
		if ((G.State == GooseWander || G.State == GooseGraze) && Dist < 380.f)
		{
			G.State = GooseHonk; G.Timer = 0.6f; G.Vel = FVector::ZeroVector;
			G.bFacingLeft = Hero.X < G.Pos.X;
			if (Session) UTSFeedback::Get(Session)->Float(FVector(G.Pos.X, G.Pos.Y, Ground(G.Pos) + 170.f), TEXT("HONK!"), FLinearColor(1.f, 0.95f, 0.8f), 0.9f);
		}
		int32 Col = 0;
		switch (G.State)
		{
		case GooseWander:
		{
			const FVector To = G.Target - G.Pos;
			if (To.Size2D() < 20.f || G.Timer <= 0.f) { G.State = GooseGraze; G.Timer = Rand.FRandRange(2.f, 4.f); G.Vel = FVector::ZeroVector; break; }
			G.Vel = To.GetSafeNormal2D() * 70.f;
			G.Pos += G.Vel * Dt;
			Col = int32(G.Anim * 5.f) % 2;
			break;
		}
		case GooseGraze:
			Col = FMath::Fmod(G.Anim, 2.f) < 1.4f ? 3 : 0;
			if (G.Timer <= 0.f)
			{
				G.State = GooseWander; G.Timer = 6.f;
				G.Target = G.Pos + FVector(Rand.FRandRange(-450, 450), Rand.FRandRange(-350, 350), 0);
				if (Grass.Num()) { float Best = 1e9f; for (const FVector& Gr : Grass) { const float Dd = FVector::DistSquared2D(Gr, G.Target); if (Dd < Best) { Best = Dd; G.Target = Gr; } } }
			}
			break;
		case GooseHonk:
			Col = 2;
			if (G.Timer <= 0.f)
			{
				G.State = GooseFlee; G.Timer = 1.6f;
				G.Vel = (G.Pos - Hero).GetSafeNormal2D() * 300.f;
			}
			break;
		case GooseFlee:
			G.Pos += G.Vel * Dt;
			Col = int32(G.Anim * 10.f) % 2;
			if (G.Timer <= 0.f) { G.State = GooseGraze; G.Timer = Rand.FRandRange(1.f, 2.f); G.Vel = FVector::ZeroVector; }
			break;
		}
		Place(G, GooseSize, Col, 0, true);
	}
}

void ARPGAmbient::TickProwler(float Dt, const FVector& Hero, bool bNight)
{
	FCard& M = Prowler;
	if (!M.Mesh || ProwlRoute.IsEmpty() || StayFrozen(M, Dt) || StayThrown(M, Dt)) return;
	M.Anim += Dt;
	M.Timer -= Dt;
	if (!bNight) { M.Mesh->SetVisibility(false); M.State = ProwlGone; M.Timer = 0.f; return; }
	const float Dist = FVector::Dist2D(M.Pos, Hero);
	// Sheet rows: side walk 9, side idle 8, down idle 0, up walk 5 (RPGSprite layout).
	int32 Row = 8, Col = int32(M.Anim * 2.2f) % 2;
	switch (M.State)
	{
	case ProwlSneak:
	{
		if (Dist < 700.f)
		{
			M.State = ProwlStartled; M.Timer = 0.5f; M.Vel = FVector::ZeroVector;
			M.bFacingLeft = Hero.X < M.Pos.X;
			if (URPGSession* S = URPGSession::Get(this)) UTSFeedback::Get(S)->Float(FVector(M.Pos.X, M.Pos.Y, Ground(M.Pos) + 260.f), TEXT("!"), FLinearColor(1.f, 0.85f, 0.3f), 1.3f);
			break;
		}
		const FVector To = ProwlRoute[ProwlLeg] - M.Pos;
		if (To.Size2D() < 30.f) { ProwlLeg = (ProwlLeg + 1) % ProwlRoute.Num(); M.State = ProwlPause; M.Timer = Rand.FRandRange(1.f, 2.5f); M.Vel = FVector::ZeroVector; break; }
		M.Vel = To.GetSafeNormal2D() * 110.f;   // slow, careful steps
		M.Pos += M.Vel * Dt;
		Row = 9; Col = int32(M.Anim * 5.f) % 4;
		break;
	}
	case ProwlPause:   // looking around
		if (Dist < 700.f) { M.State = ProwlSneak; break; }
		Row = FMath::Fmod(M.Anim, 2.f) < 1.f ? 8 : 0;
		if (M.Timer <= 0.f) M.State = ProwlSneak;
		break;
	case ProwlStartled:
		Row = 0; Col = 0;
		if (M.Timer <= 0.f) { M.State = ProwlBolt; M.Timer = 2.5f; M.Vel = (M.Pos - Hero).GetSafeNormal2D() * 480.f; }
		break;
	case ProwlBolt:
		M.Pos += M.Vel * Dt;
		Row = 9; Col = int32(M.Anim * 12.f) % 4;
		if (M.Timer <= 0.f) { M.State = ProwlGone; M.Timer = Rand.FRandRange(15.f, 30.f); }
		break;
	default:   // gone: slip back in somewhere the hero isn't looking
		M.Mesh->SetVisibility(false);
		if (M.Timer <= 0.f)
		{
			float Best = -1.f;
			for (int32 I = 0; I < ProwlRoute.Num(); ++I)
			{
				const float Dd = FVector::Dist2D(ProwlRoute[I], Hero);
				if (Dd > Best) { Best = Dd; ProwlLeg = I; }
			}
			M.Pos = ProwlRoute[ProwlLeg];
			M.State = ProwlPause; M.Timer = 1.f;
		}
		return;
	}
	Place(M, ManSize, Col, Row, true);
}

void ARPGAmbient::TickFireflies(float Dt, const FVector& Hero, float Night)
{
	const bool bOn = Night > 0.5f;
	for (FFly& F : Flies)
	{
		F.Mesh->SetVisibility(bOn);
		if (!bOn) continue;
		F.Phase += Dt;
		// Drift lazily about home; keep a respectful distance from the hero.
		const FVector Wander(FMath::Sin(F.Phase * 0.7f) * 120.f, FMath::Cos(F.Phase * 0.53f) * 120.f, FMath::Sin(F.Phase * 1.3f) * 30.f);
		FVector At = F.Home + Wander + F.Offset;
		const FVector Away = At - Hero;
		if (Away.Size2D() < 320.f) F.Offset += Away.GetSafeNormal2D() * 260.f * Dt;
		else F.Offset *= FMath::Max(0.f, 1.f - Dt * 0.2f);
		At = F.Home + Wander + F.Offset;
		F.Mesh->SetWorldLocation(FVector(At.X, At.Y, Ground(At) + (F.Home.Z - Ground(F.Home)) + Wander.Z));
		if (F.Mat) F.Mat->SetScalarParameterValue(TEXT("Intensity"), 6.f + 6.f * FMath::Max(0.f, FMath::Sin(F.Phase * 2.3f)));
	}
}
