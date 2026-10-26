// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Power/CosmerePowerAbility.h"
#include "CosmereBurnAbility.generated.h"

/**
 * A power that is held alight rather than spent in one blow, and that climbs a flare ladder while it burns.
 *
 * Tin, bronze and copper are all this shape: press once and the metal burns, it gets harder the longer it is
 * left burning, and pressing again puts it out. They differ only in what a level MEANS, which is the one thing
 * a subclass has to answer - everything about paying for the burn, stepping the ladder and ending it lives here.
 *
 * Why the ladder is on a clock rather than on a second press: the input layer treats a press on a running power
 * as the way OFF it, so a burn could never be flared by pressing again. Escalating on the clock also makes the
 * ladder something a Feruchemical tap can share, whose level comes from the metalmind instead.
 *
 * The row pays the base burn through the ordinary ResourceCostInterval; this class charges only the surcharge
 * above level 1. That keeps the price of a level-1 burn in DT_Powers where every other price lives, and makes
 * each extra level cost its own honest amount on top.
 */
UCLASS(Abstract)
class COSMERERIVALS_API UCosmereBurnAbility : public UCosmerePowerAbility
{
	GENERATED_BODY()

public:
	UCosmereBurnAbility();

	/**
	 * How hard this power is burning right now. 0 when it is not running.
	 *
	 * Public because it is the question bronze asks of everybody else: a player with a burn up at any level is
	 * a player a Seer can find.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power|Burn")
	int32 GetBurnLevel() const { return BurnLevel; }

protected:
	virtual void OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void OnPowerEnded(bool bWasCancelled) override;

	/**
	 * Subclass work: the burn just changed level. 0 means it is over, and is the only notification a subclass
	 * is guaranteed - so anything a burn turned on has to be turned off here.
	 */
	virtual void ApplyBurnLevel(int32 NewLevel);

	/** One second of burn: pay the flare surcharge, then step the ladder if it is due. */
	void HandleBurnLadder();

	/**
	 * Radius at the current level. Level 1 is the power's own reach and every flare above it doubles: 1x, 2x,
	 * 4x, 8x. Written once here so tin, bronze and copper cannot drift apart on what a flare buys.
	 */
	float GetScaledRadius(float BaseRadius) const;

	/** Hard ceiling on the ladder. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Burn", meta = (ClampMin = "1"))
	int32 MaxBurnLevel = 4;

	/** Seconds of continuous burning between one flare level and the next. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Burn", meta = (ClampMin = "0.1"))
	float FlareSeconds = 6.f;

	/** Reserve units per second this power adds for each level above 1. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Burn", meta = (ClampMin = "0.0"))
	float FlareSurchargePerSecond = 0.1f;

	/**
	 * True reads the level off the metalmind instead of climbing one. A Feruchemical tap already has a level of
	 * its own, and doubling it on top of the clock would let one press buy two ladders.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Burn")
	bool bBurnLevelFromMetalmind = false;

private:
	void SetBurnLevel(int32 NewLevel);

	int32 BurnLevel = 0;

	/** Seconds burned since the last flare step. */
	float SecondsSinceFlare = 0.f;

	FTimerHandle BurnLadderTimer;
};
