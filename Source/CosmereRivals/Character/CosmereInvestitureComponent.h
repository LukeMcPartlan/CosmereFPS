// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CosmereInvestitureComponent.generated.h"

/** One source's contribution to the two resistances. Sources compose, so nothing overwrites anything. */
USTRUCT()
struct FCosmereResistanceSource
{
	GENERATED_BODY()

	UPROPERTY()
	FName Source = NAME_None;

	/** Fraction of a physical hit shrugged off: steel and iron shoves, a pewter blow. */
	UPROPERTY()
	float Physical = 0.f;

	/** Fraction of an invested effect shrugged off: emotion, a lashing, a power's damage. */
	UPROPERTY()
	float Spiritual = 0.f;
};

/** One copper cloud currently covering this pawn. */
USTRUCT()
struct FCosmereCopperShroud
{
	GENERATED_BODY()

	UPROPERTY()
	FName Source = NAME_None;

	/** The copper's own flare. Bronze has to burn higher than this to see through it. */
	UPROPERTY()
	int32 Level = 0;

	/** World seconds the shield lapses at unless the copper re-asserts it. */
	UPROPERTY()
	double ExpiresAt = 0.0;
};

/**
 * What other people's investiture runs into on this pawn.
 *
 * Two resistances rather than one, because the metals answer two different questions. Physical resistance is
 * the body being moved or broken by metal - a steel push, an iron pull, a pewter blow - and lives on the pawn
 * rather than on the attacker so that a stored metalmind and a copper cloud compose instead of competing.
 * Spiritual resistance is the mind and soul being worked on - an emotional pull or riot, a lashing, a power's
 * damage - and is what copper lines itself with.
 *
 * The copper shroud is here too because it is the same kind of fact: a per-source entry with a level, kept
 * with the resistances rather than beside them. Copper is a cloud, so it is also a thing that expires: a cloud
 * that stops being re-asserted stops covering anybody, which is what makes killing the burner mean something.
 */
UCLASS(ClassGroup = (Cosmere), meta = (BlueprintSpawnableComponent))
class COSMERERIVALS_API UCosmereInvestitureComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCosmereInvestitureComponent();

	//~ Begin resistances

	/**
	 * Registers or replaces Source's contribution. Neutral values are removed rather than stored, so a cleared
	 * power leaves nothing behind and a grant is idempotent.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Investiture")
	void SetResistancesForSource(FName Source, float Physical, float Spiritual);

	UFUNCTION(BlueprintCallable, Category = "Cosmere|Investiture")
	void ClearResistanceSource(FName Source);

	UFUNCTION(BlueprintCallable, Category = "Cosmere|Investiture")
	void ClearAllResistances();

	/** 0..1, composed across every source: two half-resistances leave a quarter of the hit. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Investiture")
	float GetPhysicalResistance();

	/** 0..1, composed across every source. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Investiture")
	float GetSpiritualResistance();

	/** Incoming scaled by what is left after physical resistance. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Investiture")
	float ScaleForPhysical(float Incoming);

	/** Incoming scaled by what is left after spiritual resistance. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Investiture")
	float ScaleForSpiritual(float Incoming);

	//~ End resistances

	//~ Begin immunity

	/**
	 * Seals this pawn off from investiture under Source, until the same Source takes it back off.
	 *
	 * A state of its own rather than a large resistance, because it answers a different question. Resistance
	 * makes an effect smaller; immunity makes it not happen. A lashing that is merely weakened still turns a
	 * player upside down, and a riot that is merely weakened still moves them - so the effects that must be
	 * refused outright cannot be expressed as a fraction of themselves.
	 *
	 * Named by source and keyed by it, so two powers can hold a seal up at once and the first to end does not
	 * strip the second's. Every power that arms a body against investiture registers here: copper's cloud today,
	 * aluminum and duralumin as they land.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Investiture")
	void SetInvestitureImmunity(FName Source);

	UFUNCTION(BlueprintCallable, Category = "Cosmere|Investiture")
	void ClearInvestitureImmunity(FName Source);

	UFUNCTION(BlueprintCallable, Category = "Cosmere|Investiture")
	void ClearAllImmunity();

	/** True while any source is holding a seal up on this pawn. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Investiture")
	bool IsInvestitureImmune() const;

	/**
	 * The single question every invested effect asks about whoever it is aimed at.
	 *
	 * Static and taking a bare actor, so a power never has to know what it just hit or null-check first: a
	 * prop, a wall and a player all answer here, and anything without investiture answers false.
	 *
	 * One rule for the whole set - a sealed soul takes nothing in, healing included - because a per-power list
	 * of what respects immunity is a list that will always be missing whichever power was written last.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Investiture")
	static bool IsTargetInvestitureImmune(AActor* Target);

	//~ End immunity

	//~ Begin copper

	/** Covers this pawn until DurationSeconds have passed. Re-calling Source replaces its level and refreshes it. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Investiture")
	void SetCopperShroud(FName Source, int32 Level, float DurationSeconds);

	UFUNCTION(BlueprintCallable, Category = "Cosmere|Investiture")
	void ClearCopperShroud(FName Source);

	/** The strongest live cloud over this pawn, or 0 when nothing is covering it. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Investiture")
	int32 GetCopperShroudLevel();

	/**
	 * True when a Seer burning at BronzeLevel cannot see through this pawn's copper.
	 *
	 * Copper holds at its own flare and one below: only a bronze that is burning HARDER burns through, which is
	 * what makes the matchup a bidding war rather than a flat counter.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Investiture")
	bool IsHiddenFromBronze(int32 BronzeLevel);

	//~ End copper

private:
	/** World seconds, or 0 when there is no world yet. */
	double GetNowSeconds() const;

	/** Drops lapsed shrouds so a copper that stopped cannot hide anybody for longer than its last assertion. */
	void PruneExpiredShrouds();

	UPROPERTY(Transient)
	TArray<FCosmereResistanceSource> Resistances;

	UPROPERTY(Transient)
	TArray<FCosmereCopperShroud> Shrouds;

	/** One entry per power currently sealing this pawn off. Order means nothing; presence is the whole fact. */
	UPROPERTY(Transient)
	TArray<FName> Immunities;
};
