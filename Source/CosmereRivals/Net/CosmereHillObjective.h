// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Net/CosmereMatchTypes.h"
#include "CosmereHillObjective.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;

/**
 * The King of the Hill capture zone.
 *
 * Server-authoritative, and deliberately cut loose from the game mode: the hill owns capture and pays
 * team score through ACosmereGameState::AddTeamScore, which replicates on its own, while the game mode
 * owns the win check. Neither needs a pointer to the other, so either can be swapped without touching
 * the other.
 *
 * Capture is winner-takes-all rather than a tug of war. A side alone in the volume fills a meter over
 * CaptureSeconds and takes the hill outright; a contest freezes the meter where it stands. That keeps
 * the HUD readable - one bar, one direction - and means holding the point is the only way to score.
 */
UCLASS()
class COSMERERIVALS_API ACosmereHillObjective : public AActor
{
	GENERATED_BODY()

public:
	ACosmereHillObjective();

	//~ Begin AActor interface
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor interface

	/** The side that owns the hill, or None while it is neutral. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Hill")
	ECosmereTeam GetOwningTeam() const { return OwningTeam; }

	/** The side currently taking the hill, or None while it is empty or contested. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Hill")
	ECosmereTeam GetContestingTeam() const { return ContestingTeam; }

	/** 0..1 progress toward ContestingTeam. Reads 1 while the owner is holding uncontested. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Hill")
	float GetCaptureProgress() const { return CaptureProgress; }

	/** How many living members of Team are standing in the volume right now. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Hill")
	int32 GetTeamPresence(ECosmereTeam Team) const;

protected:
	/** Which side, if either, is alone inside the volume. None covers both "empty" and "contested". */
	ECosmereTeam ResolveSoleOccupant() const;

	/** Pays team score for however much of this frame the hill was held uncontested. Authority only. */
	void AwardHeldScore(float DeltaSeconds);

	/**
	 * The capture volume. Query-only and set to overlap pawns and nothing else - it is a sensor, not
	 * geometry, so it must not stop a shot or a body.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cosmere|Hill")
	TObjectPtr<UCapsuleComponent> CaptureVolume;

	/**
	 * A flat disc on the ground so the hill is legible with no authored asset at all. Purely a marker:
	 * no collision, and swapping the mesh or the material on a subclass changes nothing about capture.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cosmere|Hill")
	TObjectPtr<UStaticMeshComponent> HillMarker;

	//~ Begin replicated state. Written on the server, read everywhere - a HUD draws the bar from these.
	UPROPERTY(Replicated)
	ECosmereTeam OwningTeam = ECosmereTeam::None;

	UPROPERTY(Replicated)
	ECosmereTeam ContestingTeam = ECosmereTeam::None;

	UPROPERTY(Replicated)
	float CaptureProgress = 0.f;
	//~ End replicated state

	//~ Begin tuning
	/** Radius of the capture volume, in cm. */
	UPROPERTY(EditAnywhere, Category = "Cosmere|Hill|Tuning", meta = (ClampMin = "50.0"))
	float CaptureRadius = 700.f;

	/** Half-height of the capture volume, in cm. */
	UPROPERTY(EditAnywhere, Category = "Cosmere|Hill|Tuning", meta = (ClampMin = "50.0"))
	float CaptureHalfHeight = 300.f;

	/** Seconds of uncontested presence needed to take the hill, from neutral or from the other side. */
	UPROPERTY(EditAnywhere, Category = "Cosmere|Hill|Tuning", meta = (ClampMin = "0.1"))
	float CaptureSeconds = 4.f;

	/**
	 * Seconds of uncontested holding per team point.
	 *
	 * Expressed per point rather than as points per second so that it stays legible against a score
	 * limit: at the default, taking and holding the hill for a minute is worth 20 of the 30 points a
	 * King of the Hill match is won on.
	 */
	UPROPERTY(EditAnywhere, Category = "Cosmere|Hill|Tuning", meta = (ClampMin = "0.1"))
	float SecondsPerPoint = 3.f;
	//~ End tuning

	/**
	 * Fractional points carried between frames. Without it, a delta just under the threshold would be
	 * floored away every single tick and the hill would pay nothing at all.
	 */
	float ScoreAccumulator = 0.f;
};
