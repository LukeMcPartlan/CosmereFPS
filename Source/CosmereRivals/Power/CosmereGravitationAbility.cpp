// Copyright Epic Games, Inc. All Rights Reserved.

#include "Power/CosmereGravitationAbility.h"

#include "Character/CosmereCharacter.h"
#include "Character/CosmereGravityComponent.h"
#include "CosmereRivals.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Net/CosmerePlayerState.h"
#include "Power/CosmereSurgeResourceComponent.h"

UCosmereGravitationAbility::UCosmereGravitationAbility()
{
	// The new floor latches, so there is nothing to hold open: the ability applies the direction and ends in
	// the same activation, which is also what makes the next press always available. Nothing here has to
	// survive to a release, so the ability is free to be a plain one-shot.
	bEndAfterActivation = true;
}

FVector UCosmereGravitationAbility::GetGravityTargetDirection(const FGameplayAbilityActorInfo* ActorInfo) const
{
	const APawn* Pawn = ActorInfo ? Cast<APawn>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (!Pawn)
	{
		// No avatar to redirect. Returning the default leaves gravity where it was instead of guessing.
		return UCharacterMovementComponent::DefaultGravityDirection;
	}
	
	if (Pawn->GetController())
	{
		// The pawn's view rotation rather than the controller's control rotation. The two agree whenever the view
		// is world-upright, which is why the control rotation was enough before; they stop agreeing once a lashing
		// has the pawn in free aim. There the picture is a free 3-axis orientation whose pitch is not clamped,
		// while the control rotation is the roll-free copy the camera manager holds inside its pitch limit - so
		// reading the copy would refuse to aim past straight up, which is a direction this power has to be able
		// to take. This is also the same forward the camera and the weapon trace already use.
		const FVector AimDirection = Pawn->GetViewRotation().Vector();
		if (!AimDirection.IsNearlyZero())
		{
			return AimDirection;
		}
	}

	// A controllerless pawn still has a facing, which is a better answer than dropping to world-down.
	return Pawn->GetActorForwardVector();
}

void UCosmereGravitationAbility::OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	ACosmereCharacter* Target = GetOwnerCosmereCharacter();
	UCosmereGravityComponent* Gravity = Target ? Target->GetGravityComponent() : nullptr;
	if (!Gravity)
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereGravitationAbility: the avatar has no Cosmere gravity component, so gravity was not redirected."));
		return;
	}

	const FVector TargetDirection = GetGravityTargetDirection(ActorInfo);

	// Priced with the same call Lash is about to make, so the level charged for and the level reached cannot
	// disagree. A preview of 0 means this press would drop the lashing, and stopping is always free.
	const int32 NextLevel = Gravity->PreviewLashLevel(TargetDirection);
	const float Cost = Gravity->GetLashCostForLevel(NextLevel);

	if (Cost > 0.f)
	{
		ACosmerePlayerState* State = Target->GetPlayerState<ACosmerePlayerState>();
		UCosmereSurgeResourceComponent* Surges = State ? State->GetSurgeResourceComponent() : nullptr;
		if (!Surges)
		{
			UE_LOG(LogCosmere, Warning,
				TEXT("CosmereGravitationAbility: %s has no stormlight reserve, so Gravitation cannot be paid for."),
				*Target->GetName());
			return;
		}

		// Read from replicated state on both machines, so a client predicts the same refusal the server would
		// give instead of lashing and then being corrected a round trip later.
		if (!Surges->CanAffordStormlight(Cost))
		{
			UE_LOG(LogCosmere, Verbose,
				TEXT("CosmereGravitationAbility: %s cannot afford level %d at %.0f stormlight."),
				*Target->GetName(), NextLevel, Cost);
			return;
		}

		// Only the server deducts. Letting the client spend as well would leave the two machines one press
		// apart on every dropped or retried activation.
		if (ActorInfo && ActorInfo->IsNetAuthority())
		{
			Surges->TrySpendStormlight(Cost);
		}
	}

	// The lash itself runs on both machines: a gravity direction is movement state, not a resource, and the
	// movement component carries it to the server on its own. The client is already walking on the wall by the
	// time the server's copy of the spend arrives.
	const int32 ReachedLevel = Gravity->Lash(TargetDirection);
	
	// A lash that latched is now being paid for out of this player's reserve, so it is handed over as a target.
	// Running the reserve dry is then one rule that takes every target down at once, rather than a check that
	// only ever knows about the caster's own pawn. A press that cleared instead is not registered: there is
	// nothing left to pay for.
	if (ReachedLevel > 0)
	{
		Target->RegisterLashTarget(Gravity);
	}
	
	UE_LOG(LogCosmere, Log,
		TEXT("CosmereGravitationAbility: %s at level %d for %.0f surge, gravity now %s for %s."),
		(ReachedLevel > 0) ? TEXT("lashed") : TEXT("cleared"),
		ReachedLevel,
		Cost,
		*Gravity->GetGravityDirection().ToCompactString(),
		*Target->GetName());
}

// There is deliberately no OnPowerEnded override here.
//
// A lashing that snapped back the moment the key was released could never be used for anything: redirecting
// gravity is only useful if the new floor is still there once the hand comes off the key, which is what lets
// the player walk onto the wall they just made. The new direction therefore outlives the press that set it,
// whatever ended the ability, and the next press is what overwrites it.
