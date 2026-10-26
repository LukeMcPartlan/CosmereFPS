// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ability/CosmereAttributeSet.h"

#include "CosmereRivals.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UCosmereAttributeSet::UCosmereAttributeSet()
{
	// Baseline before any race row is read. ApplyBuildToPawn overwrites both of these from
	// DT_Races, so these values only ever survive on a pawn that never received a build - which is
	// exactly the case that should be visible rather than silently zero.
	InitHealth(100.f);
	InitMaxHealth(100.f);
}

void UCosmereAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UCosmereAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCosmereAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
}

void UCosmereAttributeSet::OnRep_Health(const FGameplayAttributeData& OldHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCosmereAttributeSet, Health, OldHealth);
}

void UCosmereAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCosmereAttributeSet, MaxHealth, OldMaxHealth);
}

void UCosmereAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	// Clamp only. The engine contract for this hook is "enforce Health = Clamp(Health, 0,
	// MaxHealth)" and explicitly not "react to being damaged" - death and hit feedback belong in
	// PostGameplayEffectExecute, which chunk 3 adds.
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.f);
	}
}
