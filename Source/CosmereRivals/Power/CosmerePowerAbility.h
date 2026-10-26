// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Data/CosmereTypes.h"
#include "CosmerePowerAbility.generated.h"

class UCosmerePowerResourceComponent;
class ACosmereCharacter;

/**
 * What a Feruchemical metalmind is doing right now.
 *
 * Storing is the only mode that is free of the buff cooldown: it is the act of filling the metalmind, and a
 * player who has just spent one must still be able to earn it back.
 */
UENUM(BlueprintType)
enum class EMetalmindMode : uint8
{
	Idle		UMETA(DisplayName = "Idle"),
	Storing		UMETA(DisplayName = "Storing"),
	Tapping		UMETA(DisplayName = "Tapping"),
	Flaring		UMETA(DisplayName = "Flaring")
};

/**
 * Base class for every power row in DT_Powers.
 *
 * A power is a data row plus this class's behaviour. FPowerDefinition::AbilityClass names the concrete
 * subclass, and the subclass finds that same row again by matching AbilityClass against its own class - once,
 * on the first activation, then cached. Nothing has to be hand-wired per power, and two rows that happen to
 * share a class are reported in the log rather than silently picking one.
 *
 * Costing deliberately does not go through a GameplayEffect. UCosmereAttributeSet records the locked decision
 * that the match model is health-only and that power resources are not attributes, and a GameplayEffect can
 * only modify attributes - so the pool is charged here, in CheckCost / ApplyCost, which are the hooks
 * UGameplayAbility::CommitAbility already calls in the correct order (check, then commit).
 *
 * Lifetime is driven by two row fields:
 *   - bToggle false and ResourceCostInterval 0: one-shot. The subclass works in OnPowerActivated and the
 *     ability ends as soon as that returns, unless the subclass clears bEndAfterActivation to hold itself
 *     open as a channel that ends itself later.
 *   - bToggle true, or ResourceCostInterval greater than 0: the ability stays active and the authority
 *     re-charges ResourceCost every ResourceCostInterval seconds. It ends when the pool can no longer pay,
 *     or when EndPower is called (input release, or a second press on a latching power).
 *
 * Every charge is authority-side only. The client predicts the power's visible effect but never the bar,
 * which is what UCosmerePowerResourceComponent's own documentation asks for.
 */
UCLASS(Abstract)
class COSMERERIVALS_API UCosmerePowerAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UCosmerePowerAbility();

	//~ Begin UGameplayAbility interface
	virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;
	virtual bool CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	//~ End UGameplayAbility interface

	/** The DT_Powers row that named this ability. NAME_None until the first activation resolves it. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	FName GetPowerId() const { return CachedPowerId; }

	/**
	 * The Source key this power registers its movement modifiers under.
	 *
	 * The row plus the metalmind slot, not the ability instance: a metalmind that escalates, ends and starts again
	 * has to land on the same entry every time, and two slots may hold the same row.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	FName GetModifierSource() const
	{
		return FName(*FString::Printf(TEXT("%s|%d"), *CachedPowerId.ToString(), MetalmindSlotIndex));
	}

	/** True when the row latches this power on until told to end, rather than running only while held. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	bool IsTogglePower() const { return CachedToggle; }

	/**
	 * True when this ability's DT_Powers row is Feruchemical, so it runs the metalmind loop instead of the
	 * row clock. False until the row has resolved once, which happens on the first activation.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	bool IsMetalmind() const { return bRowResolved && CachedSource == EPowerSource::Feruchemical; }

	/** Which metalmind mode this ability is in. Idle whenever it is not running. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	EMetalmindMode GetMetalmindMode() const { return MetalmindMode; }

	/**
	 * How many times the tap has been doubled: 1 is the row's own buff, 2 is twice it, 3 is four times it and so
	 * on. 0 whenever this ability is not spending metal, which is also what makes it safe to read while idle.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	int32 GetMetalmindLevel() const { return MetalmindLevel; }

	/**
	 * Reserve units per second this ability spends at Level: level 1 costs MetalmindTapDrainRate and every level
	 * above it doubles. Cost and effect climb together, which is what makes a level self-limiting - a level is
	 * only worth opening if the tank can pay for it.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	float GetMetalmindDrainRateForLevel(int32 Level) const
	{
		return (Level < 1) ? 0.f : MetalmindTapDrainRate * FMath::Pow(2.f, static_cast<float>(Level - 1));
	}

	/** Seconds a press must be held before it counts as storing rather than as a tap. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	float GetMetalmindHoldThreshold() const { return MetalmindHoldThreshold; }

	/** Seconds two taps may be apart and still read as a double tap. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	float GetMetalmindDoubleTapWindow() const { return MetalmindDoubleTapWindow; }

	/**
	 * Reserve units per second this metalmind gains while its owner stands still, in any mode. 0 by default, so
	 * only a metal that names a rate fills on its own.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	float GetMetalmindPassiveStandRate() const { return MetalmindPassiveStandRate; }

	/**
	 * Starts the metalmind loop in Mode for SlotIndex. 0 is EPowerSlot::Alpha and 1 is Beta.
	 *
	 * Called by ACosmereCharacter once the press has been classified, which is why the ability never sees a
	 * raw press: hold, tap and double tap are input decisions, not power decisions.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Power")
	void BeginMetalmind(EMetalmindMode Mode, int32 SlotIndex, int32 Level = 1);

	/**
	 * Tells the next activation what the press means, before it happens.
	 *
	 * ActivateAbility has to know the mode before it decides whether to run the archetype's own effect and
	 * whether to end itself, which is earlier than the pawn can call BeginMetalmind - it cannot reach the
	 * ability until TryActivateAbility returns. Idle clears a queued press that never activated.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Power")
	void QueueMetalmindMode(EMetalmindMode Mode, int32 SlotIndex, int32 Level = 1);

	/**
	 * Ends the power now. The input layer calls this on release for a held power, and on the second press
	 * for a latching one - a latching power deliberately cannot re-activate while it is already running,
	 * so this is the only way off it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Power")
	void EndPower();

	/** The cooldown this power's DT_Powers row declares, in seconds. 0 until the row is resolved once. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	float GetRowCooldownSeconds() const { return CachedCooldown; }

	/**
	 * World seconds at this power's last activation, as this machine saw it.
	 *
	 * The HUD derives its cooldown bar from this rather than from a timer of its own, so the bar and the
	 * gate that refuses the next press read the same stamp - CheckCooldown measures from here too.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	double GetLastActivatedWorldTime() const { return LastActivatedTime; }

protected:
	/**
	 * Subclass behaviour, called once the cost has been committed and the row is resolved. This is where a
	 * power does its work - shift gravity, open a field, and so on.
	 */
	virtual void OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData);

	/** Subclass cleanup, called on every end path before the base clears its own state. */
	virtual void OnPowerEnded(bool bWasCancelled);

	/**
	 * A subclass that has to stay active after activation clears this in its constructor. While it is true
	 * the default ActivateAbility ends the ability as soon as OnPowerActivated returns, which is what makes
	 * a one-shot power one-shot without the subclass having to end itself.
	 */
	bool bEndAfterActivation = true;

	//~ Begin metalmind. Rates are in reserve units, and the reserve itself lives on the pawn so it survives
	//~ a power being switched off and on again.

	/** One metalmind slice: subclasses express their own stats here. Called on activation and every tick. */
	virtual void ApplyMetalmindTick();

	/** Puts the stats this ability changed back where they were. Called on every end path. */
	virtual void ClearMetalmindStats();

	/** 0..1 reserve in this ability's slot. 0 when the slot or the pawn is gone. */
	float GetMetalmindFraction() const;

	/** Reserve units gained per second while storing. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Metalmind", meta = (ClampMin = "0.1"))
	float MetalmindStoreRate = 12.f;

	/**
	 * Multiplier on MetalmindStoreRate. Storing is the slow half of a metalmind by default, so a metal whose store
	 * is the worthwhile half - Iron, where filling it is what makes the pawn light - sets this above 1.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Metalmind", meta = (ClampMin = "0.1"))
	float MetalmindStoreRateMultiplier = 1.f;

	/** Reserve units spent per second at level 1. Every level above it doubles this. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Metalmind", meta = (ClampMin = "0.1"))
	float MetalmindTapDrainRate = 10.f;

	/**
	 * Reserve units gained per second while the owner stands still, whatever mode the metalmind is in. This is
	 * the only source of metal that costs no input, so a metal that sets it fills itself while its owner camps.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Metalmind", meta = (ClampMin = "0.0"))
	float MetalmindPassiveStandRate = 0.f;

	/** How often the metalmind moves reserve and re-asserts stats. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Metalmind", meta = (ClampMin = "0.02"))
	float MetalmindTickInterval = 0.1f;

	/** Seconds a press must be held before it reads as storing rather than as a tap. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Metalmind", meta = (ClampMin = "0.05"))
	float MetalmindHoldThreshold = 0.22f;

	/** Seconds two taps may be apart and still read as a flare. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Metalmind", meta = (ClampMin = "0.1"))
	float MetalmindDoubleTapWindow = 0.3f;

	//~ End metalmind

	/** The metal pool this power charges. Null when the owner is not an ACosmerePlayerState. */
	UCosmerePowerResourceComponent* GetPowerResource(const FGameplayAbilityActorInfo* ActorInfo) const;

	/**
	 * True when this power's row is a Surge, so it spends the shared stormlight reserve instead of a metal.
	 *
	 * False until the row resolves, which happens on the first activation - and false is the safe answer, because
	 * an unresolved row is refused outright before any cost is ever priced.
	 */
	bool SpendsStormlight() const { return bRowResolved && CachedSource == EPowerSource::Surge; }

	/**
	 * The metal pool this power charges: its own DT_Powers row name.
	 *
	 * Row name rather than slot, because two slots holding the same metal are the same reserve and a metal's
	 * reserve must not reset just because the player swapped which slot it sits in. NAME_None for a Surge, which
	 * draws stormlight instead.
	 */
	FName GetMetalPoolId() const { return SpendsStormlight() ? NAME_None : CachedPowerId; }

	/** Units left in the currency this power spends: its metal's pool, or the stormlight reserve. */
	float GetPowerCurrencyAvailable(const FGameplayAbilityActorInfo* ActorInfo) const;

	/** True when that currency can cover Cost. Read on both machines from replicated state. */
	bool CanAffordPowerCurrency(const FGameplayAbilityActorInfo* ActorInfo, float Cost) const;

	/** Authority-only charge of that currency. False when it could not cover Cost. */
	bool SpendPowerCurrency(const FGameplayAbilityActorInfo* ActorInfo, float Cost) const;

	/**
	 * The avatar's world. Taken from the avatar rather than from this object, because a non-instanced
	 * ability has no reliable outer chain to reach a world through.
	 */
	UWorld* GetPowerWorld(const FGameplayAbilityActorInfo* ActorInfo) const;

	/**
	 * How much of the row's cooldown is left, measured from the same stamp CheckCooldown refuses on.
	 *
	 * Exists because the clock is otherwise only ever asked a yes/no question. Never negative: a row that has
	 * already come back up answers 0 rather than how long ago it was last used.
	 */
	float GetCooldownRemainingSeconds() const;

	/**
	 * Moves the row's clock so that RemainingSeconds of its cooldown are left.
	 *
	 * Back-solves the inequality CheckCooldown tests, which keeps that arithmetic in one place. Correct even for
	 * a row whose cooldown is shorter than RemainingSeconds: the stamp lands ahead of now and the gate stays
	 * shut for exactly RemainingSeconds.
	 */
	void SetCooldownRemaining(float RemainingSeconds);

private:
	/** Repeating authority-side charge. Ends the power when the pool cannot pay the next interval. */
	void HandleResourceInterval();

	/** Fills the cached row values by matching AbilityClass against this ability's own class. */
	bool ResolveRow(const FGameplayAbilityActorInfo* ActorInfo) const;

	/**
	 * The resolved row, cached on the first activation. Plain scalars only, and not UPROPERTYs, so that
	 * resolving from a const hook (CheckCost / CheckCooldown) needs no mutable UPROPERTY and the cache holds
	 * no UObject reference the garbage collector would have to see.
	 */
	mutable bool bRowResolved = false;
	mutable FName CachedPowerId;
	mutable float CachedResourceCost = 0.f;
	mutable float CachedResourceCostInterval = 0.f;
	mutable float CachedCooldown = 0.f;
	mutable float CachedToggleLock = 0.f;
	mutable bool CachedToggle = false;
	mutable EPowerSource CachedSource = EPowerSource::Surge;

	/**
	 * World seconds at the last activation. Both the row's Cooldown and its ToggleLockSeconds are measured
	 * from here, so zeroing Cooldown leaves the resource as the only gate on a power.
	 */
	double LastActivatedTime = 0.0;

	FTimerHandle ResourceIntervalTimer;

	//~ Begin metalmind state
	/** The mode the loop is in, and the slot it is spending from. Not UPROPERTYs: this is transient state. */
	EMetalmindMode MetalmindMode = EMetalmindMode::Idle;
	int32 MetalmindSlotIndex = INDEX_NONE;
	int32 MetalmindLevel = 0;

	/** The press queued by QueueMetalmindMode. ActivateAbility adopts it and clears it whatever it decides. */
	EMetalmindMode PendingMetalmindMode = EMetalmindMode::Idle;
	int32 PendingMetalmindSlotIndex = INDEX_NONE;
	int32 PendingMetalmindLevel = 0;

	FTimerHandle MetalmindTimer;

	void HandleMetalmindTick();

protected:
	ACosmereCharacter* GetOwnerCosmereCharacter() const;
	//~ End metalmind state
};
