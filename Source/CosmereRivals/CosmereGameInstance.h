// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Net/CosmereMatchTypes.h"
#include "CosmereGameInstance.generated.h"

class UCosmereCatalogSubsystem;
class UCosmereSaveSubsystem;

/**
 * The game's GameInstance, registered as GameInstanceClass in DefaultEngine.ini.
 * It holds the two meta-game subsystems (static catalog data and the local profile) and
 * exposes them so Blueprint widgets never need to know how to resolve a subsystem by class.
 */
UCLASS(BlueprintType)
class COSMERERIVALS_API UCosmereGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	/** Races / weapons / powers / heroes read from the DT_* tables. */
	UFUNCTION(BlueprintPure, Category = "Cosmere")
	UCosmereCatalogSubsystem* GetCatalog() const;

	/** Wallet, unlocks and saved character builds. */
	UFUNCTION(BlueprintPure, Category = "Cosmere")
	UCosmereSaveSubsystem* GetSave() const;

	/**
	 * The host's match choices, written by the server menu before it travels and read by the match
	 * game mode once the match level is up.
	 *
	 * Deliberately here rather than on the game mode: the level travel rebuilds the game mode, so
	 * anything stored on it would be lost between the menu and the match. The GameInstance survives.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cosmere|Match")
	FCosmereMatchSettings PendingMatchSettings;
};
