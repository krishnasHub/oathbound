#pragma once

#include "CoreMinimal.h"
#include "RPGCharacterBase.h"
#include "RPGEnemy.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UAnimMontage;
class UTSRoutine;
class UTSSleep;
class UStaticMeshComponent;

UENUM()
enum class ERPGEnemyState : uint8 { Idle, Chase, Windup, Recover, Return, Leaving };

/**
 * Enemy driven by its entry in game-data.json "enemies". AI is the prototype's enemyAI() state machine:
 *
 *   idle (wander) -> chase -> windup (telegraphed) -> attack -> recover -> chase ...  | return (leash)
 *
 * Senses: TSPerception (a sight cone + short hearing radius from tuning.enemyVision, line of sight, stealth).
 * Territory: only engages the player inside its home map region unless provoked.
 * Factions: neutral faction members stand and watch until provoked (or a duel starts); they can be
 * talked to if they have dialogue, and they walk off the map when their gang disbands.
 * Languages: a foe with something to say, facing a hero who speaks its language ("speaks"), waits and watches
 * instead of attacking, until provoked. Everyone else it attacks as before.
 * Hours: "sleeps" (night | day) and "rest" (restPlaces: its bed, maybe behind a gate): Tessera's UTSSleep walks it
 * to bed and back; asleep it only hears a hero right beside it, and gets up at the end of its night.
 *
 * Unreal: no AIController/behaviour tree yet — the state machine ticks on the pawn, like the prototype.
 */
UCLASS()
class OATHBOUND_API ARPGEnemy : public ARPGCharacterBase
{
	GENERATED_BODY()

public:
	ARPGEnemy();

	void Init(const FString& InType, const FVector& InHome);

	FString Type;
	TSJson::FObj Def;
	FVector Home;
	FString Region;
	bool bProvoked = false;
	bool bResting = false;
	bool bPacified = false;
	bool bCarrying = false;
	ERPGEnemyState State = ERPGEnemyState::Idle;
	float RevealT = 0.f;     // just attacked: show even outside the player's view (threat sense)

	virtual bool IsPassive() const override;
	virtual bool IsLeaving() const override { return State == ERPGEnemyState::Leaving; }
	virtual FString FactionId() const override;
	virtual void OnDamaged(ATSCharacter* Src) override;
	/** Striking a neutral faction member (or a bystander during a duel) turns the whole faction hostile. */
	virtual void OnStruck(ATSCharacter* Src) override;
	/** A duel opponent yields (opens its yield dialogue) instead of dying. */
	virtual bool OnHurt(ATSCharacter* Src) override;
	virtual float HealthFloor() const override;
	virtual float KnockbackMul() const override { return float(TSJson::Num(Def, TEXT("knockbackMul"), 1.0)); }
	virtual void LoseTrack() override;
	/** Turned on the hero (caught stealing...): wide awake and hunting them, across regions. */
	void Provoke();
	/** Its language (enemies.<id>.speaks; "" = none, it can't be reasoned with). */
	FString Speaks() const { return TSJson::Str(Def, TEXT("speaks")); }
	/** The hero speaks its language (and so can talk to it, or daze it into talking). */
	virtual bool IsReasonable() const override;
	virtual FString NotReasonableWhy() const override;
	virtual FString ParleyNode() const override { return TSJson::Str(Def, TEXT("parleyDialogue")); }
	virtual bool IsWindingUp() const override { return bSpriteHold || State == ERPGEnemyState::Windup; }
	virtual bool IsHunting() const override { return !bPacified && !IsPassive() && (State == ERPGEnemyState::Chase || State == ERPGEnemyState::Windup || State == ERPGEnemyState::Recover); }
	FString YieldDialogueId() const { return TSJson::Str(Def, TEXT("yieldDialogue")); }
	virtual void OnStaggered() override;
	virtual void Die(AActor* Killer) override;

	/** Walk off for good: talked down (the hero earns its XP, x tuning.peaceXpMul) or gone with its faction.
	 *  bRest: laid to rest instead: it crumbles where it stands and its ghost rises, at peace. */
	void Leave(bool bRest = false, bool bPeace = true);
	/** Won over without a fight (fed, charmed, persuaded): stays where it is, friendly. Pays like Leave. Become: fields
	 *  that change with it (a new "name", "dialogue", "speaks"...). */
	void Pacify(const TSJson::FObj& Become = nullptr);
	/** Pacified, and taught a trade: walks its data's "trade" routine (Tessera UTSRoutine), carrying goods. */
	void BecomeTrader();
	bool IsPacified() const { return bPacified; }
	bool IsTrader() const { return Routine != nullptr; }
	/** Robbed of what it guards (data "prowl", flag relic_stolen) and still looking for it: out of its lair at night,
	 *  prowling the village, until it's given back (relic_returned), paid off (relic_paid) or killed (relic_brute_slain). */
	bool IsProwling() const;
	UTSRoutine* GetRoutine() const { return Routine; }
	/** Carrying its goods right now (the sack shows). */
	bool IsCarrying() const { return bCarrying; }

	/** What this one wants (one of enemies.<id>.wants, picked per spawn spot; the {"want": id} dialogue condition). */
	FString Wish;
	void ResetToHome();
	bool IsBoss() const { return TSJson::Bool(Def, TEXT("boss")); }
	TSJson::FObj CurrentAttack() const { return CurAtk; }
	float WindupProgress() const;

protected:
	virtual void Tick(float DeltaSeconds) override;

private:
	void RunAI(float Dt);
	void BeginWindup(const TSJson::FObj& Atk);
	void PerformAttack();
	void MoveToward(const FVector& Target, float SpeedMul);
	bool CanSeePlayer(float Dist) const;
	void FaceToward(const FVector& Target, float Dt, float Rate = 720.f);
	void BuildTelegraph();
	void Respawn();

	TSJson::FObj NextAttack() const;

	float T = 0.f, Cool = 0.f, RespawnTimer = 0.f, DeathHide = 0.f;
	int32 AtkIndex = 0;
	TSJson::FObj CurAtk;
	FVector WanderTarget;
	bool bHasWander = false;
	float Strafe = 1.f;
	float SwingDelay = -1.f;       // seconds until the swing montage starts (timed to land at windup end)
	float SwingRate = 1.f;
	FName SwingSection;
	UAnimMontage* SwingMontage = nullptr;

	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Telegraph;
	UPROPERTY() TObjectPtr<UTSRoutine> Routine;
	UPROPERTY() TObjectPtr<UTSSleep> Sleep;
	UPROPERTY() TObjectPtr<UTSRoutine> Prowl;
	/** A routine (Tessera UTSRoutine) from data { stops: [ { at: [tx, ty], wait, when, tag } ] }, through doors between areas. */
	UTSRoutine* MakeRoutine(const TSJson::FObj& Spec, FName Name);
	/** Prowling after its stolen relic: true when that took the tick. */
	bool TickProwl(float Dt);
	void SetupSleep();
	/** Asleep, or walking to bed: true when that took the tick (skip the AI). */
	bool TickSleep(float Dt);
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Sack;
	void GrantPeace(const TCHAR* How);
	void PlaceSack();
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> TelegraphMat;
};
