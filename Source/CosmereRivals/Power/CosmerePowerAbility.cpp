// Copyright Epic Games, Inc. All Rights Reserved.

#include "Power/CosmerePowerAbility.h"

#include "CosmereRivals.h"
#include "Character/CosmereCharacter.h"
#include "Data/CosmereCatalogSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Net/CosmerePlayerState.h"
#include "Power/CosmerePowerResourceComponent.h"
#include "Power/CosmereSurgeResourceComponent.h"
#include "TimerManager.h"

UCosmerePowerAbility::UCosmerePowerAbility()
{
	// InstancedPerActor is what GetCurrentActivationInfo / EndPower rely on, and NonInstanced is deprecated
	// as of 5.5. One instance per actor also gives the toggle its natural behaviour: activation is refused
	// while the power is already running, so a second press has to go through EndPower to switch it off.
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// Predicted so the power feels immediate. The visible effect runs on both machines; the pool is spent on
	// the authority only, so a prediction can never put the client's bar out of step with the server's.
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

UCosmerePowerResourceComponent* UCosmerePowerAbility::GetPowerResource(const FGameplayAbilityActorInfo* ActorInfo) const
{
	if (!ActorInfo || !ActorInfo->OwnerActor.IsValid())
	{
		return nullptr;
	}

	// The pool lives on the PlayerState, alongside the ASC, so it survives a respawn.
	if (const ACosmerePlayerState* PlayerState = Cast<ACosmerePlayerState>(ActorInfo->OwnerActor.Get()))
	{
		return PlayerState->GetPowerResourceComponent();
	}

	return nullptr;
}

bool UCosmerePowerAbility::CanAffordPowerCurrency(const FGameplayAbilityActorInfo* ActorInfo, float Cost) const
{
	if (Cost <= 0.f)
	{
		return true;
	}

	if (!ActorInfo || !ActorInfo->OwnerActor.IsValid())
	{
		return false;
	}

	const ACosmerePlayerState* PlayerState = Cast<ACosmerePlayerState>(ActorInfo->OwnerActor.Get());
	if (!PlayerState)
	{
		return false;
	}

	// A Surge draws stormlight; everything else draws its own metal's pool. The two are read the same way -
	// from replicated state, on both machines - so a client predicts the same refusal the server would give.
	if (SpendsStormlight())
	{
		const UCosmereSurgeResourceComponent* Stormlight = PlayerState->GetSurgeResourceComponent();
		return Stormlight && Stormlight->CanAffordStormlight(Cost);
	}

	const UCosmerePowerResourceComponent* Pool = PlayerState->GetPowerResourceComponent();
	return Pool && Pool->CanAfford(GetMetalPoolId(), Cost);
}

bool UCosmerePowerAbility::SpendPowerCurrency(const FGameplayAbilityActorInfo* ActorInfo, float Cost) const
{
	if (!ActorInfo || !ActorInfo->IsNetAuthority())
	{
		return false;
	}

	if (SpendsStormlight())
	{
		if (const ACosmerePlayerState* PlayerState = Cast<ACosmerePlayerState>(ActorInfo->OwnerActor.Get()))
		{
			if (UCosmereSurgeResourceComponent* Stormlight = PlayerState->GetSurgeResourceComponent())
			{
				return Stormlight->TrySpendStormlight(Cost);
			}
		}

		return false;
	}

	if (UCosmerePowerResourceComponent* Pool = GetPowerResource(ActorInfo))
	{
		return Pool->TrySpend(GetMetalPoolId(), Cost);
	}

	return false;
}

float UCosmerePowerAbility::GetPowerCurrencyAvailable(const FGameplayAbilityActorInfo* ActorInfo) const
{
	if (!ActorInfo || !ActorInfo->OwnerActor.IsValid())
	{
		return 0.f;
	}

	const ACosmerePlayerState* PlayerState = Cast<ACosmerePlayerState>(ActorInfo->OwnerActor.Get());
	if (!PlayerState)
	{
		return 0.f;
	}

	if (SpendsStormlight())
	{
		const UCosmereSurgeResourceComponent* Stormlight = PlayerState->GetSurgeResourceComponent();
		return Stormlight ? Stormlight->GetStormlight() : 0.f;
	}

	const UCosmerePowerResourceComponent* Pool = PlayerState->GetPowerResourceComponent();
	return Pool ? Pool->GetMetalResource(GetMetalPoolId()) : 0.f;
}

UWorld* UCosmerePowerAbility::GetPowerWorld(const FGameplayAbilityActorInfo* ActorInfo) const
{
	const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	return Avatar ? Avatar->GetWorld() : nullptr;
}

bool UCosmerePowerAbility::ResolveRow(const FGameplayAbilityActorInfo* ActorInfo) const
{
	if (bRowResolved)
	{
		return !CachedPowerId.IsNone();
	}

	// Set before the lookup so a failed resolve is not retried on every single cost check.
	bRowResolved = true;

	UCosmereCatalogSubsystem* Catalog = nullptr;
	if (const UWorld* World = GetPowerWorld(ActorInfo))
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			Catalog = GameInstance->GetSubsystem<UCosmereCatalogSubsystem>();
		}
	}

	if (!Catalog || !Catalog->IsCatalogReady())
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmerePowerAbility: %s could not reach the power catalog; the power will not run."),
			*GetClass()->GetName());
		return false;
	}

	const UClass* MyClass = GetClass();
	int32 MatchCount = 0;

	for (const FName& PowerId : Catalog->GetAllPowerIds())
	{
		FPowerDefinition Row;
		if (!Catalog->GetPowerRow(PowerId, Row) || Row.AbilityClass != MyClass)
		{
			continue;
		}

		++MatchCount;
		if (MatchCount == 1)
		{
			CachedPowerId = PowerId;
			CachedResourceCost = Row.ResourceCost;
			CachedResourceCostInterval = Row.ResourceCostInterval;
			CachedCooldown = Row.Cooldown;
			CachedToggleLock = Row.ToggleLockSeconds;
			CachedToggle = Row.bToggle;
			CachedSource = Row.Source;
		}
	}

	if (MatchCount == 0)
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmerePowerAbility: no DT_Powers row names %s, so it cannot resolve a cost or a lifetime. ")
			TEXT("Set AbilityClass on the row that should activate it."),
			*MyClass->GetName());
		return false;
	}

	if (MatchCount > 1)
	{
		// Not fatal, but a shared class means the row-level cost is ambiguous, so say so rather than let the
		// first match win silently.
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmerePowerAbility: %d DT_Powers rows name %s; using '%s' for cost and lifetime."),
			MatchCount, *MyClass->GetName(), *CachedPowerId.ToString());
	}

	return true;
}

bool UCosmerePowerAbility::CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags))
	{
		return false;
	}

	// An unresolvable row means no cost and no lifetime, so refuse rather than run a power nothing describes.
	if (!ResolveRow(ActorInfo))
	{
		return false;
	}

	if (CachedResourceCost <= 0.f)
	{
		return true;
	}

	// A Surge is priced in stormlight and a metal in its own pool, and the two are read through the same shape
	// so there is only ever one place that decides which currency a row spends.
	if (!CanAffordPowerCurrency(ActorInfo, CachedResourceCost))
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmerePowerAbility: '%s' costs %.1f %s and the owner cannot cover it; refusing."),
			*CachedPowerId.ToString(), CachedResourceCost,
			SpendsStormlight() ? TEXT("stormlight") : TEXT("of its own metal"));
		return false;
	}

	// CanAffordPowerCurrency reads the replicated value on a client. A client whose copy is a frame stale can
	// have a prediction refused; the server is the authority that ultimately charges, so that is a lost
	// prediction rather than a desync.
	return true;
}

void UCosmerePowerAbility::ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	Super::ApplyCost(Handle, ActorInfo, ActivationInfo);

	if (CachedResourceCost <= 0.f)
	{
		return;
	}

	// Predicted activations reach here on the owning client too, where the charge is deliberately a no-op.
	// Only the authority charges, and only the authority can report a lost race.
	if (!ActorInfo || !ActorInfo->IsNetAuthority())
	{
		return;
	}

	if (!SpendPowerCurrency(ActorInfo, CachedResourceCost))
	{
		// CanAfford passed in CheckCost and the reserve was drained before this point - two powers committed in
		// the same frame. The power still runs; the bar is simply empty, and the interval charge will end it
		// if the row has one.
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmerePowerAbility: '%s' committed with only %.1f of the %.1f it costs."),
			*CachedPowerId.ToString(), GetPowerCurrencyAvailable(ActorInfo), CachedResourceCost);
	}
}

bool UCosmerePowerAbility::CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CheckCooldown(Handle, ActorInfo, OptionalRelevantTags))
	{
		return false;
	}

	// A row that has not resolved yet has never activated, so neither gate below can have been earned.
	if (!ResolveRow(ActorInfo))
	{
		return true;
	}

	// A Feruchemical row is gated by its own metalmind, never by the row clock. Storing has to stay available
	// at all times, and spending metal is gated only by the reserve it has to pay - so a metalmind carries no
	// cooldown of any kind, internal or external.
	if (IsMetalmind())
	{
		return true;
	}

	const UWorld* World = GetPowerWorld(ActorInfo);
	const float Elapsed = World ? (World->GetTimeSeconds() - static_cast<float>(LastActivatedTime)) : 0.f;

	// Both gates are measured from the last activation. Zeroing Cooldown in DT_Powers is therefore the
	// documented way to make the resource the only thing gating a power.
	if (Elapsed < CachedCooldown)
	{
		return false;
	}

	// The flicker guard: a latching power cannot be switched faster than the row allows.
	if (Elapsed < CachedToggleLock)
	{
		return false;
	}

	return true;
}

float UCosmerePowerAbility::GetCooldownRemainingSeconds() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const UWorld* World = GetPowerWorld(ActorInfo);
	if (!World || !ResolveRow(ActorInfo))
	{
		return 0.f;
	}

	// The same subtraction CheckCooldown makes, kept in seconds rather than reduced to a verdict.
	const double Elapsed = static_cast<double>(World->GetTimeSeconds()) - LastActivatedTime;
	return FMath::Max(0.f, CachedCooldown - static_cast<float>(Elapsed));
}

void UCosmerePowerAbility::SetCooldownRemaining(float RemainingSeconds)
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const UWorld* World = GetPowerWorld(ActorInfo);
	if (!World || !ResolveRow(ActorInfo))
	{
		return;
	}

	// CheckCooldown asks "has CachedCooldown elapsed since LastActivatedTime". Solving that for a wanted
	// remainder is the whole operation, and it holds for a row whose cooldown is shorter than the remainder too:
	// the stamp lands ahead of now and the gate stays shut for exactly RemainingSeconds.
	LastActivatedTime =
		static_cast<double>(World->GetTimeSeconds()) - (static_cast<double>(CachedCooldown) - RemainingSeconds);
}

void UCosmerePowerAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	// The pawn queues what a press means before it activates, because it cannot reach the ability until
	// TryActivateAbility returns. Taking the mode here - and clearing it whatever this call decides - is what
	// keeps a refused activation from leaving a mode queued for the next one.
	const EMetalmindMode QueuedMode = PendingMetalmindMode;
	const int32 QueuedSlotIndex = PendingMetalmindSlotIndex;
	const int32 QueuedLevel = PendingMetalmindLevel;
	PendingMetalmindMode = EMetalmindMode::Idle;
	PendingMetalmindSlotIndex = INDEX_NONE;
	PendingMetalmindLevel = 0;

	// Resolve before committing so ApplyCost has a cost to charge. CheckCost normally did this already.
	if (!ResolveRow(ActorInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// CommitAbility is the last chance to fail, and ActivateAbility is required to call it. It re-runs
	// CheckCost / CheckCooldown and then commits the cost through ApplyCost above.
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		UE_LOG(LogCosmere, Log,
			TEXT("CosmerePowerAbility: '%s' could not commit (unaffordable or on its own gate)."),
			*CachedPowerId.ToString());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (const UWorld* World = GetPowerWorld(ActorInfo))
	{
		LastActivatedTime = World->GetTimeSeconds();
	}

	// A metalmind is a mode, not an event. The pawn has already decided what the press means, so the
	// archetype's own one-shot effect must not run on top of it - Storing in particular has to be silent,
	// because it is the act of filling the metal, not of spending it - and the ability has to stay open for
	// the loop BeginMetalmind starts once this returns.
	const bool bMetalmindPress = IsMetalmind() && QueuedMode != EMetalmindMode::Idle;
	if (bMetalmindPress)
	{
		MetalmindMode = QueuedMode;
		MetalmindSlotIndex = QueuedSlotIndex;
		MetalmindLevel = FMath::Max(1, QueuedLevel);
	}
	else
	{
		OnPowerActivated(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	}

	// The ongoing charge is authority-only, so the timer that drives it must not be started on a predicting
	// client - a client-side timer would see TrySpend no-op and end the power for itself.
	const bool bAuthority = ActorInfo && ActorInfo->IsNetAuthority();
	if (bAuthority && CachedResourceCostInterval > 0.f && CachedResourceCost > 0.f)
	{
		if (UWorld* World = GetPowerWorld(ActorInfo))
		{
			World->GetTimerManager().SetTimer(
				ResourceIntervalTimer, this, &UCosmerePowerAbility::HandleResourceInterval,
				CachedResourceCostInterval, /*bLoop=*/true);
		}
	}

	if (bEndAfterActivation && !bMetalmindPress)
	{
		// bWasCancelled is false: the power did its job, it did not run out of resource.
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
	}
}

void UCosmerePowerAbility::HandleResourceInterval()
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	if (ActorInfo && SpendPowerCurrency(ActorInfo, CachedResourceCost))
	{
		return;
	}

	// Out of resource. Cancelled, so a subclass can tell a dry end from a natural one and clean up either way.
	UE_LOG(LogCosmere, Log,
		TEXT("CosmerePowerAbility: '%s' ran dry - it costs %.1f every %.2fs and %s is at %.1f."),
		*CachedPowerId.ToString(), CachedResourceCost, CachedResourceCostInterval,
		SpendsStormlight() ? TEXT("stormlight") : TEXT("the metal"),
		GetPowerCurrencyAvailable(ActorInfo));

	EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true, true);
}

void UCosmerePowerAbility::EndPower()
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	if (!ActorInfo || !IsActive())
	{
		return;
	}

	EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true, false);
}

void UCosmerePowerAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetPowerWorld(ActorInfo))
	{
		World->GetTimerManager().ClearTimer(ResourceIntervalTimer);
		World->GetTimerManager().ClearTimer(MetalmindTimer);
	}

	// A running metalmind puts its stats back before the subclass sees the end, so a buff that is cancelled
	// still leaves the pawn at its build numbers. Nothing is measured from here: a metalmind has no post-buff
	// cooldown, so the reserve is the only thing that decides when the next tap may open.
	if (MetalmindMode != EMetalmindMode::Idle)
	{
		MetalmindMode = EMetalmindMode::Idle;
		MetalmindLevel = 0;
		ClearMetalmindStats();
	}

	// Subclass cleanup runs before Super::EndAbility so it still has a live ActorInfo and a valid ability.
	OnPowerEnded(bWasCancelled);

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UCosmerePowerAbility::OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	// Intentionally empty: the base has no effect of its own. A row that names the base class directly would
	// be a data error, which UCLASS(Abstract) already prevents.
}

void UCosmerePowerAbility::OnPowerEnded(bool bWasCancelled)
{
	// Intentionally empty: subclasses undo whatever OnPowerActivated did.
}

//~ Begin metalmind

ACosmereCharacter* UCosmerePowerAbility::GetOwnerCosmereCharacter() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	return ActorInfo ? Cast<ACosmereCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
}

float UCosmerePowerAbility::GetMetalmindFraction() const
{
	const ACosmereCharacter* Cosmere = GetOwnerCosmereCharacter();
	if (!Cosmere || MetalmindSlotIndex == INDEX_NONE)
	{
		return 0.f;
	}

	const EPowerSlot Slot = (MetalmindSlotIndex == 1) ? EPowerSlot::Beta : EPowerSlot::Alpha;
	return Cosmere->GetMetalmindReserveFraction(Slot);
}

void UCosmerePowerAbility::QueueMetalmindMode(EMetalmindMode Mode, int32 SlotIndex, int32 Level)
{
	// Idle is the clear: a press the ASC refuses never reaches ActivateAbility, so the pawn drops the queue
	// itself rather than leaving a mode waiting for an activation that is not coming.
	PendingMetalmindMode = Mode;
	PendingMetalmindSlotIndex = (Mode == EMetalmindMode::Idle) ? INDEX_NONE : SlotIndex;
	PendingMetalmindLevel = (Mode == EMetalmindMode::Idle) ? 0 : FMath::Max(1, Level);
}

void UCosmerePowerAbility::BeginMetalmind(EMetalmindMode Mode, int32 SlotIndex, int32 Level)
{
	if (!IsMetalmind() || Mode == EMetalmindMode::Idle)
	{
		return;
	}

	MetalmindMode = Mode;
	MetalmindSlotIndex = SlotIndex;
	MetalmindLevel = FMath::Max(1, Level);

	if (UWorld* World = GetPowerWorld(GetCurrentActorInfo()))
	{
		World->GetTimerManager().SetTimer(MetalmindTimer, this, &UCosmerePowerAbility::HandleMetalmindTick,
			MetalmindTickInterval, /*bLoop=*/true);
	}

	// Applied immediately rather than on the first tick, so a tap reads as a tap this frame.
	ApplyMetalmindTick();

	UE_LOG(LogCosmere, Log, TEXT("CosmereMetalmind: '%s' entered mode %d at level %d in slot %d (reserve %.0f%%, drain %.0f/s)."),
		*GetPowerId().ToString(), static_cast<int32>(MetalmindMode), MetalmindLevel, MetalmindSlotIndex,
		GetMetalmindFraction() * 100.f, GetMetalmindDrainRateForLevel(MetalmindLevel));
}

void UCosmerePowerAbility::HandleMetalmindTick()
{
	ACosmereCharacter* Cosmere = GetOwnerCosmereCharacter();
	if (!Cosmere || MetalmindSlotIndex == INDEX_NONE)
	{
		EndPower();
		return;
	}

	const EPowerSlot Slot = (MetalmindSlotIndex == 1) ? EPowerSlot::Beta : EPowerSlot::Alpha;

	if (MetalmindMode == EMetalmindMode::Storing)
	{
		// Storing is what earns the metal, and it never asks whether the buff is on cooldown.
		// The multiplier is per metal: filling one metalmind is not the same job as filling another, and Iron's
		// store is the one that has to be worth the seconds it costs to stand still.
		Cosmere->AddMetalmindReserve(
			Slot, MetalmindStoreRate * MetalmindStoreRateMultiplier * MetalmindTickInterval);
	}
	else if (MetalmindMode == EMetalmindMode::Tapping || MetalmindMode == EMetalmindMode::Flaring)
	{
		// The level sets the rate, so a doubled tap costs double and the tank empties in half the time.
		const float Rate = GetMetalmindDrainRateForLevel(MetalmindLevel);
		const float Spent = -Cosmere->AddMetalmindReserve(Slot, -Rate * MetalmindTickInterval);

		// An empty metalmind ends the buff by itself. That is the whole contract of tapping, and it is what
		// makes the one-second gate on opening a level meaningful: the tank is the clock.
		if (Spent <= KINDA_SMALL_NUMBER)
		{
			EndPower();
			return;
		}
	}

	ApplyMetalmindTick();
}

void UCosmerePowerAbility::ApplyMetalmindTick()
{
	// Intentionally empty. An archetype with no stats to move still stores and spends metal through the base,
	// which is what makes the metalmind input model uniform across every Feruchemical row.
}

void UCosmerePowerAbility::ClearMetalmindStats()
{
	// Intentionally empty: subclasses undo whatever ApplyMetalmindTick changed.
}

//~ End metalmind
