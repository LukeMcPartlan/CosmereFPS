// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/CosmereTypes.h"
#include "GameFramework/Controller.h"
#include "Net/CosmereMatchTypes.h"
#include "CosmereBotController.generated.h"

class ACosmereCharacter;
class ACosmerePlayerState;
class UCosmereWeaponComponent;

/**
 * A server-side opponent for the match modes.
 *
 * Deliberately a plain AController and not an AAIController. AAIController pulls in the AIModule edge,
 * and everything it adds - the path following component, the behavior tree runner, the focus system -
 * exists to navigate a NavMesh. Lvl_FirstPerson has no NavMeshBoundsVolume, so there is nothing to
 * navigate. What this bot actually needs is two things AController already provides: AddMovementInput
 * straight toward its target, and a control rotation to aim with.
 *
 * It also deliberately does not go through AGameModeBase::RestartPlayer. That path finishes in
 * AController::ClientSetRotation, which is a Client RPC, and a bot has no owning connection for it to
 * travel over. The game mode therefore spawns the pawn and calls Possess on it directly, which is the
 * same call minus the RPC a bot has nowhere to receive.
 *
 * The PlayerState is created here rather than being handed over, mirroring
 * APlayerController::PostInitializeComponents. It has to exist before the first possess, because
 * ACosmereCharacter::PossessedBy reads it to point the ability system's actor info at the pawn and to
 * pull the accepted kit down onto it.
 */
UCLASS()
class COSMERERIVALS_API ACosmereBotController : public AController
{
	GENERATED_BODY()

public:
	ACosmereBotController();

	/** Where on the game mode's spawn ring this bot belongs, so a respawn returns to the same spot. */
	void SetSpawnIndex(int32 InIndex) { SpawnIndex = InIndex; }
	int32 GetSpawnIndex() const { return SpawnIndex; }

	/**
	 * Gives this bot its side, its kit and its scoreboard name.
	 *
	 * Called by the game mode straight after spawning, which is before any pawn exists, so this creates
	 * the PlayerState on demand rather than assuming one is already there.
	 */
	void ConfigureBot(const FSpiritwebCharacterSave& InBuild, ECosmereTeam InTeam, const FString& InBotName);

	/**
	 * Stops the bot acting. Called when the match ends, so the results are not being read out over a
	 * firefight. A player is deliberately left in control of their own pawn - only the AI is frozen.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Bots")
	void StopBot();

	/** True once StopBot has run. A dead pawn is caught by its own IsAlive instead, not by this. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Bots")
	bool IsHalted() const { return bHalted; }
	
	/**
	 * Holds this bot idle for good, rather than until something wakes it.
	 *
	 * StopBot is a one-way latch for the end of a match, but both the configure path and every possess
	 * clear bHalted - which is correct for a match, where a respawned bot has to fight again, and wrong
	 * for a practice range, where the bots are targets that must stay targets across a respawn.
	 * This flag is deliberately not touched by either of those paths.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Bots")
	void SetIdle(bool bInIdle);
	
	/** True while this bot is being held idle, and therefore standing still and firing at nothing. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Bots")
	bool IsIdle() const { return bIdle; }
	
	/**
	 * Walks this bot in a circle around the spot it is spawned on, instead of standing still.
	 *
	 * Only ever acted on for a bot that is also idle, so a match bot's fighting is untouched: this exists for a
	 * practice target, which has to move for anything that has to be walked over to be testable. Idleness is
	 * unchanged, so the health bar drawn over it and the silence of its weapon both stay as they were.
	 *
	 * Stored on the controller rather than the pawn, for the same reason SetIdle is - a respawn replaces the
	 * pawn. A radius of 0 stands the bot still, which is what every non-practice bot gets.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Bots")
	void SetCirclePatrol(float InRadiusCm);

	/** The pawn this bot is currently engaging, or null. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Bots")
	APawn* GetCurrentTarget() const { return CurrentTarget; }

protected:
	//~ Begin AActor interface
	virtual void PostInitializeComponents() override;
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor interface

	//~ Begin AController interface
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	//~ End AController interface

	/** Re-acquires the nearest living opponent. Runs on its own timer, not every frame. */
	void RefreshTarget();

	/** The closest living pawn that is not on this bot's side, or null. */
	APawn* FindNearestOpponent() const;

	/** True when Other is somebody this bot is allowed to shoot, given the current rule set. */
	bool IsOpponent(const APawn* Other) const;

	/** True when nothing solid stands between this bot's eyes and Target. */
	bool HasLineOfSightTo(const APawn* Target) const;

	/** Pushes the stored side, name and kit onto the PlayerState, creating it if it is not there yet. */
	void ApplyIdentityToPlayerState();

	/** Points the control rotation at the target, with the tuning's aim error folded in. */
	void AimAt(const APawn& Target, float DeltaSeconds);

	/** Walks toward the target until it is inside PreferredRange. */
	void AdvanceOn(const APawn& Target);

	/** One shot if the weapon is ready, or a reload if the magazine is dry. */
	void ServiceWeapon();
	
	/** One frame of the circle: a step along the tangent of the circle struck around this bot's spawn. */
	void UpdateCirclePatrol();

	//~ Begin identity, held until the PlayerState exists to carry it. Structs rather than references,
	//~ because the PlayerState may not be spawned at the moment the game mode hands these over.
	UPROPERTY(Transient)
	FSpiritwebCharacterSave PendingBuild;

	UPROPERTY(Transient)
	ECosmereTeam PendingTeam = ECosmereTeam::None;

	UPROPERTY(Transient)
	FString PendingName;
	//~ End identity

	/** Index on the game mode's spawn ring. Outlives the pawn, so a respawn lands where it started. */
	UPROPERTY(Transient)
	int32 SpawnIndex = 0;

	UPROPERTY(Transient)
	TObjectPtr<APawn> CurrentTarget;

	/** Resolved on possess, so the tick is not searching the component list every frame. */
	UPROPERTY(Transient)
	TObjectPtr<UCosmereWeaponComponent> WeaponComponent;

	/** Set by StopBot only. Cleared again by ConfigureBot and OnPossess. */
	bool bHalted = false;
	
	/**
	 * Set by SetIdle only, and cleared by nothing in this class: a respawn re-possesses this same
	 * controller, so an idle flag that survived that is what keeps a practice-range bot a target.
	 */
	bool bIdle = false;
	
	/** Radius of the circle an idle bot walks, in cm. 0 stands it still, which is the pre-existing behaviour. */
	float PatrolRadius = 0.f;
	
	/** Centre of that circle, struck from the pawn the first time it is asked to walk it. */
	FVector PatrolCenter = FVector::ZeroVector;
	
	/** Cleared on every possess, so a respawn strikes the circle again around the pawn that replaced the old one. */
	bool bPatrolReady = false;

	//~ Begin tuning
	/** How far this bot will look for somebody to shoot, in cm. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Bots|Tuning", meta = (ClampMin = "100.0"))
	float SightRange = 12000.f;

	/** Distance the bot tries to hold, in cm. It closes the gap beyond this and stops short of it. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Bots|Tuning", meta = (ClampMin = "0.0"))
	float PreferredRange = 1500.f;

	/** Fraction of full throttle used to close the distance. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Bots|Tuning", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MoveThrottle = 1.f;

	/** Seconds between target re-acquisitions. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Bots|Tuning", meta = (ClampMin = "0.05"))
	float RetargetInterval = 0.75f;

	/**
	 * Degrees of error added to the shot, so a bot is not a perfect shot. Applied to the aim direction
	 * as a cone, which is the same shape UCosmereWeaponComponent's own spread uses.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Bots|Tuning", meta = (ClampMin = "0.0"))
	float AimErrorDegrees = 2.5f;

	/** How fast the aim swings onto a new target, in interp units. Higher is twitchier. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Bots|Tuning", meta = (ClampMin = "0.1"))
	float AimTurnRate = 6.f;
	//~ End tuning

	/** Drives the retarget cadence, so the search does not run on every frame of the tick. */
	FTimerHandle RetargetTimerHandle;
};
