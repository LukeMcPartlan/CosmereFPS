// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "UI/CosmereUITypes.h"
#include "CosmereMenuScreen.generated.h"

/** Activatable presentation only. The local UI manager owns routing and input transitions. */
UCLASS(Abstract, Blueprintable)
class COSMERERIVALS_API UCosmereMenuScreen : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	UCosmereMenuScreen(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|UI")
	ECosmereUIRoute ScreenRoute = ECosmereUIRoute::None;

	/** Name of an enabled focusable child; each screen sets its own default target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cosmere|UI")
	FName DefaultFocusWidgetName;

	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;

protected:
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual bool NativeOnHandleBackAction() override;
	virtual void NativeOnActivated() override;
};
