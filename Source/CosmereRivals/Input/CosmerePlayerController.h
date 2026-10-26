// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CosmerePlayerController.generated.h"

class UInputAction;
class UInputMappingContext;

/** Native menu ownership; combat action events remain on BP_FirstPersonCharacter. */
UCLASS(Blueprintable)
class COSMERERIVALS_API ACosmerePlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	ACosmerePlayerController();
	virtual void ReceivedPlayer() override;
 virtual void AcknowledgePossession(APawn* P) override;
 virtual void PostSeamlessTravel() override;
protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;

	UPROPERTY(EditDefaultsOnly, Category="Cosmere|Input")
	TSoftObjectPtr<UInputMappingContext> DefaultMappingContext;
	UPROPERTY(EditDefaultsOnly, Category="Cosmere|Input")
	TSoftObjectPtr<UInputMappingContext> MouseLookMappingContext;
	UPROPERTY(EditDefaultsOnly, Category="Cosmere|Input")
	TSoftObjectPtr<UInputAction> EscapeAction;
private:
	void InitializeLocalUI();
	void HandleEscape();
	bool bAddedLocalContexts = false;
};
