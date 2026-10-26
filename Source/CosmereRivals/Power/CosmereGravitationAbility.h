// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Power/CosmerePowerAbility.h"
#include "CosmereGravitationAbility.generated.h"

class UCharacterMovementComponent;

/**
 * Gravitation - the DT_Powers row Surge_Gravitation.
 *
 * Redirects the pawn's personal gravity to wherever the player is aiming, so a wall or a ceiling becomes the
 * floor. The direction is sampled once, when the power activates: this is a placement tool, not a continuous
 * steering tool, so looking around after activating does not move the new floor.
 *
 * All the state this power appears to own actually lives on UCosmereGravityComponent, which is the point of it:
 * the lashing outlives the press that set it, and landing on a surface refines it long after the ability has
 * ended. This class is therefore only two things - where the player is aiming, and whether they can afford it.
 *
 * Price ladder. A press costs 5 surge, and every press that lands within SameDirectionAngle of the floor already
 * latched doubles it: 5, 10, 20, 40. That is what makes flaring a deliberate spend rather than something to do
 * every frame, and it is why the cost is quoted by the component that decides the level - the two cannot drift.
 * Aiming back at world down is not a press at all: it drops the lashing and costs nothing.
 *
 * The row remains a one-shot with no cooldown, which is what makes a press always available to be priced.
 *
 * Why this is ability logic and not a GameplayEffect: a GameplayEffect can only modify attributes, and
 * gravity direction is a CharacterMovementComponent property. ACosmereCharacter's own documentation records
 * that constraint for run speed and jump height, and gravity direction is no different.
 */
UCLASS()
class COSMERERIVALS_API UCosmereGravitationAbility : public UCosmerePowerAbility
{
	GENERATED_BODY()

public:
	UCosmereGravitationAbility();

protected:
	//~ Begin UCosmerePowerAbility interface
	virtual void OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	//~ End UCosmerePowerAbility interface

	/** Where gravity should point: the aim direction, or the pawn's facing when nothing is driving it. */
	FVector GetGravityTargetDirection(const FGameplayAbilityActorInfo* ActorInfo) const;
};
