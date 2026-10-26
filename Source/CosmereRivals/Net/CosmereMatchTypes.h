// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CosmereMatchTypes.generated.h"

/**
 * Which rule set a session is running.
 *
 * FreeForAll is the deathmatch mode: every player is on their own side, kills score, first to the
 * limit wins. KingOfTheHill is the 6v6 mode: players are split across two sides and the score comes
 * from holding the hill rather than from kills.
 */
UENUM(BlueprintType)
enum class ECosmereMatchMode : uint8
{
	FreeForAll		UMETA(DisplayName = "Free-for-All Deathmatch"),
	KingOfTheHill	UMETA(DisplayName = "King of the Hill (6v6)")
};

/** A player's side. Free-for-All leaves every player on None, which is also the spectator value. */
UENUM(BlueprintType)
enum class ECosmereTeam : uint8
{
	None	UMETA(DisplayName = "None"),
	TeamA	UMETA(DisplayName = "Team A"),
	TeamB	UMETA(DisplayName = "Team B")
};

/** Where a match is in its lifecycle. Warmup and PostMatch both mean "do not score". */
UENUM(BlueprintType)
enum class ECosmereMatchPhase : uint8
{
	Warmup		UMETA(DisplayName = "Warmup"),
	InProgress	UMETA(DisplayName = "In Progress"),
	PostMatch	UMETA(DisplayName = "Post-Match")
};

/**
 * The choices a host makes before travelling into a match.
 *
 * Deliberately held on the GameInstance rather than on the game mode. On a listen server the game
 * mode is rebuilt by the level travel, so anything stored on it is lost between the menu and the
 * match; the GameInstance is the one object that survives the travel, which is what lets the host's
 * bot toggle still be readable once the match level is up.
 */
USTRUCT(BlueprintType)
struct FCosmereMatchSettings
{
	GENERATED_BODY()

	/** Which rule set to run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match")
	ECosmereMatchMode Mode = ECosmereMatchMode::FreeForAll;

	/**
	 * The outright win score.
	 * Free-for-All: kills a single player needs to win.
	 * King of the Hill: team points a side needs to win.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match")
	int32 ScoreLimit = 30;

	/**
	 * Match length in seconds. When it expires the match ends and whoever leads at that moment wins;
	 * a tie is reported as no winner rather than being broken arbitrarily.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match")
	float MatchDuration = 600.f;

	/** The host's bot toggle. False means no AI opponents are created at all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match")
	bool bBotsEnabled = true;
};
