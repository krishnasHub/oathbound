#include "RPGSprite.h"
#include "RPGCharacterBase.h"
#include "RPGEnemy.h"
#include "RPGLook.h"
#include "RPGAssets.h"

#include "Components/CapsuleComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	constexpr int32 SheetCols = 4, SheetRows = 13, FrameUnits = 32;   // sprite sheet layout (logical 32-unit frames)
	enum EAct { Idle = 0, Walk = 1, Attack = 2, Hurt = 3 };
}

URPGSpriteComponent::URPGSpriteComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;   // after movement, so the card never lags its owner
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);
	CastShadow = true;
}

void URPGSpriteComponent::Setup(const FString& Sheet)
{
	if (Sheet == SheetName && Mat) return;
	SheetName = Sheet;
	SetStaticMesh(RPGAssets::Shape(TEXT("Plane")));
	Mat = RPGLook::SpriteMaterial(this, TEXT("SPR_") + Sheet, SheetCols, SheetRows);
	if (Mat) SetMaterial(0, Mat);
	HideCheck = 0.f;
	if (RPGLook::Mode() == RPGLook::EMode::Flat2D && !Shadow)
	{
		SetCastShadow(false);
		Shadow = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("SpriteShadow"));
		Shadow->SetStaticMesh(RPGAssets::Shape(TEXT("Plane")));
		Shadow->SetMaterial(0, RPGLook::PropMaterial(TEXT("PR_Shadow")));
		Shadow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Shadow->SetCastShadow(false);
		Shadow->SetUsingAbsoluteLocation(true);
		Shadow->SetUsingAbsoluteRotation(true);
		Shadow->SetUsingAbsoluteScale(true);
		Shadow->SetupAttachment(GetOwner()->GetRootComponent());
		Shadow->RegisterComponent();
	}
}

void URPGSpriteComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(Dt, TickType, ThisTickFunction);
	ARPGCharacterBase* C = Cast<ARPGCharacterBase>(GetOwner());
	if (!C || !Mat) return;
	Clock += Dt;

	// Keep the 3D body hidden (weapon kits are rebuilt on style swaps). It still animates underneath.
	HideCheck -= Dt;
	if (HideCheck <= 0.f) { HideCheck = 0.5f; C->HideBody(); }

	// Took damage since last frame?
	const float HP = C->Stats->HP;
	if (LastHP >= 0.f && HP < LastHP - 0.01f) HurtT = 0.2f;
	LastHP = HP;
	HurtT -= Dt;

	// Direction relative to the fixed camera: 0 = facing the camera, 1 = away, 2 = side (mirrored for left).
	const float CamYaw = RPGLook::CameraRotation().Yaw;
	const FVector Fwd = FRotator(0, CamYaw, 0).Vector(), Right = FRotator(0, CamYaw + 90.f, 0).Vector();
	const FVector F = C->Facing();
	const float DF = FVector::DotProduct(F, Fwd), DR = FVector::DotProduct(F, Right);
	const int32 Dir = DF > 0.6f ? 1 : DF < -0.6f ? 0 : 2;
	const bool bFlip = Dir == 2 && DR < 0.f;

	// Action and frame.
	int32 Row = 0, Col = 0;
	const float SinceAttack = GetWorld()->GetTimeSeconds() - C->SpriteAttackAt;
	const ARPGEnemy* E = Cast<ARPGEnemy>(C);
	const bool bWindup = C->bSpriteHold || (E && E->State == ERPGEnemyState::Windup);
	if (C->IsDead()) { Row = 12; Col = 0; }
	else
	{
		int32 Act = Idle;
		if (HurtT > 0.f) { Act = Hurt; Col = 0; }
		else if (SinceAttack < 0.3f) { Act = Attack; Col = 1 + FMath::Min(2, int32(SinceAttack / 0.1f)); }   // swing, strike, recover
		else if (bWindup) { Act = Attack; Col = 0; }
		else if (C->GuardStyle().IsValid()) { Act = -1; Row = 12; Col = 1 + Dir; }   // shield raised (row 12, cols 1-3)
		else if (C->GetVelocity().Size2D() > 25.f) { Act = Walk; Col = int32(Clock * 9.f) % 4; }
		else { Act = Idle; Col = int32(Clock * 2.2f) % 2; }
		if (Act >= 0) Row = Dir * 4 + Act;
	}
	Mat->SetScalarParameterValue(TEXT("Row"), float(Row));
	Mat->SetScalarParameterValue(TEXT("Col"), float(Col));
	Mat->SetScalarParameterValue(TEXT("Flip"), bFlip ? 1.f : 0.f);
	Mat->SetScalarParameterValue(TEXT("Flash"), HurtT > 0.f ? 0.75f : 0.f);

	// Placement: the card's bottom edge at the feet (the art leaves ~2 px under the boots).
	const float Units = RPGLook::SpriteUnits() * C->GetActorScale3D().Z;
	const float Size = FrameUnits * Units;
	const FRotator R = RPGLook::CardRotation();
	const FVector CardUp = -FRotationMatrix(R).GetUnitAxis(EAxis::Y);
	FVector Feet = C->GetActorLocation() - FVector(0, 0, C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	FVector Center = Feet + CardUp * (Size * 0.5f - 2.5f * Units);
	if (RPGLook::Mode() == RPGLook::EMode::Flat2D) Center.Z = RPGLook::FlatSortZ(Feet.Y);
	SetWorldLocationAndRotation(Center, R);
	SetWorldScale3D(FVector(Size / 100.f, Size / 100.f, 1.f));
	if (Shadow)
	{
		Shadow->SetVisibility(!C->IsDead());
		Shadow->SetWorldLocationAndRotation(FVector(Feet.X, Feet.Y, 2.f) + CardUp * Units, R);
		Shadow->SetWorldScale3D(FVector(20.f * Units / 100.f, 7.f * Units / 100.f, 1.f));
	}
}
