// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CosmereSurgeResourceComponent.generated.h"

/** Fired whenever the stormlight reserve changes, so the HUD binds instead of polling. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCosmereSurgeChanged, float, Current, float, Max);

/**
 * One currency's remaining reserve.
 *
 * A struct rather than a TMap entry because the reserve has to replicate - a client prices a lash from it
 * before it spends - and UHT refuses to replicate a map outright. The pair is therefore spelled out.
 */
USTRUCT(BlueprintType)
struct FCosmereSurgePool
{
	GENERATED_BODY()

	/** Which currency this entry holds. Only ever StormlightKey today. */
	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|Stormlight")
	FName CurrencyId;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|Stormlight")
	float Current = 0.f;
};

/**
 * The stormlight reserve: one shared pool that every Surge spends and only the world puts back.
 *
 * Shared on purpose. A Surge is fuelled by stormlight - the same stormlight whichever Surge is being used - so
 * all ten draws come out of one reserve, and a Gravitation lash is paid for out of the same reserve a
 * Transportation step is. That is what makes the two Surge slots a real choice rather than two separate tanks.
 *
 * A metal is not a Surge and does not belong here: each Allomantic row burns its own metal, and those reserves
 * live in UCosmerePowerResourceComponent keyed per row. Bendalloy is an alloy, so it is a metal, and it spends
 * its own pool rather than this one.
 *
 * Why it is a separate component from the metal pools at all: this reserve never regenerates. It is refreshed
 * only by standing in stormlight, so it cannot share the metal pools' time-based refill.
 *
 * A reserve with no entry is full. That is what makes "starts at 100" true without seeding a list at BeginPlay,
 * and it keeps an untouched reserve down to zero replicated bytes.
 *
 * Lives on the PlayerState rather than the pawn for the same reason the metal pools do: a respawn recycles the
 * pawn, and a reserve should not be handed back just because the player died.
 */
UCLASS(ClassGroup = (Cosmere), meta = (BlueprintSpawnableComponent))
class COSMERERIVALS_API UCosmereSurgeResourceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCosmereSurgeResourceComponent();

	//~ Begin UActorComponent interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent interface

	/**
	 * The single currency every Surge spends.
	 *
	 * Row ids deliberately do not key this reserve any more: a Surge id would make two Surges read two different
	 * numbers out of what is one tank of stormlight.
	 */
	static const FName StormlightKey;

	UPROPERTY(BlueprintAssignable, Category = "Cosmere|Stormlight")
	FCosmereSurgeChanged OnSurgeChanged;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Stormlight")
	float GetMaxStormlight() const { return MaxStormlight; }

	/** Units of stormlight left. A reserve that has never been spent reads full. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Stormlight")
	float GetStormlight() const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Stormlight")
	float GetStormlightFraction() const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Stormlight")
	bool CanAffordStormlight(float Cost) const;

	/**
	 * Deducts Cost and returns true, or returns false and changes nothing.
	 *
	 * Authority only, matching the metal pools: the client predicts the lash but never the bar, so it can never
	 * disagree with the server about whether a Surge was affordable.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Stormlight")
	bool TrySpendStormlight(float Cost);

	/** Adds Amount, clamped to MaxStormlight. Authority only. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Stormlight")
	void AddStormlight(float Amount);

	/** Back to full. Authority only. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Stormlight")
	void RefillStormlight();

protected:
	/**
	 * The full reserve.
	 *
	 * Sized so a flared Gravitation ladder - 5, 10, 20, 40 - runs the tank nearly dry in four presses rather than
	 * letting a player lash forever, while still leaving room for a Transportation step out of the same tank.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cosmere|Stormlight", meta = (ClampMin = "1.0"))
	float MaxStormlight = 100.f;

private:
	/**
	 * One entry, held only while the reserve is below full. An absent entry means full.
	 *
	 * An array rather than a bare float because "absent means full" is what makes the starting value true
	 * without a BeginPlay write that a late-joining client could miss, and it is already the shape the metal
	 * pools replicate through.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Stormlight)
	TArray<FCosmereSurgePool> Pools;

	/** Index of CurrencyId's entry, or INDEX_NONE when the reserve is still full. */
	int32 FindPoolIndex(FName CurrencyId) const;

	UFUNCTION()
	void OnRep_Stormlight();
};
