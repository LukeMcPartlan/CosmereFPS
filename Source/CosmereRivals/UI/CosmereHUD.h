// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CosmereHUD.generated.h"

class UCosmereMatchHUD;

/**
 * Creates the match HUD for each local player.
 *
 * This is assigned as HUDClass by ACosmereGameMode's constructor, which is how a GameMode hands a
 * per-player HUD to the engine. It is spawned per player controller rather than once per match, so a
 * listen server gets its own copy and each connected client would get theirs.
 */
UCLASS()
class COSMERERIVALS_API ACosmereHUD : public AHUD
{
	GENERATED_BODY()

public:
	ACosmereHUD();

	/** The local UI route owns this switch. Removing the widget stops its tick-based refresh. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|HUD")
	void SetGameplayHUDEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Cosmere|HUD")
	bool IsGameplayHUDEnabled() const { return bGameplayHUDEnabled; }

	//~ Begin AActor interface
	virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Draws a health bar over every idle bot, and over nothing else.
	 *
	 * This is the only thing this HUD draws itself - the player's own readouts are WBP_MatchHUD's business -
	 * and it exists because a practice-range bot is a target to shoot at rather than an opponent: without a
	 * bar there is nothing on screen that says a hit landed.
	 *
	 * Scoped to idle bots deliberately. A bar over an opponent that is actually fighting would be a
	 * wallhack, and a bar over the player's own pawn is noise in the crosshair, so neither is drawn.
	 */
	virtual void DrawHUD() override;
	//~ End AActor interface

protected:
	/** Creates at most one local gameplay widget, only while gameplay presentation is enabled. */
	void EnsureGameplayWidget();

	UPROPERTY(Transient)
	bool bGameplayHUDEnabled = true;

	/**
	 * The widget to create, held as a soft class.
	 *
	 * Deliberately not a ConstructorHelpers::FClassFinder: WBP_MatchHUD is a Blueprint, and resolving it
	 * here at spawn time means the Blueprint can be reparented onto UCosmereMatchHUD - or replaced
	 * outright - without this class needing to be recompiled. Mirrors the TSoftObjectPtr + FSoftObjectPath
	 * pattern UCosmereCatalogSubsystem already uses for its tables.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|HUD")
	TSoftClassPtr<UCosmereMatchHUD> HUDWidgetClass;

	/** The live widget, so it is not collected while it is on screen. */
	UPROPERTY(Transient)
	TObjectPtr<UCosmereMatchHUD> HUDWidget;
};
