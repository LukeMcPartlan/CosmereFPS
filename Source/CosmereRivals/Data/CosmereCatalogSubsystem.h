// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Data/CosmereTypes.h"
#include "CosmereCatalogSubsystem.generated.h"

class UDataTable;

/**
 * Loads and validates the game's static catalogs (races, weapons, powers, prebuilt heroes).
 * Lives on the GameInstance so the front-end menus and the match can both resolve row lookups.
 */
UCLASS(BlueprintType)
class COSMERERIVALS_API UCosmereCatalogSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UCosmereCatalogSubsystem();

	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

#if !UE_BUILD_SHIPPING
	/** Copies supplied tables into transient caches, without registering or initializing a subsystem. */
	static UCosmereCatalogSubsystem* CreateDevelopmentFixture(UGameInstance* Owner,
		const UDataTable* Races, const UDataTable* Weapons, const UDataTable* Powers, const UDataTable* Heroes);
	bool IsDevelopmentFixture() const { return bDevelopmentFixture; }
	/** Mutable fixture overlay only; never returns a production table. */
	UDataTable* GetDevelopmentTable(FName Kind) const;
#endif
	//~ End USubsystem interface

	/** Loads every catalog table and logs a summary. Safe to call again after an asset reload. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Catalog")
	void LoadCatalog();

	/** True once every catalog table resolved to a valid DataTable. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Catalog")
	bool IsCatalogReady() const { return RaceTableCache && WeaponTableCache && PowerTableCache && HeroTableCache; }

	UFUNCTION(BlueprintPure, Category = "Cosmere|Catalog")
	bool GetRaceRow(FName RaceId, FRaceDefinition& OutRace) const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Catalog")
	bool GetWeaponRow(FName WeaponId, FWeaponDefinition& OutWeapon) const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Catalog")
	bool GetPowerRow(FName PowerId, FPowerDefinition& OutPower) const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Catalog")
	bool GetHeroRow(FName HeroId, FHeroDefinition& OutHero) const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Catalog")
	TArray<FName> GetAllRaceIds() const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Catalog")
	TArray<FName> GetAllWeaponIds() const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Catalog")
	TArray<FName> GetAllPowerIds() const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Catalog")
	TArray<FName> GetAllHeroIds() const;

	/** Powers of one magic system, used to page the Spiritweb catalog. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Catalog")
	TArray<FName> GetPowerIdsBySource(EPowerSource Source) const;

	/** Races selectable at character creation without spending Duralumin. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Catalog")
	TArray<FName> GetDefaultUnlockedRaceIds() const;

	/** Weapons owned by a brand new character. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Catalog")
	TArray<FName> GetStarterWeaponIds() const;

	/** Powers owned by a brand new character. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Catalog")
	TArray<FName> GetStarterPowerIds() const;

protected:
	/** DT_Races - row name is the RaceId. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Catalog")
	TSoftObjectPtr<UDataTable> RaceTable;

	/** DT_Weapons - row name is the WeaponId. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Catalog")
	TSoftObjectPtr<UDataTable> WeaponTable;

	/** DT_Powers - row name is the PowerId. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Catalog")
	TSoftObjectPtr<UDataTable> PowerTable;

	/** DT_Heroes - row name is the HeroId. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Catalog")
	TSoftObjectPtr<UDataTable> HeroTable;

private:
	UPROPERTY(Transient)
	TObjectPtr<UDataTable> RaceTableCache = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> WeaponTableCache = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> PowerTableCache = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> HeroTableCache = nullptr;
	bool bDevelopmentFixture = false;
};
