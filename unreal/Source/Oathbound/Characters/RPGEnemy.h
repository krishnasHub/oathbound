#pragma once

#include "CoreMinimal.h"
#include "RPGCharacterBase.h"
#include "RPGEnemy.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UAnimMontage;

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
	virtual bool IsReasonable() const override { return TSJson::Bool(Def, TEXT("intelligent")); }
	virtual FString ParleyNode() const override { return TSJson::Str(Def, TEXT("parleyDialogue")); }
	virtual bool IsWindingUp() const override { return bSpriteHold || State == ERPGEnemyState::Windup; }
	virtual bool IsHunting() const override { return !IsPassive() && (State == ERPGEnemyState::Chase || State == ERPGEnemyState::Windup || State == ERPGEnemyState::Recover); }
	FString YieldDialogueId() const { return TSJson::Str(Def, TEXT("yieldDialogue")); }
	virtual void OnStaggered() override;
	virtual void Die(AActor* Killer) override;

	void Leave();
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
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> TelegraphMat;
};
