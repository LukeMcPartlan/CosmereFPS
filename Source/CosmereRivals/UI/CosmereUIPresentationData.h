// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CosmereUIPresentationData.generated.h"

class UTexture2D;

/** No prices, ownership or gameplay tuning: those remain in the existing catalogs/save. */
USTRUCT(BlueprintType)
struct COSMERERIVALS_API FCosmereUIEntryPresentation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName CatalogKind;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName CatalogId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName Category;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Art")
	TSoftObjectPtr<UTexture2D> Icon;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Art")
	TSoftObjectPtr<UTexture2D> LockedIcon;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Board")
	FVector2D BoardPosition = FVector2D(0.5, 0.5);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Navigation")
	FName UpId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Navigation")
	FName DownId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Navigation")
	FName LeftId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Navigation")
	FName RightId;
};

UCLASS(BlueprintType)
class COSMERERIVALS_API UCosmereUIPresentationData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmere|UI")
	TArray<FCosmereUIEntryPresentation> Entries;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmere|UI|Theme")
	FLinearColor BackgroundColor = FLinearColor(0.015f, 0.022f, 0.035f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmere|UI|Theme")
	FLinearColor TextColor = FLinearColor(0.85f, 0.82f, 0.73f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmere|UI|Theme")
	FLinearColor TrimColor = FLinearColor(0.42f, 0.29f, 0.12f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmere|UI|Theme")
	FLinearColor FocusColor = FLinearColor(0.10f, 0.50f, 0.48f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmere|UI|Theme")
	FLinearColor ErrorColor = FLinearColor(0.62f, 0.13f, 0.10f, 1.f);

	UFUNCTION(BlueprintPure, Category = "Cosmere|UI")
	bool FindPresentation(FName CatalogKind, FName CatalogId, FCosmereUIEntryPresentation& OutPresentation) const;
};
