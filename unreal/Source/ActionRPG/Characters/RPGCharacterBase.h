#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "RPGAnimNotifies.h"
#include "RPGStatsComponent.h"
#include "RPGJson.h"
#include "RPGCharacterBase.generated.h"

class UStaticMeshComponent;
class UAnimMontage;
class UAnimInstance;
class UMaterialInstanceDynamic;

/**
 * Shared base for the player, enemies and villagers.
 *
 *   - appearance from world3d.looks: Manny / Quinn mannequin (combat anim blueprint) or a slime blob,
 *     optional colour tint and scale
 *   - stats (URPGStatsComponent) and timed tags (Invulnerable, Staggered, ...)
 *   - poise, knockback, hit flash, death
 *
 * Unreal: ACharacter (capsule + CharacterMovement + skeletal mesh). Prototype equivalent: `Character`.
 */
UENUM()
enum class ERPGTeam : uint8 { Player, Enemy, Villager };

UCLASS(Abstract)
class ACTIONRPG_API ARPGCharacterBase : public ACharacter, public IRPGAttacker
{
	GENERATED_BODY()

public:
	ARPGCharacterBase();

	ERPGTeam Team = ERPGTeam::Villager;

	// ---- identity (dialogue / UI) ----
	FString DisplayName;
	FLinearColor NameColor = FLinearColor::White;
	FString DialogueRoot;      // root dialogue node ("" = can't talk)
	FString TalkKey;           // key for disposition / seeded rolls ("elder", "bandit_captain", ...)

	// ---- hooks the damage pipeline uses (overridden by player / enemy) ----
	/** Neutral faction member: won't fight, can be talked to. */
	virtual bool IsPassive() const { return false; }
	virtual bool IsLeaving() const { return false; }
	virtual FString FactionId() const { return FString(); }
	/** Weapon style "secondary" block entry while the guard is raised (null otherwise). */
	virtual RPGJson::FObj GuardStyle() const { return nullptr; }
	float GuardTime = 0.f;                 // seconds since the guard went up (perfect-block window)
	/** Weapon style "passive" (mana shield) or null. */
	virtual RPGJson::FObj PassiveStyle() const { return nullptr; }
	virtual void OnDamaged(ARPGCharacterBase* Src) {}
	/** Dialogue node opened when this character yields a duel. */
	virtual FString YieldDialogueId() const { return FString(); }
	float MarkMul = 1.f;                   // Insight: damage taken multiplier while Marked

	void Stagger(float Duration) { Tags.Add(TEXT("Staggered"), Duration); OnStaggered(); }
	FVector Chest() const { return GetActorLocation() + FVector(0, 0, 30.f * GetActorScale3D().Z); }
	FVector Head() const { return GetActorLocation() + FVector(0, 0, HeadZ * GetActorScale3D().Z); }
	float HeadZ = 80.f;        // top of the head above the actor centre (a sprite is taller than the mannequin)
	float Radius() const;

	UPROPERTY(VisibleAnywhere, Category = "RPG")
	TObjectPtr<URPGStatsComponent> Stats;

	FRPGTags Tags;

	/** Appearance: "manny" | "quinn" | "slime", tint (#rrggbb, empty = default look), uniform scale. */
	void SetLook(const FString& MeshKind, const FString& TintHex, float Scale);
	/** Reads { "mesh", "tint", "scale" } from world3d.looks[Id]. */
	void SetLookFromData(const FString& LookId);
	/** Builds weapon kits (world3d.kits) from simple parts and attaches them to the skeleton. */
	void SetWeaponKits(const TArray<FString>& KitIds);
	/** Move a kit off its bone to a pose relative to the body (e.g. shield raised in front while blocking), or back. */
	void PoseKit(const FString& KitId, bool bOffBone, const FTransform& BodyRelative = FTransform::Identity);
	/** Move a kit between its hand mount and its holster ("holster" in world3d.kits: belt, back...). */
	void SetKitHolstered(const FString& KitId, bool bHolstered);
	/** The glowing part of a kit (e.g. the staff orb) and its light, or null. */
	UStaticMeshComponent* KitGlow(const FString& KitId) const;
	/** A kit part by its "id" in the data (e.g. the bow's "string"), and a kit's mount (its weapon space). */
	UStaticMeshComponent* KitPart(const FString& KitId, const FString& PartId) const { const TObjectPtr<UStaticMeshComponent>* P = KitParts.Find(KitId + TEXT("/") + PartId); return P ? P->Get() : nullptr; }
	USceneComponent* KitRoot(const FString& KitId) const { const FKitMount* K = KitMounts.Find(KitId); return K ? K->Root.Get() : nullptr; }
	/** Brighten a kit glow: 0 = resting, 1 = full flash. */
	void SetKitGlow(const FString& KitId, float Flash);

	bool IsDead() const { return bDead; }
	FString MeshKind;

	// Poise / stagger
	float MaxPoise = 50.f, Poise = 50.f, PoiseTimer = 0.f;

	/** Push the character (prototype knockback units are px/s). */
	void Knock(const FVector& Velocity);
	void Flash();

	virtual void Die(AActor* Killer);
	virtual void OnStaggered() {}

	// IRPGAttacker (default: no melee)
	virtual void DoAttackTrace(FName SourceBone) override {}
	virtual void CheckCombo() override {}

	/** Unit vector the character faces (yaw only). */
	FVector Facing() const { return GetActorForwardVector().GetSafeNormal2D(); }

	/** The mesh that is actually rendered (weapons attach here). The player renders a posed copy. */
	virtual USkinnedMeshComponent* BodyMesh() const { return GetMesh(); }

	// ---- 2D looks (RPGLook): a pixel-art sprite instead of the 3D body ----
	/** Show sprite sheet SPR_<Sheet> instead of the 3D body (no-op in the 3D look). */
	void UseSprite(const FString& Sheet);
	/** Hide the 3D body, weapons and blob (they keep animating, so hit timing is unchanged). */
	void HideBody();
	float SpriteAttackAt = -100.f;   // when the last attack/cast/shot started (sprite plays its swing frames)
	bool bSpriteHold = false;        // hold the wind-up frame (drawing a bow)
	UPROPERTY() TObjectPtr<class URPGSpriteComponent> Sprite;

	UAnimInstance* Anim() const;
	float PlayMontage(UAnimMontage* Montage, float Rate = 1.f, FName Section = NAME_None);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY() TObjectPtr<UStaticMeshComponent> Blob;   // slime body
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> BodyMaterials;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> WeaponParts;
	struct FKitMount { TObjectPtr<USceneComponent> Root; FName Bone; FTransform OnBone; bool bOffBone = false;
		FName HolsterBone; FTransform Holster; bool bHasHolster = false; bool bHolstered = false; };
	struct FKitGlow { TObjectPtr<UStaticMeshComponent> Mesh; TObjectPtr<UMaterialInstanceDynamic> Mat; TObjectPtr<class UPointLightComponent> Light; FVector BaseScale = FVector::OneVector; };
	TMap<FString, FKitGlow> KitGlows;
	UPROPERTY() TMap<FString, TObjectPtr<UStaticMeshComponent>> KitParts;
	TMap<FString, FKitMount> KitMounts;

	bool bDead = false;
	float FlashTime = 0.f;
	FVector KnockVelocity = FVector::ZeroVector;
	float DeathTime = 0.f;
	float BlobPhase = 0.f;
};
