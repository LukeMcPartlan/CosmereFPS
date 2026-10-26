// Copyright Epic Games, Inc. All Rights Reserved.

#include "Power/CosmereSurgeResourceComponent.h"

#include "Net/UnrealNetwork.h"

const FName UCosmereSurgeResourceComponent::StormlightKey(TEXT("Stormlight"));

UCosmereSurgeResourceComponent::UCosmereSurgeResourceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UCosmereSurgeResourceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCosmereSurgeResourceComponent, Pools);
}

int32 UCosmereSurgeResourceComponent::FindPoolIndex(FName CurrencyId) const
{
	for (int32 PoolIndex = 0; PoolIndex < Pools.Num(); ++PoolIndex)
	{
		if (Pools[PoolIndex].CurrencyId == CurrencyId)
		{
			return PoolIndex;
		}
	}

	return INDEX_NONE;
}

float UCosmereSurgeResourceComponent::GetStormlight() const
{
	// A reserve that is absent has never been spent, so it reads full rather than zero. That is what makes
	// "stormlight starts at 100" true without seeding a list this component would then have to maintain.
	const int32 PoolIndex = FindPoolIndex(StormlightKey);
	return (PoolIndex == INDEX_NONE) ? MaxStormlight : Pools[PoolIndex].Current;
}

float UCosmereSurgeResourceComponent::GetStormlightFraction() const
{
	return (MaxStormlight > 0.f) ? FMath::Clamp(GetStormlight() / MaxStormlight, 0.f, 1.f) : 0.f;
}

bool UCosmereSurgeResourceComponent::CanAffordStormlight(float Cost) const
{
	return Cost <= 0.f || GetStormlight() >= Cost;
}

bool UCosmereSurgeResourceComponent::TrySpendStormlight(float Cost)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || !CanAffordStormlight(Cost))
	{
		return false;
	}

	if (Cost <= 0.f)
	{
		return true;
	}

	const float Remaining = FMath::Max(0.f, GetStormlight() - Cost);
	const int32 PoolIndex = FindPoolIndex(StormlightKey);
	if (PoolIndex == INDEX_NONE)
	{
		FCosmereSurgePool Pool;
		Pool.CurrencyId = StormlightKey;
		Pool.Current = Remaining;
		Pools.Add(Pool);
	}
	else
	{
		Pools[PoolIndex].Current = Remaining;
	}

	OnSurgeChanged.Broadcast(Remaining, MaxStormlight);
	return true;
}

void UCosmereSurgeResourceComponent::AddStormlight(float Amount)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Amount <= 0.f)
	{
		return;
	}

	const int32 PoolIndex = FindPoolIndex(StormlightKey);
	if (PoolIndex == INDEX_NONE)
	{
		// Already full, which is the same state as never touched: nothing to add, nothing to announce.
		return;
	}

	const float Updated = FMath::Min(MaxStormlight, Pools[PoolIndex].Current + Amount);
	Pools[PoolIndex].Current = Updated;
	OnSurgeChanged.Broadcast(Updated, MaxStormlight);
}

void UCosmereSurgeResourceComponent::RefillStormlight()
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	const int32 PoolIndex = FindPoolIndex(StormlightKey);
	if (PoolIndex == INDEX_NONE)
	{
		// Already full.
		return;
	}

	Pools.RemoveAt(PoolIndex);
	OnSurgeChanged.Broadcast(MaxStormlight, MaxStormlight);
}

void UCosmereSurgeResourceComponent::OnRep_Stormlight()
{
	// Replication carries the whole array, not a per-entry delta, so the reserve is re-announced on every change.
	// One broadcast per change is cheap next to a polling bar, and the HUD only ever reads the one number.
	OnSurgeChanged.Broadcast(GetStormlight(), MaxStormlight);
}
