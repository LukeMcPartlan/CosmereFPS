// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/CosmereTypes.h"
#include "CosmereRealmComponent.generated.h"

class FLifetimeProperty;
class UPrimitiveComponent;

/** Fired on every machine once this pawn's realm has changed and been applied. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCosmereRealmChanged, ECosmereRealm, Realm);

/**
 * Which side of the veil a pawn stands on, and everything that follows from standing there.
 *
 * The Physical Realm and Shadesmar share one map: the same floor, the same walls, the same geometry. What
 * changes is who else is real. Two bodies in the same realm collide, shoot and push each other exactly as
 * before; two bodies on opposite sides pass through each other, cannot see each other, and cannot land a
 * bullet, a strike, a field or a heal on each other.
 *
 * The split is a pawn-side fact rather than a match mode, because every consequence of it is pawn-side: the
 * movement stack belongs to the pawn, the camera belongs to the pawn, and the body that has to step through
 * another body is the pawn's capsule.
 *
 * Why the realm is an enum on a component rather than a collision channel: the pairwise rule is "these two
 * bodies ignore each other, but each of them still collides with a Shadesmar partner", which no per-component
 * channel response can express. It is expressed here instead, on the two capsules, and by the single
 * ShareRealm() question every effect already asks.
 *
 * Why it is authority-owned: a client that could flip its own realm would be a client that could decide,
 * unilaterally, that nothing can hit it. SetRealm() therefore refuses everywhere but the authority, and the
 * owning client learns the new answer the same way everyone else does - by replication.
 */
UCLASS(ClassGroup = (Cosmere), meta = (BlueprintSpawnableComponent))
class COSMERERIVALS_API UCosmereRealmComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCosmereRealmComponent();

	//~ Begin UActorComponent interface
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent interface

	/**
	 * The realm Actor stands in.
	 *
	 * Three answers, in this order: the component if the actor carries one (every character does), the
	 * Shadesmar stamp if it is something a phased pawn spawned, and the material world for everything else.
	 *
	 * An unstamped actor is part of the world rather than a participant in the match, which is the answer that
	 * keeps the level solid in both realms: a floor has no realm, so it is the floor for whoever is standing on
	 * it, phased or not. Spawns are stamped by StampRealm for exactly that reason - so that a trap can be told
	 * apart from a wall.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Realm")
	static ECosmereRealm GetActorRealm(const AActor* Actor);

	/**
	 * True when A and B can affect each other.
	 *
	 * One question, asked by every system that could cross the veil: the weapon trace, the strike trace, the
	 * field bubble, the group blink, the heal, the reveal and a bot's target pick. It answers true when either
	 * side has no realm of its own, because "a wall" is not a participant and must never be skipped - the only
	 * way past a wall is the wall.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Realm")
	static bool ShareRealm(const AActor* A, const AActor* B);

	/**
	 * Copies Source's realm onto something Source has just spawned.
	 *
	 * A stamped spawn answers GetActorRealm, so ShareRealm can tell a phased trap from a piece of architecture
	 * and a material player cannot interact with it. Nothing is stamped for a material-world spawn: an unstamped
	 * actor is already material, and leaving it unstamped keeps it indistinguishable from the world it was
	 * spawned into.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Realm")
	static void StampRealm(AActor* Spawned, const AActor* Source);

	/** The realm component an actor carries. Null for anything that is not a character. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Realm")
	static UCosmereRealmComponent* FindRealm(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "Cosmere|Realm")
	ECosmereRealm GetRealm() const { return Realm; }

	UFUNCTION(BlueprintPure, Category = "Cosmere|Realm")
	bool IsInShadesmar() const { return Realm == ECosmereRealm::Shadesmar; }

	/**
	 * Moves this pawn to NewRealm. Authority only, and returns whether anything changed.
	 *
	 * The authority is the only side allowed to answer, so the re-check is here rather than at each caller: a
	 * power that runs on both machines can call this without first asking which machine it is on.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Realm")
	bool SetRealm(ECosmereRealm NewRealm);

	/** Flips this pawn between the two realms. Authority only, like SetRealm. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Realm")
	bool ToggleRealm();

	/**
	 * Re-decides what the local viewer can see, for every pawn and every stamped spawn in the world.
	 *
	 * Static and world-wide because a realm change on any one pawn moves the answer for all of them: the
	 * viewer is a single player per machine, so "hide the ones that are not on my side" has to be answered
	 * once for the whole world rather than by whoever happened to change.
	 */
	static void RefreshLocalView(UWorld* World);

	UPROPERTY(BlueprintAssignable, Category = "Cosmere|Realm")
	FCosmereRealmChanged OnRealmChanged;

protected:
	UFUNCTION()
	void OnRep_Realm();

	/** Everything the new realm means on this machine: speed, filter, bodies, and the local picture. */
	void ApplyRealm();

	/**
	 * The travel speed, registered on the pawn's own movement stack rather than written onto the movement
	 * component, so it multiplies whatever else is already moving this pawn instead of replacing it.
	 *
	 * Applied on both machines, because the machine that has to feel it is the one simulating the pawn.
	 */
	void ApplySpeedModifier();

	/** The black-and-white grade. Local, and only on the machine that owns this pawn's camera. */
	void ApplyLocalFilter();

	/**
	 * Body collision between the two realms.
	 *
	 * IgnoreActorWhenMoving only covers the body that moves, so both ends of every pair are written here. One
	 * directional pass is enough because a realm change on either pawn re-runs this for all of its own pairs.
	 */
	void RefreshPairing();

	/** Hides or restores this pawn's own components for the local viewer. Unreplicated, like the picture. */
	void SetPhasedOutLocally(bool bPhasedOut);

	/** The tag a spawn carries so that it can be told apart from the world it was spawned into. */
	static FName ShadesmarTagName();

	/**
	 * Which realm this pawn is in. Replicated without a condition, so every machine - including the ones that
	 * only need it to decide whether to draw this body - learns the same answer at the same time.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Realm)
	ECosmereRealm Realm = ECosmereRealm::Material;

	/**
	 * Multiplier on the pawn's own run speed while phased. 1.5 is the whole point of the trip: Shadesmar's
	 * distances are shorter, so the same walk covers more ground.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Realm", meta = (ClampMin = "1.0", ClampMax = "5.0"))
	float ShadesmarSpeedMultiplier = 1.5f;

	/**
	 * Colour grading saturation while phased. 0 is black and white, which is what the Cognitive Realm reads as:
	 * the same shapes with the colour taken out of them.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Realm", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ShadesmarSaturation = 0.f;

private:
	/** Captured on the first grade, so leaving Shadesmar restores what the camera shipped with. */
	bool bFilterCached = false;
	bool bCachedSaturationOverride = false;
	FVector4 CachedSaturation = FVector4(1.0, 1.0, 1.0, 1.0);

	/**
	 * True while this pawn's components are hidden from the local viewer. Held so that restoring puts back
	 * only what was taken away - a component that was already invisible stays invisible.
	 */
	bool bPhasedOut = false;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> HiddenPrimitives;
};
