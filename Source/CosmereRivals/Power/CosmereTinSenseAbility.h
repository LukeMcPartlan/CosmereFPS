// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Power/CosmerePowerAbility.h"
#include "CosmereTinSenseAbility.generated.h"

class UCosmereTinSensesComponent;

/**
 * The burn shared by both tins: keep the sense level up for as long as the power is running, and climb a
 * flare ladder while it burns.
 *
 * Why a timer rather than another press: the input layer treats a running power's next press as the way
 * OFF it, so tin could never be flared by pressing again. Escalating on the clock instead makes the ladder
 * something the player chooses by keeping the burn up - which is also the only version of it that works
 * for a Feruchemical tap, whose level comes from the metalmind rather than from the input.
 *
 * The row pays the base burn through the ordinary ResourceCostInterval; this class charges only the
 * surcharge above level 1, so a level-1 burn costs exactly what DT_Powers says and each flare adds its own
 * half per second on top. That keeps the price of tin in one readable place and still makes flaring cost
 * something.
 */
UCLASS(Abstract)
class COSMERERIVALS_API UCosmereTinSenseAbility : public UCosmerePowerAbility
{
	GENERATED_BODY()

public:
	UCosmereTinSenseAbility();

protected:
	virtual void OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void OnPowerEnded(bool bWasCancelled) override;

	/** One second of burn: pay the flare surcharge, then step the ladder if it is due. */
	void HandleTinLadder();

	/** Pushes Level onto the owning pawn's tin senses. 0 turns the burn off. */
	void ApplyTinLevel(int32 Level) const;

	int32 GetOwnerTinLevel() const;

	UCosmereTinSensesComponent* GetOwnerTinSenses() const;

	/** Hard ceiling on the ladder. Level 1 is the metal's own base effect. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Tin", meta = (ClampMin = "1"))
	int32 MaxTinLevel = 4;

	/** Seconds of continuous burning between one flare level and the next. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Tin", meta = (ClampMin = "0.1"))
	float FlareSeconds = 4.f;

	/** Reserve units per second this power adds for each level above 1. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Tin", meta = (ClampMin = "0.0"))
	float FlareSurchargePerSecond = 0.5f;

	/**
	 * True reads the level off the metalmind instead of climbing one. A Feruchemical tap already has a
	 * level of its own, and doubling it on top of the clock would let one press buy two ladders.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Tin")
	bool bSenseLevelFromMetalmind = false;

	/** Seconds burned since the last flare step. Reset on activation. */
	float SecondsSinceFlare = 0.f;

	FTimerHandle TinLadderTimer;
};
