// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ability/CosmereAbilitySystemComponent.h"

UCosmereAbilitySystemComponent::UCosmereAbilitySystemComponent()
{
	// The owning PlayerState replicates, and this component rides along with it.
	SetIsReplicatedByDefault(true);

	// Mixed, not Full, and not Minimal. The owning client predicts its own gameplay effects and has
	// to receive them; every other client only needs the resulting tags and cue data. Full would
	// replicate every effect to every client, Minimal would starve the owner's prediction.
	SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
}
