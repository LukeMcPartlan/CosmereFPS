// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/CosmereUIPresentationData.h"

bool UCosmereUIPresentationData::FindPresentation(FName CatalogKind, FName CatalogId,
	FCosmereUIEntryPresentation& OutPresentation) const
{
	OutPresentation = FCosmereUIEntryPresentation();
	const FCosmereUIEntryPresentation* Entry = Entries.FindByPredicate(
		[CatalogKind, CatalogId](const FCosmereUIEntryPresentation& Item)
		{
			return Item.CatalogKind == CatalogKind && Item.CatalogId == CatalogId;
		});
	if (!Entry)
	{
		return false;
	}
	OutPresentation = *Entry;
	return true;
}
