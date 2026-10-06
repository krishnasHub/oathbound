#include "RPGStory.h"
#include "ActionRPG.h"
#include "RPGData.h"
#include "RPGCharacterBase.h"
#include "RPGPlayerCharacter.h"
#include "RPGEnemy.h"
#include "RPGNPC.h"
#include "RPGLoot.h"
#include "RPGInventoryComponent.h"
#include "RPGGameMode.h"
#include "RPGWorldBuilder.h"
#include "RPGPlayerController.h"

#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

URPGStory* URPGStory::Get(const UObject* WorldContext)
{
	const UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	return W ? W->GetSubsystem<URPGStory>() : nullptr;
}

ARPGPlayerCharacter* URPGStory::Player() const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	return PC ? Cast<ARPGPlayerCharacter>(PC->GetPawn()) : nullptr;
}

// ---------------------------------------------------------------------------------------------
// Feedback
// ---------------------------------------------------------------------------------------------

void URPGStory::Float(const FVector& At, const FString& Text, const FLinearColor& Color, float Size)
{
	FRPGFloater F;
	F.World = At + FVector(FMath::FRandRange(-20.f, 20.f), FMath::FRandRange(-20.f, 20.f), 0);
	F.Text = Text; F.Color = Color; F.Size = Size;
	Floaters.Add(F);
}

void URPGStory::Toast(const FString& Text, const FLinearColor& Color)
{
	Toasts.Add({ Text, Color, 0.f });
	if (Toasts.Num() > 5) Toasts.RemoveAt(0);
	UE_LOG(LogRPG, Display, TEXT("[toast] %s"), *Text);
}

void URPGStory::Tick(float Dt)
{
	const bool bPaused = GetWorld()->IsPaused();
	for (FRPGToast& T : Toasts) T.Age += Dt;
	Toasts.RemoveAll([](const FRPGToast& T) { return T.Age > 3.4f; });
	if (bPaused) return;

	for (FRPGFloater& F : Floaters) { F.Age += Dt; F.World.Z += 60.f * Dt; }
	Floaters.RemoveAll([](const FRPGFloater& F) { return F.Age > F.Life; });
	ShakeAmount *= FMath::Pow(0.002f, Dt);

	if (Seed == 0) Seed = FMath::RandRange(1, 1000000000);
	EncounterCooldown -= Dt;
	UpdateEncounters();
}

// ---------------------------------------------------------------------------------------------
// Quests
// ---------------------------------------------------------------------------------------------

void URPGStory::StartQuest(const FString& Id)
{
	Quests.Add(Id, FRPGQuest{ TEXT("active"), 0 });
	Toast(TEXT("Quest started: ") + RPGJson::Str(URPGData::Get(this).Entry(TEXT("quests"), Id), TEXT("name")), FLinearColor(1.f, 0.83f, 0.3f));
	RefreshQuest(Id);
}

void URPGStory::RefreshQuest(const FString& Id)
{
	FRPGQuest* Q = Quests.Find(Id);
	if (!Q || Q->Status == TEXT("turnedIn")) return;
	const RPGJson::FObj Def = URPGData::Get(this).Entry(TEXT("quests"), Id);
	const RPGJson::FObj O = RPGJson::Obj(Def, TEXT("objective"));
	const FString Type = RPGJson::Str(O, TEXT("type"));
	const int32 Count = int32(RPGJson::Num(O, TEXT("count"), 1));
	if (Type == TEXT("collect")) { const ARPGPlayerCharacter* P = Player(); Q->Progress = P ? FMath::Min(Count, P->Inventory->Count(RPGJson::Str(O, TEXT("item")))) : 0; }
	if (Type == TEXT("flag")) Q->Progress = HasFlag(RPGJson::Str(O, TEXT("flag"))) ? 1 : 0;
	const bool bDone = Q->Progress >= Count;
	if (bDone && Q->Status == TEXT("active"))
	{
		Q->Status = TEXT("complete");
		Toast(FString::Printf(TEXT("%s: complete — return to %s"), *RPGJson::Str(Def, TEXT("name")), *RPGJson::Str(Def, TEXT("giver"))), FLinearColor(0.44f, 0.88f, 0.54f));
	}
	else if (!bDone && Q->Status == TEXT("complete")) Q->Status = TEXT("active");
}

void URPGStory::OnKill(const FString& EnemyType)
{
	for (auto& KV : Quests)
	{
		const RPGJson::FObj O = RPGJson::Obj(URPGData::Get(this).Entry(TEXT("quests"), KV.Key), TEXT("objective"));
		if (KV.Value.Status == TEXT("active") && RPGJson::Str(O, TEXT("type")) == TEXT("kill") && RPGJson::Str(O, TEXT("target")) == EnemyType)
		{
			KV.Value.Progress = FMath::Min(int32(RPGJson::Num(O, TEXT("count"), 1)), KV.Value.Progress + 1);
		}
	}
	RefreshQuests();
}

void URPGStory::TurnIn(const FString& Id)
{
	FRPGQuest* Q = Quests.Find(Id);
	ARPGPlayerCharacter* P = Player();
	if (!Q || !P) return;
	const RPGJson::FObj Def = URPGData::Get(this).Entry(TEXT("quests"), Id);
	const RPGJson::FObj O = RPGJson::Obj(Def, TEXT("objective"));
	if (RPGJson::Str(O, TEXT("type")) == TEXT("collect")) P->Inventory->Remove(RPGJson::Str(O, TEXT("item")), int32(RPGJson::Num(O, TEXT("count"), 1)));
	Q->Status = TEXT("turnedIn");
	Toast(TEXT("Quest complete: ") + RPGJson::Str(Def, TEXT("name")), FLinearColor(0.44f, 0.88f, 0.54f));

	const RPGJson::FObj R = RPGJson::Obj(Def, TEXT("reward"));
	if (const int32 Gold = int32(RPGJson::Num(R, TEXT("gold"), 0))) { P->Inventory->Gold += Gold; Toast(FString::Printf(TEXT("+%d gold"), Gold), FLinearColor(1.f, 0.83f, 0.3f)); }
	if (RPGJson::Has(R, TEXT("item")))
	{
		RPGJson::FObj Weights;
		if (RPGJson::Has(R, TEXT("rarity"))) { Weights = MakeShared<FJsonObject>(); Weights->SetNumberField(RPGJson::Str(R, TEXT("rarity")), 1); }
		const FRPGItem It = URPGInventoryComponent::MakeItem(this, RPGJson::Str(R, TEXT("item")), Weights);
		if (P->Inventory->Add(It)) Toast(TEXT("Received ") + It.Name, URPGInventoryComponent::RarityColor(this, It.Rarity));
		else RPGLoot::Spawn(GetWorld(), P->GetActorLocation(), &It, 0);
	}
	P->GainXp(int32(RPGJson::Num(R, TEXT("xp"), 0)));
	P->Inventory->OnChanged.Broadcast();
}

FString URPGStory::ProgressText(const FString& Id) const
{
	const RPGJson::FObj O = RPGJson::Obj(URPGData::Get(this).Entry(TEXT("quests"), Id), TEXT("objective"));
	const FString Type = RPGJson::Str(O, TEXT("type"));
	if (Type == TEXT("flag")) return RPGJson::Str(O, TEXT("text"));
	const FRPGQuest* Q = Quests.Find(Id);
	return FString::Printf(TEXT("%s %d/%d"), Type == TEXT("kill") ? TEXT("Slain") : TEXT("Found"), Q ? Q->Progress : 0, int32(RPGJson::Num(O, TEXT("count"), 1)));
}

// ---------------------------------------------------------------------------------------------
// Encounters / factions
// ---------------------------------------------------------------------------------------------

void URPGStory::SetHostile(const FString& FactionName, const FString& Bark)
{
	if (Faction(FactionName) == TEXT("hostile")) return;
	Factions.Add(FactionName, TEXT("hostile"));
	Duel = nullptr;
	Toast(Bark.IsEmpty() ? FString(TEXT("They attack!")) : Bark, FLinearColor(1.f, 0.5f, 0.5f));
	CloseDialogue();
}

void URPGStory::StartDuel(ARPGCharacterBase* Opponent)
{
	Duel = Opponent;
	if (ARPGEnemy* E = Cast<ARPGEnemy>(Opponent)) E->State = ERPGEnemyState::Chase;
	Toast(FString::Printf(TEXT("Duel! Bring %s to his knees (below 20%% health). His men won't interfere."), *Opponent->DisplayName), FLinearColor(1.f, 0.83f, 0.3f));
}

void URPGStory::Resolve(const FString& Encounter, const FString& Outcome)
{
	const RPGJson::FObj Enc = URPGData::Get(this).Entry(TEXT("encounters"), Encounter);
	const FString Flag = RPGJson::Str(Enc, TEXT("flag"));
	if (Flag.IsEmpty() || HasFlag(Flag)) return;
	SetFlag(Flag);
	SetFlag(RPGJson::Str(Enc, TEXT("outcomeFlag")), Outcome);
	SetFlag(Encounter + TEXT("_passable"));
	Toast(TEXT("The bridge is open."), FLinearColor(0.44f, 0.88f, 0.54f));
	UE_LOG(LogRPG, Display, TEXT("Encounter %s resolved: %s"), *Encounter, *Outcome);
	RefreshQuests();
}

void URPGStory::UpdateEncounters()
{
	ARPGPlayerCharacter* P = Player();
	if (!P || P->IsDead() || bDialogueOpen || Duel.IsValid() || EncounterCooldown > 0.f) return;
	const URPGData& D = URPGData::Get(this);
	const RPGJson::FObj All = D.Section(TEXT("encounters"));
	if (!All) return;
	for (const auto& KV : All->Values)
	{
		const FString Id = FString(*KV.Key);
		const RPGJson::FObj Enc = KV.Value->AsObject();
		if (HasFlag(Id + TEXT("_passable"))) continue;
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
// Dialogue
// ---------------------------------------------------------------------------------------------

bool URPGStory::CheckCond(const TSharedPtr<FJsonValue>& Cond) const
{
	if (!Cond.IsValid() || Cond->IsNull()) return true;
	if (Cond->Type == EJson::Array)
	{
		for (const TSharedPtr<FJsonValue>& C : Cond->AsArray()) if (!CheckCond(C)) return false;
		return true;
	}
	return Cond->Type == EJson::Object ? CondObj(Cond->AsObject()) : true;
}

bool URPGStory::CondObj(const RPGJson::FObj& C) const
{
	const ARPGPlayerCharacter* P = Player();
	auto OneOf = [&](const FString& Key, const FString& Value)
	{
		const TSharedPtr<FJsonValue> V = C->TryGetField(Key);
		if (!V) return false;
		if (V->Type == EJson::Array) { for (const auto& X : V->AsArray()) if (X->AsString() == Value) return true; return false; }
		return V->AsString() == Value;
	};
	if (C->HasField(TEXT("not"))) return !CheckCond(C->TryGetField(TEXT("not")));
	if (C->HasField(TEXT("quest"))) return OneOf(TEXT("is"), QuestStatus(RPGJson::Str(C, TEXT("quest"))));
	if (C->HasField(TEXT("class"))) return P && OneOf(TEXT("class"), P->ClassId);
	if (C->HasField(TEXT("sex"))) return P && RPGJson::Str(C, TEXT("sex")) == P->Sex;
	if (C->HasField(TEXT("flag")))
	{
		const FString* V = Flags.Find(RPGJson::Str(C, TEXT("flag")));
		return C->HasField(TEXT("is")) ? (V && *V == RPGJson::Str(C, TEXT("is"))) : V != nullptr;
	}
	if (C->HasField(TEXT("gold"))) return P && P->Inventory->Gold >= RPGJson::Num(C, TEXT("gold"));
	if (C->HasField(TEXT("hasItem"))) return P && P->Inventory->Count(RPGJson::Str(C, TEXT("hasItem"))) > 0;
	if (C->HasField(TEXT("disposition"))) return Disposition.FindRef(TalkKey()) >= RPGJson::Num(RPGJson::Obj(C, TEXT("disposition")), TEXT("gte"));
	return true;
}

FString URPGStory::Template(const FString& Text) const
{
	FString Out = Text;
	const ARPGPlayerCharacter* P = Player();
	if (P)
	{
		Out = Out.Replace(TEXT("{gold}"), *FString::FromInt(P->Inventory->Gold));
		const TSharedPtr<FJsonValue> Title = P->ClassDef ? P->ClassDef->TryGetField(TEXT("title")) : nullptr;
		FString TitleText = TEXT("traveler");
		if (Title) TitleText = Title->Type == EJson::Object ? RPGJson::Str(Title->AsObject(), P->Sex) : Title->AsString();
		Out = Out.Replace(TEXT("{title}"), *TitleText);
	}
	int32 Start;
	while ((Start = Out.Find(TEXT("{quest:"))) != INDEX_NONE)
	{
		const int32 End = Out.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
		if (End == INDEX_NONE) break;
		const FString Id = Out.Mid(Start + 7, End - Start - 7);
		const FRPGQuest* Q = Quests.Find(Id);
		const int32 Count = int32(RPGJson::Num(RPGJson::Obj(URPGData::Get(this).Entry(TEXT("quests"), Id), TEXT("objective")), TEXT("count"), 1));
		Out = Out.Left(Start) + FString::Printf(TEXT("%d/%d"), Q ? Q->Progress : 0, Count) + Out.Mid(End + 1);
	}
	return Out;
}

FString URPGStory::MarkerFor(const ARPGCharacterBase* Npc) const
{
	if (!Npc || Npc->DialogueRoot.IsEmpty()) return FString();
	const RPGJson::FObj Root = URPGData::Get(this).Entry(TEXT("dialogue"), Npc->DialogueRoot);
	FString Marker;
	for (const TSharedPtr<FJsonValue>& V : RPGJson::Arr(Root, TEXT("choices")))
	{
		const RPGJson::FObj C = V->AsObject();
		const FString M = RPGJson::Str(C, TEXT("marker"));
		if (M.IsEmpty() || !CheckCond(C->TryGetField(TEXT("if")))) continue;
		if (M == TEXT("?")) return M;
		Marker = M;
	}
	return Marker;
}

FString URPGStory::TalkKey() const
{
	const ARPGCharacterBase* N = DialogueNpc.Get();
	return N ? N->TalkKey : FString();
}

FString URPGStory::NodeText(const RPGJson::FObj& Node) const
{
	const TSharedPtr<FJsonValue> T = Node->TryGetField(TEXT("text"));
	if (!T) return FString();
	if (T->Type != EJson::Array) return T->AsString();
	const TArray<TSharedPtr<FJsonValue>>& Variants = T->AsArray();
	for (const TSharedPtr<FJsonValue>& V : Variants)
	{
		const RPGJson::FObj O = V->AsObject();
		if (CheckCond(O->TryGetField(TEXT("if")))) return RPGJson::Str(O, TEXT("text"));
	}
	return Variants.Num() ? RPGJson::Str(Variants.Last()->AsObject(), TEXT("text")) : FString();
}

FString URPGStory::CheckKey(const RPGJson::FObj& Choice) const
{
	return FString::Printf(TEXT("%d|%s|%s"), Seed, *TalkKey(), *RPGJson::Str(Choice, TEXT("_key")));
}

float URPGStory::CheckChance(const RPGJson::FObj& Choice) const
{
	const URPGData& D = URPGData::Get(this);
	const ARPGPlayerCharacter* P = Player();
	const RPGJson::FObj Check = RPGJson::Obj(Choice, TEXT("check"));
	const RPGJson::FObj V = D.Entry(TEXT("dialogueVerbs"), RPGJson::Str(Choice, TEXT("verb"), TEXT("persuade")));
	const float Score = P->Stats->Get(FName(RPGJson::Str(V, TEXT("stat"), TEXT("presence"))))
		+ (RPGJson::Has(V, TEXT("secondary")) ? P->Stats->Get(FName(RPGJson::Str(V, TEXT("secondary")))) * 0.5f : 0.f);
	float Pct = 50.f + 5.f * (Score - float(RPGJson::Num(Check, TEXT("resolve"), 5))) + float(RPGJson::Num(V, TEXT("bonus"), 0)) + Disposition.FindRef(TalkKey()) / 4.f;
	for (const TSharedPtr<FJsonValue>& B : RPGJson::Arr(Check, TEXT("bonus")))
		if (CheckCond(B->AsObject()->TryGetField(TEXT("if")))) Pct += float(RPGJson::Num(B->AsObject(), TEXT("add"), 0));
	return FMath::Clamp(Pct, 5.f, 95.f) / 100.f;
}

bool URPGStory::ChoiceVisible(const RPGJson::FObj& Choice) const
{
	const ARPGPlayerCharacter* P = Player();
	if (RPGJson::Has(Choice, TEXT("verb")))
	{
		const FString VerbClass = RPGJson::Str(URPGData::Get(this).Entry(TEXT("dialogueVerbs"), RPGJson::Str(Choice, TEXT("verb"))), TEXT("class"));
		if (!VerbClass.IsEmpty() && (!P || P->ClassId != VerbClass)) return false;   // class-only options
	}
	if (RPGJson::Has(Choice, TEXT("check")))
	{
		const bool* Done = Checks.Find(CheckKey(Choice));
		if (Done && !*Done) return false;   // a failed check stays failed
	}
	return CheckCond(Choice->TryGetField(TEXT("if")));
}

namespace
{
	// Seeded roll: same save seed + same NPC + same option => same result (no reload-scumming).
	float SeededRandom(const FString& Key)
	{
		uint32 H = 2166136261u;
		for (TCHAR C : Key) { H ^= uint32(C); H *= 16777619u; }
		uint32 T = H + 0x6D2B79F5u;
		T = (T ^ (T >> 15)) * (T | 1u);
		T ^= T + (T ^ (T >> 7)) * (T | 61u);
		return float((T ^ (T >> 14)) & 0xFFFFFF) / float(0x1000000);
	}
}

void URPGStory::OpenDialogue(ARPGCharacterBase* Npc, const FString& InNodeId)
{
	if (!Npc) return;
	DialogueNpc = Npc;
	bDialogueOpen = true;
	DialogueSpeaker = Npc->DisplayName;
	SpeakerColor = Npc->NameColor;
	if (ARPGPlayerCharacter* P = Player()) P->Tags.Remove(TEXT("Hidden"));
	UGameplayStatics::SetGamePaused(this, true);
	if (ARPGPlayerController* PC = Cast<ARPGPlayerController>(GetWorld()->GetFirstPlayerController())) PC->EnterUI();
	ShowNode(InNodeId.IsEmpty() ? Npc->DialogueRoot : InNodeId);
}

void URPGStory::ShowNode(const FString& Id)
{
	const RPGJson::FObj Node = URPGData::Get(this).Entry(TEXT("dialogue"), Id);
	if (Id.IsEmpty() || !Node) { CloseDialogue(); return; }
	NodeId = Id;

	const FString Text = Template(NodeText(Node));   // pick the text before the node's actions run
	RunActions(RPGJson::Arr(Node, TEXT("do")));
	if (!bDialogueOpen) return;
	DialogueText = Text;

	VisibleChoices.Reset();
	ChoiceViews.Reset();
	const TArray<TSharedPtr<FJsonValue>> Choices = RPGJson::Arr(Node, TEXT("choices"));
	const ARPGPlayerCharacter* P = Player();
	for (int32 I = 0; I < Choices.Num(); ++I)
	{
		const RPGJson::FObj C = Choices[I]->AsObject();
		C->SetStringField(TEXT("_key"), FString::Printf(TEXT("%s#%d"), *Id, I));
		if (!ChoiceVisible(C)) continue;
		VisibleChoices.Add(C);

		FRPGChoiceView View;
		View.Text = Template(RPGJson::Str(C, TEXT("text")));
		if (RPGJson::Has(C, TEXT("verb")))
		{
			const RPGJson::FObj V = URPGData::Get(this).Entry(TEXT("dialogueVerbs"), RPGJson::Str(C, TEXT("verb")));
			const int32 Mana = int32(RPGJson::Num(V, TEXT("mana"), 0));
			View.Verb = RPGJson::Str(V, TEXT("label")) + (Mana ? FString::Printf(TEXT(" · %d mana"), Mana) : FString());
			View.VerbColor = RPGJson::Has(V, TEXT("class")) && P ? P->NameColor : FLinearColor(1.f, 0.83f, 0.3f);
			if (Mana && P && P->Stats->Mana < Mana) View.bEnabled = false;
		}
		if (bDebug && RPGJson::Has(C, TEXT("check"))) View.Odds = FString::Printf(TEXT("%d%%"), FMath::RoundToInt(CheckChance(C) * 100.f));
		ChoiceViews.Add(View);
	}
	OnDialogueChanged.Broadcast();
}

void URPGStory::Choose(int32 Index)
{
	if (!bDialogueOpen || !VisibleChoices.IsValidIndex(Index) || !ChoiceViews[Index].bEnabled) return;
	const RPGJson::FObj C = VisibleChoices[Index];
	ARPGPlayerCharacter* P = Player();

	const RPGJson::FObj V = URPGData::Get(this).Entry(TEXT("dialogueVerbs"), RPGJson::Str(C, TEXT("verb")));
	if (const double Mana = RPGJson::Num(V, TEXT("mana"), 0)) { if (P) P->Stats->Mana -= float(Mana); }

	FString Next = RPGJson::Str(C, TEXT("next"));
	if (const RPGJson::FObj Check = RPGJson::Obj(C, TEXT("check")))
	{
		const FString Key = CheckKey(C);
		if (ForcedCheck.IsSet()) { Checks.Add(Key, ForcedCheck.GetValue()); ForcedCheck.Reset(); }
		if (!Checks.Contains(Key)) Checks.Add(Key, SeededRandom(Key) < CheckChance(C));
		Next = Checks[Key] ? RPGJson::Str(Check, TEXT("success")) : RPGJson::Str(Check, TEXT("fail"));
		UE_LOG(LogRPG, Display, TEXT("Dialogue check %s (%s): %s"), *Key, *RPGJson::Str(C, TEXT("verb")), Checks[Key] ? TEXT("success") : TEXT("fail"));
	}
	RunActions(RPGJson::Arr(C, TEXT("do")));
	if (!bDialogueOpen) return;
	if (Next.IsEmpty()) CloseDialogue(); else ShowNode(Next);
}

void URPGStory::CloseDialogue()
{
	if (!bDialogueOpen) return;
	bDialogueOpen = false;
	EncounterCooldown = 0.5f;
	UGameplayStatics::SetGamePaused(this, false);
	if (ARPGPlayerController* PC = Cast<ARPGPlayerController>(GetWorld()->GetFirstPlayerController())) PC->EnterGameplay();
	OnDialogueChanged.Broadcast();
}

void URPGStory::RunActions(const TArray<TSharedPtr<FJsonValue>>& Actions)
{
	ARPGPlayerCharacter* P = Player();
	ARPGCharacterBase* Npc = DialogueNpc.Get();
	for (const TSharedPtr<FJsonValue>& V : Actions)
	{
		const RPGJson::FObj A = V->AsObject();
		if (!A || !CheckCond(A->TryGetField(TEXT("if")))) continue;

		if (RPGJson::Has(A, TEXT("startQuest"))) StartQuest(RPGJson::Str(A, TEXT("startQuest")));
		if (RPGJson::Has(A, TEXT("turnIn"))) TurnIn(RPGJson::Str(A, TEXT("turnIn")));
		if (RPGJson::Bool(A, TEXT("restore")) && P) P->Restore();
		if (const RPGJson::FObj Buy = RPGJson::Obj(A, TEXT("buy")))
		{
			const int32 Cost = int32(RPGJson::Num(Buy, TEXT("cost"), 0));
			if (P && P->Inventory->Gold >= Cost)
			{
				const FRPGItem It = URPGInventoryComponent::MakeItem(this, RPGJson::Str(Buy, TEXT("item")));
				if (P->Inventory->Add(It)) { P->Inventory->Gold -= Cost; Toast(TEXT("Bought ") + It.Name, URPGInventoryComponent::RarityColor(this, It.Rarity)); }
				else Toast(TEXT("Bag is full"));
			}
		}
		if (RPGJson::Has(A, TEXT("setFlag"))) SetFlag(RPGJson::Str(A, TEXT("setFlag")));
		if (RPGJson::Has(A, TEXT("disposition"))) { float& Dv = Disposition.FindOrAdd(TalkKey()); Dv = FMath::Clamp(Dv + float(RPGJson::Num(A, TEXT("disposition"))), -100.f, 100.f); }
		if (RPGJson::Has(A, TEXT("gold")) && P) { const int32 G = int32(RPGJson::Num(A, TEXT("gold"))); P->Inventory->Gold += G; Toast(FString::Printf(TEXT("+%d gold"), G), FLinearColor(1.f, 0.83f, 0.3f)); }
		if (RPGJson::Has(A, TEXT("takeGold")) && P) { const int32 G = int32(RPGJson::Num(A, TEXT("takeGold"))); P->Inventory->Gold -= G; Toast(FString::Printf(TEXT("-%d gold"), G), FLinearColor(1.f, 0.83f, 0.3f)); }
		if (RPGJson::Has(A, TEXT("giveItem")) && P)
		{
			const FRPGItem It = URPGInventoryComponent::MakeItem(this, RPGJson::Str(A, TEXT("giveItem")));
			if (P->Inventory->Add(It)) Toast(TEXT("Received ") + It.Name, URPGInventoryComponent::RarityColor(this, It.Rarity));
			else RPGLoot::Spawn(GetWorld(), P->GetActorLocation(), &It, 0);
		}
		if (RPGJson::Has(A, TEXT("resolve")))
		{
			FString Enc, Outcome;
			if (RPGJson::Str(A, TEXT("resolve")).Split(TEXT(":"), &Enc, &Outcome)) Resolve(Enc, Outcome);
		}
		if (RPGJson::Has(A, TEXT("hostile"))) SetHostile(RPGJson::Str(A, TEXT("hostile")));
		if (RPGJson::Bool(A, TEXT("duel")) && Npc) StartDuel(Npc);
		if (RPGJson::Has(A, TEXT("leave")))
		{
			const FString F = RPGJson::Str(A, TEXT("leave"));
			for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (It->FactionId() == F && !It->IsDead()) It->Leave();
		}
		if (RPGJson::Bool(A, TEXT("leaveSelf"))) if (ARPGEnemy* E = Cast<ARPGEnemy>(Npc)) E->Leave();
		if (RPGJson::Has(A, TEXT("recruit")))
		{
			const FString Id = RPGJson::Str(A, TEXT("recruit"));
			const RPGJson::FObj Def = URPGData::Get(this).Entry(TEXT("npcs"), Id);
			const TArray<TSharedPtr<FJsonValue>> At = RPGJson::Arr(Def, TEXT("spawnAt"));
			if (At.Num() == 2)
			{
				FVector L = URPGData::Get(this).TileCenter(int32(At[0]->AsNumber()), int32(At[1]->AsNumber())) + FVector(0, 0, 120);
				FActorSpawnParameters SP;
				SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
				if (ARPGNPC* N = GetWorld()->SpawnActor<ARPGNPC>(L, FRotator::ZeroRotator, SP)) N->Init(Id);
			}
		}
		if (RPGJson::Bool(A, TEXT("dropStash")) && Npc) RPGLoot::DropTable(GetWorld(), Npc->GetActorLocation(), TEXT("stash"));
	}
	if (P) P->Inventory->OnChanged.Broadcast();
	RefreshQuests();
}
