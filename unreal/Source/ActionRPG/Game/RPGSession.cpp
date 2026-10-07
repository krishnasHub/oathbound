#include "RPGSession.h"
#include "ActionRPG.h"
#include "LMStory.h"
#include "RPGData.h"
#include "RPGCharacterBase.h"
#include "RPGPlayerCharacter.h"
#include "RPGEnemy.h"
#include "RPGNPC.h"
#include "RPGLoot.h"
#include "RPGInventoryComponent.h"
#include "RPGPlayerController.h"

#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

namespace
{
	const FLinearColor Gold(1.f, 0.83f, 0.3f), Green(0.44f, 0.88f, 0.54f), Red(1.f, 0.5f, 0.5f);
}

URPGSession* URPGSession::Get(const UObject* WorldContext)
{
	const UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	return W ? W->GetSubsystem<URPGSession>() : nullptr;
}

ULMStory* URPGSession::Story() const { return GetWorld()->GetSubsystem<ULMStory>(); }

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

// ---------------------------------------------------------------------------------------------
// Feedback
// ---------------------------------------------------------------------------------------------

void URPGSession::Float(const FVector& At, const FString& Text, const FLinearColor& Color, float Size)
{
	FRPGFloater F;
	F.World = At + FVector(FMath::FRandRange(-20.f, 20.f), FMath::FRandRange(-20.f, 20.f), 0);
	F.Text = Text; F.Color = Color; F.Size = Size;
	Floaters.Add(F);
}

void URPGSession::Toast(const FString& Text, const FLinearColor& Color)
{
	Toasts.Add({ Text, Color, 0.f });
	if (Toasts.Num() > 5) Toasts.RemoveAt(0);
	UE_LOG(LogRPG, Display, TEXT("[toast] %s"), *Text);
}

void URPGSession::Tick(float Dt)
{
	const bool bPaused = GetWorld()->IsPaused();
	for (FRPGToast& T : Toasts) T.Age += Dt;
	Toasts.RemoveAll([](const FRPGToast& T) { return T.Age > 3.4f; });
	if (bPaused) return;

	for (FRPGFloater& F : Floaters) { F.Age += Dt; F.World.Z += 60.f * Dt; }
	Floaters.RemoveAll([](const FRPGFloater& F) { return F.Age > F.Life; });
	ShakeAmount *= FMath::Pow(0.002f, Dt);

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
	Toast(Bark.IsEmpty() ? FString(TEXT("They attack!")) : Bark, Red);
	L->CloseDialogue();
}

void URPGSession::StartDuel(ARPGCharacterBase* Opponent)
{
	Duel = Opponent;
	if (ARPGEnemy* E = Cast<ARPGEnemy>(Opponent)) E->State = ERPGEnemyState::Chase;
	Toast(FString::Printf(TEXT("Duel! Bring %s to his knees (below 20%% health). His men won't interfere."), *Opponent->DisplayName), Gold);
}

void URPGSession::UpdateEncounters()
{
	ARPGPlayerCharacter* P = Player();
	const ULMStory* L = Story();
	if (!P || P->IsDead() || L->IsDialogueOpen() || Duel.IsValid() || EncounterCooldown > 0.f) return;
	const URPGData& D = URPGData::Get(this);
	const RPGJson::FObj All = D.Section(TEXT("encounters"));
	if (!All) return;
	for (const auto& KV : All->Values)
	{
		const FString Id = FString(*KV.Key);
		const RPGJson::FObj Enc = KV.Value->AsObject();
		if (L->HasFlag(Id + TEXT("_passable"))) continue;
		ARPGEnemy* Leader = nullptr;
		for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (It->Type == RPGJson::Str(Enc, TEXT("leader"))) { Leader = *It; break; }
		if (!Leader || Leader->IsDead() || !Leader->IsPassive()) continue;

		// (Nobody stops you to talk: the hero chooses to talk (E + click) or to fight. Slipping past without either
		// still turns the faction hostile.)
		if (P->GetActorLocation().Y > RPGJson::Num(Enc, TEXT("crossRow"), 12) * D.TileSize + 30.f)
		{
			SetHostile(RPGJson::Str(Enc, TEXT("faction")), RPGJson::Str(Enc, TEXT("crossBark")));   // you tried to slip past
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Talking
// ---------------------------------------------------------------------------------------------

void URPGSession::OpenDialogue(ARPGCharacterBase* Npc, const FString& NodeId)
{
	if (!Npc) return;
	Story()->OpenDialogue(Npc, FLMSpeaker{ Npc->DisplayName, Npc->NameColor, Npc->TalkKey, Npc->DialogueRoot }, NodeId);
}

FString URPGSession::MarkerFor(const ARPGCharacterBase* Npc) const
{
	return Npc ? Story()->MarkerFor(Npc->DialogueRoot) : FString();
}

ARPGCharacterBase* URPGSession::DialogueNpc() const { return Cast<ARPGCharacterBase>(Story()->Speaker()); }

// ---------------------------------------------------------------------------------------------
// The game's half of the story rules, registered with Loom
// ---------------------------------------------------------------------------------------------

void URPGSession::Bind(ULMStory* L)
{
	L->SetData(URPGData::Get(this).Root);
	L->DefaultVerb = TEXT("persuade");

	// What the hero is and has.
	L->StatValue = [this](FName Stat) { const ARPGPlayerCharacter* P = Player(); return P ? P->Stats->Get(Stat) : 0.f; };
	L->ItemCount = [this](const FString& Item) { const ARPGPlayerCharacter* P = Player(); return P ? P->Inventory->Count(Item) : 0; };
	auto OneOf = [](const RPGJson::FObj& C, const FString& Key, const FString& Value)
	{
		const TSharedPtr<FJsonValue> V = C->TryGetField(Key);
		if (!V) return false;
		if (V->Type == EJson::Array) { for (const auto& X : V->AsArray()) if (X->AsString() == Value) return true; return false; }
		return V->AsString() == Value;
	};
	L->AddCondition(TEXT("class"), [this, OneOf](const RPGJson::FObj& C) { const ARPGPlayerCharacter* P = Player(); return P && OneOf(C, TEXT("class"), P->ClassId); });
	L->AddCondition(TEXT("sex"), [this](const RPGJson::FObj& C) { const ARPGPlayerCharacter* P = Player(); return P && RPGJson::Str(C, TEXT("sex")) == P->Sex; });
	L->AddCondition(TEXT("gold"), [this](const RPGJson::FObj& C) { const ARPGPlayerCharacter* P = Player(); return P && P->Inventory->Gold >= RPGJson::Num(C, TEXT("gold")); });
	L->AddCondition(TEXT("hasItem"), [this](const RPGJson::FObj& C) { const ARPGPlayerCharacter* P = Player(); return P && P->Inventory->Count(RPGJson::Str(C, TEXT("hasItem"))) > 0; });

	L->AddPlaceholder(TEXT("gold"), [this]() { const ARPGPlayerCharacter* P = Player(); return P ? FString::FromInt(P->Inventory->Gold) : FString(TEXT("0")); });
	L->AddPlaceholder(TEXT("title"), [this]()
	{
		const ARPGPlayerCharacter* P = Player();
		const TSharedPtr<FJsonValue> Title = P && P->ClassDef ? P->ClassDef->TryGetField(TEXT("title")) : nullptr;
		if (!Title) return FString(TEXT("traveler"));
		return Title->Type == EJson::Object ? RPGJson::Str(Title->AsObject(), P->Sex) : Title->AsString();
	});

	// Verbs: class-only verbs, and the Mage's Hypnotize costs mana.
	L->VerbAvailable = [this](const RPGJson::FObj& V)
	{
		const FString VerbClass = RPGJson::Str(V, TEXT("class"));
		const ARPGPlayerCharacter* P = Player();
		return VerbClass.IsEmpty() || (P && P->ClassId == VerbClass);
	};
	L->DecorateVerb = [this](const RPGJson::FObj& V, FLMChoiceView& View)
	{
		const int32 Mana = int32(RPGJson::Num(V, TEXT("mana"), 0));
		if (!Mana) return;
		View.Verb += FString::Printf(TEXT(" · %d mana"), Mana);
		const ARPGPlayerCharacter* P = Player();
		if (P && P->Stats->Mana < Mana) View.bEnabled = false;
	};
	L->PayVerb = [this](const RPGJson::FObj& V) { if (ARPGPlayerCharacter* P = Player()) P->Stats->Mana -= float(RPGJson::Num(V, TEXT("mana"), 0)); };

	// Actions.
	auto GiveItem = [this](const FString& Id, const RPGJson::FObj& Weights)
	{
		ARPGPlayerCharacter* P = Player();
		if (!P) return;
		const FRPGItem It = URPGInventoryComponent::MakeItem(this, Id, Weights);
		if (P->Inventory->Add(It)) Toast(TEXT("Received ") + It.Name, URPGInventoryComponent::RarityColor(this, It.Rarity));
		else RPGLoot::Spawn(GetWorld(), P->GetActorLocation(), &It, 0);
		P->Inventory->OnChanged.Broadcast();
	};
	auto AddGold = [this](int32 Amount)
	{
		ARPGPlayerCharacter* P = Player();
		if (!P) return;
		P->Inventory->Gold += Amount;
		Toast(FString::Printf(TEXT("%+d gold"), Amount), Gold);
		P->Inventory->OnChanged.Broadcast();
	};
	L->AddAction(TEXT("restore"), [this](const RPGJson::FObj& A) { if (ARPGPlayerCharacter* P = Player(); P && RPGJson::Bool(A, TEXT("restore"))) P->Restore(); });
	L->AddAction(TEXT("gold"), [AddGold](const RPGJson::FObj& A) { AddGold(int32(RPGJson::Num(A, TEXT("gold")))); });
	L->AddAction(TEXT("takeGold"), [AddGold](const RPGJson::FObj& A) { AddGold(-int32(RPGJson::Num(A, TEXT("takeGold")))); });
	L->AddAction(TEXT("giveItem"), [GiveItem](const RPGJson::FObj& A) { GiveItem(RPGJson::Str(A, TEXT("giveItem")), nullptr); });
	L->AddAction(TEXT("buy"), [this](const RPGJson::FObj& A)
	{
		ARPGPlayerCharacter* P = Player();
		const RPGJson::FObj Buy = RPGJson::Obj(A, TEXT("buy"));
		const int32 Cost = int32(RPGJson::Num(Buy, TEXT("cost"), 0));
		if (!P || P->Inventory->Gold < Cost) return;
		const FRPGItem It = URPGInventoryComponent::MakeItem(this, RPGJson::Str(Buy, TEXT("item")));
		if (P->Inventory->Add(It)) { P->Inventory->Gold -= Cost; Toast(TEXT("Bought ") + It.Name, URPGInventoryComponent::RarityColor(this, It.Rarity)); }
		else Toast(TEXT("Bag is full"));
		P->Inventory->OnChanged.Broadcast();
	});
	L->AddAction(TEXT("hostile"), [this](const RPGJson::FObj& A) { SetHostile(RPGJson::Str(A, TEXT("hostile"))); });
	L->AddAction(TEXT("duel"), [this](const RPGJson::FObj& A) { if (ARPGCharacterBase* N = DialogueNpc(); N && RPGJson::Bool(A, TEXT("duel"))) StartDuel(N); });
	L->AddAction(TEXT("leave"), [this](const RPGJson::FObj& A)
	{
		const FString F = RPGJson::Str(A, TEXT("leave"));
		for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (It->FactionId() == F && !It->IsDead()) It->Leave();
	});
	L->AddAction(TEXT("leaveSelf"), [this](const RPGJson::FObj& A) { if (ARPGEnemy* E = Cast<ARPGEnemy>(DialogueNpc()); E && RPGJson::Bool(A, TEXT("leaveSelf"))) E->Leave(); });
	L->AddAction(TEXT("recruit"), [this](const RPGJson::FObj& A)
	{
		const URPGData& D = URPGData::Get(this);
		const FString Id = RPGJson::Str(A, TEXT("recruit"));
		const TArray<TSharedPtr<FJsonValue>> At = RPGJson::Arr(D.Entry(TEXT("npcs"), Id), TEXT("spawnAt"));
		if (At.Num() != 2) return;
		const FVector Loc = D.TileCenter(int32(At[0]->AsNumber()), int32(At[1]->AsNumber())) + FVector(0, 0, 120);
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		if (ARPGNPC* N = GetWorld()->SpawnActor<ARPGNPC>(Loc, FRotator::ZeroRotator, SP)) N->Init(Id);
	});
	L->AddAction(TEXT("dropStash"), [this](const RPGJson::FObj& A)
	{
		if (ARPGCharacterBase* N = DialogueNpc(); N && RPGJson::Bool(A, TEXT("dropStash"))) RPGLoot::DropTable(GetWorld(), N->GetActorLocation(), TEXT("stash"));
	});

	// Events.
	L->ObjectiveLabels = { { TEXT("kill"), TEXT("Slain") }, { TEXT("collect"), TEXT("Found") } };
	L->OnQuest.AddLambda([this, GiveItem](const FString& Id, FName What)
	{
		const RPGJson::FObj Def = Story()->Entry(TEXT("quests"), Id);
		const FString Name = RPGJson::Str(Def, TEXT("name"));
		if (What == TEXT("started")) { Toast(TEXT("Quest started: ") + Name, Gold); return; }
		if (What == TEXT("complete")) { Toast(FString::Printf(TEXT("%s: complete — return to %s"), *Name, *RPGJson::Str(Def, TEXT("giver"))), Green); return; }
		if (What != TEXT("turnedIn")) return;
		Toast(TEXT("Quest complete: ") + Name, Green);
		ARPGPlayerCharacter* P = Player();
		if (!P) return;
		const RPGJson::FObj O = RPGJson::Obj(Def, TEXT("objective"));
		if (RPGJson::Str(O, TEXT("type")) == TEXT("collect")) P->Inventory->Remove(RPGJson::Str(O, TEXT("item")), int32(RPGJson::Num(O, TEXT("count"), 1)));
		const RPGJson::FObj R = RPGJson::Obj(Def, TEXT("reward"));
		if (const int32 G = int32(RPGJson::Num(R, TEXT("gold"), 0))) { P->Inventory->Gold += G; Toast(FString::Printf(TEXT("+%d gold"), G), Gold); }
		if (RPGJson::Has(R, TEXT("item")))
		{
			RPGJson::FObj Weights;
			if (RPGJson::Has(R, TEXT("rarity"))) { Weights = MakeShared<FJsonObject>(); Weights->SetNumberField(RPGJson::Str(R, TEXT("rarity")), 1); }
			GiveItem(RPGJson::Str(R, TEXT("item")), Weights);
		}
		P->GainXp(int32(RPGJson::Num(R, TEXT("xp"), 0)));
		P->Inventory->OnChanged.Broadcast();
	});
	L->OnResolved.AddLambda([this](const FString& Encounter, const FString&)
	{
		Story()->SetFlag(Encounter + TEXT("_passable"));
		Toast(TEXT("The bridge is open."), Green);
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
