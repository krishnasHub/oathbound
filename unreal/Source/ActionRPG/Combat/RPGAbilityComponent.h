#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TSJson.h"
#include "RPGAbilityComponent.generated.h"

class ARPGPlayerCharacter;
class ARPGCharacterBase;

/**
 * The four class abilities (keys 1-4). Each ability is data (game-data.json "abilities"); behaviour comes
 * from its "type", one implementation per type — same 12 types as the prototype:
 *   projectile, aoe, cone, dashStrike, buff, blink, chain, smoke, weaponBuff, heal, daze, mark
 * A cast with no valid target costs nothing and starts no cooldown.
 *
 * Unreal (later): one UGameplayAbility subclass per type if we move to GAS.
 */
UCLASS(ClassGroup = (RPG))
class ACTIONRPG_API URPGAbilityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGAbilityComponent();

	TArray<FString> Ids;
	TMap<FString, float> Cooldowns;

	void Setup(const TArray<FString>& InIds);
	TSJson::FObj Def(const FString& Id) const;
	bool Unlocked(const FString& Id) const;
	bool CanAfford(const TSJson::FObj& D) const;
	bool TryActivate(int32 Slot);
	void TickCooldowns(float Dt);

private:
	ARPGPlayerCharacter* Player() const;
	bool Run(const TSJson::FObj& D);
	ARPGCharacterBase* TargetNearAim(float Range) const;
	float Scaled(const TSJson::FObj& D, float V) const;
	void Fail(const FString& Msg) const;
};
