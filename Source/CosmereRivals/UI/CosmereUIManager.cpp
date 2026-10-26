// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/CosmereUIManager.h"
#include "UI/CosmereSettingsSubsystem.h"
#include "UI/CosmereSettingsScreen.h"
#include "UI/CosmereSessionScreens.h"
#include "Net/CosmereSessionSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerInput.h"

#include "Blueprint/UserWidget.h"
#include "Character/CosmereCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Combat/CosmereWeaponComponent.h"
#include "CommonInputBaseTypes.h"
#include "CosmereRivals.h"
#include "Data/CosmereCatalogSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "Input/CommonUIActionRouterBase.h"
#include "InputMappingContext.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Net/CosmereGameMode.h"
#include "Net/CosmerePracticeGameMode.h"
#include "Input/UIActionBindingHandle.h"
#include "Save/CosmereSaveSubsystem.h"
#include "TimerManager.h"
#include "UI/CosmereHUD.h"
#include "UI/CosmereMenuScreen.h"
#include "UI/CosmereNativeMenus.h"
#include "UI/CosmereStoreScreen.h"
#include "UI/CosmereMenuScene.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

UCosmereUIManager::UCosmereUIManager()
{
	RootWidgetClass = TSoftClassPtr<UUserWidget>(FSoftObjectPath(TEXT("/Game/UI/WBP_UIRoot.WBP_UIRoot_C")));
	// Native screens contain no legacy Blueprint event graph, so handlers run exactly once.
	ScreenClasses.Add(ECosmereUIRoute::CharacterSelect,
		TSoftClassPtr<UCosmereMenuScreen>(UCosmereNativeCharacterSelectScreen::StaticClass()));
	ScreenClasses.Add(ECosmereUIRoute::CharacterCreate,
		TSoftClassPtr<UCosmereMenuScreen>(UCosmereNativeCharacterCreateScreen::StaticClass()));
	ScreenClasses.Add(ECosmereUIRoute::MainMenu,
		TSoftClassPtr<UCosmereMenuScreen>(UCosmereNativeMainMenuScreen::StaticClass()));
	ScreenClasses.Add(ECosmereUIRoute::GameSelect,
		TSoftClassPtr<UCosmereMenuScreen>(UCosmereNativeGameSelectScreen::StaticClass()));
	ScreenClasses.Add(ECosmereUIRoute::ShopHub, TSoftClassPtr<UCosmereMenuScreen>(UCosmereNativeShopHubScreen::StaticClass()));
	ScreenClasses.Add(ECosmereUIRoute::SpiritWeb, TSoftClassPtr<UCosmereMenuScreen>(UCosmereNativeSpiritWebScreen::StaticClass()));
	ScreenClasses.Add(ECosmereUIRoute::Armory, TSoftClassPtr<UCosmereMenuScreen>(UCosmereNativeArmoryScreen::StaticClass()));
	ScreenClasses.Add(ECosmereUIRoute::IdentityStore, TSoftClassPtr<UCosmereMenuScreen>(UCosmereNativeIdentityStoreScreen::StaticClass()));
 ScreenClasses.Add(ECosmereUIRoute::Settings,TSoftClassPtr<UCosmereMenuScreen>(UCosmereSettingsScreen::StaticClass()));
 ScreenClasses.Add(ECosmereUIRoute::ServerMenu,TSoftClassPtr<UCosmereMenuScreen>(UCosmereServerMenuScreen::StaticClass()));
 ScreenClasses.Add(ECosmereUIRoute::EscapeMenu,TSoftClassPtr<UCosmereMenuScreen>(UCosmereEscapeMenuScreen::StaticClass()));
	GameplayContexts.Add(TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(TEXT("/Game/Input/IMC_Default.IMC_Default"))));
	GameplayContexts.Add(TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(TEXT("/Game/Input/IMC_MouseLook.IMC_MouseLook"))));
	GameplayContexts.Add(TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(TEXT("/Game/Input/IMC_Match.IMC_Match"))));
}

void UCosmereUIManager::Deinitialize()
{
 if(BoundSettings.IsValid())
 {
  BoundSettings->OnPreviewChanged.RemoveDynamic(this,&ThisClass::HandlePreferencesChanged);
  BoundSettings->OnApplied.RemoveDynamic(this,&ThisClass::HandlePreferencesChanged);
 }
	if (ACosmereMenuCameraRig* Rig = Cast<ACosmereMenuCameraRig>(MenuCameraRig.Get()))
		OnScenePoseRequested.RemoveDynamic(Rig, &ACosmereMenuCameraRig::RequestPose);
	if (BoundSave)
	{
		BoundSave->OnProfileChanged.RemoveDynamic(this, &UCosmereUIManager::HandleProfileChanged);
	}
	BoundSave = nullptr;
	ReleaseRoot();
	CurrentController.Reset();
	Super::Deinitialize();
}

void UCosmereUIManager::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	Super::PlayerControllerChanged(NewPlayerController);
	if (CurrentController.Get() != NewPlayerController)
	{
		ReleaseRoot();
  bMenuInputBlocked=false;
		if (ACosmereMenuCameraRig* Rig = Cast<ACosmereMenuCameraRig>(MenuCameraRig.Get()))
			OnScenePoseRequested.RemoveDynamic(Rig, &ACosmereMenuCameraRig::RequestPose);
		SuspendedContexts.Reset();
		MenuCameraRig.Reset();
		MenuPreviewActor.Reset();
		CurrentRoute = ECosmereUIRoute::None;
		CurrentController = NewPlayerController;
	}
}

bool UCosmereUIManager::InitializeForController(APlayerController* Controller, bool bReturningToMenu)
{
	if (!Controller || !Controller->IsLocalController() || Controller->GetLocalPlayer() != GetLocalPlayer())
	{
		return Fail(TEXT("UI initialization requires this local player's controller."));
	}
	PlayerControllerChanged(Controller);
	UCosmereSaveSubsystem* Save = ResolveSave();
	if (!Save)
	{
		return Fail(TEXT("The local profile is unavailable."));
	}
	if (BoundSave != Save)
	{
		if (BoundSave)
		{
			BoundSave->OnProfileChanged.RemoveDynamic(this, &UCosmereUIManager::HandleProfileChanged);
		}
		BoundSave = Save;
		BoundSave->OnProfileChanged.AddUniqueDynamic(this, &UCosmereUIManager::HandleProfileChanged);
	}
	const ACosmereGameMode* Mode = Controller->GetWorld()->GetAuthGameMode<ACosmereGameMode>();
 if(auto* Settings=GetLocalPlayer()->GetSubsystem<UCosmereSettingsSubsystem>())
 {
  Settings->EnsureSettings(); Settings->ApplyRuntimePreferences(); BoundSettings=Settings;
  Settings->OnPreviewChanged.AddUniqueDynamic(this,&ThisClass::HandlePreferencesChanged);
  Settings->OnApplied.AddUniqueDynamic(this,&ThisClass::HandlePreferencesChanged);
 }
 if(Mode && Mode->IsFrontEndSession())
  if(auto* Sessions=Controller->GetGameInstance()->GetSubsystem<UCosmereSessionSubsystem>())
   bReturningToMenu |= Sessions->ConsumeReturnToMainMenu();
	if (Mode && Mode->IsFrontEndSession()) DiscoverMenuSceneActors();
	if (!Mode || !Mode->IsFrontEndSession())
	{
  if(IsMenuOpen()) {ApplyInputMode(true);return true;}
		SetRoute(ECosmereUIRoute::Gameplay);
		ApplyInputMode(false);
		return true;
	}
	if (!bReturningToMenu && CurrentRoute != ECosmereUIRoute::None)
	{
		// A second initialization on the same controller must not bypass cold-start selection.
		ApplyInputMode(IsMenuOpen());
		return true;
	}
	FSpiritwebCharacterSave Selected;
	const bool bHasSelected = Save->GetSelectedCharacter(Selected);
	PendingCharacterId = bHasSelected ? Selected.CharacterId : FGuid();
	const ECosmereUIRoute StartupRoute = (bHasSelected && (bReturningToMenu || bHasOpenedStartupSelection))
		? ECosmereUIRoute::MainMenu : ECosmereUIRoute::CharacterSelect;
	if (!Navigate(StartupRoute, true))
	{
		return false;
	}
	bHasOpenedStartupSelection = true;
 if(bReturningToMenu)
  if(auto* Sessions=Controller->GetGameInstance()->GetSubsystem<UCosmereSessionSubsystem>())
   if(!Sessions->GetLastError().IsEmpty())
    ShowConfirmation(FText::FromString(TEXT("Returned to Main Menu.\n")+Sessions->GetLastError().ToString()),FSimpleDelegate(),FSimpleDelegate());
	return true;
}

bool UCosmereUIManager::EnsureRoot()
{
	APlayerController* Controller = CurrentController.Get();
	if (!Controller)
	{
		return Fail(TEXT("The owning controller is not ready."));
	}
	if (RootWidget && RootWidget->GetOwningPlayer() == Controller)
	{
		return ScreenStack && ModalStack;
	}
	ReleaseRoot();
	UClass* RootClass = RootWidgetClass.LoadSynchronous();
	if (!RootClass)
	{
		return Fail(TEXT("WBP_UIRoot is missing; the UI asset stage must be completed."));
	}
	RootWidget = CreateWidget<UUserWidget>(Controller, RootClass);
	if (!RootWidget)
	{
		return Fail(TEXT("Could not create the local UI root."));
	}
	ScreenStack = Cast<UCommonActivatableWidgetStack>(RootWidget->GetWidgetFromName(TEXT("ScreenStack")));
	ModalStack = Cast<UCommonActivatableWidgetStack>(RootWidget->GetWidgetFromName(TEXT("ModalStack")));
	if (!ScreenStack || !ModalStack || !RootWidget->AddToPlayerScreen(100))
	{
		ReleaseRoot();
		return Fail(TEXT("WBP_UIRoot requires ScreenStack and ModalStack CommonUI stacks."));
	}
	ScreenStack->OnDisplayedWidgetChanged().AddUObject(this, &UCosmereUIManager::HandleDisplayedWidgetChanged);
	ModalStack->OnDisplayedWidgetChanged().AddUObject(this, &UCosmereUIManager::HandleDisplayedWidgetChanged);
	return true;
}

void UCosmereUIManager::ReleaseRoot()
{
 // Losing the actual root (travel/controller teardown) ends its pending transaction.
 // Ordinary screen/modal activation does not call this, so discard/display dialogs preserve edits.
 if(RootWidget && BoundSettings.IsValid()) BoundSettings->Cancel();
 if (ScreenStack) ScreenStack->OnDisplayedWidgetChanged().RemoveAll(this);
	if (ModalStack) ModalStack->OnDisplayedWidgetChanged().RemoveAll(this);
	if (RootWidget) RootWidget->RemoveFromParent();
	ScreenStack = nullptr;
	ModalStack = nullptr;
	RootWidget = nullptr;
 Confirmation.Reset();
 BaseTextSizes.Reset();
}

bool UCosmereUIManager::Navigate(ECosmereUIRoute Route, bool bReplaceRoot, bool bAsModal)
{
	LastError = FText::GetEmpty();
	if (Route == ECosmereUIRoute::Gameplay) return EnterPractice();
	const TSoftClassPtr<UCosmereMenuScreen>* Reference = ScreenClasses.Find(Route);
	UClass* ScreenClass = Reference ? Reference->LoadSynchronous() : nullptr;
	if (!ScreenClass || !ScreenClass->IsChildOf(UCosmereMenuScreen::StaticClass()))
	{
		return Fail(FString::Printf(TEXT("Route %d has no configured CosmereMenuScreen yet."), static_cast<int32>(Route)));
	}
	if (!EnsureRoot()) return false;
	if (!bReplaceRoot && CurrentRoute == Route) return true;
	TGuardValue<bool> RouteGuard(bChangingRoute, true);
	if (bReplaceRoot)
	{
		ModalStack->ClearWidgets();
		ScreenStack->ClearWidgets();
	}
	RootWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UCommonActivatableWidgetStack* Destination = bAsModal ? ModalStack.Get() : ScreenStack.Get();
	UCosmereMenuScreen* Screen = Destination->AddWidget<UCosmereMenuScreen>(ScreenClass,
		[Route](UCosmereMenuScreen& Instance) { Instance.ScreenRoute = Route; });
	if (!Screen) return Fail(TEXT("The requested screen could not be added to its stack."));
	SetRoute(Route);
	ApplyInputMode(true);
	return true;
}

void UCosmereUIManager::HandleDisplayedWidgetChanged(UCommonActivatableWidget* DisplayedWidget)
{
	if (bChangingRoute) return;
	UCommonActivatableWidget* Active = ModalStack ? ModalStack->GetActiveWidget() : nullptr;
	if (!Active && ScreenStack) Active = ScreenStack->GetActiveWidget();
	if (const UCosmereMenuScreen* Screen = Cast<UCosmereMenuScreen>(Active))
	{
		SetRoute(Screen->ScreenRoute);
		ApplyInputMode(true);
	}
}

bool UCosmereUIManager::NavigateBack()
{
 if(Confirmation.IsValid() && Confirmation->IsActivated()) {Confirmation->Resolve(false);return true;}
 if(ScreenStack) if(auto* Settings=Cast<UCosmereSettingsScreen>(ScreenStack->GetActiveWidget())) {Settings->RequestBack();return true;}
 if(CurrentRoute==ECosmereUIRoute::EscapeMenu) {ResumeGameplay();return true;}
 return PopScreen();
}

bool UCosmereUIManager::PopScreen()
{
	if (ModalStack && ModalStack->GetActiveWidget())
	{
		ModalStack->GetActiveWidget()->DeactivateWidget();
		return true;
	}
	if (ScreenStack && ScreenStack->GetNumWidgets() > 1 && ScreenStack->GetActiveWidget())
	{
		ScreenStack->GetActiveWidget()->DeactivateWidget();
		return true;
	}
	return false;
}

UCosmereSaveSubsystem* UCosmereUIManager::ResolveSave() const
{
	if (DevelopmentSave) return DevelopmentSave;
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UGameInstance* Instance = LocalPlayer ? LocalPlayer->GetGameInstance() : nullptr;
	return Instance ? Instance->GetSubsystem<UCosmereSaveSubsystem>() : nullptr;
}

UCosmereCatalogSubsystem* UCosmereUIManager::GetCatalogSource() const
{
	if (DevelopmentCatalog) return DevelopmentCatalog;
	UGameInstance* Instance = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr;
	return Instance ? Instance->GetSubsystem<UCosmereCatalogSubsystem>() : nullptr;
}

#if !UE_BUILD_SHIPPING
bool UCosmereUIManager::SetDevelopmentSources(UCosmereSaveSubsystem* FixtureSave, UCosmereCatalogSubsystem* FixtureCatalog)
{
	if (RootWidget || CurrentController.IsValid() || BoundSave || !FixtureSave || !FixtureCatalog
		|| !FixtureCatalog->IsDevelopmentFixture() || FixtureSave->GetDevelopmentSlotName().IsEmpty()
		|| FixtureSave->GetDevelopmentCatalog() != FixtureCatalog
		|| !GetLocalPlayer() || FixtureSave->GetGameInstance() != GetLocalPlayer()->GetGameInstance()) return false;
	DevelopmentSave = FixtureSave;
	DevelopmentCatalog = FixtureCatalog;
	return true;
}
#endif

int32 UCosmereUIManager::FindCharacterIndex(FGuid CharacterId) const
{
	const UCosmereSaveSubsystem* Save = ResolveSave();
	if (!Save || !CharacterId.IsValid()) return INDEX_NONE;
	const TArray<FSpiritwebCharacterSave> Characters = Save->GetCharacters();
	return Characters.IndexOfByPredicate([CharacterId](const FSpiritwebCharacterSave& Build)
		{ return Build.CharacterId == CharacterId; });
}

bool UCosmereUIManager::HighlightCharacter(FGuid CharacterId)
{
	if (FindCharacterIndex(CharacterId) == INDEX_NONE) return Fail(TEXT("That character no longer exists."));
	PendingCharacterId = CharacterId;
	LastError = FText::GetEmpty();
	OnViewModelsChanged.Broadcast();
	return true;
}

bool UCosmereUIManager::CanConfirmCharacter() const
{
	const UCosmereSaveSubsystem* Save = ResolveSave();
	FSpiritwebCharacterSave Build;
	if (!Save || !Save->GetCharacter(FindCharacterIndex(PendingCharacterId), Build)
		|| !Build.CharacterId.IsValid() || !BuildCard(Build).bCatalogResolved) return false;
	if (Build.IsHeroBuild()) return Save->IsHeroUnlocked(Build.EquippedHeroId);
	if (!Save->IsRaceUnlocked(Build.RaceId)) return false;
	for (FName Id : Build.WeaponIds) if (!Save->IsWeaponUnlocked(Id)) return false;
	for (FName Id : Build.PowerIds) if (!Save->IsPowerUnlocked(Id)) return false;
	return true;
}

bool UCosmereUIManager::ConfirmCharacter()
{
	UCosmereSaveSubsystem* Save = ResolveSave();
	const int32 Index = FindCharacterIndex(PendingCharacterId);
	const TSoftClassPtr<UCosmereMenuScreen>* MainReference = ScreenClasses.Find(ECosmereUIRoute::MainMenu);
	UClass* MainClass = MainReference ? MainReference->LoadSynchronous() : nullptr;
	if (!Save || Index == INDEX_NONE || !CanConfirmCharacter()) return Fail(TEXT("Select a valid character before confirming."));
	if (!MainClass || !MainClass->IsChildOf(UCosmereMenuScreen::StaticClass()))
		return Fail(TEXT("Main Menu must be configured before confirming a character."));
	if (!Save->SetSelectedCharacterIndex(Index)) return Fail(Save->GetLastError().ToString());
	UE_LOG(LogCosmereUI, Log, TEXT("Confirmed character %s."), *PendingCharacterId.ToString());
	return Navigate(ECosmereUIRoute::MainMenu, true);
}

bool UCosmereUIManager::EnterPractice()
{
	APlayerController* Controller = CurrentController.Get();
	ACosmerePracticeGameMode* Mode = Controller ? Controller->GetWorld()->GetAuthGameMode<ACosmerePracticeGameMode>() : nullptr;
	UCosmereSaveSubsystem* Save = ResolveSave();
	FSpiritwebCharacterSave Build;
	FText Error;
	const bool bLeavingMenu = IsMenuOpen();
	if (!Mode || !Save || !Save->GetSelectedCharacter(Build))
		return Fail(TEXT("Practice requires the practice hub and a confirmed character."));
	if (!Mode->StartPractice(Controller, Build, Error)) return Fail(Error.ToString());
	{
		TGuardValue<bool> RouteGuard(bChangingRoute, true);
		if (ModalStack) ModalStack->ClearWidgets();
		if (ScreenStack) ScreenStack->ClearWidgets();
	}
	if (RootWidget) RootWidget->SetVisibility(ESlateVisibility::Collapsed);
	if (MenuPreviewActor.IsValid()) MenuPreviewActor->SetActorHiddenInGame(true);
	Controller->SetViewTarget(Controller->GetPawn());
	SetRoute(ECosmereUIRoute::Gameplay);
	ApplyInputMode(false);
	if (bLeavingMenu)
	{
		// Ignore-input flags are stacked counters. Release all menu locks only at
		// the successful standalone practice-entry boundary, not during normal play.
		Controller->ResetIgnoreMoveInput();
		Controller->ResetIgnoreLookInput();
	}
	// CommonUI can finish its deactivation on the next frame. Restore this same
	// controller only while practice is still the active route.
	Controller->GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
		[this, PracticeController = TWeakObjectPtr<APlayerController>(Controller), bLeavingMenu]()
		{
			APlayerController* PC = PracticeController.Get();
			if (!PC || PC != CurrentController.Get() || CurrentRoute != ECosmereUIRoute::Gameplay) return;
			ACosmerePracticeGameMode* PracticeMode = PC->GetWorld()->GetAuthGameMode<ACosmerePracticeGameMode>();
			if (!PracticeMode || !PracticeMode->IsPracticeRunning()) return;
			ApplyInputMode(false);
			if (bLeavingMenu)
			{
				PC->ResetIgnoreMoveInput();
				PC->ResetIgnoreLookInput();
			}
			UE_LOG(LogCosmereUI, Log, TEXT("Practice input handoff: pawn=%s, movement ignored=%d, look ignored=%d."),
				*GetNameSafe(PC->GetPawn()), PC->IsMoveInputIgnored(), PC->IsLookInputIgnored());
		}));
	return true;
}

bool UCosmereUIManager::ReturnFromPractice()
{
	APlayerController* Controller = CurrentController.Get();
	ACosmerePracticeGameMode* Mode = Controller ? Controller->GetWorld()->GetAuthGameMode<ACosmerePracticeGameMode>() : nullptr;
	if (!Mode || !Controller->IsLocalController() || Controller->GetWorld()->GetNetMode() != NM_Standalone)
		return Fail(TEXT("This world is not a local standalone practice hub."));
	Mode->StopPractice(Controller);
	return Navigate(ECosmereUIRoute::MainMenu, true);
}

void UCosmereUIManager::ApplyInputMode(bool bMenu)
{
	APlayerController* Controller = CurrentController.Get();
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (!Controller || !LocalPlayer) return;
 if(bMenu)
 {
  if(auto* Character=Cast<ACosmereCharacter>(Controller->GetPawn()))
  {
   Character->SetPowerSlotHeld(EPowerSlot::Alpha,false); Character->SetPowerSlotHeld(EPowerSlot::Beta,false);
   if(auto* Weapon=Character->FindComponentByClass<UCosmereWeaponComponent>()) Weapon->SetTriggerHeld(false);
  }
 }
	if (UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
	{
		FModifyContextOptions Options;
		Options.bIgnoreAllPressedKeysUntilRelease = true;
		Options.bForceImmediately = true;
		if (bMenu)
		{
			for (const TSoftObjectPtr<UInputMappingContext>& Reference : GameplayContexts)
			{
				UInputMappingContext* Context = Reference.LoadSynchronous();
				int32 Priority = 0;
				if (Context && Input->HasMappingContext(Context, Priority))
				{
					if (!SuspendedContexts.ContainsByPredicate([Context](const FCosmereSuspendedInputContext& Entry)
						{ return Entry.Context == Context; }))
					{
						FCosmereSuspendedInputContext Entry;
						Entry.Context = Context;
						Entry.Priority = Priority;
						SuspendedContexts.Add(Entry);
					}
					Input->RemoveMappingContext(Context, Options);
				}
			}
		}
		else
		{
			for (const FCosmereSuspendedInputContext& Entry : SuspendedContexts)
				if (Entry.Context) Input->AddMappingContext(Entry.Context, Entry.Priority, Options);
			SuspendedContexts.Reset();
		}
	}
 if(bMenuInputBlocked!=bMenu)
 {
  Controller->SetIgnoreMoveInput(bMenu);Controller->SetIgnoreLookInput(bMenu);bMenuInputBlocked=bMenu;
  if(Controller->PlayerInput) Controller->PlayerInput->FlushPressedKeys();
 }
	if (UCommonUIActionRouterBase* Router = LocalPlayer->GetSubsystem<UCommonUIActionRouterBase>())
	{
		FUIInputConfig DesiredConfig(bMenu ? ECommonInputMode::Menu : ECommonInputMode::Game,
			bMenu ? EMouseCaptureMode::NoCapture : EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown,
			!bMenu);
		DesiredConfig.bIgnoreMoveInput = bMenu;
		DesiredConfig.bIgnoreLookInput = bMenu;
		Router->SetActiveUIInputConfig(DesiredConfig, this);
	}
	Controller->bShowMouseCursor = bMenu;
	if (ACosmereHUD* HUD = Cast<ACosmereHUD>(Controller->GetHUD())) HUD->SetGameplayHUDEnabled(!bMenu);
	if (bMenu)
	{
		const ACosmereGameMode* Mode = Controller->GetWorld()->GetAuthGameMode<ACosmereGameMode>();
		if (Mode && Mode->IsFrontEndSession())
		{
			if (MenuCameraRig.IsValid()) Controller->SetViewTarget(MenuCameraRig.Get());
			if (MenuPreviewActor.IsValid()) MenuPreviewActor->SetActorHiddenInGame(false);
			if (APawn* Pawn = Controller->GetPawn())
			{
				Pawn->SetActorHiddenInGame(true);
				Pawn->SetActorEnableCollision(false);
				if (ACosmereCharacter* Character = Cast<ACosmereCharacter>(Pawn))
				{
					Character->GetCharacterMovement()->StopMovementImmediately();
					Character->GetCharacterMovement()->DisableMovement();
				}
			}
		}
	}
}

void UCosmereUIManager::SetRoute(ECosmereUIRoute Route)
{
	if (CurrentRoute == Route) return;
	CurrentRoute = Route;
 HandlePreferencesChanged();
	UE_LOG(LogCosmereUI, Log, TEXT("Route changed to %s."),
		*StaticEnum<ECosmereUIRoute>()->GetNameStringByValue(static_cast<int64>(Route)));
	OnRouteChanged.Broadcast(Route);
	const bool bShop = Route == ECosmereUIRoute::ShopHub || Route == ECosmereUIRoute::SpiritWeb
		|| Route == ECosmereUIRoute::Armory || Route == ECosmereUIRoute::IdentityStore;
	if (Route != ECosmereUIRoute::Gameplay)
		OnScenePoseRequested.Broadcast(bShop ? FName(TEXT("Shop")) : FName(TEXT("MainMenu")));
}

void UCosmereUIManager::SetMenuSceneActors(AActor* CameraRig, AActor* PreviewActor)
{
	APlayerController* Controller = CurrentController.Get();
	if (!Controller || !Controller->IsLocalController() || Controller->GetWorld()->GetNetMode() != NM_Standalone) return;
	if ((CameraRig && CameraRig->GetWorld() != Controller->GetWorld()) || (PreviewActor && PreviewActor->GetWorld() != Controller->GetWorld())) return;
	if (ACosmereMenuCameraRig* Previous = Cast<ACosmereMenuCameraRig>(MenuCameraRig.Get()))
		OnScenePoseRequested.RemoveDynamic(Previous, &ACosmereMenuCameraRig::RequestPose);
	MenuCameraRig = CameraRig;
	MenuPreviewActor = PreviewActor;
	if (PreviewActor) PreviewActor->SetActorHiddenInGame(!IsMenuOpen());
	if (ACosmereMenuCameraRig* NativeRig = Cast<ACosmereMenuCameraRig>(CameraRig))
	{
  if(BoundSettings.IsValid()) NativeRig->SetReducedMotion(BoundSettings->GetRuntimePreferences().bReducedMotion);
		OnScenePoseRequested.AddUniqueDynamic(NativeRig, &ACosmereMenuCameraRig::RequestPose);
		const bool bShop = CurrentRoute == ECosmereUIRoute::ShopHub || CurrentRoute == ECosmereUIRoute::SpiritWeb
			|| CurrentRoute == ECosmereUIRoute::Armory || CurrentRoute == ECosmereUIRoute::IdentityStore;
		if (IsMenuOpen()) NativeRig->RequestPose(bShop ? FName(TEXT("Shop")) : FName(TEXT("MainMenu")));
	}
	if (IsMenuOpen()) ApplyInputMode(true);
}

void UCosmereUIManager::DiscoverMenuSceneActors()
{
	APlayerController* Controller = CurrentController.Get();
	if (!Controller || !Controller->IsLocalController() || Controller->GetWorld()->GetNetMode() != NM_Standalone) return;
	AActor* Rig = MenuCameraRig.Get();
	AActor* Preview = MenuPreviewActor.Get();
	if (!Rig)
		for (TActorIterator<ACosmereMenuCameraRig> It(Controller->GetWorld()); It; ++It) { Rig = *It; break; }
	if (!Preview)
		for (TActorIterator<ACosmereMenuCharacterPreview> It(Controller->GetWorld()); It; ++It) { Preview = *It; break; }
	SetMenuSceneActors(Rig, Preview);
	if (!Rig || !Preview)
		UE_LOG(LogCosmereUI, Warning, TEXT("Menu scene needs placed native camera/preview actors or explicit SetMenuSceneActors registration. No actors were spawned."));
}

void UCosmereUIManager::HandleProfileChanged()
{
	if (PendingCharacterId.IsValid() && FindCharacterIndex(PendingCharacterId) == INDEX_NONE)
		PendingCharacterId.Invalidate();
	OnViewModelsChanged.Broadcast();
}

FCosmereCharacterCardViewModel UCosmereUIManager::BuildCard(const FSpiritwebCharacterSave& Build) const
{
	FCosmereCharacterCardViewModel Card;
	Card.CharacterId = Build.CharacterId;
	Card.CharacterName = Build.CharacterName;
	Card.bIsHeroBuild = Build.IsHeroBuild();
	const UCosmereSaveSubsystem* Save = ResolveSave();
	if (Save)
	{
		Card.Nicrosil = Save->GetCurrency(ECosmereCurrency::Nicrosil);
		Card.Gold = Save->GetCurrency(ECosmereCurrency::Gold);
		Card.Duralumin = Save->GetCurrency(ECosmereCurrency::Duralumin);
	}
	const UCosmereCatalogSubsystem* Catalog = GetCatalogSource();
	if (!Catalog || !Catalog->IsCatalogReady()) return Card;
	FName RaceId = Build.RaceId;
	TArray<FName> Weapons = Build.WeaponIds;
	TArray<FName> Powers = Build.PowerIds;
	if (Build.IsHeroBuild())
	{
		FHeroDefinition Hero;
		if (!Catalog->GetHeroRow(Build.EquippedHeroId, Hero)) return Card;
		Card.HeroName = Hero.DisplayName;
		RaceId = Hero.RaceId;
		Weapons = Hero.WeaponIds;
		Powers = Hero.PowerIds;
	}
	FRaceDefinition Race;
	Card.bCatalogResolved = Catalog->GetRaceRow(RaceId, Race) && Weapons.Num() == 2 && Powers.Num() == 2;
	if (Card.bCatalogResolved)
		Card.bCatalogResolved = Weapons[0] != Weapons[1] && Powers[0] != Powers[1];
	Card.RaceName = Card.bCatalogResolved ? Race.DisplayName : FText::FromName(RaceId);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FWeaponDefinition Weapon;
		FPowerDefinition Power;
		const bool bWeapon = Weapons.IsValidIndex(Index) && Catalog->GetWeaponRow(Weapons[Index], Weapon);
		const bool bPower = Powers.IsValidIndex(Index) && Catalog->GetPowerRow(Powers[Index], Power);
		Card.bCatalogResolved &= bWeapon && bPower;
		FText& WeaponText = Index == 0 ? Card.PrimaryWeaponName : Card.SecondaryWeaponName;
		FText& PowerText = Index == 0 ? Card.AlphaPowerName : Card.BetaPowerName;
		WeaponText = bWeapon ? Weapon.DisplayName : NSLOCTEXT("CosmereUI", "MissingWeapon", "Unavailable weapon");
		PowerText = bPower ? Power.DisplayName : NSLOCTEXT("CosmereUI", "MissingPower", "Unavailable power");
	}
	return Card;
}

TArray<FCosmereCharacterCardViewModel> UCosmereUIManager::GetCharacterCards() const
{
	TArray<FCosmereCharacterCardViewModel> Cards;
	if (const UCosmereSaveSubsystem* Save = ResolveSave())
		for (const FSpiritwebCharacterSave& Build : Save->GetCharacters()) Cards.Add(BuildCard(Build));
	return Cards;
}

bool UCosmereUIManager::GetSelectedCharacterCard(FCosmereCharacterCardViewModel& OutCard) const
{
	OutCard = FCosmereCharacterCardViewModel();
	const UCosmereSaveSubsystem* Save = ResolveSave();
	FSpiritwebCharacterSave Build;
	if (!Save || !Save->GetSelectedCharacter(Build)) return false;
	OutCard = BuildCard(Build);
	return OutCard.bCatalogResolved;
}

bool UCosmereUIManager::Fail(const FString& Reason)
{
	LastError = FText::FromString(Reason);
	UE_LOG(LogCosmereUI, Warning, TEXT("%s"), *Reason);
	return false;
}

void UCosmereUIManager::RequestQuit()
{
	if (APlayerController* Controller = CurrentController.Get())
	{
		UE_LOG(LogCosmereUI, Log, TEXT("Quit requested by the local UI."));
		UKismetSystemLibrary::QuitGame(Controller, Controller, EQuitPreference::Quit, false);
	}
}

bool UCosmereUIManager::ShowConfirmation(const FText& Message,FSimpleDelegate Accept,FSimpleDelegate Decline)
{
 if(Confirmation.IsValid() && Confirmation->IsActivated()) return false;
 if(!EnsureRoot()) return false;
 TGuardValue<bool> Guard(bChangingRoute,true);
 UCosmereConfirmDialog* Dialog=ModalStack->AddWidget<UCosmereConfirmDialog>(UCosmereConfirmDialog::StaticClass(),
  [this,Message,Accept,Decline](UCosmereConfirmDialog& D){D.ScreenRoute=CurrentRoute;D.Configure(Message,Accept,Decline);});
 Confirmation=Dialog; HandlePreferencesChanged(); return Dialog!=nullptr;
}
void UCosmereUIManager::UpdateConfirmationMessage(const FText& Message)
{if(Confirmation.IsValid()) Confirmation->SetMessage(Message);}
void UCosmereUIManager::DismissConfirmation()
{if(Confirmation.IsValid()) Confirmation->DeactivateWidget();Confirmation.Reset();}
void UCosmereUIManager::ResumeGameplay()
{
 if(auto* PC=CurrentController.Get()) if(auto* S=PC->GetGameInstance()->GetSubsystem<UCosmereSessionSubsystem>())
  if(S->GetOperation()==ECosmereSessionOperation::Leaving) return;
 if(CurrentRoute!=ECosmereUIRoute::EscapeMenu && CurrentRoute!=ECosmereUIRoute::Settings) return;
 if(BoundSettings.IsValid() && BoundSettings->IsEditing()) BoundSettings->Cancel();
 {
  TGuardValue<bool> Guard(bChangingRoute,true);
  if(ModalStack) ModalStack->ClearWidgets();if(ScreenStack) ScreenStack->ClearWidgets();
 }
 Confirmation.Reset();if(RootWidget) RootWidget->SetVisibility(ESlateVisibility::Collapsed);
 SetRoute(ECosmereUIRoute::Gameplay);ApplyInputMode(false);
 if(APlayerController* PC=CurrentController.Get()) PC->GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,[this](){if(CurrentRoute==ECosmereUIRoute::Gameplay) ApplyInputMode(false);}));
}
void UCosmereUIManager::RequestReturnToMainMenu()
{
 APlayerController* PC=CurrentController.Get();if(!PC) return;
 if(PC->GetWorld()->GetNetMode()==NM_Standalone && PC->GetWorld()->GetAuthGameMode<ACosmerePracticeGameMode>()) {ReturnFromPractice();return;}
 const bool bHost=PC->GetWorld()->GetNetMode()==NM_ListenServer;
 ShowConfirmation(FText::FromString(bHost?TEXT("Leave and close your LAN server? All connected players will be disconnected."):TEXT("Leave the online match and return to Main Menu?")),
  FSimpleDelegate::CreateWeakLambda(this,[this]()
  {
   if(auto* Controller=CurrentController.Get())
   {
    ApplyInputMode(true);
    if(auto* Sessions=Controller->GetGameInstance()->GetSubsystem<UCosmereSessionSubsystem>()) Sessions->LeaveSession(true);
    else Fail(TEXT("Session cleanup subsystem unavailable; no UI-only travel was performed."));
   }
  }),FSimpleDelegate());
}
void UCosmereUIManager::ScaleWidgetText(UUserWidget* Widget,float Scale)
{
 if(!Widget || !Widget->WidgetTree) return;
 TArray<UWidget*> Children;Widget->WidgetTree->GetAllWidgets(Children);
 for(UWidget* Child:Children)
 {
  if(auto* Text=Cast<UTextBlock>(Child))
  {
   FSlateFontInfo Font=Text->GetFont();
   const TWeakObjectPtr<UTextBlock> Key(Text);
   int32* Base=BaseTextSizes.Find(Key);if(!Base) Base=&BaseTextSizes.Add(Key,Font.Size);
   const int32 Desired=FMath::RoundToInt(*Base*Scale);
   if(Font.Size!=Desired) {Font.Size=Desired;Text->SetFont(Font);}
  }
  if(auto* Nested=Cast<UUserWidget>(Child)) ScaleWidgetText(Nested,Scale);
 }
}
void UCosmereUIManager::HandlePreferencesChanged()
{
 const auto P=BoundSettings.IsValid()?BoundSettings->GetRuntimePreferences():FCosmerePreferences();
 if(auto* Rig=Cast<ACosmereMenuCameraRig>(MenuCameraRig.Get())) Rig->SetReducedMotion(P.bReducedMotion);
 for(auto It=BaseTextSizes.CreateIterator();It;++It) if(!It.Key().IsValid()) It.RemoveCurrent();
 ScaleWidgetText(RootWidget,P.TextScale);
 // Stack children can be held in Slate rather than the root WidgetTree.
 if(ScreenStack) ScaleWidgetText(Cast<UUserWidget>(ScreenStack->GetActiveWidget()),P.TextScale);
 if(ModalStack) ScaleWidgetText(Cast<UUserWidget>(ModalStack->GetActiveWidget()),P.TextScale);
}
