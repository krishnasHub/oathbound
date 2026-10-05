#pragma once

#include "CoreMinimal.h"
#include "RPGCharacterBase.h"
#include "InputActionValue.h"
#include "RPGPlayerCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UAnimMontage;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class URPGInventoryComponent;
class URPGAbilityComponent;
class URPGPoseMesh;

/**
 * The player. Prototype equivalent: `Player`.
 *
 *   class + sex      from character select (stats, Manny/Quinn, weapon styles, abilities)
 *   camera           third-person boom; ranged attacks aim at the screen centre
 *   input            Enhanced Input, actions + mapping context created in code
 *   primary (LMB)    melee combo on the template's combat montage, or the Mage's arcane bolt
 *   secondary (RMB)  hold to block (perfect block window) or to draw the bow (release fires)
 *   jump (Space)     normal jump (jump/fall/land animations)
 *   dodge (Shift)    i-frames, class-specific cost/speed
 *   E / Q / X        talk, drink a potion, swap weapon style (Knight)
 *   1-4              class abilities
 */
UCLASS()
class ACTIONRPG_API ARPGPlayerCharacter : public ARPGCharacterBase
{
	GENERATED_BODY()

public:
	ARPGPlayerCharacter();

	void ApplyClass(const FString& ClassId, const FString& Sex);

	FString ClassId = TEXT("knight");
	FString Sex = TEXT("male");
	RPGJson::FObj ClassDef;
	RPGJson::FObj Style() const;
	int32 StyleIndex = 0;

	UPROPERTY(VisibleAnywhere, Category = "Camera") TObjectPtr<USpringArmComponent> CameraBoom;
	UPROPERTY(VisibleAnywhere, Category = "Camera") TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere, Category = "RPG") TObjectPtr<URPGInventoryComponent> Inventory;
	UPROPERTY(VisibleAnywhere, Category = "RPG") TObjectPtr<URPGAbilityComponent> Abilities;
	/** What you see: a posed copy of the animated mesh (arm raised to guard, etc.). */
	UPROPERTY(VisibleAnywhere, Category = "RPG") TObjectPtr<URPGPoseMesh> PoseMesh;
	virtual USkinnedMeshComponent* BodyMesh() const override;

	// Progression
	int32 Xp = 0;
	int32 AttrPoints = 0;
	int32 Level() const { return FMath::RoundToInt(Stats->Get(TEXT("level"))); }
	static int32 XpToNext(const UObject* Ctx, int32 Level);
	void GainXp(int32 Amount);

	// Combat state the HUD reads
	bool IsAttacking() const { return bAttacking; }
	bool IsDodging() const { return DodgeTime > 0.f; }
	bool IsGuarding() const { return bGuardHeld && GuardStyle().IsValid(); }
	bool IsDrawing() const { return bDrawing; }
	float DrawFraction() const;
	float DeathTimer = 0.f;

	// Damage-pipeline hooks
	virtual RPGJson::FObj GuardStyle() const override;
	virtual RPGJson::FObj PassiveStyle() const override;
	virtual void OnStaggered() override;
	virtual void Die(AActor* Killer) override;

	// IRPGAttacker
	virtual void DoAttackTrace(FName SourceBone) override;
	virtual void CheckCombo() override;

	/** Where the crosshair points (trace from the camera); used to aim projectiles and abilities. */
	FVector AimPoint(float MaxDistance = 6000.f) const;
	/** Direction from a point (default: the chest) to what the crosshair is on (with light aim assist). */
	FVector AimDirection() const { return AimDirection(Chest()); }
	FVector AimDirection(const FVector& From) const;
	/** Where shots leave from: the staff orb (Mage), the bow (Thief, bow out), otherwise the chest. */
	FVector Muzzle() const;
	/** Called by the ability component after a successful cast, to play the matching pose. */
	void OnAbilityUsed(const RPGJson::FObj& Ability);
	/** Turn to face the camera direction (yaw only). */
	void FaceAim();

	/** Nearest character you can talk to (villager, or a neutral enemy with dialogue), if in range. */
	ARPGCharacterBase* TalkTarget() const;
	void Restore();
	void DrinkPotion();
	void SwapStyle();
	/** Weapon meshes for the current style (world3d.styleKits). */
	void RefreshWeapons();
	/** Invulnerable dash; with a Strike definition it damages everything it passes through (Charge, Shadow Dash). */
	void StartDash(const FVector& Dir, float Speed, float Duration, const RPGJson::FObj& Strike);
	void BindUIHooks(TFunction<void(FName)> Handler) { UIHandler = MoveTemp(Handler); }

	FVector SpawnPoint;

	/** Self-test hook: drives exactly the same handlers as real input ("Attack", "Secondary", "Dodge", or any OnKey name). */
	void TestPress(FName Action, bool bDown);
	/** Drop held buttons (UI opened: the game will not see their release). */
	void ClearHeldInput() { bAttackHeld = false; bGuardHeld = false; bDrawing = false; MoveInput = FVector2D::ZeroVector; }
	bool bInputLocked = false;     // -RPGNoInput: ignore the real keyboard/mouse (automated runs)

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;

private:
	void CreateInput();
	void OnMove(const FInputActionValue& V);
	void OnLook(const FInputActionValue& V);
	void OnAttack();
	void OnAttackReleased();
	void OnSecondary();
	void OnSecondaryReleased();
	void OnDodge();
	void OnJump();
	void OnKey(FName Key);

	void StartCombo();
	void OnComboEnded(UAnimMontage* Montage, bool bInterrupted);
	void FireBolt();
	void FireArrow(float Held);
	void Respawn();
	void UpdateVisuals(float Dt);

	UPROPERTY() TObjectPtr<UInputMappingContext> InputContext;
	TMap<FName, TObjectPtr<UInputAction>> Actions;

	UPROPERTY() TObjectPtr<UAnimMontage> ComboMontage;
	TArray<FName> ComboSections;

	UPROPERTY() TObjectPtr<UStaticMeshComponent> ShieldBubble;    // mana shield
	UPROPERTY() TObjectPtr<UStaticMeshComponent> GuardArc;        // raised guard
	UPROPERTY() TObjectPtr<UStaticMeshComponent> AimLine;         // bow draw
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BubbleMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GuardMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> AimMat;

	TFunction<void(FName)> UIHandler;

	FVector2D MoveInput = FVector2D::ZeroVector;
	bool bAttackHeld = false;
	bool bAttacking = false;
	int32 ComboStep = 0;
	float LastAttackInput = -100.f;
	bool bGuardHeld = false;
	bool bDrawing = false;
	float DrawTime = 0.f;
	float BoltCooldown = 0.f;
	float CastSlow = 0.f;

	float DodgeTime = 0.f;
	FVector DodgeDir = FVector::ForwardVector;
	float DodgeSpeed = 0.f;
	RPGJson::FObj DashStrike;
	TSet<TWeakObjectPtr<AActor>> DashHit;
	FVector ShakeOffset = FVector::ZeroVector;

	// Shooting poses / holsters (procedural, see URPGPoseMesh)
	float CastKick = 0.f, CastHold = 0.f, OrbFlash = 0.f;     // Mage: staff thrust + orb pulse
	float BowRelease = 0.f, BowOut = 0.f;                      // Thief: string snap, bow kept out after a shot
	void UpdatePoses(float Dt);
	void PlayCast();
	void PlayBowShot();
	// Drawn bowstring (two halves pulled to the hand) + the nocked arrow, shown while drawing.
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> DrawParts;
	void UpdateBowString();
};
