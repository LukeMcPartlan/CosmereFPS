// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CosmerePowerResourceComponent.generated.h"

/** Fired whenever one metal's pool changes, so a HUD binds instead of polling. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FCosmereResourceChanged, FName, MetalId, float, Current, float, Max);

/**
 * One metal's remaining reserve.
 *
 * A struct rather than a TMap entry because the pools have to replicate and UHT refuses to replicate a map
 * outright. The pair is spelled out, and the list only ever holds metals that have actually been spent into.
 */
USTRUCT(BlueprintType)
struct FCosmereMetalPool
{
	GENERATED_BODY()

	/** The DT_Powers row of the metal this reserve belongs to, e.g. Allomantic_Bendalloy. */
	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|Resource")
	FName MetalId;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|Resource")
	float Current = 0.f;

	/**
	 * Seconds of regen still owed on this pool after its own last spend. Authority-only and deliberately not a
	 * UPROPERTY: it is timing state, not economy state, so nothing about it needs to cross the wire.
	 */
	float RegenHoldRemaining = 0.f;
};

/**
 * Per-metal power pools: one reserve per Allomantic row, spent only by that row and refilled by time.
 *
 * Every metal burns its own metal, so every metal gets its own bar. A Bendalloy press must not shorten a Steel
 * reserve, which is exactly what one shared pool did - and it is why this component is keyed by the same
 * DT_Powers row name the ability resolves for itself rather than holding a single number.
 *
 * A metal with no entry is full. That is what makes "every metal starts at 100" true without seeding a list at
 * BeginPlay, and it keeps a metal nobody has spent down to zero replicated bytes. Only a Surge shares a reserve,
 * and that one lives in UCosmereSurgeResourceComponent.
 *
 * Deliberately NOT an AttributeSet attribute. UCosmereAttributeSet documents the locked decision that the match
 * model is health-only and that power resources are not attributes, because a shared reserve attribute would
 * contradict the per-ability gating design. A GameplayEffect can only modify attributes, so a resource modelled
 * as one could not be spent by an ability's own cost anyway.
 *
 * It lives on the PlayerState rather than the pawn for the same reason UCosmereAbilitySystemComponent does: a
 * respawn recycles the pawn, and the reserves should not refill just because a player died.
 */
UCLASS(ClassGroup = (Cosmere), meta = (BlueprintSpawnableComponent))
class COSMERERIVALS_API UCosmerePowerResourceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCosmerePowerResourceComponent();

	//~ Begin UActorComponent interface
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent interface

	/** Broadcast on the authority on every change, and on clients as each replication arrives. */
	UPROPERTY(BlueprintAssignable, Category = "Cosmere|Resource")
	FCosmereResourceChanged OnResourceChanged;

	/** The size of every metal's pool. One number because the design caps them all at 100. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Resource")
	float GetMaxResource() const { return MaxResource; }

	/** Units left in MetalId's pool. A metal that has never been spent reads full. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Resource")
	float GetMetalResource(FName MetalId) const;

	/** MetalId's reserve as a 0..1 fraction, for a progress bar. 0 when it has no readable size. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Resource")
	float GetMetalFraction(FName MetalId) const;

	/** True when MetalId's pool can cover Cost. A cost of 0 or less is always affordable. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Resource")
	bool CanAfford(FName MetalId, float Cost) const;

	/**
	 * Deducts Cost from MetalId's pool and returns true, or returns false and changes nothing.
	 *
	 * Authority only: the client predicts nothing about the bar, so it can never disagree with the server about
	 * whether a power was affordable.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Resource")
	bool TrySpend(FName MetalId, float Cost);

	/** Adds Amount to one metal's pool, clamped to MaxResource. Authority only. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Resource")
	void Refill(FName MetalId, float Amount);

	/**
	 * Adds Amount to every pool this player has already spent into. Authority only.
	 *
	 * A reserve that is absent is full, so there is nothing to top up - which is why this walks the list that
	 * exists rather than seeding one entry per metal in the catalog.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Resource")
	void RefillAll(float Amount);

	/**
	 * Deducts Amount from every pool this player has already spent into, for a leech. Authority only.
	 *
	 * A leech takes metal, not a specific metal, and the victim's two equipped rows are the only ones it could
	 * have burned - so every opened pool is what there is to take. A metal at full has no entry and is skipped.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Resource")
	void DrainAll(float Amount);

	/** One metal back to full. Authority only. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Resource")
	void RefillToMax(FName MetalId);

protected:
	/**
	 * The full bar, for every metal.
	 *
	 * Sized against the costs the design specifies: a cost of 5 is a twentieth of the bar, so a player gets twenty
	 * presses out of that metal from full. Every metal reads against this same 100, which is why the number lives
	 * here and not on a DT_Powers row - FPowerDefinition has no cap column and does not need one.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cosmere|Resource", meta = (ClampMin = "1.0"))
	float MaxResource = 100.f;

	/**
	 * Units restored per second to each pool that has been spent into. "Slowly over time" is this number.
	 *
	 * Applied per pool, not once overall: two equipped metals therefore recover at most twice this between them,
	 * which is correct because they are two separate reserves rather than one shared one.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cosmere|Resource", meta = (ClampMin = "0.0"))
	float RegenPerSecond = 1.f;

	/** Seconds after a spend before that pool's regen resumes, so a burst cannot be sustained forever. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cosmere|Resource", meta = (ClampMin = "0.0"))
	float RegenDelayAfterSpend = 1.f;

private:
	/**
	 * One entry per metal that has been spent. A missing entry means full.
	 *
	 * An array rather than a TMap because a replicated map is a UHT error, and these must replicate: a HUD that
	 * never received them would draw every metal as full. Order-preserving, and only as long as the number of
	 * metals actually spent into, so it stays tiny.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_MetalPools)
	TArray<FCosmereMetalPool> MetalPools;

	/** Index of MetalId's pool, or INDEX_NONE when that metal is still full. */
	int32 FindPoolIndex(FName MetalId) const;

	UFUNCTION()
	void OnRep_MetalPools();
};
