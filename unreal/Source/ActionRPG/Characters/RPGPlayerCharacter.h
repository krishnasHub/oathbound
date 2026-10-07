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
 *   camera           top-down 3/4 view aimed with the cursor (mouse wheel zooms), or the third-person boom
 *                    aimed at the screen centre (world3d.camera.mode)
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
	TSJson::FObj ClassDef;
	TSJson::FObj Style() const;
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
	virtual TSJson::FObj GuardStyle() const override;
	virtual TSJson::FObj PassiveStyle() const override;
	virtual void OnStaggered() override;
	virtual void Die(AActor* Killer) override;

	// IRPGAttacker
	virtual void DoAttackTrace(FName SourceBone) override;
	virtual void CheckCombo() override;

	/** Top-down camera (world3d.camera.mode): fixed 3/4 view, the mouse cursor aims, WASD is screen-relative. */
	static bool IsTopDown(const UObject* WorldContext);
	/** Where the crosshair (third person) or the cursor (top-down) points; used to aim projectiles and abilities. */
	FVector AimPoint(float MaxDistance = 6000.f) const;
	/** Direction from a point (default: the chest) to what the crosshair is on (with light aim assist). */
	FVector AimDirection() const { return AimDirection(Chest()); }
	FVector AimDirection(const FVector& From) const;
	/** Where shots leave from: the staff orb (Mage), the bow (Thief, bow out), otherwise the chest. */
	FVector Muzzle() const;
	/** Where an arrow from From should land: the foe you're aiming at (chest), else the ground at the cursor,
	 *  no further than MaxRange. */
	FVector ArrowTarget(const FVector& From, float MaxRange) const;
	/** Called by the ability component after a successful cast, to play the matching pose. */
	void OnAbilityUsed(const TSJson::FObj& Ability);
	/** Turn to face the aim (camera direction, or the cursor when top-down; yaw only). */
	void FaceAim();
	/** While blocking: turn toward the nearest foe coming at you (else the aim). */
	void FaceThreat();

	/** Nearest character you can talk to (villager, or a neutral enemy with dialogue), if in range. */
	ARPGCharacterBase* TalkTarget() const;
	void Restore();
	void DrinkPotion();
	void SwapStyle();
	/** Weapon meshes for the current style (world3d.styleKits). */
	void RefreshWeapons();
	/** Invulnerable dash; with a Strike definition it damages everything it passes through (Charge, Shadow Dash). */
	void StartDash(const FVector& Dir, float Speed, float Duration, const TSJson::FObj& Strike);
	void BindUIHooks(TFunction<void(FName)> Handler) { UIHandler = MoveTemp(Handler); }

	FVector SpawnPoint;

	/** Self-test hook: drives exactly the same handlers as real input ("Attack", "Secondary", "Dodge", or any OnKey name). */
	void TestPress(FName Action, bool bDown);
	/** Self-test hook for click-to-move: a click on a character (attack or talk) or, with none, on the ground at Point. */
	void TestClick(const FVector& Point, ARPGCharacterBase* On = nullptr);

	// Top-down click-to-move: LMB on the ground walks there (hold to follow the cursor), on an enemy walks
	// into range and attacks, on a villager (or a foe willing to talk) walks up and opens the dialogue.
	enum class EClickGoal : uint8 { None, Move, Attack, Talk };
	EClickGoal GetClickGoal() const { return Goal; }
	/** Where a click-to-move is heading (for the HUD marker). */
	bool ClickDestination(FVector& Out) const;
	/** The character under the mouse cursor, if any (bHostile: it would be attacked rather than talked to). */
	ARPGCharacterBase* UnderCursor(bool& bHostile) const;
	/** The current path (navmesh corners), for tests and debug drawing. */
	const TArray<FVector>& GetPath() const { return Path; }

	/** The cursor for what a click would do now: "sword" / "dagger" / "wand" / "arrow" (attack), "talk", "talk_off"
	 *  (talk mode with nobody to talk to), "pointer", or NAME_None (no game cursor: UI, cutscenes, third person). */
	FName CursorIcon() const;

	/** Talk mode (E): the next click on a character walks up and talks instead of attacking. */
	bool IsTalkMode() const { return bTalkMode; }
	/** Why the hero can't talk to C right now (empty = they can). */
	FString TalkBlocker(const ARPGCharacterBase* C) const;
	/** Walk up to C and open the dialogue, or say why not. */
	void TryTalk(ARPGCharacterBase* C);

	/** Ability picker (Shift + mouse wheel; slow motion while open): the highlighted slot, -1 when closed. */
	int32 PickerSlot() const { return Picker; }
	/** Self-test hook: open the picker, move it Steps slots, then cast (or cancel). */
	void TestPicker(int32 Steps, bool bOpenOnly);
	void TestPickerRelease(bool bCast) { ClosePicker(bCast); }
	/** Drop held buttons (UI opened: the game will not see their release). */
	void ClearHeldInput() { bAttackHeld = false; bGuardHeld = false; bDrawing = false; MoveInput = FVector2D::ZeroVector; ClearGoal(); ClosePicker(false); SetTalkMode(false); }
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
	void OnZoom(const FInputActionValue& V);
	/** Top-down: the ray under the mouse cursor. False in third person, automated runs, or with no cursor. */
	bool CursorRay(FVector& Origin, FVector& Dir) const;
	/** The yaw WASD moves relative to: the camera's. */
	FRotator MoveFrame() const;
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

	UPROPERTY() TObjectPtr<UStaticMeshComponent> ShieldBubble;    // the Mage's barrier (held RMB)
	float BarrierPulse = 0.f;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> GuardArc;        // raised guard
	UPROPERTY() TObjectPtr<UStaticMeshComponent> AimLine;         // bow draw (unused: the arc preview replaced it)
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> ArcDots; // bow draw: dots along the arrow's arc
	UPROPERTY() TObjectPtr<class UPointLightComponent> NightGlow; // a soft light around the hero after dark
	void UpdateArcPreview();
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BubbleMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GuardMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> AimMat;

	TFunction<void(FName)> UIHandler;

	bool bTopDown = false;
	bool bScripted = false;   // inside TestPress: handlers must not read the real mouse cursor
	float ZoomTarget = 0.f, MinArm = 0.f, MaxArm = 0.f, ZoomStep = 0.f;

	// Click-to-move
	EClickGoal Goal = EClickGoal::None;
	FVector GoalPoint = FVector::ZeroVector;
	TWeakObjectPtr<ARPGCharacterBase> GoalActor;
	bool bMoveHeld = false;          // LMB held after a ground click: keep walking toward the cursor
	TArray<FVector> Path;
	int32 PathIndex = 0;
	float RepathIn = 0.f;
	void Click(ARPGCharacterBase* On, bool bHostile, const FVector& Ground);
	void ClearGoal() { Goal = EClickGoal::None; GoalActor = nullptr; Path.Reset(); bMoveHeld = false; }
	void Repath(const FVector& To);
	/** The ground point under the cursor. */
	bool CursorGround(FVector& Out) const;
	/** Steps click-to-move: arrives, attacks or talks when in range; returns the direction to walk (zero to stand). */
	FVector UpdateClickGoal(float Dt);
	/** How close a primary attack must be to reach Target (melee swing reach, or bolt range). */
	bool InAttackRange(const ARPGCharacterBase* Target) const;
	/** LMB attack in place (third person, Shift+LMB top-down, or the end of a click-to-attack walk). */
	void PrimaryAttack();
	float TalkRange() const;

	// Talk mode + ability picker
	bool bTalkMode = false;
	void SetTalkMode(bool bOn);
	int32 Picker = -1, LastPicked = 0;
	void OnPickReleased();
	void OpenPicker();
	void CyclePicker(int32 Step);
	void ClosePicker(bool bCast);

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
	TSJson::FObj DashStrike;
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
