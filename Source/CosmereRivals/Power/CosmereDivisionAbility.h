// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Power/CosmerePowerAbility.h"
#include "CosmereDivisionAbility.generated.h"

/**
 * Division - the DT_Powers row Surge_Division.
 *
 * Decay: whatever the player is touching crumbles and burns away. The power is a held channel, so it
 * eats the surface in front of the crosshair for as long as the input is down and stops the moment it
 * is released.
 *
 * Why this is ability logic and not a GameplayEffect: a GameplayEffect can only modify attributes,
 * and what this power does depends on where the player is looking on each tick. The damage it deals
 * still travels through UGameplayStatics::ApplyDamage, which is the same pipeline the weapons use, so
 * a decay kill is credited to the player exactly as a shot is.
 *
 * The decay tick and the field actor are authority-only. A predicting client runs the ability - that
 * is what LocalPredicted buys - but it must not run its own copy of the damage, or every target the
 * server is burning would be burned twice.
 *
 * The row is a channel: the cost is charged on activation and then again on the row's own interval, so
 * the ability holds itself open until the input layer calls EndPower or the pool runs dry.
 */
UCLASS()
class COSMERERIVALS_API UCosmereDivisionAbility : public UCosmerePowerAbility
{
	GENERATED_BODY()

public:
	UCosmereDivisionAbility();

protected:
	//~ Begin UCosmerePowerAbility interface
	virtual void OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void OnPowerEnded(bool bWasCancelled) override;
	//~ End UCosmerePowerAbility interface

	/** One decay tick: burn whatever is in reach, and keep the field sitting on it. */
	void ApplyDecayTick();

	/**
	 * Where the power is currently touching the world.
	 *
	 * Returns false when nothing is in reach, in which case the field is hidden rather than left
	 * hanging in the air with nothing under it.
	 */
	bool ResolveTouch(FVector& OutTouchPoint, AActor*& OutVictim) const;

	/** Damage applied to the touched actor on every tick. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Division", meta = (ClampMin = "0.0"))
	float DamagePerTick = 12.f;

	/** Seconds between decay ticks. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Division", meta = (ClampMin = "0.01"))
	float TickInterval = 0.25f;

	/** How far ahead the decay reaches. Out to a bot's own stand-off distance, and still far short of a
	 * weapon's 20000 uu MaxTraceDistance, so this reads as a reach rather than as a shot. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Division", meta = (ClampMin = "0.0"))
	float TouchDistance = 3000.f;

	/**
	 * Spawned at the touch point while the power is held, and destroyed when it ends.
	 *
	 * Left unset by default, in which case the power is still fully functional and simply has no
	 * field underneath it - this is where the presentation lives, so the ability itself stays free of
	 * any dependency on a VFX system or a mesh.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Division")
	TSubclassOf<AActor> FieldClass;

private:
	FTimerHandle DecayTimer;

	/** The field instance, while the power is held. Authority only. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> ActiveField;

	/** Last tick's touch state, so the log records transitions rather than every tick. */
	bool bTouchingLastTick = false;
};
