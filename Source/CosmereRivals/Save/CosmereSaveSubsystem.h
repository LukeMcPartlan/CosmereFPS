// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Data/CosmereTypes.h"
#include "CosmereSaveSubsystem.generated.h"

class UCosmereCatalogSubsystem;
class UCosmereSaveGame;
class UGameInstance;

/** Fired once after a profile has been loaded or successfully committed to disk. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCosmereProfileChanged);

/**
 * Owns the local profile - wallet, store unlocks and saved character builds - and is the only
 * place that writes the save slot. Lives on the GameInstance so the front-end menus and the
 * match share one instance.
 *
 * Prices and starter flags are never duplicated here: they are read from UCosmereCatalogSubsystem
 * so tuning a DataTable row changes both the store UI and the validation behind it.
 */
UCLASS(BlueprintType)
class COSMERERIVALS_API UCosmereSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	//~ End USubsystem interface

	/** Loads the slot from disk, minting a fresh profile when none exists. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Save")
	bool LoadOrCreateSave();

	/** Writes the in-memory profile to the slot. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Save")
	bool SaveToDisk();

	/** Deletes the slot and starts a brand new profile. Backs the main menu's reset option. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Save")
	void ResetProfile();

	/** Why the most recent mutating call refused. Empty when the last call succeeded. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Save")
	FText GetLastError() const { return LastError; }

	/** Menus refresh from committed profile data, never from an attempted purchase or selection. */
	UPROPERTY(BlueprintAssignable, Category = "Cosmere|Save")
	FCosmereProfileChanged OnProfileChanged;

	// ---- Wallet ------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Cosmere|Wallet")
	int32 GetCurrency(ECosmereCurrency Currency) const;

	UFUNCTION(BlueprintCallable, Category = "Cosmere|Wallet")
	void AddCurrency(ECosmereCurrency Currency, int32 Amount);

	/** Spends Amount, or refuses and leaves the wallet untouched. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Wallet")
	bool TrySpendCurrency(ECosmereCurrency Currency, int32 Amount);

	// ---- Unlocks -----------------------------------------------------------

	/** Catalog rows flagged bUnlockedByDefault count as unlocked without being stored. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Unlocks")
	bool IsWeaponUnlocked(FName WeaponId) const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Unlocks")
	bool IsPowerUnlocked(FName PowerId) const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Unlocks")
	bool IsRaceUnlocked(FName RaceId) const;

	/** Prebuilt heroes have no starter flag, so only an explicit purchase unlocks one. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Unlocks")
	bool IsHeroUnlocked(FName HeroId) const;

	/**
	 * The ids the player may currently pick, in catalog row order. The Spiritweb, Armory and
	 * Identity Store all page through these, and a menu combo box hands back an index into this
	 * list, so the index -> row name mapping lives here and nowhere else.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Unlocks")
	TArray<FName> GetUnlockedRaceIds() const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Unlocks")
	TArray<FName> GetUnlockedWeaponIds() const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Unlocks")
	TArray<FName> GetUnlockedPowerIds() const;

	// ---- Store purchases ---------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Cosmere|Store")
	bool PurchaseWeapon(FName WeaponId);

	UFUNCTION(BlueprintCallable, Category = "Cosmere|Store")
	bool PurchasePower(FName PowerId);

	UFUNCTION(BlueprintCallable, Category = "Cosmere|Store")
	bool PurchaseRace(FName RaceId);

	UFUNCTION(BlueprintCallable, Category = "Cosmere|Store")
	bool PurchaseHero(FName HeroId);

	// ---- Character builds --------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Cosmere|Character")
	TArray<FSpiritwebCharacterSave> GetCharacters() const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Character")
	bool GetCharacter(int32 CharacterIndex, FSpiritwebCharacterSave& OutCharacter) const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Character")
	int32 GetSelectedCharacterIndex() const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Character")
	bool GetSelectedCharacter(FSpiritwebCharacterSave& OutCharacter) const;

	/** INDEX_NONE clears the selection; any other value must address an existing build. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Character")
	bool SetSelectedCharacterIndex(int32 CharacterIndex);

	/**
	 * Validates one unlocked race, two distinct unlocked weapons and two distinct unlocked
	 * powers; on success appends the build without changing the confirmed selection.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Character")
	bool CreateCharacter(const FText& CharacterName, FName RaceId, const TArray<FName>& WeaponIds, const TArray<FName>& PowerIds);

	/**
	 * Creates a build from GetUnlocked*Ids() list indices instead of row names, so a menu can pass
	 * raw combo-box indices straight through. Any -1 or out-of-range index refuses the creation,
	 * and GetLastError() says which slot was incomplete.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Character")
	bool CreateCharacterFromSelection(const FText& CharacterName, int32 RaceIndex,
		int32 PrimaryWeaponIndex, int32 SecondaryWeaponIndex, int32 AlphaPowerIndex, int32 BetaPowerIndex);

	UFUNCTION(BlueprintCallable, Category = "Cosmere|Character")
	bool EquipWeapon(int32 CharacterIndex, EWeaponSlot Slot, FName WeaponId);

	UFUNCTION(BlueprintCallable, Category = "Cosmere|Character")
	bool EquipPower(int32 CharacterIndex, EPowerSlot Slot, FName PowerId);
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Character")
	bool EquipRace(int32 CharacterIndex, FName RaceId);

#if !UE_BUILD_SHIPPING
	/** Separate unregistered object; seed is copied, never loads/resets the real profile. */
	static UCosmereSaveSubsystem* CreateDevelopmentFixture(UGameInstance* Owner,
		const UCosmereSaveGame* Seed, UCosmereCatalogSubsystem* FixtureCatalog);
	void FailNextDevelopmentCommit() { if (bDevelopmentFixture) bFailNextCommit = true; }
	int32 GetDevelopmentSaveAttempts() const { return DevelopmentSaveAttempts; }
	int32 GetDevelopmentNotifications() const { return DevelopmentNotifications; }
	FString GetDevelopmentSlotName() const { return bDevelopmentFixture ? SaveSlotName : FString(); }
	UCosmereCatalogSubsystem* GetDevelopmentCatalog() const { return bDevelopmentFixture ? DevelopmentCatalog.Get() : nullptr; }
	bool ReloadDevelopmentFixture();
#endif

	/** Selects a prebuilt hero. The hand-built kit is kept underneath so ClearHero restores it. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Character")
	bool EquipHero(int32 CharacterIndex, FName HeroId);

	UFUNCTION(BlueprintCallable, Category = "Cosmere|Character")
	bool ClearHero(int32 CharacterIndex);

	UFUNCTION(BlueprintCallable, Category = "Cosmere|Character")
	bool DeleteCharacter(int32 CharacterIndex);

protected:
	/** Slot holding this machine's profile. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Save")
	FString SaveSlotName = TEXT("CosmereSave");

	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Save")
	int32 SaveUserIndex = 0;

private:
	bool EnsureSaveLoaded();

	/** Debits the wallet without writing to disk, so a compound purchase saves once. */
	bool SpendCurrency(ECosmereCurrency Currency, int32 Amount);

	void SetError(const FText& InError);

	UCosmereCatalogSubsystem* GetCatalog() const;

	bool ValidateSlotIndex(int32 CharacterIndex) const;

	bool ValidateNewCharacter(FName RaceId, const TArray<FName>& WeaponIds, const TArray<FName>& PowerIds, FText& OutError) const;

	UPROPERTY(Transient)
	TObjectPtr<UCosmereSaveGame> SaveData = nullptr;

	/** In-memory copy only; restores all compound mutations if the slot write fails. */
	UPROPERTY(Transient)
	TObjectPtr<UCosmereSaveGame> LastCommittedData = nullptr;

	UPROPERTY(Transient)
	FText LastError;
	UPROPERTY(Transient)
	TObjectPtr<UCosmereCatalogSubsystem> DevelopmentCatalog;
	bool bDevelopmentFixture = false;
#if !UE_BUILD_SHIPPING
	bool bFailNextCommit = false;
	int32 DevelopmentSaveAttempts = 0;
	int32 DevelopmentNotifications = 0;
#endif
};
