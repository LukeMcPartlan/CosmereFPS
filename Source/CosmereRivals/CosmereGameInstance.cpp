// Copyright Epic Games, Inc. All Rights Reserved.

#include "CosmereGameInstance.h"

#include "Data/CosmereCatalogSubsystem.h"
#include "Save/CosmereSaveSubsystem.h"

UCosmereCatalogSubsystem* UCosmereGameInstance::GetCatalog() const
{
	return GetSubsystem<UCosmereCatalogSubsystem>();
}

UCosmereSaveSubsystem* UCosmereGameInstance::GetSave() const
{
	return GetSubsystem<UCosmereSaveSubsystem>();
}
