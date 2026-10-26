// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/CosmereCatalogSubsystem.h"

#include "CosmereRivals.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"

namespace CosmereCatalog
{
	static const TCHAR* RowLookupContext = TEXT("CosmereCatalog");
}

UCosmereCatalogSubsystem::UCosmereCatalogSubsystem()
{
	RaceTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_Races.DT_Races")));
	WeaponTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_Weapons.DT_Weapons")));
	PowerTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_Powers.DT_Powers")));
	HeroTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_Heroes.DT_Heroes")));
}

void UCosmereCatalogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadCatalog();
}

void UCosmereCatalogSubsystem::LoadCatalog()
{
	if (bDevelopmentFixture) return; // Never overwrite a test overlay with production catalogs.
	RaceTableCache = RaceTable.IsNull() ? nullptr : RaceTable.LoadSynchronous();
	WeaponTableCache = WeaponTable.IsNull() ? nullptr : WeaponTable.LoadSynchronous();
	PowerTableCache = PowerTable.IsNull() ? nullptr : PowerTable.LoadSynchronous();
	HeroTableCache = HeroTable.IsNull() ? nullptr : HeroTable.LoadSynchronous();

	const struct FTableCheck
	{
		const TCHAR* Label;
		UDataTable* Table;
	} Checks[] =
	{
		{ TEXT("DT_Races"), RaceTableCache },
		{ TEXT("DT_Weapons"), WeaponTableCache },
		{ TEXT("DT_Powers"), PowerTableCache },
		{ TEXT("DT_Heroes"), HeroTableCache }
	};

	for (const FTableCheck& Check : Checks)
	{
		if (Check.Table)
		{
			UE_LOG(LogCosmere, Log, TEXT("Catalog: %s loaded with %d rows."), Check.Label, Check.Table->GetRowMap().Num());
		}
		else
		{
			UE_LOG(LogCosmere, Error, TEXT("Catalog: %s failed to load - catalog lookups will fail."), Check.Label);
		}
	}

	if (PowerTableCache)
	{
		int32 SurgeCount = 0;
		int32 AllomanticCount = 0;
		int32 FeruchemicalCount = 0;

		for (const FName& PowerId : PowerTableCache->GetRowNames())
		{
			const FPowerDefinition* Row = PowerTableCache->FindRow<FPowerDefinition>(PowerId, CosmereCatalog::RowLookupContext, false);
			if (!Row)
			{
				UE_LOG(LogCosmere, Error, TEXT("Catalog: power row '%s' is not a valid FPowerDefinition."), *PowerId.ToString());
				continue;
			}

			switch (Row->Source)
			{
			case EPowerSource::Surge: ++SurgeCount; break;
			case EPowerSource::Allomantic: ++AllomanticCount; break;
			case EPowerSource::Feruchemical: ++FeruchemicalCount; break;
			}
		}

		UE_LOG(LogCosmere, Log, TEXT("Catalog: powers resolved as %d Surges, %d Allomantic metals, %d Feruchemical metals."), SurgeCount, AllomanticCount, FeruchemicalCount);
	}
}

bool UCosmereCatalogSubsystem::GetRaceRow(FName RaceId, FRaceDefinition& OutRace) const
{
	if (!RaceTableCache)
	{
		return false;
	}

	if (const FRaceDefinition* Row = RaceTableCache->FindRow<FRaceDefinition>(RaceId, CosmereCatalog::RowLookupContext, false))
	{
		OutRace = *Row;
		return true;
	}

	return false;
}

bool UCosmereCatalogSubsystem::GetWeaponRow(FName WeaponId, FWeaponDefinition& OutWeapon) const
{
	if (!WeaponTableCache)
	{
		return false;
	}

	if (const FWeaponDefinition* Row = WeaponTableCache->FindRow<FWeaponDefinition>(WeaponId, CosmereCatalog::RowLookupContext, false))
	{
		OutWeapon = *Row;
		return true;
	}

	return false;
}

bool UCosmereCatalogSubsystem::GetPowerRow(FName PowerId, FPowerDefinition& OutPower) const
{
	if (!PowerTableCache)
	{
		return false;
	}

	if (const FPowerDefinition* Row = PowerTableCache->FindRow<FPowerDefinition>(PowerId, CosmereCatalog::RowLookupContext, false))
	{
		OutPower = *Row;
		return true;
	}

	return false;
}

bool UCosmereCatalogSubsystem::GetHeroRow(FName HeroId, FHeroDefinition& OutHero) const
{
	if (!HeroTableCache)
	{
		return false;
	}

	if (const FHeroDefinition* Row = HeroTableCache->FindRow<FHeroDefinition>(HeroId, CosmereCatalog::RowLookupContext, false))
	{
		OutHero = *Row;
		return true;
	}

	return false;
}

TArray<FName> UCosmereCatalogSubsystem::GetAllRaceIds() const
{
	return RaceTableCache ? RaceTableCache->GetRowNames() : TArray<FName>();
}

TArray<FName> UCosmereCatalogSubsystem::GetAllWeaponIds() const
{
	return WeaponTableCache ? WeaponTableCache->GetRowNames() : TArray<FName>();
}

TArray<FName> UCosmereCatalogSubsystem::GetAllPowerIds() const
{
	return PowerTableCache ? PowerTableCache->GetRowNames() : TArray<FName>();
}

TArray<FName> UCosmereCatalogSubsystem::GetAllHeroIds() const
{
	return HeroTableCache ? HeroTableCache->GetRowNames() : TArray<FName>();
}

TArray<FName> UCosmereCatalogSubsystem::GetPowerIdsBySource(EPowerSource Source) const
{
	TArray<FName> Result;
	if (!PowerTableCache)
	{
		return Result;
	}

	for (const FName& PowerId : PowerTableCache->GetRowNames())
	{
		const FPowerDefinition* Row = PowerTableCache->FindRow<FPowerDefinition>(PowerId, CosmereCatalog::RowLookupContext, false);
		if (Row && Row->Source == Source)
		{
			Result.Add(PowerId);
		}
	}

	return Result;
}

TArray<FName> UCosmereCatalogSubsystem::GetDefaultUnlockedRaceIds() const
{
	TArray<FName> Result;
	if (!RaceTableCache)
	{
		return Result;
	}

	for (const FName& RaceId : RaceTableCache->GetRowNames())
	{
		const FRaceDefinition* Row = RaceTableCache->FindRow<FRaceDefinition>(RaceId, CosmereCatalog::RowLookupContext, false);
		if (Row && Row->bUnlockedByDefault)
		{
			Result.Add(RaceId);
		}
	}

	return Result;
}

TArray<FName> UCosmereCatalogSubsystem::GetStarterWeaponIds() const
{
	TArray<FName> Result;
	if (!WeaponTableCache)
	{
		return Result;
	}

	for (const FName& WeaponId : WeaponTableCache->GetRowNames())
	{
		const FWeaponDefinition* Row = WeaponTableCache->FindRow<FWeaponDefinition>(WeaponId, CosmereCatalog::RowLookupContext, false);
		if (Row && Row->bUnlockedByDefault)
		{
			Result.Add(WeaponId);
		}
	}

	return Result;
}

TArray<FName> UCosmereCatalogSubsystem::GetStarterPowerIds() const
{
	TArray<FName> Result;
	if (!PowerTableCache)
	{
		return Result;
	}

	for (const FName& PowerId : PowerTableCache->GetRowNames())
	{
		const FPowerDefinition* Row = PowerTableCache->FindRow<FPowerDefinition>(PowerId, CosmereCatalog::RowLookupContext, false);
		if (Row && Row->bUnlockedByDefault)
		{
			Result.Add(PowerId);
		}
	}

	return Result;
}

#if !UE_BUILD_SHIPPING
UCosmereCatalogSubsystem* UCosmereCatalogSubsystem::CreateDevelopmentFixture(UGameInstance* Owner,
	const UDataTable* Races, const UDataTable* Weapons, const UDataTable* Powers, const UDataTable* Heroes)
{
	if (!Owner || !Races || !Weapons || !Powers || !Heroes
		|| Races->GetRowStruct() != FRaceDefinition::StaticStruct()
		|| Weapons->GetRowStruct() != FWeaponDefinition::StaticStruct()
		|| Powers->GetRowStruct() != FPowerDefinition::StaticStruct()
		|| Heroes->GetRowStruct() != FHeroDefinition::StaticStruct()) return nullptr;
	UCosmereCatalogSubsystem* Fixture = NewObject<UCosmereCatalogSubsystem>(Owner, NAME_None, RF_Transient);
	Fixture->bDevelopmentFixture = true;
	const auto Copy = [Fixture](const UDataTable* Source)
	{
		UDataTable* Table = DuplicateObject<UDataTable>(Source, Fixture);
		Table->ClearFlags(RF_Public | RF_Standalone);
		Table->SetFlags(RF_Transient);
		return Table;
	};
	Fixture->RaceTableCache = Copy(Races);
	Fixture->WeaponTableCache = Copy(Weapons);
	Fixture->PowerTableCache = Copy(Powers);
	Fixture->HeroTableCache = Copy(Heroes);
	return Fixture;
}

UDataTable* UCosmereCatalogSubsystem::GetDevelopmentTable(FName Kind) const
{
	if (!bDevelopmentFixture) return nullptr;
	if (Kind == TEXT("Race")) return RaceTableCache;
	if (Kind == TEXT("Weapon")) return WeaponTableCache;
	if (Kind == TEXT("Power")) return PowerTableCache;
	return Kind == TEXT("Hero") ? HeroTableCache.Get() : nullptr;
}
#endif
