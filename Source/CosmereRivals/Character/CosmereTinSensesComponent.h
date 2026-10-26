// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CosmereTinSensesComponent.generated.h"

class AActor;
class ACosmereCharacter;

/** Fired whenever the burn changes, so presentation binds instead of polling. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCosmereTinSensesChanged, int32, TinLevel, float, SenseScale);

/**
 * Everything burning tin does to the player who is burning it.
 *
 * The split here is deliberate and is the whole design: the LEVEL is state that both machines agree on,
 * because it is pushed by the ability, while every effect it drives is local. Night vision, the raised
 * master volume and the smoke a tin burner can see through are all things one machine does to its own
 * picture - nothing here is replicated, and nothing here changes what any other player sees.
 *
 * Sense scale is 2^Level, so level 1 is the doubled hearing the metal is named for and every flare above
 * it doubles again. Both the hearing radius and the sight radius are derived from that one number rather
 * than being tuned separately, which is what keeps a flared burner's two senses in step.
 *
 * Sound is the only channel that reaches through geometry. A noise this machine hears inside its own
 * hearing radius is registered as a reveal on the listening pawn, which is the same seam Bronze uses -
 * so a heard enemy is bracketed through walls by the existing HUD pass, and an enemy that has not made a
 * sound is only bracketed once there is an unbroken line of sight to them.
 */
UCLASS(ClassGroup = (Cosmere), meta = (BlueprintSpawnableComponent))
class COSMERERIVALS_API UCosmereTinSensesComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCosmereTinSensesComponent();

	//~ Begin AActorComponent interface
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActorComponent interface

	//~ Begin burn

	/**
	 * Sets how hard tin is burning. 0 is off, 1 is the base burn, and each level above it doubles the
	 * senses. Clamped to MaxTinLevel so a power cannot drive the component past what it was tuned for.
	 *
	 * Safe to call repeatedly with the same value: the level is compared first, so an ability re-asserting
	 * its state every tick costs nothing and does not re-broadcast.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Tin")
	void SetTinLevel(int32 NewLevel);

	UFUNCTION(BlueprintPure, Category = "Cosmere|Tin")
	bool IsTinActive() const { return TinLevel > 0; }

	UFUNCTION(BlueprintPure, Category = "Cosmere|Tin")
	int32 GetTinLevel() const { return TinLevel; }

	/** 2^Level. 1 while tin is off, 2 at level 1, 4 at level 2, and so on. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Tin")
	float GetSenseScale() const;

	/** How far this player can hear a noise, in cm. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Tin")
	float GetHearingRadius() const;

	/** How far this player can pick a target out of the dark, in cm. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Tin")
	float GetVisionRadius() const;

	/**
	 * True when Target can be picked out right now: tin burning, inside the sight radius and roughly in
	 * front, with an unbroken line of sight. Deliberately not a reveal - the HUD asks this every frame, so
	 * it must be false the moment a wall comes between the two pawns.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Tin")
	bool IsVisibleWithTin(const AActor* Target) const;

	/**
	 * A noise this machine just heard. Registers Source as revealed on the owning pawn when it is inside
	 * the hearing radius, which is what lets a burner place an enemy through a wall by ear.
	 *
	 * Called on every machine by the game state's noise multicast; the owning-machine check inside is what
	 * makes only the listener's own picture change.
	 */
	void NotifyHeardNoise(AActor* Source);

	//~ End burn

	UPROPERTY(BlueprintAssignable, Category = "Cosmere|Tin")
	FCosmereTinSensesChanged OnTinSensesChanged;

protected:
	/** Applies or clears the local picture for the current level. Called from SetTinLevel and BeginPlay. */
	void ApplyLocalSenses();

	/** Puts the master volume back. Called on every end path, so a dying burner cannot muffle the editor. */
	void RestoreLocalSenses();

	ACosmereCharacter* GetOwningCosmereCharacter() const;

	/** How far an unaided ear reaches. Tin doubles this at level 1 and doubles it again per flare. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Tin", meta = (ClampMin = "0.0"))
	float BaseHearingRadius = 1400.f;

	/** How long a heard noise keeps its target marked, in seconds. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Tin", meta = (ClampMin = "0.1"))
	float RevealDuration = 3.f;

	/** Hard ceiling on the burn, so a power cannot drive the ladder past what was tuned. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Tin", meta = (ClampMin = "1"))
	int32 MaxTinLevel = 4;

	/** How far a target can be picked out of the dark at level 0 of the scale. Scaled by the sense scale. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Tin", meta = (ClampMin = "0.0"))
	float VisionRadius = 6000.f;

	/** Exposure stops added at level 1. Each flare doubles it, so this is the whole dark-vision ladder. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Tin", meta = (ClampMin = "0.0"))
	float DarkVisionExposureBias = 4.f;

	/** Extra master volume per level: 0.5 is +50% at level 1 and +200% at level 4. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Tin", meta = (ClampMin = "0.0"))
	float SenseVolumePerLevel = 0.5f;

private:
	/** The burn level. Transient: it is re-pushed by the ability on every activation. */
	UPROPERTY(Transient)
	int32 TinLevel = 0;

	/** What the camera carried before tin touched it, so the boost is exactly reversible. */
	float CachedExposureBias = 0.f;
	bool bCachedExposureOverride = false;
	bool bLocalSensesApplied = false;
};
