// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/CosmereMenuScreen.h"

#include "CommonInputBaseTypes.h"
#include "Engine/EngineTypes.h"
#include "Engine/LocalPlayer.h"
#include "Input/UIActionBindingHandle.h"
#include "UI/CosmereUIManager.h"

UCosmereMenuScreen::UCosmereMenuScreen(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bIsBackHandler = true;
	bAutoRestoreFocus = true;
}

TOptional<FUIInputConfig> UCosmereMenuScreen::GetDesiredInputConfig() const
{
	FUIInputConfig Config(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture, false);
	Config.bIgnoreMoveInput = true;
	Config.bIgnoreLookInput = true;
	return Config;
}

void UCosmereMenuScreen::NativeOnActivated()
{
	Super::NativeOnActivated();
	if (ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
	{
		if (UCosmereUIManager* Manager = LocalPlayer->GetSubsystem<UCosmereUIManager>())
		{
			Manager->OnViewModelsChanged.Broadcast();
		}
	}
}

UWidget* UCosmereMenuScreen::NativeGetDesiredFocusTarget() const
{
	if (!DefaultFocusWidgetName.IsNone())
	{
		UWidget* Target = GetWidgetFromName(DefaultFocusWidgetName);
		if (Target && Target->GetIsEnabled())
		{
			return Target;
		}
	}
	return Super::NativeGetDesiredFocusTarget();
}

bool UCosmereMenuScreen::NativeOnHandleBackAction()
{
	if (ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
	{
		if (UCosmereUIManager* Manager = LocalPlayer->GetSubsystem<UCosmereUIManager>())
		{
			// Consume Back even at a protected root: never pop Character Select/Main Menu by accident.
			Manager->NavigateBack();
			return true;
		}
	}
	return Super::NativeOnHandleBackAction();
}
