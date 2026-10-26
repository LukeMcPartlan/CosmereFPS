// Copyright Epic Games, Inc. All Rights Reserved.

#include "Power/CosmereTinSenseAbility.h"

#include "Character/CosmereCharacter.h"
#include "Character/CosmereTinSensesComponent.h"
#include "Engine/World.h"
#include "Power/CosmerePowerResourceComponent.h"
#include "TimerManager.h"
#include "CosmereRivals.h"

UCosmereTinSenseAbility::UCosmereTinSenseAbility()
{
	// Tin is a burn, not a blow: it stays up until the input layer takes it down again, so the default
	// one-shot end after activation is exactly wrong for it.
	bEndAfterActivation = false;
}

void UCosmereTinSenseAbility::OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::OnPowerActivated(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	SecondsSinceFlare = 0.f;

	// A metalmind tap opens at whatever level the tap was paid for; a burn opens at the metal's base.
	const int32 OpeningLevel = bSenseLevelFromMetalmind ? FMath::Max(1, GetMetalmindLevel()) : 1;
	ApplyTinLevel(OpeningLevel);

	UWorld* World = GetPowerWorld(ActorInfo);
	if (!World)
	{
		return;
	}

	// One second of clock: the surcharge is a per-second rate and the flare step counts these seconds.
	World->GetTimerManager().SetTimer(TinLadderTimer, this, &UCosmereTinSenseAbility::HandleTinLadder, 1.f, true);

	UE_LOG(LogCosmere, Log, TEXT("CosmereTinSenseAbility: '%s' burning at level %d."),
		*GetPowerId().ToString(), OpeningLevel);
}

void UCosmereTinSenseAbility::OnPowerEnded(bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TinLadderTimer);
	}

	ApplyTinLevel(0);

	UE_LOG(LogCosmere, Log, TEXT("CosmereTinSenseAbility: '%s' stopped burning."), *GetPowerId().ToString());

	Super::OnPowerEnded(bWasCancelled);
}

void UCosmereTinSenseAbility::HandleTinLadder()
{
	const int32 Level = GetOwnerTinLevel();
	if (Level <= 0)
	{
		// The senses were cleared by something other than this ability - the pawn went away, or a later
		// burn took the slot. There is nothing left to pay for.
		EndPower();
		return;
	}

	if (Level > 1 && FlareSurchargePerSecond > 0.f)
	{
		const float Surcharge = FlareSurchargePerSecond * static_cast<float>(Level - 1);
		if (!SpendPowerCurrency(CurrentActorInfo, Surcharge))
		{
			// Flaring is worth what it costs: a burner who cannot pay for the extra level drops back to
			// the one the row is paying for, rather than the whole sense cutting out mid-fight.
			ApplyTinLevel(1);
			SecondsSinceFlare = 0.f;
			return;
		}
	}

	if (bSenseLevelFromMetalmind)
	{
		return;
	}

	SecondsSinceFlare += 1.f;
	if (SecondsSinceFlare < FlareSeconds)
	{
		return;
	}

	SecondsSinceFlare = 0.f;
	if (Level < MaxTinLevel)
	{
		ApplyTinLevel(Level + 1);
	}
}

void UCosmereTinSenseAbility::ApplyTinLevel(int32 Level) const
{
	if (UCosmereTinSensesComponent* Senses = GetOwnerTinSenses())
	{
		Senses->SetTinLevel(Level);
	}
}

int32 UCosmereTinSenseAbility::GetOwnerTinLevel() const
{
	const UCosmereTinSensesComponent* Senses = GetOwnerTinSenses();
	return Senses ? Senses->GetTinLevel() : 0;
}

UCosmereTinSensesComponent* UCosmereTinSenseAbility::GetOwnerTinSenses() const
{
	const ACosmereCharacter* Character = GetOwnerCosmereCharacter();
	return Character ? Character->GetTinSensesComponent() : nullptr;
}
