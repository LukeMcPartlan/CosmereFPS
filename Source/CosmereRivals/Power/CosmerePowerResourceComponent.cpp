// Copyright Epic Games, Inc. All Rights Reserved.

#include "Power/CosmerePowerResourceComponent.h"
#include "Net/UnrealNetwork.h"

UCosmerePowerResourceComponent::UCosmerePowerResourceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UCosmerePowerResourceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCosmerePowerResourceComponent, MetalPools);
}

int32 UCosmerePowerResourceComponent::FindPoolIndex(FName MetalId) const
{
	for (int32 PoolIndex = 0; PoolIndex < MetalPools.Num(); ++PoolIndex)
	{
		if (MetalPools[PoolIndex].MetalId == MetalId)
		{
			return PoolIndex;
		}
	}

	return INDEX_NONE;
}

float UCosmerePowerResourceComponent::GetMetalResource(FName MetalId) const
{
	// A pool that is absent has never been spent, so it reads full rather than zero. That is what makes
	// "every metal starts at 100" true without seeding a list this component would then have to maintain.
	// A pool with no id is not a pool: answering it with a full bar would hand out a reserve no row owns.
	if (MetalId.IsNone())
	{
		return 0.f;
	}

	const int32 PoolIndex = FindPoolIndex(MetalId);
	return (PoolIndex == INDEX_NONE) ? MaxResource : MetalPools[PoolIndex].Current;
}

float UCosmerePowerResourceComponent::GetMetalFraction(FName MetalId) const
{
	return (MaxResource > 0.f) ? FMath::Clamp(GetMetalResource(MetalId) / MaxResource, 0.f, 1.f) : 0.f;
}

bool UCosmerePowerResourceComponent::CanAfford(FName MetalId, float Cost) const
{
	return Cost <= 0.f || GetMetalResource(MetalId) >= Cost;
}

bool UCosmerePowerResourceComponent::TrySpend(FName MetalId, float Cost)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || MetalId.IsNone() || !CanAfford(MetalId, Cost))
	{
		return false;
	}

	if (Cost <= 0.f)
	{
		return true;
	}

	const float Remaining = FMath::Max(0.f, GetMetalResource(MetalId) - Cost);
	const int32 PoolIndex = FindPoolIndex(MetalId);
	if (PoolIndex == INDEX_NONE)
	{
		FCosmereMetalPool Pool;
		Pool.MetalId = MetalId;
		Pool.Current = Remaining;
		Pool.RegenHoldRemaining = RegenDelayAfterSpend;
		MetalPools.Add(Pool);
	}
	else
	{
		MetalPools[PoolIndex].Current = Remaining;
		MetalPools[PoolIndex].RegenHoldRemaining = RegenDelayAfterSpend;
	}

	OnResourceChanged.Broadcast(MetalId, Remaining, MaxResource);
	return true;
}

void UCosmerePowerResourceComponent::Refill(FName MetalId, float Amount)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || MetalId.IsNone() || Amount <= 0.f)
	{
		return;
	}

	const int32 PoolIndex = FindPoolIndex(MetalId);
	if (PoolIndex == INDEX_NONE)
	{
		// Already full, which is the same state as never touched: nothing to add, nothing to announce.
		return;
	}

	const float Updated = FMath::Min(MaxResource, MetalPools[PoolIndex].Current + Amount);
	MetalPools[PoolIndex].Current = Updated;
	OnResourceChanged.Broadcast(MetalId, Updated, MaxResource);
}

void UCosmerePowerResourceComponent::RefillAll(float Amount)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Amount <= 0.f)
	{
		return;
	}

	for (FCosmereMetalPool& Pool : MetalPools)
	{
		if (Pool.Current >= MaxResource)
		{
			continue;
		}

		Pool.Current = FMath::Min(MaxResource, Pool.Current + Amount);
		OnResourceChanged.Broadcast(Pool.MetalId, Pool.Current, MaxResource);
	}
}

void UCosmerePowerResourceComponent::DrainAll(float Amount)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Amount <= 0.f)
	{
		return;
	}

	for (FCosmereMetalPool& Pool : MetalPools)
	{
		if (Pool.Current <= 0.f)
		{
			continue;
		}

		// A leech takes metal without arming the regen hold: it is someone else's work, not a burn, so the
		// victim's own recovery should not be delayed by it.
		Pool.Current = FMath::Max(0.f, Pool.Current - Amount);
		OnResourceChanged.Broadcast(Pool.MetalId, Pool.Current, MaxResource);
	}
}

void UCosmerePowerResourceComponent::RefillToMax(FName MetalId)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || MetalId.IsNone())
	{
		return;
	}

	const int32 PoolIndex = FindPoolIndex(MetalId);
	if (PoolIndex == INDEX_NONE)
	{
		// Already full. Nothing to write, nothing to announce.
		return;
	}

	MetalPools[PoolIndex].Current = MaxResource;
	MetalPools[PoolIndex].RegenHoldRemaining = 0.f;
	OnResourceChanged.Broadcast(MetalId, MaxResource, MaxResource);
}

void UCosmerePowerResourceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	if (MaxResource <= 0.f || RegenPerSecond <= 0.f || MetalPools.Num() == 0)
	{
		return;
	}

	// Per pool rather than one global timer: each metal holds its own regen off after its own spend, so a Steel
	// press cannot delay Bendalloy coming back and vice versa.
	for (FCosmereMetalPool& Pool : MetalPools)
	{
		if (Pool.Current >= MaxResource)
		{
			continue;
		}

		if (Pool.RegenHoldRemaining > 0.f)
		{
			Pool.RegenHoldRemaining = FMath::Max(0.f, Pool.RegenHoldRemaining - DeltaTime);
			continue;
		}

		const float Previous = Pool.Current;
		Pool.Current = FMath::Min(MaxResource, Pool.Current + RegenPerSecond * DeltaTime);

		if (!FMath::IsNearlyEqual(Previous, Pool.Current))
		{
			OnResourceChanged.Broadcast(Pool.MetalId, Pool.Current, MaxResource);
		}
	}
}

void UCosmerePowerResourceComponent::OnRep_MetalPools()
{
	// Replication carries the whole array, not a per-entry delta, so every opened pool is re-announced. A HUD
	// reading one metal ignores the rest, and one broadcast per spent metal is cheap next to a polling bar.
	for (const FCosmereMetalPool& Pool : MetalPools)
	{
		OnResourceChanged.Broadcast(Pool.MetalId, Pool.Current, MaxResource);
	}
}
