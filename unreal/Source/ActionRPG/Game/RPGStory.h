#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RPGJson.h"
#include "RPGStory.generated.h"

class ARPGCharacterBase;
class ARPGPlayerCharacter;

struct FRPGFloater { FVector World; FString Text; FLinearColor Color; float Size = 1.f; float Age = 0.f; float Life = 1.f; };
struct FRPGToast { FString Text; FLinearColor Color; float Age = 0.f; };
struct FRPGQuest { FString Status = TEXT("none"); int32 Progress = 0; };

/** What the dialogue UI shows for one choice. */
struct FRPGChoiceView
{
	FString Text;
	FString Verb;               // "" or e.g. "Intimidate · 20 mana"
	FLinearColor VerbColor = FLinearColor::White;
	bool bEnabled = true;
	FString Odds;               // debug only
};

DECLARE_MULTICAST_DELEGATE(FRPGStoryEvent);

/**
 * World state + story systems, ported from the prototype:
 *   - flags, NPC disposition, seeded dialogue checks, faction attitudes, the active duel
 *   - quests (kill / collect / flag objectives, rewards)
 *   - the dialogue engine (conditions, actions, text variants, hidden seeded rolls)
 *   - encounters (parley when you approach, hostility if you sneak past, outcomes)
 *   - feedback queues the HUD draws: floating combat text, toasts, camera shake
 *
 * Unreal: a tickable world subsystem — one per level, reachable from anywhere via URPGStory::Get().
 */
UCLASS()
class ACTIONRPG_API URPGStory : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static URPGStory* Get(const UObject* WorldContext);

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(URPGStory, STATGROUP_Tickables); }
	virtual bool IsTickableWhenPaused() const override { return true; }

	ARPGPlayerCharacter* Player() const;

	// ---- feedback ----
	void Float(const FVector& At, const FString& Text, const FLinearColor& Color, float Size = 1.f);
	void Toast(const FString& Text, const FLinearColor& Color = FLinearColor::White);
	void Shake(float Amount) { ShakeAmount = FMath::Max(ShakeAmount, Amount); }
	TArray<FRPGFloater> Floaters;
	TArray<FRPGToast> Toasts;
	float ShakeAmount = 0.f;
	bool bDebug = false;

	// ---- world state ----
	int32 Seed = 0;
	TMap<FString, FString> Flags;          // "met_elder" -> "true", "toll_outcome" -> "honor"
	TMap<FString, float> Disposition;      // talk key -> -100..100
	TMap<FString, bool> Checks;            // seeded check key -> passed
	TMap<FString, FString> Factions;       // "bandits" -> "neutral" | "hostile"
	TWeakObjectPtr<ARPGCharacterBase> Duel;

	bool HasFlag(const FString& Key) const { return Flags.Contains(Key); }
	void SetFlag(const FString& Key, const FString& Value = TEXT("true")) { Flags.Add(Key, Value); }
	FString Faction(const FString& Id) const { const FString* F = Factions.Find(Id); return F ? *F : TEXT("neutral"); }

	// ---- quests ----
	TMap<FString, FRPGQuest> Quests;
	FString QuestStatus(const FString& Id) const { const FRPGQuest* Q = Quests.Find(Id); return Q ? Q->Status : TEXT("none"); }
	void StartQuest(const FString& Id);
	void RefreshQuest(const FString& Id);
	void RefreshQuests() { TArray<FString> Ids; Quests.GetKeys(Ids); for (const FString& Id : Ids) RefreshQuest(Id); }
	void OnKill(const FString& EnemyType);
	void TurnIn(const FString& Id);
	FString ProgressText(const FString& Id) const;

	// ---- encounters / factions ----
	void SetHostile(const FString& FactionName, const FString& Bark = FString());
	void StartDuel(ARPGCharacterBase* Opponent);
	void Resolve(const FString& Encounter, const FString& Outcome);

	// ---- dialogue ----
	bool IsDialogueOpen() const { return bDialogueOpen; }
	void OpenDialogue(ARPGCharacterBase* Npc, const FString& NodeId = FString());
	void Choose(int32 VisibleIndex);
	void CloseDialogue();
	FString DialogueSpeaker, DialogueText;
	FLinearColor SpeakerColor = FLinearColor::White;
	TArray<FRPGChoiceView> ChoiceViews;
	FRPGStoryEvent OnDialogueChanged;
	TWeakObjectPtr<ARPGCharacterBase> DialogueNpc;

	/** Self-test hook: force the next dialogue check's result (otherwise the hidden seeded roll decides). */
	TOptional<bool> ForcedCheck;
	/** Index of the first visible choice whose text starts with Prefix, or INDEX_NONE. */
	int32 FindChoice(const FString& Prefix) const { return ChoiceViews.IndexOfByPredicate([&](const FRPGChoiceView& V) { return V.Text.StartsWith(Prefix); }); }

	bool CheckCond(const TSharedPtr<FJsonValue>& Cond) const;
	FString Template(const FString& Text) const;
	/** "!" (quest available), "?" (quest to turn in) or "" for a character's root dialogue. */
	FString MarkerFor(const ARPGCharacterBase* Npc) const;

private:
	void UpdateEncounters();
	void ShowNode(const FString& Id);
	void RunActions(const TArray<TSharedPtr<FJsonValue>>& Actions);
	bool CondObj(const RPGJson::FObj& C) const;
	float CheckChance(const RPGJson::FObj& Choice) const;
	bool ChoiceVisible(const RPGJson::FObj& Choice) const;
	FString CheckKey(const RPGJson::FObj& Choice) const;
	FString NodeText(const RPGJson::FObj& Node) const;
	FString TalkKey() const;

	bool bDialogueOpen = false;
	FString NodeId;
	TArray<RPGJson::FObj> VisibleChoices;
	TArray<FString> VisibleKeys;
	float EncounterCooldown = 0.f;
};
