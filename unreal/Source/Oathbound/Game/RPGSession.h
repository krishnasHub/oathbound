#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TSJson.h"
#include "STSDialogueBox.h"
#include "TSDayNight.h"
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
class OATHBOUND_API URPGSession : public UTickableWorldSubsystem
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
	/** bByHero: the hero struck them while they were willing to talk (a betrayal: the world darkens). */
	void SetHostile(const FString& FactionName, const FString& Bark = FString(), bool bByHero = false);
	void StartDuel(ARPGCharacterBase* Opponent);

	// ---- talking (characters -> Loom) ----
	void OpenDialogue(ATSCharacter* Npc, const FString& NodeId = FString());
	/** "!" / "?" over a character's head (their root dialogue), or "". */
	FString MarkerFor(const ATSCharacter* Npc) const;
	/** Who the open dialogue is with. */
	ARPGCharacterBase* DialogueNpc() const;
	/** Loom's conversation as Tessera's dialogue box shows it (verbs in the hero's colour when class-only). */
	FTSDialogueView DialogueView();

	// ---- things in the world (Tessera's interactables: graves, lost items...) ----
	/** Spawn data "interactables" { id: { ...ATSInteractable fields, "at": [tileX, tileY], "showIf": cond } }. */
	void SpawnInteractables();
	/** Show / hide each by its "showIf" (re-checked whenever the story moves). */
	void RefreshInteractables();
	/** Open a thing's conversation, or (a door) go through it. */
	void UseInteractable(class ATSInteractable* It);
	/** Through a door: a short fade, and the hero comes out at the other side (two-way: that one leads back). */
	void Travel(class ATSInteractable* Door);
	/** 0..1 black over the screen (doors). */
	float FadeAlpha() const;
	float FadeStart = -10.f;
	float FadeOut = 0.3f, FadeIn = 0.45f;

	// ---- the world's mood (Loom) shown as ambient life (Tessera) ----
	/** Show the world's mood now: weights Tessera's ambient life by band (data: moodLife { kind: [band -3 .. +3] }). */
	void ApplyMood();
	/** A base price at today's mood (tuning.moodPrices, per shown band -3..+3). */
	int32 PriceOf(int32 Base) const;
	/** The band last shown (the one in effect in the world). */
	int32 ShownMoodBand() const { return ShownBand; }
	/** Loom's mood changed band: shown at the next dawn or dusk (Tessera's day/night event). */
	void OnMoodBand(float Mood, int32 Band);
	void OnDayPhase(ETSDayPhase Phase);

private:
	void Bind(ULMStory* L);
	bool bMoodPending = false;
	int32 ShownBand = 0;
	/** Tessera's death event: a fallen enemy's ghost rises (ARPGGhost); killing one who could talk darkens the world. */
	void OnCharacterDied(ATSCharacter* Who, AActor* Killer);
	void UpdateEncounters();
	float EncounterCooldown = 0.f;
	bool bDebug = false;
};
