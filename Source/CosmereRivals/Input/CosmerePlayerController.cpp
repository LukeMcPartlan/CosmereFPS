// Copyright Epic Games, Inc. All Rights Reserved.
#include "Input/CosmerePlayerController.h"

#include "CosmereRivals.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "TimerManager.h"
#include "UI/CosmereUIManager.h"

ACosmerePlayerController::ACosmerePlayerController()
{
	// Soft paths only: no Blueprint or asset loading during CDO construction.
	DefaultMappingContext = TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(TEXT("/Game/Input/IMC_Default.IMC_Default")));
	MouseLookMappingContext = TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(TEXT("/Game/Input/IMC_MouseLook.IMC_MouseLook")));
	EscapeAction = TSoftObjectPtr<UInputAction>(FSoftObjectPath(TEXT("/Game/Input/Actions/IA_Escape.IA_Escape")));
}

void ACosmerePlayerController::BeginPlay()
{
	Super::BeginPlay();
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
		[this]() { InitializeLocalUI(); }));
}

void ACosmerePlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();
	if (HasActorBegunPlay()) InitializeLocalUI();
}

void ACosmerePlayerController::AcknowledgePossession(APawn* P)
{
 Super::AcknowledgePossession(P);
 GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,[this](){InitializeLocalUI();}));
}
void ACosmerePlayerController::PostSeamlessTravel()
{
 Super::PostSeamlessTravel();bAddedLocalContexts=false;
 GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,[this](){InitializeLocalUI();}));
}
void ACosmerePlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (HasActorBegunPlay()) InitializeLocalUI();
}

void ACosmerePlayerController::InitializeLocalUI()
{
	ULocalPlayer* OwningLocalPlayer = GetLocalPlayer();
	if (!IsLocalController() || !OwningLocalPlayer) return;
	if (!bAddedLocalContexts)
	{
		UEnhancedInputLocalPlayerSubsystem* Input = OwningLocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
		if (!Input) return;
		FModifyContextOptions Options;
		Options.bIgnoreAllPressedKeysUntilRelease = true;
		for (const TSoftObjectPtr<UInputMappingContext>& Reference : {DefaultMappingContext, MouseLookMappingContext})
		{
			if (UInputMappingContext* Context = Reference.LoadSynchronous())
			{
				int32 ExistingPriority = 0;
				if (!Input->HasMappingContext(Context, ExistingPriority)) Input->AddMappingContext(Context, 0, Options);
			}
			else UE_LOG(LogCosmereUI, Error, TEXT("Missing native controller context: %s"), *Reference.ToString());
		}
		bAddedLocalContexts = true;
	}
	if (UCosmereUIManager* Manager = OwningLocalPlayer->GetSubsystem<UCosmereUIManager>())
		Manager->InitializeForController(this);
}

void ACosmerePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	UEnhancedInputComponent* Enhanced = Cast<UEnhancedInputComponent>(InputComponent);
	UInputAction* Escape = EscapeAction.LoadSynchronous();
	if (!Enhanced || !Escape)
	{
		UE_LOG(LogCosmereUI, Error, TEXT("Native Escape binding requires EnhancedInputComponent and IA_Escape."));
		return;
	}
	// Exactly one replacement for the old controller's Escape event; no new combat bindings.
	Enhanced->BindAction(Escape, ETriggerEvent::Started, this, &ACosmerePlayerController::HandleEscape);
}

void ACosmerePlayerController::HandleEscape()
{
	ULocalPlayer* OwningLocalPlayer = GetLocalPlayer();
	UCosmereUIManager* Manager = OwningLocalPlayer ? OwningLocalPlayer->GetSubsystem<UCosmereUIManager>() : nullptr;
	if (!Manager) return;
	if (Manager->IsMenuOpen()) Manager->NavigateBack();
	else Manager->Navigate(ECosmereUIRoute::EscapeMenu, true);
	// Escape/session-leave screen implementation is part of Plan 3; never fall back to global pause.
}
