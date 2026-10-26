// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CosmereUITypes.generated.h"

UENUM(BlueprintType)
enum class ECosmereUIRoute : uint8
{
	None, CharacterSelect, CharacterCreate, MainMenu, GameSelect, ShopHub,
	SpiritWeb, Armory, IdentityStore, Settings, ServerMenu, EscapeMenu, Gameplay
};

/** Resolved presentation, including a preset hero's effective kit and the shared wallet. */
USTRUCT(BlueprintType)
struct COSMERERIVALS_API FCosmereCharacterCardViewModel
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|UI")
	FGuid CharacterId;
	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|UI")
	FText CharacterName;
	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|UI")
	FText RaceName;
	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|UI")
	FText PrimaryWeaponName;
	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|UI")
	FText SecondaryWeaponName;
	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|UI")
	FText AlphaPowerName;
	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|UI")
	FText BetaPowerName;
	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|UI")
	FText HeroName;
	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|UI")
	int32 Nicrosil = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|UI")
	int32 Gold = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|UI")
	int32 Duralumin = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|UI")
	bool bIsHeroBuild = false;
	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|UI")
	bool bCatalogResolved = false;
};
