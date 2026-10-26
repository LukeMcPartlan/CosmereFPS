// Copyright Epic Games, Inc. All Rights Reserved.

#include "Save/CosmereSaveSubsystem.h"

#include "CosmereRivals.h"
#include "Data/CosmereCatalogSubsystem.h"
#include "Save/CosmereSaveGame.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	/** Player-facing name for a wallet balance, used in refusal messages. */
	FString CurrencyLabel(ECosmereCurrency Currency)
	{
		switch (Currency)
		{
		case ECosmereCurrency::Nicrosil:  return TEXT("Nicrosil");
		case ECosmereCurrency::Gold:      return TEXT("Gold");
		case ECosmereCurrency::Duralumin: return TEXT("Duralumin");
		default:                          return TEXT("currency");
		}
	}
}

void UCosmereSaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Every front-end menu reads the profile immediately, so resolve it up front rather than
	// lazily on first access.
	LoadOrCreateSave();
}

UCosmereCatalogSubsystem* UCosmereSaveSubsystem::GetCatalog() const
{
	if (bDevelopmentFixture) return DevelopmentCatalog;
	UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UCosmereCatalogSubsystem>() : nullptr;
}

void UCosmereSaveSubsystem::SetError(const FText& InError)
{
	LastError = InError;

	if (!InError.IsEmpty())
	{
		UE_LOG(LogCosmereUI, Warning, TEXT("Save: %s"), *InError.ToString());
	}
}

bool UCosmereSaveSubsystem::LoadOrCreateSave()
{
	if (bDevelopmentFixture && !SaveSlotName.StartsWith(TEXT("AuraUITest_"))) return false;
	if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, SaveUserIndex))
	{
		UCosmereSaveGame* Loaded = Cast<UCosmereSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, SaveUserIndex));

		if (Loaded)
		{
			SaveData = Loaded;
			LastCommittedData = DuplicateObject<UCosmereSaveGame>(Loaded, this);
			SetError(FText::GetEmpty());
			UE_LOG(LogCosmere, Log, TEXT("Save: loaded '%s' - %d character(s), %d weapon unlock(s), %d power unlock(s)."),
				*SaveSlotName, SaveData->Characters.Num(), SaveData->UnlockedWeapons.Num(), SaveData->UnlockedPowers.Num());
#if !UE_BUILD_SHIPPING
			if (bDevelopmentFixture) ++DevelopmentNotifications;
#endif
			OnProfileChanged.Broadcast();
			return true;
		}

		// Never turn a failed read into a destructive write of starter balances over a real profile.
		SetError(FText::FromString(TEXT("The existing profile could not be loaded. It has not been overwritten.")));
		return false;
	}

	SaveData = Cast<UCosmereSaveGame>(UGameplayStatics::CreateSaveGameObject(UCosmereSaveGame::StaticClass()));

	if (!SaveData)
	{
		SetError(FText::FromString(TEXT("Could not create a local profile.")));
		return false;
	}

	UE_LOG(LogCosmere, Log, TEXT("Save: created a new profile in '%s'."), *SaveSlotName);
	return SaveToDisk();
}

bool UCosmereSaveSubsystem::EnsureSaveLoaded()
{
	return SaveData ? true : LoadOrCreateSave();
}

bool UCosmereSaveSubsystem::SaveToDisk()
{
	if (!SaveData)
	{
		SetError(FText::FromString(TEXT("There is no profile to save.")));
		return false;
	}

	if (bDevelopmentFixture && !SaveSlotName.StartsWith(TEXT("AuraUITest_"))) return false;
	bool bInjectedFailure = false;
#if !UE_BUILD_SHIPPING
	if (bDevelopmentFixture)
	{
		++DevelopmentSaveAttempts;
		bInjectedFailure = bFailNextCommit;
		bFailNextCommit = false;
	}
#endif
	if (bInjectedFailure || !UGameplayStatics::SaveGameToSlot(SaveData, SaveSlotName, SaveUserIndex))
	{
		SaveData = LastCommittedData
			? DuplicateObject<UCosmereSaveGame>(LastCommittedData.Get(), this) : nullptr;
		SetError(FText::FromString(TEXT("Writing the save slot failed.")));
		UE_LOG(LogCosmereUI, Warning, TEXT("Profile commit failed; in-memory changes rolled back and no change notification sent."));
		return false;
	}

	LastCommittedData = DuplicateObject<UCosmereSaveGame>(SaveData.Get(), this);
	SetError(FText::GetEmpty());
#if !UE_BUILD_SHIPPING
	if (bDevelopmentFixture) ++DevelopmentNotifications;
#endif
	OnProfileChanged.Broadcast();
	return true;
}

void UCosmereSaveSubsystem::ResetProfile()
{
	if (bDevelopmentFixture && !SaveSlotName.StartsWith(TEXT("AuraUITest_"))) return;
	if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, SaveUserIndex)
		&& !UGameplayStatics::DeleteGameInSlot(SaveSlotName, SaveUserIndex))
	{
		SetError(FText::FromString(TEXT("The profile could not be reset; the existing slot was retained.")));
		return;
	}
	SaveData = nullptr;
	LastCommittedData = nullptr;
	if (!LoadOrCreateSave()) return;

	UE_LOG(LogCosmere, Log, TEXT("Save: profile reset - slot '%s' deleted and recreated."), *SaveSlotName);
}

// ---------------------------------------------------------------------------
// Wallet
// ---------------------------------------------------------------------------

int32 UCosmereSaveSubsystem::GetCurrency(ECosmereCurrency Currency) const
{
	if (!SaveData)
	{
		return 0;
	}

	switch (Currency)
	{
	case ECosmereCurrency::Nicrosil:  return SaveData->Nicrosil;
	case ECosmereCurrency::Gold:      return SaveData->Gold;
	case ECosmereCurrency::Duralumin: return SaveData->Duralumin;
	default:                          return 0;
	}
}

void UCosmereSaveSubsystem::AddCurrency(ECosmereCurrency Currency, int32 Amount)
{
	SetError(FText::GetEmpty());

	if (Amount == 0 || !EnsureSaveLoaded())
	{
		return;
	}

	switch (Currency)
	{
	case ECosmereCurrency::Nicrosil:  SaveData->Nicrosil  = FMath::Max(0, SaveData->Nicrosil + Amount);  break;
	case ECosmereCurrency::Gold:      SaveData->Gold      = FMath::Max(0, SaveData->Gold + Amount);      break;
	case ECosmereCurrency::Duralumin: SaveData->Duralumin = FMath::Max(0, SaveData->Duralumin + Amount); break;
	default: break;
	}

	SaveToDisk();
}

bool UCosmereSaveSubsystem::SpendCurrency(ECosmereCurrency Currency, int32 Amount)
{
	if (!EnsureSaveLoaded())
	{
		return false;
	}

	if (Amount <= 0)
	{
		return true;
	}

	if (GetCurrency(Currency) < Amount)
	{
		return false;
	}

	switch (Currency)
	{
	case ECosmereCurrency::Nicrosil:  SaveData->Nicrosil  -= Amount; break;
	case ECosmereCurrency::Gold:      SaveData->Gold      -= Amount; break;
	case ECosmereCurrency::Duralumin: SaveData->Duralumin -= Amount; break;
	default: break;
	}

	return true;
}

bool UCosmereSaveSubsystem::TrySpendCurrency(ECosmereCurrency Currency, int32 Amount)
{
	SetError(FText::GetEmpty());

	if (!EnsureSaveLoaded())
	{
		return false;
	}

	if (!SpendCurrency(Currency, Amount))
	{
		SetError(FText::FromString(FString::Printf(TEXT("Not enough %s: %d needed, %d held."),
			*CurrencyLabel(Currency), Amount, GetCurrency(Currency))));
		return false;
	}

	return SaveToDisk();
}

// ---------------------------------------------------------------------------
// Unlocks
// ---------------------------------------------------------------------------

bool UCosmereSaveSubsystem::IsWeaponUnlocked(FName WeaponId) const
{
	if (WeaponId.IsNone())
	{
		return false;
	}

	if (SaveData && SaveData->UnlockedWeapons.Contains(WeaponId))
	{
		return true;
	}

	const UCosmereCatalogSubsystem* Catalog = GetCatalog();
	FWeaponDefinition Row;
	return Catalog && Catalog->GetWeaponRow(WeaponId, Row) && Row.bUnlockedByDefault;
}

bool UCosmereSaveSubsystem::IsPowerUnlocked(FName PowerId) const
{
	if (PowerId.IsNone())
	{
		return false;
	}

	if (SaveData && SaveData->UnlockedPowers.Contains(PowerId))
	{
		return true;
	}

	const UCosmereCatalogSubsystem* Catalog = GetCatalog();
	FPowerDefinition Row;
	return Catalog && Catalog->GetPowerRow(PowerId, Row) && Row.bUnlockedByDefault;
}

bool UCosmereSaveSubsystem::IsRaceUnlocked(FName RaceId) const
{
	if (RaceId.IsNone())
	{
		return false;
	}

	if (SaveData && SaveData->UnlockedRaces.Contains(RaceId))
	{
		return true;
	}

	const UCosmereCatalogSubsystem* Catalog = GetCatalog();
	FRaceDefinition Row;
	return Catalog && Catalog->GetRaceRow(RaceId, Row) && Row.bUnlockedByDefault;
}

bool UCosmereSaveSubsystem::IsHeroUnlocked(FName HeroId) const
{
	if (HeroId.IsNone())
	{
		return false;
	}

	return SaveData && SaveData->UnlockedHeroes.Contains(HeroId);
}

// ---------------------------------------------------------------------------
// Store purchases
// ---------------------------------------------------------------------------

bool UCosmereSaveSubsystem::PurchaseWeapon(FName WeaponId)
{
	SetError(FText::GetEmpty());

	if (!EnsureSaveLoaded())
	{
		return false;
	}

	const UCosmereCatalogSubsystem* Catalog = GetCatalog();
	FWeaponDefinition Row;
	if (!Catalog || !Catalog->GetWeaponRow(WeaponId, Row))
	{
		SetError(FText::FromString(TEXT("That weapon doesn't exist in the Armory.")));
		return false;
	}

	if (IsWeaponUnlocked(WeaponId))
	{
		SetError(FText::FromString(FString::Printf(TEXT("%s is already unlocked."), *Row.DisplayName.ToString())));
		return false;
	}

	if (!SpendCurrency(ECosmereCurrency::Gold, Row.GoldCost))
	{
		SetError(FText::FromString(FString::Printf(TEXT("%s costs %d Gold and you have %d."),
			*Row.DisplayName.ToString(), Row.GoldCost, GetCurrency(ECosmereCurrency::Gold))));
		return false;
	}

	SaveData->UnlockedWeapons.Add(WeaponId);
	if (!SaveToDisk()) return false;

	UE_LOG(LogCosmere, Log, TEXT("Armory: unlocked %s for %d Gold."), *WeaponId.ToString(), Row.GoldCost);
	return true;
}

bool UCosmereSaveSubsystem::PurchasePower(FName PowerId)
{
	SetError(FText::GetEmpty());

	if (!EnsureSaveLoaded())
	{
		return false;
	}

	const UCosmereCatalogSubsystem* Catalog = GetCatalog();
	FPowerDefinition Row;
	if (!Catalog || !Catalog->GetPowerRow(PowerId, Row))
	{
		SetError(FText::FromString(TEXT("That power doesn't exist in the Spiritweb.")));
		return false;
	}

	if (IsPowerUnlocked(PowerId))
	{
		SetError(FText::FromString(FString::Printf(TEXT("%s is already unlocked."), *Row.DisplayName.ToString())));
		return false;
	}

	if (!SpendCurrency(ECosmereCurrency::Nicrosil, Row.NicrosilCost))
	{
		SetError(FText::FromString(FString::Printf(TEXT("%s costs %d Nicrosil and you have %d."),
			*Row.DisplayName.ToString(), Row.NicrosilCost, GetCurrency(ECosmereCurrency::Nicrosil))));
		return false;
	}

	SaveData->UnlockedPowers.Add(PowerId);
	if (!SaveToDisk()) return false;

	UE_LOG(LogCosmere, Log, TEXT("Spiritweb: unlocked %s for %d Nicrosil."), *PowerId.ToString(), Row.NicrosilCost);
	return true;
}

bool UCosmereSaveSubsystem::PurchaseRace(FName RaceId)
{
	SetError(FText::GetEmpty());

	if (!EnsureSaveLoaded())
	{
		return false;
	}

	const UCosmereCatalogSubsystem* Catalog = GetCatalog();
	FRaceDefinition Row;
	if (!Catalog || !Catalog->GetRaceRow(RaceId, Row))
	{
		SetError(FText::FromString(TEXT("That race doesn't exist in the Identity Store.")));
		return false;
	}

	if (IsRaceUnlocked(RaceId))
	{
		SetError(FText::FromString(FString::Printf(TEXT("%s is already unlocked."), *Row.DisplayName.ToString())));
		return false;
	}

	if (!SpendCurrency(ECosmereCurrency::Duralumin, Row.DuraluminCost))
	{
		SetError(FText::FromString(FString::Printf(TEXT("%s costs %d Duralumin and you have %d."),
			*Row.DisplayName.ToString(), Row.DuraluminCost, GetCurrency(ECosmereCurrency::Duralumin))));
		return false;
	}

	SaveData->UnlockedRaces.Add(RaceId);
	if (!SaveToDisk()) return false;

	UE_LOG(LogCosmere, Log, TEXT("Identity Store: unlocked race %s for %d Duralumin."), *RaceId.ToString(), Row.DuraluminCost);
	return true;
}

bool UCosmereSaveSubsystem::PurchaseHero(FName HeroId)
{
	SetError(FText::GetEmpty());

	if (!EnsureSaveLoaded())
	{
		return false;
	}

	const UCosmereCatalogSubsystem* Catalog = GetCatalog();
	FHeroDefinition Row;
	if (!Catalog || !Catalog->GetHeroRow(HeroId, Row))
	{
		SetError(FText::FromString(TEXT("That hero doesn't exist in the Identity Store.")));
		return false;
	}

	if (IsHeroUnlocked(HeroId))
	{
		SetError(FText::FromString(FString::Printf(TEXT("%s is already unlocked."), *Row.DisplayName.ToString())));
		return false;
	}

	if (!SpendCurrency(ECosmereCurrency::Duralumin, Row.DuraluminCost))
	{
		SetError(FText::FromString(FString::Printf(TEXT("%s costs %d Duralumin and you have %d."),
			*Row.DisplayName.ToString(), Row.DuraluminCost, GetCurrency(ECosmereCurrency::Duralumin))));
		return false;
	}

	SaveData->UnlockedHeroes.Add(HeroId);
	if (!SaveToDisk()) return false;

	UE_LOG(LogCosmere, Log, TEXT("Identity Store: unlocked hero %s for %d Duralumin."), *HeroId.ToString(), Row.DuraluminCost);
	return true;
}

// ---------------------------------------------------------------------------
// Character builds
// ---------------------------------------------------------------------------

TArray<FSpiritwebCharacterSave> UCosmereSaveSubsystem::GetCharacters() const
{
	return SaveData ? SaveData->Characters : TArray<FSpiritwebCharacterSave>();
}

bool UCosmereSaveSubsystem::GetCharacter(int32 CharacterIndex, FSpiritwebCharacterSave& OutCharacter) const
{
	if (!SaveData || !SaveData->Characters.IsValidIndex(CharacterIndex))
	{
		return false;
	}

	OutCharacter = SaveData->Characters[CharacterIndex];
	return true;
}

int32 UCosmereSaveSubsystem::GetSelectedCharacterIndex() const
{
	if (!SaveData)
	{
		return INDEX_NONE;
	}

	return SaveData->Characters.IsValidIndex(SaveData->SelectedCharacterIndex) ? SaveData->SelectedCharacterIndex : INDEX_NONE;
}

bool UCosmereSaveSubsystem::GetSelectedCharacter(FSpiritwebCharacterSave& OutCharacter) const
{
	return GetCharacter(GetSelectedCharacterIndex(), OutCharacter);
}

bool UCosmereSaveSubsystem::ValidateSlotIndex(int32 CharacterIndex) const
{
	return SaveData && SaveData->Characters.IsValidIndex(CharacterIndex);
}

bool UCosmereSaveSubsystem::SetSelectedCharacterIndex(int32 CharacterIndex)
{
	SetError(FText::GetEmpty());

	if (!EnsureSaveLoaded())
	{
		return false;
	}

	if (CharacterIndex != INDEX_NONE && !ValidateSlotIndex(CharacterIndex))
	{
		SetError(FText::FromString(TEXT("That character no longer exists.")));
		return false;
	}

	if (SaveData->SelectedCharacterIndex == CharacterIndex) return true;
	SaveData->SelectedCharacterIndex = CharacterIndex;
	return SaveToDisk();
}

bool UCosmereSaveSubsystem::ValidateNewCharacter(FName RaceId, const TArray<FName>& WeaponIds, const TArray<FName>& PowerIds, FText& OutError) const
{
	const UCosmereCatalogSubsystem* Catalog = GetCatalog();
	if (!Catalog || !Catalog->IsCatalogReady())
	{
		OutError = FText::FromString(TEXT("The catalog hasn't finished loading."));
		return false;
	}

	// One race, unlocked.
	FRaceDefinition RaceRow;
	if (!Catalog->GetRaceRow(RaceId, RaceRow))
	{
		OutError = FText::FromString(TEXT("Pick a race for this character."));
		return false;
	}

	if (!IsRaceUnlocked(RaceId))
	{
		OutError = FText::FromString(FString::Printf(TEXT("%s is still locked in the Identity Store."), *RaceRow.DisplayName.ToString()));
		return false;
	}

	// Two distinct weapons, both unlocked.
	if (WeaponIds.Num() != 2)
	{
		OutError = FText::FromString(TEXT("A character carries exactly two weapons."));
		return false;
	}

	if (WeaponIds[0] == WeaponIds[1])
	{
		OutError = FText::FromString(TEXT("The two weapon slots must hold different weapons."));
		return false;
	}

	for (const FName& WeaponId : WeaponIds)
	{
		FWeaponDefinition WeaponRow;
		if (!Catalog->GetWeaponRow(WeaponId, WeaponRow))
		{
			OutError = FText::FromString(FString::Printf(TEXT("Unknown weapon '%s'."), *WeaponId.ToString()));
			return false;
		}

		if (!IsWeaponUnlocked(WeaponId))
		{
			OutError = FText::FromString(FString::Printf(TEXT("%s is still locked in the Armory."), *WeaponRow.DisplayName.ToString()));
			return false;
		}
	}

	// Two distinct powers, both unlocked.
	if (PowerIds.Num() != 2)
	{
		OutError = FText::FromString(TEXT("A character equips exactly two powers."));
		return false;
	}

	if (PowerIds[0] == PowerIds[1])
	{
		OutError = FText::FromString(TEXT("The two power slots must hold different powers."));
		return false;
	}

	for (const FName& PowerId : PowerIds)
	{
		FPowerDefinition PowerRow;
		if (!Catalog->GetPowerRow(PowerId, PowerRow))
		{
			OutError = FText::FromString(FString::Printf(TEXT("Unknown power '%s'."), *PowerId.ToString()));
			return false;
		}

		if (!IsPowerUnlocked(PowerId))
		{
			OutError = FText::FromString(FString::Printf(TEXT("%s is still locked in the Spiritweb."), *PowerRow.DisplayName.ToString()));
			return false;
		}
	}

	OutError = FText::GetEmpty();
	return true;
}

bool UCosmereSaveSubsystem::CreateCharacter(const FText& CharacterName, FName RaceId, const TArray<FName>& WeaponIds, const TArray<FName>& PowerIds)
{
	SetError(FText::GetEmpty());

	if (!EnsureSaveLoaded())
	{
		return false;
	}

	FText Error;
	if (!ValidateNewCharacter(RaceId, WeaponIds, PowerIds, Error))
	{
		SetError(Error);
		return false;
	}

	FSpiritwebCharacterSave NewCharacter;
	NewCharacter.CharacterId = FGuid::NewGuid();
	NewCharacter.CharacterName = CharacterName.IsEmpty() ? FText::FromString(TEXT("Unnamed")) : CharacterName;
	NewCharacter.RaceId = RaceId;
	NewCharacter.WeaponIds = WeaponIds;
	NewCharacter.PowerIds = PowerIds;

	SaveData->Characters.Add(NewCharacter);
	// Creation is not confirmation. The roster highlights this new stable id in the UI stage.
	if (!SaveToDisk()) return false;

	UE_LOG(LogCosmere, Log, TEXT("Character: created '%s' (race %s, %d weapons, %d powers)."),
		*NewCharacter.CharacterName.ToString(), *RaceId.ToString(), WeaponIds.Num(), PowerIds.Num());
	return true;
}

bool UCosmereSaveSubsystem::EquipWeapon(int32 CharacterIndex, EWeaponSlot Slot, FName WeaponId)
{
	SetError(FText::GetEmpty());

	if (!EnsureSaveLoaded())
	{
		return false;
	}

	if (!ValidateSlotIndex(CharacterIndex))
	{
		SetError(FText::FromString(TEXT("That character no longer exists.")));
		return false;
	}

	if (!IsWeaponUnlocked(WeaponId))
	{
		SetError(FText::FromString(TEXT("That weapon hasn't been unlocked in the Armory yet.")));
		return false;
	}

	FSpiritwebCharacterSave& Character = SaveData->Characters[CharacterIndex];
	while (Character.WeaponIds.Num() < 2)
	{
		Character.WeaponIds.Add(NAME_None);
	}

	const int32 SlotIndex = (Slot == EWeaponSlot::Primary) ? 0 : 1;
	const int32 OtherIndex = 1 - SlotIndex;

	if (Character.WeaponIds[OtherIndex] == WeaponId)
	{
		SetError(FText::FromString(TEXT("The other weapon slot already holds that weapon.")));
		return false;
	}

	Character.WeaponIds[SlotIndex] = WeaponId;
	return SaveToDisk();
}

bool UCosmereSaveSubsystem::EquipPower(int32 CharacterIndex, EPowerSlot Slot, FName PowerId)
{
	SetError(FText::GetEmpty());
	UE_LOG(LogCosmereUI, Log, TEXT("EquipPower character=%d slot=%d id=%s"), CharacterIndex, int32(Slot), *PowerId.ToString());
	if (!EnsureSaveLoaded()) return false;
	if (!ValidateSlotIndex(CharacterIndex) || (Slot != EPowerSlot::Alpha && Slot != EPowerSlot::Beta))
	{
		SetError(FText::FromString(TEXT("That character or power slot is invalid.")));
		return false;
	}
	const FSpiritwebCharacterSave Original = SaveData->Characters[CharacterIndex];
	if (Original.IsHeroBuild())
	{
		SetError(FText::FromString(TEXT("A preset hero has fixed powers. Clear the hero to edit the preserved custom build.")));
		return false;
	}
	FText Error;
	// Refuse incomplete/invalid legacy kits rather than manufacturing a missing second slot.
	if (!ValidateNewCharacter(Original.RaceId, Original.WeaponIds, Original.PowerIds, Error))
	{
		SetError(Error);
		return false;
	}
	const int32 SlotIndex = (Slot == EPowerSlot::Alpha) ? 0 : 1;
	const int32 OtherIndex = 1 - SlotIndex;
	TArray<FName> Proposed = Original.PowerIds;
	const bool bSwap = Proposed[OtherIndex] == PowerId;
	if (bSwap) Swap(Proposed[SlotIndex], Proposed[OtherIndex]);
	else Proposed[SlotIndex] = PowerId;
	if (!ValidateNewCharacter(Original.RaceId, Original.WeaponIds, Proposed, Error))
	{
		SetError(Error);
		return false;
	}
	if (Proposed == Original.PowerIds) return true; // No disk write or notification.
	SaveData->Characters[CharacterIndex].PowerIds = MoveTemp(Proposed);
	if (!SaveToDisk()) return false; // Existing whole-profile rollback owns failure recovery.
	UE_LOG(LogCosmereUI, Log, TEXT("EquipPower committed one %s transaction."), bSwap ? TEXT("swap") : TEXT("replacement"));
	return true;
}

bool UCosmereSaveSubsystem::EquipRace(int32 CharacterIndex, FName RaceId)
{
	SetError(FText::GetEmpty());
	if (!EnsureSaveLoaded()) return false;
	if (!ValidateSlotIndex(CharacterIndex))
	{
		SetError(FText::FromString(TEXT("That character no longer exists.")));
		return false;
	}
	const FSpiritwebCharacterSave Original = SaveData->Characters[CharacterIndex];
	if (Original.IsHeroBuild())
	{
		SetError(FText::FromString(TEXT("Clear the preset hero before changing the custom race.")));
		return false;
	}
	FText Error;
	if (!ValidateNewCharacter(RaceId, Original.WeaponIds, Original.PowerIds, Error))
	{
		SetError(Error);
		return false;
	}
	if (Original.RaceId == RaceId) return true;
	SaveData->Characters[CharacterIndex].RaceId = RaceId;
	return SaveToDisk();
}

bool UCosmereSaveSubsystem::EquipHero(int32 CharacterIndex, FName HeroId)
{
	SetError(FText::GetEmpty());

	if (!EnsureSaveLoaded())
	{
		return false;
	}

	if (!ValidateSlotIndex(CharacterIndex))
	{
		SetError(FText::FromString(TEXT("That character no longer exists.")));
		return false;
	}

	const UCosmereCatalogSubsystem* Catalog = GetCatalog();
	FHeroDefinition Row;
	if (!Catalog || !Catalog->GetHeroRow(HeroId, Row))
	{
		SetError(FText::FromString(TEXT("That hero doesn't exist.")));
		return false;
	}

	if (!IsHeroUnlocked(HeroId))
	{
		SetError(FText::FromString(FString::Printf(TEXT("%s is still locked in the Identity Store."), *Row.DisplayName.ToString())));
		return false;
	}

	// The hand-built kit is deliberately left in place so ClearHero can restore it.
	SaveData->Characters[CharacterIndex].EquippedHeroId = HeroId;
	if (!SaveToDisk()) return false;

	UE_LOG(LogCosmere, Log, TEXT("Character: equipped hero %s on slot %d."), *HeroId.ToString(), CharacterIndex);
	return true;
}

bool UCosmereSaveSubsystem::ClearHero(int32 CharacterIndex)
{
	SetError(FText::GetEmpty());

	if (!EnsureSaveLoaded())
	{
		return false;
	}

	if (!ValidateSlotIndex(CharacterIndex))
	{
		SetError(FText::FromString(TEXT("That character no longer exists.")));
		return false;
	}

	SaveData->Characters[CharacterIndex].EquippedHeroId = NAME_None;
	return SaveToDisk();
}

bool UCosmereSaveSubsystem::DeleteCharacter(int32 CharacterIndex)
{
	SetError(FText::GetEmpty());

	if (!EnsureSaveLoaded())
	{
		return false;
	}

	if (!ValidateSlotIndex(CharacterIndex))
	{
		SetError(FText::FromString(TEXT("That character no longer exists.")));
		return false;
	}

	const int32 PreviousSelection = SaveData->SelectedCharacterIndex;
	SaveData->Characters.RemoveAt(CharacterIndex);

	if (PreviousSelection == CharacterIndex || SaveData->Characters.IsEmpty())
	{
		// Deleting the confirmed character does not silently confirm another one.
		SaveData->SelectedCharacterIndex = INDEX_NONE;
	}
	else if (PreviousSelection > CharacterIndex)
	{
		// Keep the same confirmed character identity after earlier rows shift left.
		SaveData->SelectedCharacterIndex = PreviousSelection - 1;
	}

	return SaveToDisk();
}

// ---------------------------------------------------------------------------
// Selection-index bridge
//
// Menus hand back a combo-box index, so the index -> row name mapping is resolved here, next to
// the same unlock filter the UI pages through. Both walk the catalog id list in row order and
// keep the unlocked entries, which is what keeps UI option N and list entry N the same thing.
// ---------------------------------------------------------------------------

TArray<FName> UCosmereSaveSubsystem::GetUnlockedRaceIds() const
{
	TArray<FName> Ids;

	const UCosmereCatalogSubsystem* Catalog = GetCatalog();
	if (!Catalog)
	{
		return Ids;
	}

	for (const FName& RaceId : Catalog->GetAllRaceIds())
	{
		if (IsRaceUnlocked(RaceId))
		{
			Ids.Add(RaceId);
		}
	}

	return Ids;
}

TArray<FName> UCosmereSaveSubsystem::GetUnlockedWeaponIds() const
{
	TArray<FName> Ids;

	const UCosmereCatalogSubsystem* Catalog = GetCatalog();
	if (!Catalog)
	{
		return Ids;
	}

	for (const FName& WeaponId : Catalog->GetAllWeaponIds())
	{
		if (IsWeaponUnlocked(WeaponId))
		{
			Ids.Add(WeaponId);
		}
	}

	return Ids;
}

TArray<FName> UCosmereSaveSubsystem::GetUnlockedPowerIds() const
{
	TArray<FName> Ids;

	const UCosmereCatalogSubsystem* Catalog = GetCatalog();
	if (!Catalog)
	{
		return Ids;
	}

	for (const FName& PowerId : Catalog->GetAllPowerIds())
	{
		if (IsPowerUnlocked(PowerId))
		{
			Ids.Add(PowerId);
		}
	}

	return Ids;
}

bool UCosmereSaveSubsystem::CreateCharacterFromSelection(const FText& CharacterName, int32 RaceIndex,
	int32 PrimaryWeaponIndex, int32 SecondaryWeaponIndex, int32 AlphaPowerIndex, int32 BetaPowerIndex)
{
	SetError(FText::GetEmpty());

	const TArray<FName> RaceIds = GetUnlockedRaceIds();
	const TArray<FName> WeaponIds = GetUnlockedWeaponIds();
	const TArray<FName> PowerIds = GetUnlockedPowerIds();

	if (!RaceIds.IsValidIndex(RaceIndex))
	{
		SetError(FText::FromString(TEXT("Pick a race.")));
		return false;
	}

	if (!WeaponIds.IsValidIndex(PrimaryWeaponIndex) || !WeaponIds.IsValidIndex(SecondaryWeaponIndex))
	{
		SetError(FText::FromString(TEXT("Pick two weapons.")));
		return false;
	}

	if (!PowerIds.IsValidIndex(AlphaPowerIndex) || !PowerIds.IsValidIndex(BetaPowerIndex))
	{
		SetError(FText::FromString(TEXT("Pick two powers.")));
		return false;
	}

	TArray<FName> ChosenWeapons;
	ChosenWeapons.Add(WeaponIds[PrimaryWeaponIndex]);
	ChosenWeapons.Add(WeaponIds[SecondaryWeaponIndex]);

	TArray<FName> ChosenPowers;
	ChosenPowers.Add(PowerIds[AlphaPowerIndex]);
	ChosenPowers.Add(PowerIds[BetaPowerIndex]);

	// CreateCharacter owns the distinctness and unlock checks, so they are not repeated here.
	return CreateCharacter(CharacterName, RaceIds[RaceIndex], ChosenWeapons, ChosenPowers);
}
