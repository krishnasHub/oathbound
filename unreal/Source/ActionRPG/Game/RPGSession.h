#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TSJson.h"
#include "STSDialogueBox.h"
#include "RPGSession.generated.h"

class ARPGCharacterBase;
class ATSCharacter;
class ARPGPlayerCharacter;
class ULMStory;
class UTSFeedback;

/**
 * This game's side of the story and its run-time feedback:
 *   - plugs the game into Loom (ULMStory): data, conditions (class, sex, gold, items), actions (buy, duel, leave,
 *     recruit...), text placeholders, verb stats and costs; reacts to Loom's events (pause for dialogue, toasts,
 *     quest rewards)
 *   - duels, faction hostility, and the toll bridge's "slipped past without talking" rule
 * (Floating text, toasts and shake are Tessera's UTSFeedback.)
 *
 * Unreal: a tickable world subsystem — one per level, reachable from anywhere via URPGSession::Get().
 */
UCLASS()
class ACTIONRPG_API URPGSession : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static URPGSession* Get(const UObject* WorldContext);

	/** Game worlds only (the engine also makes transient worlds that have no game instance or data). */
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override { return WorldType == EWorldType::Game || WorldType == EWorldType::PIE; }
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	/** Hooks the game into Loom (the game instance and its data exist from here on, not yet in Initialize). */
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(URPGSession, STATGROUP_Tickables); }
	virtual bool IsTickableWhenPaused() const override { return true; }

	ARPGPlayerCharacter* Player() const;
	ULMStory* Story() const;
	UTSFeedback* Feedback() const;

	/** The ~ debug view (dialogue odds, AI states, cheats). */
	bool IsDebug() const { return bDebug; }
	void SetDebug(bool bOn);

	// ---- duels / factions ----
	TWeakObjectPtr<ARPGCharacterBase> Duel;
	void SetHostile(const FString& FactionName, const FString& Bark = FString());
	void StartDuel(ARPGCharacterBase* Opponent);

	// ---- talking (characters -> Loom) ----
	void OpenDialogue(ATSCharacter* Npc, const FString& NodeId = FString());
	/** "!" / "?" over a character's head (their root dialogue), or "". */
	FString MarkerFor(const ATSCharacter* Npc) const;
	/** Who the open dialogue is with. */
	ARPGCharacterBase* DialogueNpc() const;
	/** Loom's conversation as Tessera's dialogue box shows it (verbs in the hero's colour when class-only). */
	FTSDialogueView DialogueView();

private:
	void Bind(ULMStory* L);
	void UpdateEncounters();
	float EncounterCooldown = 0.f;
	bool bDebug = false;
};
