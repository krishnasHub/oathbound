#include "RPGNPC.h"
#include "TSData.h"
#include "RPGSession.h"
#include "LMStory.h"
#include "RPGPlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "TSSleep.h"
#include "TSDayNight.h"
#include "RPGWorldBuilder.h"
#include "RPGTheft.h"

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
	Home = GetActorLocation();

	// Its hours, and its bed: in its cottage, gone into by the front door.
	const ETSSleepHours Hours = UTSSleep::Parse(TSJson::Str(Def, TEXT("sleeps")));
	const TArray<TSharedPtr<FJsonValue>> HouseTile = TSJson::Arr(Def, TEXT("house"));
	URPGSession* Session = URPGSession::Get(this);
	ARPGWorldBuilder* B = Session ? Session->Builder() : nullptr;
	const ARPGWorldBuilder::FHouse* House = B && HouseTile.Num() == 2
		? B->HouseAt(UTSData::Get(this).TileCenter(int32(HouseTile[0]->AsNumber()), int32(HouseTile[1]->AsNumber()))) : nullptr;
	if (Hours == ETSSleepHours::Never || !House) return;
	// On the bed, not in it: the lying sprite goes a little above the mattress.
	const float Lift = float(TSJson::Num(UTSData::Get(this).Section(TEXT("houses")), TEXT("sleeperLift"), 0));
	Sleep = UTSSleep::Add(this, Hours, House->Bed + FVector(0, 0, Lift), true);
	Sleep->SetEntry(House->Door);
	Sleep->BedYaw = House->BedYaw;
	Sleep->bHasBedYaw = true;
	HouseCutaway = House->Cutaway;
	// While it sleeps in there, passing by shows it (the house cuts away as the hero comes close).
	Sleep->OnFellAsleep.AddWeakLambda(this, [this](ATSCharacter*) { if (ARPGWorldBuilder* W = URPGSession::Get(this)->Builder()) W->SetCutawayPeek(HouseCutaway, true); });
	Sleep->OnWoke.AddWeakLambda(this, [this](ATSCharacter*) { if (!Sleep->IsBedtime()) if (ARPGWorldBuilder* W = URPGSession::Get(this)->Builder()) W->SetCutawayPeek(HouseCutaway, false); });
}

void ARPGNPC::Tick(float Dt)
{
	Super::Tick(Dt);
	if (IsFrozen()) return;
	URPGSession* Session = URPGSession::Get(this);
	if (Sleep)
	{
		if (Session->DialogueNpc() == this) Sleep->Hold();
		if (Sleep->IsAsleep() || Sleep->IsIndoors()) return;
		// Off to bed, or (up again) back to its spot.
		const FVector Dir = Sleep->IsTurningIn() ? Sleep->Direction(Dt) : FVector::Dist2D(Home, GetActorLocation()) > 120.f ? Sleep->DirectionTo(Home, Dt) : FVector::ZeroVector;
		if (!Dir.IsNearlyZero()) { GetCharacterMovement()->bOrientRotationToMovement = true; AddMovementInput(Dir, 0.6f); return; }
	}
	// Turn to the hero only when talking to them or when they notice them (a crouched Thief behind them goes unseen).
	const ARPGPlayerCharacter* P = Session->Player();
	if (!P || FVector::Dist2D(P->GetActorLocation(), GetActorLocation()) > 600.f) return;
	if (Session->DialogueNpc() != this && !RPGTheft::Notices(this, P)) return;
	const FRotator Want(0, (P->GetActorLocation() - GetActorLocation()).GetSafeNormal2D().Rotation().Yaw, 0);
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), Want, Dt, 4.f));
}
