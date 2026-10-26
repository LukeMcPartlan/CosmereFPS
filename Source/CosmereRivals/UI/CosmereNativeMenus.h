// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "CommonButtonBase.h"
#include "Components/ComboBoxString.h"
#include "UI/CosmereMenuScreen.h"
#include "UI/CosmereUITypes.h"
#include "CosmereNativeMenus.generated.h"

class UCommonTextBlock;
class UEditableTextBox;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UImage;
class UTexture2D;
class UCosmereUIManager;
class UCosmereSaveSubsystem;

/** Native behavior; a designer-only child can supply ButtonContent and LabelText. */
UCLASS(Blueprintable)
class COSMERERIVALS_API UCosmereNativeButton : public UCommonButtonBase
{
	GENERATED_BODY()
public:
	UCosmereNativeButton(const FObjectInitializer& ObjectInitializer);
	UFUNCTION(BlueprintCallable, Category="Cosmere|UI")
	void SetButtonText(const FText& InText);
	UFUNCTION(BlueprintCallable, Category="Cosmere|UI")
	void SetButtonIcon(TSoftObjectPtr<UTexture2D> InIcon, bool bInLocked = false);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cosmere|UI")
	FText ButtonText;
protected:
	virtual void NativeOnInitialized() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativePreConstruct() override;
	virtual void NativeOnCurrentTextStyleChanged() override;
	virtual void NativeOnAddedToFocusPath(const FFocusEvent& InFocusEvent) override;
	virtual void NativeOnRemovedFromFocusPath(const FFocusEvent& InFocusEvent) override;
	UPROPERTY(Transient)
	TObjectPtr<UCommonTextBlock> NativeLabel;
	UPROPERTY(EditDefaultsOnly, Category="Cosmere|UI")
	TSoftClassPtr<UCommonButtonStyle> DefaultButtonStyle;
	UPROPERTY(Transient)
	TObjectPtr<UImage> NativeIcon;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NativeFocusText;
	UPROPERTY(Transient)
	TSoftObjectPtr<UTexture2D> ButtonIcon;
	bool bIconLocked = false;
	void RefreshIcon();
	void RefreshLabel();
};

/** A card highlights a stable ID, never writes selection to the save. */
UCLASS(Blueprintable)
class COSMERERIVALS_API UCosmereNativeCharacterCard : public UCosmereNativeButton
{
	GENERATED_BODY()
public:
	UCosmereNativeCharacterCard(const FObjectInitializer& ObjectInitializer);
	void SetCard(const FCosmereCharacterCardViewModel& InCard, bool bInSelected);
	FGuid GetCharacterId() const { return Card.CharacterId; }
protected:
	virtual void NativeOnClicked() override;
private:
	UPROPERTY(Transient)
	FCosmereCharacterCardViewModel Card;
};

/** Event-driven native menu behavior with a graph-free default layout. */
UCLASS(Abstract, Blueprintable)
class COSMERERIVALS_API UCosmereNativeMenuScreen : public UCosmereMenuScreen
{
	GENERATED_BODY()
protected:
	UCosmereNativeMenuScreen(const FObjectInitializer& ObjectInitializer);
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual void NativeDestruct() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;

	UCosmereUIManager* GetUIManager() const;
	UCosmereSaveSubsystem* GetSave() const;
	void BuildDefaultLayout();
	void BindActions();
	void PopulateCreationOptions();
	void RefreshCards();
	void ScrollToCard(FGuid CharacterId);
	void ValidateCreation();
	void ShowError(const FText& Error);
	void NavigateTo(ECosmereUIRoute Route);
	void ConfirmCharacter();
	void CreateCharacter();
	void EnterPractice();
	void GoBack();
	void Quit();
	void DetachManager();
	UFUNCTION()
	void RefreshView();
	UFUNCTION()
	void CreationOptionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION()
	void CreationNameChanged(const FText& Text);

	UPROPERTY(Transient)
	TObjectPtr<UCosmereUIManager> BoundManager;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCosmereNativeCharacterCard>> CharacterCards;
	UPROPERTY(Transient)
	TArray<FName> RaceIds;
	UPROPERTY(Transient)
	TArray<FName> WeaponIds;
	UPROPERTY(Transient)
	TArray<FName> PowerIds;
	bool bPopulatingOptions = false;
	bool bCreatingCharacter = false;
};

UCLASS(Blueprintable)
class COSMERERIVALS_API UCosmereNativeCharacterSelectScreen : public UCosmereNativeMenuScreen
{
	GENERATED_BODY()
public:
	UCosmereNativeCharacterSelectScreen(const FObjectInitializer& ObjectInitializer);
};

UCLASS(Blueprintable)
class COSMERERIVALS_API UCosmereNativeCharacterCreateScreen : public UCosmereNativeMenuScreen
{
	GENERATED_BODY()
public:
	UCosmereNativeCharacterCreateScreen(const FObjectInitializer& ObjectInitializer);
};

UCLASS(Blueprintable)
class COSMERERIVALS_API UCosmereNativeMainMenuScreen : public UCosmereNativeMenuScreen
{
	GENERATED_BODY()
public:
	UCosmereNativeMainMenuScreen(const FObjectInitializer& ObjectInitializer);
};

UCLASS(Blueprintable)
class COSMERERIVALS_API UCosmereNativeGameSelectScreen : public UCosmereNativeMenuScreen
{
	GENERATED_BODY()
public:
	UCosmereNativeGameSelectScreen(const FObjectInitializer& ObjectInitializer);
};
