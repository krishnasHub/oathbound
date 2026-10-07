#include "RPGNPC.h"
#include "TSData.h"
#include "RPGSession.h"
#include "LMStory.h"
#include "RPGPlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"

ARPGNPC::ARPGNPC()
{
	Team = ETSTeam::Neutral;
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = AAIController::StaticClass();
}

void ARPGNPC::Init(const FString& InId)
{
	NpcId = InId;
	const TSJson::FObj Def = UTSData::Get(this).Entry(TEXT("npcs"), NpcId);
	DisplayName = TSJson::Str(Def, TEXT("name"), NpcId);
	NameColor = TSJson::Color(TSJson::Str(Def, TEXT("color")), FLinearColor::White);
	DialogueRoot = TSJson::Str(Def, TEXT("dialogue"));
	TalkKey = NpcId;
	SetLookFromData(NpcId);
	Stats->Base.Add(TEXT("hpFlat"), 100.f);
	Stats->Fill();
	GetCharacterMovement()->MaxWalkSpeed = 200.f;
}

void ARPGNPC::Tick(float Dt)
{
	Super::Tick(Dt);
	if (IsFrozen()) return;
	const ARPGPlayerCharacter* P = URPGSession::Get(this)->Player();
	if (!P || FVector::Dist2D(P->GetActorLocation(), GetActorLocation()) > 600.f) return;
	const FRotator Want(0, (P->GetActorLocation() - GetActorLocation()).GetSafeNormal2D().Rotation().Yaw, 0);
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), Want, Dt, 4.f));
}
