#include "RPGSession.h"
#include "ActionRPG.h"
#include "LMStory.h"
#include "TSFeedback.h"
#include "TSData.h"
#include "RPGCharacterBase.h"
#include "RPGPlayerCharacter.h"
#include "RPGEnemy.h"
#include "RPGNPC.h"
#include "TSLoot.h"
#include "TSInventory.h"
#include "RPGPlayerController.h"

#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

namespace
{
	const FLinearColor ToastGold(1.f, 0.83f, 0.3f), ToastGreen(0.44f, 0.88f, 0.54f), ToastRed(1.f, 0.5f, 0.5f);   // (unique names: unity builds merge files)
}

URPGSession* URPGSession::Get(const UObject* WorldContext)
{
	const UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	return W ? W->GetSubsystem<URPGSession>() : nullptr;
}

ULMStory* URPGSession::Story() const { return GetWorld()->GetSubsystem<ULMStory>(); }
UTSFeedback* URPGSession::Feedback() const { return GetWorld()->GetSubsystem<UTSFeedback>(); }

ARPGPlayerCharacter* URPGSession::Player() const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	return PC ? Cast<ARPGPlayerCharacter>(PC->GetPawn()) : nullptr;
}

void URPGSession::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<ULMStory>();
}

void URPGSession::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	Bind(Story());
}

void URPGSession::SetDebug(bool bOn)
{
	bDebug = bOn;
	Story()->bShowOdds = bOn;
}

void URPGSession::Tick(float Dt)
{
	if (GetWorld()->IsPaused()) return;
	EncounterCooldown -= Dt;
	UpdateEncounters();
}

// ---------------------------------------------------------------------------------------------
// Duels, factions, encounters
// ---------------------------------------------------------------------------------------------

void URPGSession::SetHostile(const FString& FactionName, const FString& Bark)
{
	ULMStory* L = Story();
	if (L->Faction(FactionName) == TEXT("hostile")) return;
	L->Factions.Add(FactionName, TEXT("hostile"));
	Duel = nullptr;
	Feedback()->Toast(Bark.IsEmpty() ? FString(TEXT("They attack!")) : Bark, ToastRed);
	L->CloseDialogue();
}

void URPGSession::StartDuel(ARPGCharacterBase* Opponent)
{
	Duel = Opponent;
	if (ARPGEnemy* E = Cast<ARPGEnemy>(Opponent)) E->State = ERPGEnemyState::Chase;
	Feedback()->Toast(FString::Printf(TEXT("Duel! Bring %s to his knees (below 20%% health). His men won't interfere."), *Opponent->DisplayName), ToastGold);
}

void URPGSession::UpdateEncounters()
{
	ARPGPlayerCharacter* P = Player();
	const ULMStory* L = Story();
	if (!P || P->IsDead() || L->IsDialogueOpen() || Duel.IsValid() || EncounterCooldown > 0.f) return;
	const UTSData& D = UTSData::Get(this);
	const TSJson::FObj All = D.Section(TEXT("encounters"));
	if (!All) return;
	for (const auto& KV : All->Values)
	{
		const FString Id = FString(*KV.Key);
		const TSJson::FObj Enc = KV.Value->AsObject();
		if (L->HasFlag(Id + TEXT("_passable"))) continue;
		ARPGEnemy* Leader = nullptr;
		for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (It->Type == TSJson::Str(Enc, TEXT("leader"))) { Leader = *It; break; }
		if (!Leader || Leader->IsDead() || !Leader->IsPassive()) continue;

		// (Nobody stops you to talk: the hero chooses to talk (E + click) or to fight. Slipping past without either
		// still turns the faction hostile.)
		if (P->GetActorLocation().Y > TSJson::Num(Enc, TEXT("crossRow"), 12) * D.TileSize + 30.f)
		{
			SetHostile(TSJson::Str(Enc, TEXT("faction")), TSJson::Str(Enc, TEXT("crossBark")));   // you tried to slip past
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Talking
// ---------------------------------------------------------------------------------------------

void URPGSession::OpenDialogue(ATSCharacter* Npc, const FString& NodeId)
{
	if (!Npc) return;
	Story()->OpenDialogue(Npc, FLMSpeaker{ Npc->DisplayName, Npc->NameColor, Npc->TalkKey, Npc->DialogueRoot }, NodeId);
}

FString URPGSession::MarkerFor(const ATSCharacter* Npc) const
{
	return Npc ? Story()->MarkerFor(Npc->DialogueRoot) : FString();
}

ARPGCharacterBase* URPGSession::DialogueNpc() const { return Cast<ARPGCharacterBase>(Story()->Speaker()); }

// ---------------------------------------------------------------------------------------------
// The game's half of the story rules, registered with Loom
// ---------------------------------------------------------------------------------------------

void URPGSession::Bind(ULMStory* L)
{
	L->SetData(UTSData::Get(this).Root);
	L->DefaultVerb = TEXT("persuade");

	// What the hero is and has.
	L->StatValue = [this](FName Stat) { const ARPGPlayerCharacter* P = Player(); return P ? P->Stats->Get(Stat) : 0.f; };
	L->ItemCount = [this](const FString& Item) { const ARPGPlayerCharacter* P = Player(); return P ? P->Inventory->Count(Item) : 0; };
	auto OneOf = [](const TSJson::FObj& C, const FString& Key, const FString& Value)
	{
		const TSharedPtr<FJsonValue> V = C->TryGetField(Key);
		if (!V) return false;
		if (V->Type == EJson::Array) { for (const auto& X : V->AsArray()) if (X->AsString() == Value) return true; return false; }
		return V->AsString() == Value;
	};
	L->AddCondition(TEXT("class"), [this, OneOf](const TSJson::FObj& C) { const ARPGPlayerCharacter* P = Player(); return P && OneOf(C, TEXT("class"), P->ClassId); });
	L->AddCondition(TEXT("sex"), [this](const TSJson::FObj& C) { const ARPGPlayerCharacter* P = Player(); return P && TSJson::Str(C, TEXT("sex")) == P->Sex; });
	L->AddCondition(TEXT("gold"), [this](const TSJson::FObj& C) { const ARPGPlayerCharacter* P = Player(); return P && P->Inventory->Currency >= TSJson::Num(C, TEXT("gold")); });
	L->AddCondition(TEXT("hasItem"), [this](const TSJson::FObj& C) { const ARPGPlayerCharacter* P = Player(); return P && P->Inventory->Count(TSJson::Str(C, TEXT("hasItem"))) > 0; });

	L->AddPlaceholder(TEXT("gold"), [this]() { const ARPGPlayerCharacter* P = Player(); return P ? FString::FromInt(P->Inventory->Currency) : FString(TEXT("0")); });
	L->AddPlaceholder(TEXT("title"), [this]()
	{
		const ARPGPlayerCharacter* P = Player();
		const TSharedPtr<FJsonValue> Title = P && P->ClassDef ? P->ClassDef->TryGetField(TEXT("title")) : nullptr;
		if (!Title) return FString(TEXT("traveler"));
		return Title->Type == EJson::Object ? TSJson::Str(Title->AsObject(), P->Sex) : Title->AsString();
	});

	// Verbs: class-only verbs, and the Mage's Hypnotize costs mana.
	L->VerbAvailable = [this](const TSJson::FObj& V)
	{
		const FString VerbClass = TSJson::Str(V, TEXT("class"));
		const ARPGPlayerCharacter* P = Player();
		return VerbClass.IsEmpty() || (P && P->ClassId == VerbClass);
	};
	L->DecorateVerb = [this](const TSJson::FObj& V, FLMChoiceView& View)
	{
		const int32 Mana = int32(TSJson::Num(V, TEXT("mana"), 0));
		if (!Mana) return;
		View.Verb += FString::Printf(TEXT(" · %d mana"), Mana);
		const ARPGPlayerCharacter* P = Player();
		if (P && P->Stats->Pool(RPGStat::Mana).Current < Mana) View.bEnabled = false;
	};
	L->PayVerb = [this](const TSJson::FObj& V) { if (ARPGPlayerCharacter* P = Player()) P->Stats->Pool(RPGStat::Mana).Current -= float(TSJson::Num(V, TEXT("mana"), 0)); };

	// Actions.
	auto GiveItem = [this](const FString& Id, const TSJson::FObj& Weights)
	{
		ARPGPlayerCharacter* P = Player();
		if (!P) return;
		const FTSItem It = UTSInventoryComponent::MakeItem(this, Id, Weights);
		if (P->Inventory->Add(It)) Feedback()->Toast(TEXT("Received ") + It.Name, UTSInventoryComponent::RarityColor(this, It.Rarity));
		else TSLoot::Spawn(GetWorld(), P->GetActorLocation(), &It, 0);
		P->Inventory->OnChanged.Broadcast();
	};
	auto AddGold = [this](int32 Amount)
	{
		ARPGPlayerCharacter* P = Player();
		if (!P) return;
		P->Inventory->Currency += Amount;
		Feedback()->Toast(FString::Printf(TEXT("%+d gold"), Amount), ToastGold);
		P->Inventory->OnChanged.Broadcast();
	};
	L->AddAction(TEXT("restore"), [this](const TSJson::FObj& A) { if (ARPGPlayerCharacter* P = Player(); P && TSJson::Bool(A, TEXT("restore"))) P->Restore(); });
	L->AddAction(TEXT("gold"), [AddGold](const TSJson::FObj& A) { AddGold(int32(TSJson::Num(A, TEXT("gold")))); });
	L->AddAction(TEXT("takeGold"), [AddGold](const TSJson::FObj& A) { AddGold(-int32(TSJson::Num(A, TEXT("takeGold")))); });
	L->AddAction(TEXT("giveItem"), [GiveItem](const TSJson::FObj& A) { GiveItem(TSJson::Str(A, TEXT("giveItem")), nullptr); });
	L->AddAction(TEXT("buy"), [this](const TSJson::FObj& A)
	{
		ARPGPlayerCharacter* P = Player();
		const TSJson::FObj Buy = TSJson::Obj(A, TEXT("buy"));
		const int32 Cost = int32(TSJson::Num(Buy, TEXT("cost"), 0));
		if (!P || P->Inventory->Currency < Cost) return;
		const FTSItem It = UTSInventoryComponent::MakeItem(this, TSJson::Str(Buy, TEXT("item")));
		if (P->Inventory->Add(It)) { P->Inventory->Currency -= Cost; Feedback()->Toast(TEXT("Bought ") + It.Name, UTSInventoryComponent::RarityColor(this, It.Rarity)); }
		else Feedback()->Toast(TEXT("Bag is full"));
		P->Inventory->OnChanged.Broadcast();
	});
	L->AddAction(TEXT("hostile"), [this](const TSJson::FObj& A) { SetHostile(TSJson::Str(A, TEXT("hostile"))); });
	L->AddAction(TEXT("duel"), [this](const TSJson::FObj& A) { if (ARPGCharacterBase* N = DialogueNpc(); N && TSJson::Bool(A, TEXT("duel"))) StartDuel(N); });
	L->AddAction(TEXT("leave"), [this](const TSJson::FObj& A)
	{
		const FString F = TSJson::Str(A, TEXT("leave"));
		for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (It->FactionId() == F && !It->IsDead()) It->Leave();
	});
	L->AddAction(TEXT("leaveSelf"), [this](const TSJson::FObj& A) { if (ARPGEnemy* E = Cast<ARPGEnemy>(DialogueNpc()); E && TSJson::Bool(A, TEXT("leaveSelf"))) E->Leave(); });
	L->AddAction(TEXT("recruit"), [this](const TSJson::FObj& A)
	{
		const UTSData& D = UTSData::Get(this);
		const FString Id = TSJson::Str(A, TEXT("recruit"));
		const TArray<TSharedPtr<FJsonValue>> At = TSJson::Arr(D.Entry(TEXT("npcs"), Id), TEXT("spawnAt"));
		if (At.Num() != 2) return;
		const FVector Loc = D.TileCenter(int32(At[0]->AsNumber()), int32(At[1]->AsNumber())) + FVector(0, 0, 120);
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		if (ARPGNPC* N = GetWorld()->SpawnActor<ARPGNPC>(Loc, FRotator::ZeroRotator, SP)) N->Init(Id);
	});
	L->AddAction(TEXT("dropStash"), [this](const TSJson::FObj& A)
	{
		if (ARPGCharacterBase* N = DialogueNpc(); N && TSJson::Bool(A, TEXT("dropStash"))) TSLoot::DropTable(GetWorld(), N->GetActorLocation(), TEXT("stash"));
	});

	// Events.
	L->ObjectiveLabels = { { TEXT("kill"), TEXT("Slain") }, { TEXT("collect"), TEXT("Found") } };
	L->OnQuest.AddLambda([this, GiveItem](const FString& Id, FName What)
	{
		const TSJson::FObj Def = Story()->Entry(TEXT("quests"), Id);
		const FString Name = TSJson::Str(Def, TEXT("name"));
		if (What == TEXT("started")) { Feedback()->Toast(TEXT("Quest started: ") + Name, ToastGold); return; }
		if (What == TEXT("complete")) { Feedback()->Toast(FString::Printf(TEXT("%s: complete — return to %s"), *Name, *TSJson::Str(Def, TEXT("giver"))), ToastGreen); return; }
		if (What != TEXT("turnedIn")) return;
		Feedback()->Toast(TEXT("Quest complete: ") + Name, ToastGreen);
		ARPGPlayerCharacter* P = Player();
		if (!P) return;
		const TSJson::FObj O = TSJson::Obj(Def, TEXT("objective"));
		if (TSJson::Str(O, TEXT("type")) == TEXT("collect")) P->Inventory->Remove(TSJson::Str(O, TEXT("item")), int32(TSJson::Num(O, TEXT("count"), 1)));
		const TSJson::FObj R = TSJson::Obj(Def, TEXT("reward"));
		if (const int32 G = int32(TSJson::Num(R, TEXT("gold"), 0))) { P->Inventory->Currency += G; Feedback()->Toast(FString::Printf(TEXT("+%d gold"), G), ToastGold); }
		if (TSJson::Has(R, TEXT("item")))
		{
			TSJson::FObj Weights;
			if (TSJson::Has(R, TEXT("rarity"))) { Weights = MakeShared<FJsonObject>(); Weights->SetNumberField(TSJson::Str(R, TEXT("rarity")), 1); }
			GiveItem(TSJson::Str(R, TEXT("item")), Weights);
		}
		P->GainXp(int32(TSJson::Num(R, TEXT("xp"), 0)));
		P->Inventory->OnChanged.Broadcast();
	});
	L->OnResolved.AddLambda([this](const FString& Encounter, const FString&)
	{
		Story()->SetFlag(Encounter + TEXT("_passable"));
		Feedback()->Toast(TEXT("The bridge is open."), ToastGreen);
	});
	L->OnDialogueOpened.AddLambda([this]()
	{
		if (ARPGPlayerCharacter* P = Player()) P->Tags.Remove(TEXT("Hidden"));
		UGameplayStatics::SetGamePaused(this, true);
		if (ARPGPlayerController* PC = Cast<ARPGPlayerController>(GetWorld()->GetFirstPlayerController())) PC->EnterUI();
	});
	L->OnDialogueClosed.AddLambda([this]()
	{
		EncounterCooldown = 0.5f;
		UGameplayStatics::SetGamePaused(this, false);
		if (ARPGPlayerController* PC = Cast<ARPGPlayerController>(GetWorld()->GetFirstPlayerController())) PC->EnterGameplay();
	});
}
