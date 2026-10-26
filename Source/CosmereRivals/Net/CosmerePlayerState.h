// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "Data/CosmereTypes.h"
#include "Net/CosmereMatchTypes.h"
#include "GameFramework/PlayerState.h"
#include "CosmerePlayerState.generated.h"

class UCosmereAbilitySystemComponent;
class UCosmereAttributeSet;
class UCosmerePowerResourceComponent;
class UCosmereSurgeResourceComponent;

/**
 * Fired when this player's authoritative build becomes available - on the server as it is applied,
 * and on clients as the property replicates in.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCosmereBuildReplicated, bool, bHasBuild);

/**
 * Carries one player's selected character build across the wire.
 *
 * Why the build travels as data rather than as an index: a profile lives in the local "CosmereSave"
 * slot on the machine that created it, so the server has nothing to resolve a client's
 * SelectedCharacterIndex against - it would be reading its own unrelated save. The owning client
 * therefore submits the whole FSpiritwebCharacterSave and the server's job is only to confirm that
 * every id in it exists in the catalog.
 *
 * The server deliberately does not check ownership (unlocked / not unlocked). Unlocks are local
 * profile state the server cannot see or verify, so an ownership check here would be security
 * theatre that rejects legitimate clients. Ids that do not exist in the catalog ARE rejected, which
 * is what stops a malformed or hostile payload from reaching the pawn.
 *
 * A Server RPC is valid on this actor because AController::InitPlayerState spawns the PlayerState
 * with SpawnInfo.Owner = this (Controller.cpp:671), so it is net-owned by the client's connection.
 */
UCLASS()
class COSMERERIVALS_API ACosmerePlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ACosmerePlayerState();

	//~ Begin AActor interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	//~ End AActor interface

	/** Submit the owning client's local build after Super establishes the network owner. */
	virtual void ClientInitialize(AController* C) override;

	//~ Begin IAbilitySystemInterface
	/**
	 * The match ability system. Owned by the PlayerState rather than the pawn so that a respawn can
	 * recycle the pawn while player-scoped ability state survives it.
	 */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface

	/**
	 * The per-metal pools UCosmerePowerAbility spends against: one 100-unit reserve per Allomantic row, so each
	 * metal burns only its own. Player-scoped like the ASC and the attribute set, so a respawn does not wipe them.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Resource")
	UCosmerePowerResourceComponent* GetPowerResourceComponent() const { return PowerResourceComponent; }

	/**
	 * The shared stormlight reserve every Surge spends and a stormlight cloud refills. Player-scoped like the
	 * metal pools, so a respawn does not hand a player their lashing back.
	 *
	 * Deliberately separate from the metal pools: those regenerate on their own, one per metal, while stormlight
	 * never regenerates and is refreshed only by standing in something.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Resource")
	UCosmereSurgeResourceComponent* GetSurgeResourceComponent() const { return SurgeResourceComponent; }

	/**
	 * Reads this machine's selected build out of the local profile and submits it to the server.
	 * Call before travelling; safe (and a no-op) on the server, which is already authoritative.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Build")
	bool SubmitSelectedCharacterFromProfile();

	/** True once the server has accepted and replicated a build for this player. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Build")
	bool HasSelectedBuild() const { return bHasSelectedBuild; }

	/** The accepted build. Only meaningful when HasSelectedBuild() is true. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Build")
	FSpiritwebCharacterSave GetSelectedBuild() const { return SelectedBuild; }

	/**
	 * Broadcast when the build is in place, so the pawn can push race stats onto its movement
	 * component. Race run speed / jump height cannot come from a GameplayEffect - GAS cannot modify
	 * CharacterMovementComponent - so this is the hook an explicit pawn-stats step would use.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Cosmere|Build")
	FOnCosmereBuildReplicated OnBuildReplicated;

	//~ Begin match scoreboard. Replicated, so a scoreboard widget reads the same numbers on every
	//~ machine rather than only on the server.
	/** This player's side. None in Free-for-All, where every player is their own side. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Match")
	ECosmereTeam GetMatchTeam() const { return MatchTeam; }

	UFUNCTION(BlueprintPure, Category = "Cosmere|Match")
	int32 GetKills() const { return Kills; }

	UFUNCTION(BlueprintPure, Category = "Cosmere|Match")
	int32 GetDeaths() const { return Deaths; }

	/**
	 * True when this PlayerState drives an AI opponent rather than a human connection.
	 *
	 * Separate from the engine's APlayerState::bIsABot on purpose: that one is not replicated, so a
	 * client cannot use it to tell a bot row from a human row on the scoreboard.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Match")
	bool IsBotPlayer() const { return bIsBotPlayer; }
	//~ End match scoreboard

	//~ Begin authority-only mutators
	/** Places this player on a side. Used for humans and bots alike. */
	void SetMatchTeam(ECosmereTeam NewTeam);

	/** Marks this PlayerState as an AI opponent. */
	void SetBotPlayer(bool bNewIsBot);

	/**
	 * Records one kill: bumps Kills and the inherited APlayerState::Score, which is the number
	 * Free-for-All compares against the score limit and the point column the scoreboard shows.
	 */
	void AddKill();

	/** Records one death, for the scoreboard's death column. */
	void AddDeath();
	//~ End authority-only mutators

	/**
	 * Server-side build assignment for an AI opponent.
	 *
	 * A bot has no owning connection, so there is no client to call ServerSubmitCharacter and no
	 * ownership question to answer - the server picked this kit itself. Validation is deliberately
	 * the same call the RPC path uses, so a bot can never be spawned with a kit that a human client
	 * would have been refused.
	 */
	void SetBuildForBot(const FSpiritwebCharacterSave& Build);

	/** Standalone hub only: resolve the confirmed local profile, validate, then apply through the existing build seam. */
	bool ApplyLocalPracticeBuild(const FSpiritwebCharacterSave& Build, FText& OutError);

	/** Standalone practice only; ordinary network matches retain their existing statistics. */
	void ResetPracticeStatistics();

protected:
	/**
	 * Server entry point. Validated against the catalog in the implementation rather than in a
	 * _Validate body, so an unresolvable build is refused with a log instead of kicking the client.
	 */
	UFUNCTION(Server, Reliable)
	void ServerSubmitCharacter(const FSpiritwebCharacterSave& Build);

	UFUNCTION()
	void OnRep_SelectedBuild();

	/** The match ability system. Created here so that it outlives any individual pawn. */
	UPROPERTY()
	TObjectPtr<UCosmereAbilitySystemComponent> AbilitySystemComponent;

	/**
	 * Health lives here for the same reason the ASC does: a respawn replaces the pawn, so the
	 * attribute set has to outlive it.
	 */
	UPROPERTY()
	TObjectPtr<UCosmereAttributeSet> AttributeSet;

	/**
	 * The per-metal power pools. Player-scoped like the attribute set, for the same reason: a respawn replaces
	 * the pawn but must not refill a metal's reserve.
	 */
	UPROPERTY()
	TObjectPtr<UCosmerePowerResourceComponent> PowerResourceComponent;

	/**
	 * The shared stormlight reserve. Player-scoped for the same reason as the metal pools: a respawn replaces
	 * the pawn but must not refill stormlight. One entry while it is below full; a missing entry is full.
	 */
	UPROPERTY()
	TObjectPtr<UCosmereSurgeResourceComponent> SurgeResourceComponent;

	/** True when every id in Build resolves in this machine's catalog. Fills OutReason on refusal. */
	bool ValidateBuildAgainstCatalog(const FSpiritwebCharacterSave& Build, FString& OutReason) const;

	/** Server-side apply. Separate from the RPC so accept and replicate are distinguishable in logs. */
	void ApplyBuildOnServer(const FSpiritwebCharacterSave& Build);

	/**
	 * Grants one ability spec per power in Build, replacing anything granted by a previous build.
	 *
	 * Authority only. Abilities live on the ASC on this PlayerState rather than on the pawn, so the
	 * specs survive a respawn and there is nothing to re-grant when the pawn is recycled.
	 */
	void GrantPowersFromBuild(const FSpiritwebCharacterSave& Build);

	UPROPERTY(ReplicatedUsing = OnRep_SelectedBuild)
	FSpiritwebCharacterSave SelectedBuild;

	UPROPERTY(Replicated)
	bool bHasSelectedBuild = false;

	/** Running kill total. Also drives the inherited Score in Free-for-All. */
	UPROPERTY(Replicated)
	int32 Kills = 0;

	/** Running death total, for the scoreboard. */
	UPROPERTY(Replicated)
	int32 Deaths = 0;

	/**
	 * This player's side. Named MatchTeam rather than Team so it cannot collide with any team member
	 * a future engine version adds to APlayerState.
	 */
	UPROPERTY(Replicated)
	ECosmereTeam MatchTeam = ECosmereTeam::None;

	UPROPERTY(Replicated)
	bool bIsBotPlayer = false;
};
