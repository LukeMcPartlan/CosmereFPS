// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "CosmereAbilitySystemComponent.generated.h"

/**
 * The match ability system component. It lives on ACosmerePlayerState rather than on the pawn, so
 * that a respawn can recycle the pawn while player-scoped ability state survives it.
 *
 * Thin by intent. It exists as the named extension point for the power framework: per-ability gating
 * hangs off CheckCooldown / CheckCost on the ability, and anything that needs intercepting at the
 * component level lands here rather than as an edit to the engine class.
 */
UCLASS()
class COSMERERIVALS_API UCosmereAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UCosmereAbilitySystemComponent();
};
