// Copyright Epic Games, Inc. All Rights Reserved.

#include "Power/CosmereBurnAbility.h"

#include "Engine/World.h"
#include "Power/CosmerePowerResourceComponent.h"
#include "TimerManager.h"
#include "CosmereRivals.h"

UCosmereBurnAbility::UCosmereBurnAbility()
{
	// A burn is held alight, not spent in a blow: it stays up until the input layer takes it down again, so the
	// default one-shot end after activation is exactly wrong for it.
	bEndAfterActivation = false;
}

void UCosmereBurnAbility::OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::OnPowerActivated(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	SecondsSinceFlare = 0.f;

	// A metalmind tap opens at whatever level the tap was paid for; a plain burn opens at the metal's base.
	const int32 OpeningLevel = bBurnLevelFromMetalmind ? FMath::Max(1, GetMetalmindLevel()) : 1;
	SetBurnLevel(OpeningLevel);

	UWorld* World = GetPowerWorld(ActorInfo);
	if (!World)
	{
		return;
	}

	// One second of clock: the surcharge is a per-second rate and the flare step counts these seconds.
	World->GetTimerManager().SetTimer(BurnLadderTimer, this, &UCosmereBurnAbility::HandleBurnLadder, 1.f, true);

	UE_LOG(LogCosmere, Log, TEXT("CosmereBurnAbility: '%s' burning at level %d."),
		*GetPowerId().ToString(), OpeningLevel);
}

void UCosmereBurnAbility::OnPowerEnded(bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BurnLadderTimer);
	}

	// The end notification is the only one a subclass is guaranteed, so everything the burn turned on is turned
	// off through the same call that turned it on.
	SetBurnLevel(0);

	UE_LOG(LogCosmere, Log, TEXT("CosmereBurnAbility: '%s' stopped burning."), *GetPowerId().ToString());

	Super::OnPowerEnded(bWasCancelled);
}

void UCosmereBurnAbility::HandleBurnLadder()
{
	if (BurnLevel <= 0)
	{
		// Something other than this ability put the burn out - the pawn went away, or a later burn took the
		// slot. There is nothing left to pay for.
		EndPower();
		return;
	}

	if (BurnLevel > 1 && FlareSurchargePerSecond > 0.f)
	{
		const float Surcharge = FlareSurchargePerSecond * static_cast<float>(BurnLevel - 1);
		if (!SpendPowerCurrency(CurrentActorInfo, Surcharge))
		{
			// Flaring is worth what it costs: a burner who cannot pay for the extra level drops back to the one
			// the row is paying for, rather than the whole power cutting out mid-fight.
			SetBurnLevel(1);
			SecondsSinceFlare = 0.f;
			return;
		}
	}

	if (bBurnLevelFromMetalmind)
	{
		return;
	}

	SecondsSinceFlare += 1.f;
	if (SecondsSinceFlare < FlareSeconds)
	{
		return;
	}

	SecondsSinceFlare = 0.f;
	if (BurnLevel < MaxBurnLevel)
	{
		SetBurnLevel(BurnLevel + 1);
	}
}

void UCosmereBurnAbility::SetBurnLevel(int32 NewLevel)
{
	const int32 ClampedLevel = FMath::Clamp(NewLevel, 0, MaxBurnLevel);
	if (ClampedLevel == BurnLevel)
	{
		return;
	}

	BurnLevel = ClampedLevel;
	ApplyBurnLevel(BurnLevel);
}

void UCosmereBurnAbility::ApplyBurnLevel(int32 NewLevel)
{
	// Nothing by default. A burn whose level means nothing is a burn with no effect, which is a data error
	// rather than something to paper over here.
}

float UCosmereBurnAbility::GetScaledRadius(float BaseRadius) const
{
	if (BurnLevel <= 0)
	{
		return 0.f;
	}

	return BaseRadius * FMath::Pow(2.f, static_cast<float>(BurnLevel - 1));
}
