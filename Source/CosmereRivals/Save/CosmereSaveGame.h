// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Data/CosmereTypes.h"
#include "CosmereSaveGame.generated.h"

/**
 * The player's local profile: wallet balances, store unlocks and saved character builds.
 * Serialized by UCosmereSaveSubsystem into the "CosmereSave" slot.
 *
 * Only PURCHASES are recorded in the Unlocked* arrays - rows flagged bUnlockedByDefault
 * in the catalog are treated as owned without being written here, so the starters can be
 * re-tuned in data without invalidating existing profiles.
 */
UCLASS(BlueprintType)
class COSMERERIVALS_API UCosmereSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** Bumped when the layout below changes so a future migration can detect old profiles. */
	UPROPERTY(BlueprintReadWrite, Category = "Cosmere|Save")
	int32 SaveVersion = 1;

	// ---- Wallet ------------------------------------------------------------

	/** Spiritweb currency. */
	UPROPERTY(BlueprintReadWrite, Category = "Cosmere|Wallet")
	int32 Nicrosil = 1000;

	/** Armory currency. */
	UPROPERTY(BlueprintReadWrite, Category = "Cosmere|Wallet")
	int32 Gold = 1500;

	/** Identity Store currency. */
	UPROPERTY(BlueprintReadWrite, Category = "Cosmere|Wallet")
	int32 Duralumin = 3500;

	// ---- Store unlocks -----------------------------------------------------

	UPROPERTY(BlueprintReadWrite, Category = "Cosmere|Unlocks")
	TArray<FName> UnlockedWeapons;

	UPROPERTY(BlueprintReadWrite, Category = "Cosmere|Unlocks")
	TArray<FName> UnlockedPowers;

	UPROPERTY(BlueprintReadWrite, Category = "Cosmere|Unlocks")
	TArray<FName> UnlockedRaces;

	UPROPERTY(BlueprintReadWrite, Category = "Cosmere|Unlocks")
	TArray<FName> UnlockedHeroes;

	UPROPERTY(BlueprintReadWrite, Category = "Cosmere|Unlocks")
	TArray<FName> UnlockedCosmetics;

	// ---- Character builds --------------------------------------------------

	UPROPERTY(BlueprintReadWrite, Category = "Cosmere|Character")
	TArray<FSpiritwebCharacterSave> Characters;

	/** Index into Characters, or INDEX_NONE when no build has been selected yet. */
	UPROPERTY(BlueprintReadWrite, Category = "Cosmere|Character")
	int32 SelectedCharacterIndex = INDEX_NONE;
};
