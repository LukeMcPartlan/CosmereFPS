#include "Save/CosmereSaveSubsystem.h"
#include "Save/CosmereSaveGame.h"
#include "Data/CosmereCatalogSubsystem.h"
#include "Engine/GameInstance.h"

#if !UE_BUILD_SHIPPING
UCosmereSaveSubsystem* UCosmereSaveSubsystem::CreateDevelopmentFixture(UGameInstance* Owner,
	const UCosmereSaveGame* Seed, UCosmereCatalogSubsystem* FixtureCatalog)
{
	if (!Owner || !Seed || !FixtureCatalog || !FixtureCatalog->IsDevelopmentFixture()
		|| FixtureCatalog->GetGameInstance() != Owner) return nullptr;
	UCosmereSaveSubsystem* Fixture = NewObject<UCosmereSaveSubsystem>(Owner, NAME_None, RF_Transient);
	Fixture->bDevelopmentFixture = true;
	Fixture->DevelopmentCatalog = FixtureCatalog;
	Fixture->SaveSlotName = TEXT("AuraUITest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Fixture->SaveData = DuplicateObject<UCosmereSaveGame>(Seed, Fixture);
	Fixture->LastCommittedData = DuplicateObject<UCosmereSaveGame>(Seed, Fixture);
	return Fixture; // No Initialize, LoadOrCreateSave, reset, or disk operation.
}

bool UCosmereSaveSubsystem::ReloadDevelopmentFixture()
{
	return bDevelopmentFixture && SaveSlotName.StartsWith(TEXT("AuraUITest_")) && LoadOrCreateSave();
}
#endif
