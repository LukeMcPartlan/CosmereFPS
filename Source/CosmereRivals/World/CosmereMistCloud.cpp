// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/CosmereMistCloud.h"

#include "Character/CosmereCharacter.h"
#include "Character/CosmereTinSensesComponent.h"
#include "Components/SceneComponent.h"
#include "Kismet/GameplayStatics.h"

ACosmereMistCloud::ACosmereMistCloud()
{
	PrimaryActorTick.bCanEverTick = true;

	// Fast enough that stepping into a cloud with tin up reads as instant, slow enough that a field of
	// clouds costs nothing. The tick early-outs unless the answer actually changed.
	PrimaryActorTick.TickInterval = 0.1f;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

void ACosmereMistCloud::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Player 0 on this machine is the only viewer whose picture exists here. A dedicated server has none,
	// in which case the cloud stays visible and nothing below runs.
	const ACosmereCharacter* Character = Cast<ACosmereCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	const UCosmereTinSensesComponent* Senses = Character ? Character->GetTinSensesComponent() : nullptr;

	const bool bBurning = Senses && Senses->IsTinActive();
	const bool bShouldBeVisible = !(bBurning && TinVisibility <= 0.f);

	if (bShouldBeVisible != bMistVisible)
	{
		ApplyMistVisibility(bShouldBeVisible);
	}
}

void ACosmereMistCloud::ApplyMistVisibility(bool bVisible)
{
	bMistVisible = bVisible;

	TArray<USceneComponent*> Visuals;
	GetComponents<USceneComponent>(Visuals);

	for (USceneComponent* Visual : Visuals)
	{
		if (!Visual || Visual == SceneRoot)
		{
			continue;
		}

		// Visibility is a client-side flag and is not replicated, so setting it here changes only this
		// machine's picture - and a later replication pass will not undo it.
		Visual->SetVisibility(bVisible, true);
	}
}
