// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "UI/CosmereUITypes.h"
#include "CosmereUIManager.generated.h"

class APlayerController;
class UCosmereSaveSubsystem;
class UCosmereCatalogSubsystem;
class UCosmereMenuScreen;
class UCommonActivatableWidget;
class UCommonActivatableWidgetStack;
class UCosmereConfirmDialog;
class UCosmereSettingsSubsystem;
class UTextBlock;
class UInputMappingContext;
class UUserWidget;
struct FSpiritwebCharacterSave;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCosmereUIRouteChanged, ECosmereUIRoute, Route);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCosmereUIViewModelsChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCosmereUIScenePoseRequested, FName, Pose);

USTRUCT()
struct FCosmereSuspendedInputContext
{
	GENERATED_BODY()
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> Context;
	UPROPERTY(Transient)
	int32 Priority = 0;
};

/** Owns local presentation only, never authoritative combat or another client's save. */
UCLASS()
class COSMERERIVALS_API UCosmereUIManager : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	UCosmereUIManager();
	virtual void Deinitialize() override;
	virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;
	/** Native screens resolve through the coordinator, keeping test overlays consistent. */
	UCosmereSaveSubsystem* GetProfileSource() const { return ResolveSave(); }
	UCosmereCatalogSubsystem* GetCatalogSource() const;
#if !UE_BUILD_SHIPPING
	/** Explicit fixture opt-in only, before any UI root/controller initialization. */
	bool SetDevelopmentSources(UCosmereSaveSubsystem* FixtureSave, UCosmereCatalogSubsystem* FixtureCatalog);
#endif

	/** Called by the existing controller after its normal local input setup. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|UI")
	bool InitializeForController(APlayerController* Controller, bool bReturningToMenu = false);
	UFUNCTION(BlueprintCallable, Category = "Cosmere|UI")
	bool Navigate(ECosmereUIRoute Route, bool bReplaceRoot = false, bool bAsModal = false);
	UFUNCTION(BlueprintCallable, Category = "Cosmere|UI")
	bool NavigateBack();
 bool PopScreen();
 void ResumeGameplay();
 void RequestReturnToMainMenu();
 void RefreshLocalPreferences() { HandlePreferencesChanged(); }
 bool ShowConfirmation(const FText& Message, FSimpleDelegate Accept, FSimpleDelegate Decline);
 void UpdateConfirmationMessage(const FText& Message);
 void DismissConfirmation();
	UFUNCTION(BlueprintCallable, Category = "Cosmere|UI")
	bool HighlightCharacter(FGuid CharacterId);
	UFUNCTION(BlueprintPure, Category = "Cosmere|UI")
	bool CanConfirmCharacter() const;

	UFUNCTION(BlueprintCallable, Category = "Cosmere|UI")
	bool ConfirmCharacter();
	UFUNCTION(BlueprintCallable, Category = "Cosmere|UI")
	bool EnterPractice();
	UFUNCTION(BlueprintCallable, Category = "Cosmere|UI")
	bool ReturnFromPractice();
	UFUNCTION(BlueprintCallable, Category = "Cosmere|UI")
	void RequestQuit();
	UFUNCTION(BlueprintCallable, Category = "Cosmere|UI")
	void SetMenuSceneActors(AActor* CameraRig, AActor* PreviewActor);
	UFUNCTION(BlueprintCallable, Category = "Cosmere|UI")
	void DiscoverMenuSceneActors();

	UFUNCTION(BlueprintPure, Category = "Cosmere|UI")
	ECosmereUIRoute GetCurrentRoute() const { return CurrentRoute; }
	UFUNCTION(BlueprintPure, Category = "Cosmere|UI")
	bool IsMenuOpen() const { return CurrentRoute != ECosmereUIRoute::None && CurrentRoute != ECosmereUIRoute::Gameplay; }
	UFUNCTION(BlueprintPure, Category = "Cosmere|UI")
	FGuid GetPendingCharacterId() const { return PendingCharacterId; }
	UFUNCTION(BlueprintPure, Category = "Cosmere|UI")
	FText GetLastError() const { return LastError; }
	UFUNCTION(BlueprintPure, Category = "Cosmere|UI")
	TArray<FCosmereCharacterCardViewModel> GetCharacterCards() const;
	UFUNCTION(BlueprintPure, Category = "Cosmere|UI")
	bool GetSelectedCharacterCard(FCosmereCharacterCardViewModel& OutCard) const;

	UPROPERTY(BlueprintAssignable, Category = "Cosmere|UI")
	FCosmereUIRouteChanged OnRouteChanged;
	UPROPERTY(BlueprintAssignable, Category = "Cosmere|UI")
	FCosmereUIViewModelsChanged OnViewModelsChanged;
	UPROPERTY(BlueprintAssignable, Category = "Cosmere|UI")
	FCosmereUIScenePoseRequested OnScenePoseRequested;

private:
	bool EnsureRoot();
	void ReleaseRoot();
	void ApplyInputMode(bool bMenu);
	void SetRoute(ECosmereUIRoute Route);
	void HandleDisplayedWidgetChanged(UCommonActivatableWidget* DisplayedWidget);
	bool Fail(const FString& Reason);
	UCosmereSaveSubsystem* ResolveSave() const;
	int32 FindCharacterIndex(FGuid CharacterId) const;
	FCosmereCharacterCardViewModel BuildCard(const FSpiritwebCharacterSave& Build) const;
	UFUNCTION()
	void HandleProfileChanged();
 UFUNCTION() void HandlePreferencesChanged();
 void ScaleWidgetText(UUserWidget* Widget, float Scale);

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> RootWidget;
	UPROPERTY(Transient)
	TObjectPtr<UCommonActivatableWidgetStack> ScreenStack;
	UPROPERTY(Transient)
	TObjectPtr<UCommonActivatableWidgetStack> ModalStack;
	UPROPERTY(Transient)
	TObjectPtr<UCosmereSaveSubsystem> BoundSave;
	UPROPERTY(Transient)
	TObjectPtr<UCosmereSaveSubsystem> DevelopmentSave;
	UPROPERTY(Transient)
	TObjectPtr<UCosmereCatalogSubsystem> DevelopmentCatalog;
	UPROPERTY(Transient)
	TSoftClassPtr<UUserWidget> RootWidgetClass;
	UPROPERTY(Transient)
	TMap<ECosmereUIRoute, TSoftClassPtr<UCosmereMenuScreen>> ScreenClasses;
	UPROPERTY(Transient)
	TArray<TSoftObjectPtr<UInputMappingContext>> GameplayContexts;
	UPROPERTY(Transient)
	TArray<FCosmereSuspendedInputContext> SuspendedContexts;

	TWeakObjectPtr<APlayerController> CurrentController;
	TWeakObjectPtr<AActor> MenuCameraRig;
	TWeakObjectPtr<AActor> MenuPreviewActor;
	ECosmereUIRoute CurrentRoute = ECosmereUIRoute::None;
	FGuid PendingCharacterId;
	FText LastError;
	bool bHasOpenedStartupSelection = false;
	bool bChangingRoute = false;
 bool bMenuInputBlocked = false;
 TWeakObjectPtr<UCosmereConfirmDialog> Confirmation;
 TWeakObjectPtr<UCosmereSettingsSubsystem> BoundSettings;
 TMap<TWeakObjectPtr<UTextBlock>,int32> BaseTextSizes;
};
