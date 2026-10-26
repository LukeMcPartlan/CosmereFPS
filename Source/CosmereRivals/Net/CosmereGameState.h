// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Net/CosmereMatchTypes.h"
#include "CosmereGameState.generated.h"

class USoundAttenuation;
class USoundBase;
/**
 * The replicated scoreboard and clock for one match.
 *
 * Everything the HUD and the post-match screen need reads from here rather than from the game mode,
 * because the game mode exists only on the server while the GameState reaches every client.
 *
 * The clock is deliberately not a replicated countdown. MatchEndTime replicates once as a server
 * world time and every machine subtracts its own GetServerWorldTimeSeconds(), so the timer advances
 * smoothly between replications instead of stepping once per net update.
 */
UCLASS()
class COSMERERIVALS_API ACosmereGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ACosmereGameState();

	//~ Begin AActor interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor interface

	//~ Begin match state. These read on the server and on every client.
	UFUNCTION(BlueprintPure, Category = "Cosmere|Match")
	ECosmereMatchMode GetMatchMode() const { return MatchMode; }

	UFUNCTION(BlueprintPure, Category = "Cosmere|Match")
	ECosmereMatchPhase GetMatchPhase() const { return MatchPhase; }

	/** Whole seconds left on the clock, or 0 when no clock is running. Never negative. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Match")
	int32 GetTimeRemainingSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Match")
	int32 GetScoreLimit() const { return ScoreLimit; }

	/** 0 for an unknown or non-team value, so this is always safe to call from a HUD. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Match")
	int32 GetTeamScore(ECosmereTeam Team) const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Match")
	ECosmereTeam GetWinningTeam() const { return WinningTeam; }

	/**
	 * The individual winner's name. Empty when a side won, and empty on a draw.
	 *
	 * Free-for-All has no team to name, so without this a deathmatch result could only ever report
	 * "nobody", even though the scoreboard behind it plainly shows who won.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Match")
	FString GetWinningPlayerName() const { return WinningPlayerName; }

	UFUNCTION(BlueprintPure, Category = "Cosmere|Match")
	bool AreBotsEnabled() const { return bBotsEnabled; }
	//~ End match state

	//~ Begin sound perception

	/**
	 * Puts one noise on every machine, with the sound that made it.
	 *
	 * The single funnel every noise in a match passes through, because a noise is by definition something
	 * everyone may hear: a footstep is simulated on the walker's own client and a shot is fired on the
	 * authority, and neither of those reaches the other machines on its own. Authority only.
	 */
	void ReportNoise(AActor* Source, FVector Location, USoundBase* Sound, float Loudness);

	/** Plays Sound at Location and offers Source to every local listener as a heard noise. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastNoise(AActor* Source, FVector Location, USoundBase* Sound, float Loudness);

	/**
	 * Hands one seer's bronze sweep to that seer's machine, and to nobody else's.
	 *
	 * Bronze is the one sense a client cannot work out for itself: whether a pawn is burning is a number on
	 * that pawn's own ability instance, and an instance only exists on its owner's machine. The authority does
	 * hold every level, so it does the looking and sends back what it found - the same funnel ReportNoise uses,
	 * because it is the same shape of problem, a fact only one machine can see that another machine needs.
	 *
	 * Broadcast rather than aimed at one connection, because the seer is identified by the actor it already
	 * is: every client is told and all but one ignore it, exactly as a noise is offered to every ear.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBronzeReveal(AActor* Seer, const TArray<AActor*>& Burners, float Duration);

	//~ End sound perception

	//~ Begin authority-only mutators. Calling these on a client will not replicate.
	void SetMatchMode(ECosmereMatchMode InMode);
	void SetMatchPhase(ECosmereMatchPhase InPhase);
	void SetScoreLimit(int32 InLimit);
	void SetBotsEnabled(bool bInEnabled);

	/** Starts the clock. A non-positive DurationSeconds clears it instead of starting one. */
	void SetMatchDuration(float DurationSeconds);

	/** Adds Delta to a side's score. Inert for ECosmereTeam::None. */
	void AddTeamScore(ECosmereTeam Team, int32 Delta);

	void SetWinningTeam(ECosmereTeam InTeam);

	/** Pass an empty string for a side win or a draw. */
	void SetWinningPlayerName(const FString& InName);
	//~ End authority-only mutators

protected:
	/**
	 * Sides that carry a score. TeamScores is always kept at this length so that AddTeamScore can
	 * index it without a validity branch, and so a client that has not yet received the array still
	 * reads 0 rather than crashing.
	 */
	static constexpr int32 NumTeams = 2;

	UPROPERTY(Replicated)
	ECosmereMatchMode MatchMode = ECosmereMatchMode::FreeForAll;

	UPROPERTY(Replicated)
	ECosmereMatchPhase MatchPhase = ECosmereMatchPhase::Warmup;

	/** Server world time the match ends at. Zero means no clock is running. */
	UPROPERTY(Replicated)
	float MatchEndTime = 0.f;

	UPROPERTY(Replicated)
	int32 ScoreLimit = 30;

	/** Index 0 is TeamA and index 1 is TeamB. */
	UPROPERTY(Replicated)
	TArray<int32> TeamScores;

	UPROPERTY(Replicated)
	ECosmereTeam WinningTeam = ECosmereTeam::None;
	
	/** Empty unless an individual won, which is the Free-for-All case. */
	UPROPERTY(Replicated)
	FString WinningPlayerName;

	UPROPERTY(Replicated)
	bool bBotsEnabled = true;

	/**
	 * Attenuation for the noises that carry a sound, built once at runtime.
	 *
	 * There is no asset to point at - a noise arrives as a bare USoundBase and has to be spatialised on the
	 * spot - so the shape lives here instead. The falloff is long enough to cover a fully flared burner's
	 * hearing, which is the widest ear in the game.
	 */
	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> NoiseAttenuation;

	/** Builds NoiseAttenuation on first use. */
	USoundAttenuation* GetNoiseAttenuation();
};
