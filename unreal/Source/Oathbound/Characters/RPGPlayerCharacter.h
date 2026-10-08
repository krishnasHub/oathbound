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
class UTSInventoryComponent;
class UTSAbilityComponent;
class UTSPoseMesh;
class UTSCameraRig;
class UTSHeroControl;
class UTSChannel;

/**
 * The player. Prototype equivalent: `Player`.
 *
 *   class + sex      from character select (stats, Manny/Quinn, weapon styles, abilities)
 *   camera           UTSCameraRig: top-down 3/4 view aimed with the cursor (mouse wheel zooms), or the
 *                    third-person boom aimed at the screen centre (world3d.camera.mode)
 *   mouse            UTSHeroControl: click-to-move / attack / talk, talk mode, the ability picker; this class
 *                    supplies the rules (attack reach, who will talk) and the attacks themselves
 *   input            Enhanced Input, actions + mapping context created in code
 *   primary (LMB)    melee combo on the template's combat montage, or the Mage's arcane bolt
 *   secondary (RMB)  hold to block (perfect block window) or to draw the bow (release fires)
 *   jump (Space)     normal jump (jump/fall/land animations)
 *   dodge (Shift)    i-frames, class-specific cost/speed
 *   E / Q / X        talk, drink a potion, swap weapon style (Knight)
 *   1-4              class abilities
 */
UCLASS()
class OATHBOUND_API ARPGPlayerCharacter : public ARPGCharacterBase
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
	UPROPERTY(VisibleAnywhere, Category = "Camera") TObjectPtr<UTSCameraRig> Rig;
	/** Jump the camera to the hero (after a door), no lag swoop. */
	void SnapCamera();
	/** The map area the hero is in ("" = the main map); entering one names it and sets its mood (map.areas.<id>.look). */
	FString AreaId;
	void UpdateArea();
	UPROPERTY(VisibleAnywhere, Category = "RPG") TObjectPtr<UTSHeroControl> Control;
	UPROPERTY(VisibleAnywhere, Category = "RPG") TObjectPtr<UTSInventoryComponent> Inventory;
	/** A moment's work done standing still (picking a lock, lifting a purse): Tessera's channel. */
	UPROPERTY(VisibleAnywhere, Category = "RPG") TObjectPtr<UTSChannel> Channel;

	// The Thief's crouch (classes.<id>.sneak { speed }: Space toggles it instead of the dodge; tag "Sneaking").
	bool CanSneak() const;
	bool IsSneaking() const { return Tags.Has(TEXT("Sneaking")); }
	void SetSneaking(bool bOn);
	/** Walk speed from the class, crouched or not. */
	void UpdateSpeed();
	/** The next use of a thing came from E (picking a lock), not a click. */
	bool bUseByKey = false;

	// Action mode (E, then click): the click does the dedicated thing for what it's on.
	//   standing: talk to whoever will talk (a sleeper is woken); crouched Thief: steal from whoever can't see you;
	//   a lock: the Thief picks it; anything else usable: use it. Without E, clicks move / attack / talk as ever.
	/** The click in progress was made in action mode. */
	bool bActionClick = false;
	/** In action mode, the cursor for On (a character) or It (a thing), or nothing under the cursor:
	 *  "talk" / "talk_off" / "gear" (something to do) / "gear_off" (nothing). */
	FName ActionIcon(const ATSCharacter* On, const class ATSInteractable* It) const;
	/** The HUD's words for it ("Steal: Captain Brask (the toll purse)", "Pick the lock", or why not). */
	FString ActionLabel(const ATSCharacter* On, const class ATSInteractable* It) const;
	/** Steal from On right now (crouched Thief in action mode), as opposed to talking. */
	bool IsStealing() const;
	/** Self-test hook: an action-mode click on On. */
	void ActionClick(ATSCharacter* On);
	UPROPERTY(VisibleAnywhere, Category = "RPG") TObjectPtr<UTSAbilityComponent> Abilities;
	/** What you see: a posed copy of the animated mesh (arm raised to guard, etc.). */
	UPROPERTY(VisibleAnywhere, Category = "RPG") TObjectPtr<UTSPoseMesh> PoseMesh;
	virtual USkinnedMeshComponent* BodyMesh() const override;

	// Progression
	int32 Xp = 0;
	int32 AttrPoints = 0;
	virtual int32 Level() const override { return FMath::RoundToInt(Stats->Get(TEXT("level"))); }
	static int32 XpToNext(const UObject* Ctx, int32 Level);
	void GainXp(int32 Amount);

	// Combat state the HUD reads
	bool IsAttacking() const { return bAttacking; }
	bool IsDodging() const { return DodgeTime > 0.f; }
	bool IsGuarding() const { return bGuardHeld && GuardStyle().IsValid(); }
	bool IsDrawing() const { return bDrawing; }
	float DrawFraction() const;
	float DeathTimer = 0.f;

	// Damage-pipeline and ability hooks (ATSCharacter)
	virtual TSJson::FObj GuardStyle() const override;
	virtual void OnStaggered() override;
	virtual bool CanAct() const override { return Super::CanAct() && !IsDodging(); }
	/** "shield": the current weapon style must block. */
	virtual bool MeetsRequirement(const FString& Requirement, FString& Why) const override;
	virtual void OnParley(ATSCharacter* Target, const FString& Node) override;
	virtual void Die(AActor* Killer) override;

	// ITSAttacker
	virtual void DoAttackTrace(FName SourceBone) override;
	virtual void CheckCombo() override;

	/** Where the crosshair (third person) or the cursor (top-down) points; used to aim projectiles and abilities. */
	FVector AimPoint(float MaxDistance = 6000.f) const;
	/** Direction from a point (default: the chest) to what the crosshair is on (with light aim assist). */
	using ATSCharacter::AimDirection;
	virtual FVector AimDirection(const FVector& From) const override;
	/** Where shots leave from: the staff orb (Mage), the bow (Thief, bow out), otherwise the chest. */
	virtual FVector Muzzle() const override;
	/** Where an arrow from From should land: the foe you're aiming at (chest), else the ground at the cursor,
	 *  no further than MaxRange. */
	virtual FVector ArrowTarget(const FVector& From, float MaxRange) const override;
	/** Called by the ability component after a successful cast, to play the matching pose. */
	virtual void OnAbilityUsed(const TSJson::FObj& Ability) override;
	/** Turn to face the aim (camera direction, or the cursor when top-down; yaw only). */
	virtual void FaceAim() override;
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
	virtual void StartDash(const FVector& Dir, float Speed, float Duration, const TSJson::FObj& Strike) override;
	void BindUIHooks(TFunction<void(FName)> Handler) { UIHandler = MoveTemp(Handler); }

	FVector SpawnPoint;

	/** Self-test hook: drives exactly the same handlers as real input ("Attack", "Secondary", "Dodge", or any OnKey name). */
	void TestPress(FName Action, bool bDown);

	/** The cursor for what a click would do now: "sword" / "dagger" / "wand" / "arrow" (attack), "talk", "talk_off"
	 *  (talk mode with nobody to talk to), "pointer", or NAME_None (no game cursor: UI, cutscenes, third person). */
	FName CursorIcon() const;

	/** Does the hero speak Language (classes.<id>.languages; "*" = every language)? An empty language: nobody does. */
	bool Speaks(const FString& Language) const;
	/** "It speaks the Old Tongue. You don't." (languages.<id>.name) */
	static FString CantSpeakWhy(const UObject* WorldContext, const FString& Language);
	/** Why the hero can't talk to C right now (empty = they can). Talk mode (E) and clicks: UTSHeroControl. */
	FString TalkBlocker(const ATSCharacter* C) const;
	/** Drop held buttons (UI opened: the game will not see their release). */
	void ClearHeldInput();
	/** -RPGNoInput: ignore the real keyboard/mouse (automated runs). */
	void SetInputLocked(bool bLocked);

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
	UPROPERTY() TObjectPtr<class UPointLightComponent> OrbLight;  // the staff orb's night glow (a kit part's "nightLight")
	float OrbCandelas = 0.f, OrbSight = 0.f;
	/** Tessera's day/night event (UTSDayNight::OnNightLevel): the hero's glow and the staff orb follow the dark. */
	void OnNightLevel(float Night);
	/** The current weapons' kit parts with a "nightLight": light the first one's glowing part. */
	void BuildOrbLight();
	void UpdateArcPreview();
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BubbleMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GuardMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> AimMat;

	TFunction<void(FName)> UIHandler;

	bool bTopDown = false;

	/** The foe a click-to-attack is going for (aim at it), or null. */
	const ATSCharacter* ClickedFoe() const;
	/** How close a primary attack must be to reach Target (melee swing reach, or bolt range). */
	bool InAttackRange(const ATSCharacter* Target) const;
	/** LMB attack in place (third person, Shift+LMB top-down, or the end of a click-to-attack walk). */
	void PrimaryAttack();
	float TalkRange() const;
	void OnPickReleased();

	FVector2D MoveInput = FVector2D::ZeroVector;
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

	// Shooting poses / holsters (procedural, see UTSPoseMesh)
	float CastKick = 0.f, CastHold = 0.f, OrbFlash = 0.f;     // Mage: staff thrust + orb pulse
	float BowRelease = 0.f, BowOut = 0.f;                      // Thief: string snap, bow kept out after a shot
	void UpdatePoses(float Dt);
	void PlayCast();
	void PlayBowShot();
	// Drawn bowstring (two halves pulled to the hand) + the nocked arrow, shown while drawing.
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> DrawParts;
	void UpdateBowString();
};
