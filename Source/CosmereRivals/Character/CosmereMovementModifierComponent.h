// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CosmereMovementModifierComponent.generated.h"

/** The CharacterMovement values a modifier can touch. */
UENUM(BlueprintType)
enum class ECosmereMovementChannel : uint8
{
	/** CharacterMovement->MaxWalkSpeed, in cm/s. */
	WalkSpeed,

	/** CharacterMovement->JumpZVelocity, in cm/s. */
	JumpVelocity,

	/** CharacterMovement->GravityScale, a multiplier on world gravity. */
	GravityScale,

	/** CharacterMovement->AirControl, 0..1. */
	AirControl
};

/** How a modifier combines with the others on its channel. */
UENUM(BlueprintType)
enum class ECosmereMovementOp : uint8
{
	/** Multiplied with every other Multiply on the channel. */
	Multiply,

	/** Summed with every other Add on the channel, then multiplied by the product of the multiplies. */
	Add
};

/**
 * One contribution to one channel.
 *
 * Source identifies the contribution rather than describing it: registering a second modifier with the same
 * Source and Channel replaces the first. That is what lets a power re-assert its number every tick, or every
 * escalation, without stacking on itself.
 */
USTRUCT(BlueprintType)
struct FCosmereMovementModifier
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|Movement")
	FName Source;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|Movement")
	ECosmereMovementChannel Channel = ECosmereMovementChannel::WalkSpeed;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|Movement")
	ECosmereMovementOp Op = ECosmereMovementOp::Multiply;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|Movement")
	float Value = 1.f;
};

/**
 * The only writer of its owner's CharacterMovement values.
 *
 * Nothing assigns MaxWalkSpeed, JumpZVelocity, GravityScale or AirControl directly. Every system that wants to
 * change them registers a modifier and keeps the Source it used; this component aggregates all of them against
 * the owner's base values - the race row's numbers, never whatever the live component happens to hold - and
 * writes the result. Two things follow, and both are the reason it exists:
 *
 *  - Sources compose. A slow and a speed buff resolve to (base x 0.5) x 4 rather than to whichever ran last,
 *    and the order they arrived in does not change the answer, because the aggregation is a sum and a product.
 *  - Re-applying is idempotent. Aggregating from the base means a per-tick re-assert cannot compound with
 *    itself, and a value replicated from the server converges to the same answer the client computed.
 *
 * Removal is by Source, so a power that ends takes exactly its own contribution with it and nobody else's.
 */
UCLASS(ClassGroup = (Cosmere), meta = (BlueprintSpawnableComponent))
class COSMERERIVALS_API UCosmereMovementModifierComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCosmereMovementModifierComponent();

	/** Registers or replaces Source's contribution to Channel, then rewrites the movement component. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Movement")
	void SetModifier(FName Source, ECosmereMovementChannel Channel, ECosmereMovementOp Op, float Value);

	/** Removes Source's contribution to one channel. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Movement")
	void ClearModifier(FName Source, ECosmereMovementChannel Channel);

	/** Removes everything Source registered, on every channel. This is what a power calls when it ends. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Movement")
	void ClearSource(FName Source);

	/** Drops every contribution that makes this pawn slower or weaker, whoever registered it. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Movement")
	void CleansePenalties();

	/** What Channel resolves to with the modifiers as they stand. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Movement")
	float GetEffectiveValue(ECosmereMovementChannel Channel) const;

	/** The unmodified value Channel resolves from. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Movement")
	float GetBaseValue(ECosmereMovementChannel Channel) const;

	/** Rewrites all four channels from the aggregate. Called by every mutation, and by anything moving a base. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Movement")
	void Apply();

	/** Read-only view of the live stack, for inspection while tuning. */
	const TArray<FCosmereMovementModifier>& GetModifiers() const { return Modifiers; }

private:
	/** Index of Source's modifier on Channel, or INDEX_NONE. */
	int32 FindModifier(FName Source, ECosmereMovementChannel Channel) const;

	/** The owner's build numbers, or a one-time snapshot for a pawn that has no build behind it. */
	float ResolveBase(ECosmereMovementChannel Channel) const;

	/** At most one entry per Source and Channel pair. */
	UPROPERTY(VisibleInstanceOnly, Category = "Cosmere|Movement")
	TArray<FCosmereMovementModifier> Modifiers;

	/** Only used by a pawn that is not an ACosmereCharacter: the first request records what the CMC held. */
	mutable bool bSnapshotTaken = false;
	mutable float SnapshotWalkSpeed = 0.f;
	mutable float SnapshotJumpVelocity = 0.f;
	mutable float SnapshotGravityScale = 1.f;

	mutable float SnapshotAirControl = 0.f;
};
