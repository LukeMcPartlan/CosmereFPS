#pragma once
#include "CoreMinimal.h"
#include "UI/CosmereMenuScreen.h"
#include "UI/CosmereUIPresentationData.h"
#include "Data/CosmereTypes.h"
#include "CosmereStoreScreen.generated.h"

class UCosmereNativeButton;
class UCosmereSaveSubsystem;
class UCosmereCatalogSubsystem;
class UTextBlock;
class UVerticalBox;
class UCanvasPanel;
class UCosmereUIManager;

USTRUCT(BlueprintType)
struct COSMERERIVALS_API FCosmereStoreEntryViewModel
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Cosmere|Store") FName CatalogKind;
	UPROPERTY(BlueprintReadOnly, Category="Cosmere|Store") FName CatalogId;
	UPROPERTY(BlueprintReadOnly, Category="Cosmere|Store") FName Category;
	UPROPERTY(BlueprintReadOnly, Category="Cosmere|Store") FText DisplayName;
	UPROPERTY(BlueprintReadOnly, Category="Cosmere|Store") FText Description;
	UPROPERTY(BlueprintReadOnly, Category="Cosmere|Store") ECosmereCurrency Currency = ECosmereCurrency::Nicrosil;
	UPROPERTY(BlueprintReadOnly, Category="Cosmere|Store") int32 Price = 0;
	UPROPERTY(BlueprintReadOnly, Category="Cosmere|Store") bool bUnlocked = false;
	UPROPERTY(BlueprintReadOnly, Category="Cosmere|Store") bool bAffordable = false;
	UPROPERTY(BlueprintReadOnly, Category="Cosmere|Store") FText EquippedStatus;
	UPROPERTY(BlueprintReadOnly, Category="Cosmere|Store") FCosmereUIEntryPresentation Presentation;
	UPROPERTY(BlueprintReadOnly, Category="Cosmere|Store") bool bHasPresentation = false;
};

/** Catalog/economy decisions stay in existing subsystems; this is presentation/action routing only. */
UCLASS(Abstract, Blueprintable)
class COSMERERIVALS_API UCosmereStoreScreen : public UCosmereMenuScreen
{
	GENERATED_BODY()
public:
	UCosmereStoreScreen(const FObjectInitializer& ObjectInitializer);
	UFUNCTION(BlueprintCallable, Category="Cosmere|Store") void SelectEntry(FName CatalogId);
	UFUNCTION(BlueprintCallable, Category="Cosmere|Store") void SetStoreCategory(FName Category);
	UFUNCTION(BlueprintCallable, Category="Cosmere|Store") void UnlockSelected();
	UFUNCTION(BlueprintCallable, Category="Cosmere|Store") void EquipSelected(int32 SlotIndex);
	UFUNCTION(BlueprintCallable, Category="Cosmere|Store") void RestoreCustomBuild();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cosmere|Store") TSoftObjectPtr<UCosmereUIPresentationData> PresentationAsset;
	UPROPERTY(BlueprintReadOnly, Category="Cosmere|Store") TArray<FCosmereStoreEntryViewModel> Entries;
	UPROPERTY(BlueprintReadOnly, Category="Cosmere|Store") FName SelectedEntryId;
	UPROPERTY(BlueprintReadOnly, Category="Cosmere|Store") FName ActiveCategory;
	UPROPERTY(BlueprintReadOnly, Category="Cosmere|Store") FGuid SelectedCharacterId;
#if !UE_BUILD_SHIPPING
	/** Accepts only isolated sources. Set before activation; does not swap real subsystems. */
	bool SetDevelopmentSources(UCosmereSaveSubsystem* FixtureSave, UCosmereCatalogSubsystem* FixtureCatalog);
#endif
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual void NativeDestruct() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
private:
	UCosmereSaveSubsystem* ResolveSave() const;
	UCosmereCatalogSubsystem* ResolveCatalog() const;
	UCosmereUIManager* ResolveManager() const;
	void BuildLayout();
	void Detach();
	void RefreshEntries();
	void RefreshDetails();
	void ConnectNavigation();
	void RememberFocus(FName Id);
	void OpenRoute(ECosmereUIRoute Route);
	void Back();
	void ShowStatus(const FText& Message);
	int32 ResolveCharacterIndex(FSpiritwebCharacterSave& OutCharacter) const;
	FName CurrentKind() const;
	const FCosmereStoreEntryViewModel* SelectedModel() const;
	UFUNCTION() void RefreshStore();
	UPROPERTY(Transient) TObjectPtr<UCosmereSaveSubsystem> BoundSave;
	UPROPERTY(Transient) TObjectPtr<UCosmereSaveSubsystem> DevelopmentSave;
	UPROPERTY(Transient) TObjectPtr<UCosmereCatalogSubsystem> DevelopmentCatalog;
	UPROPERTY(Transient) TObjectPtr<UCosmereUIPresentationData> Presentation;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UCosmereNativeButton>> Tiles;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> CatalogList;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> SymbolBoard;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Wallet;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Details;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
	UPROPERTY(Transient) TObjectPtr<UCosmereNativeButton> Unlock;
	UPROPERTY(Transient) TObjectPtr<UCosmereNativeButton> EquipOne;
	UPROPERTY(Transient) TObjectPtr<UCosmereNativeButton> EquipTwo;
	UPROPERTY(Transient) TObjectPtr<UCosmereNativeButton> ClearHero;
	UPROPERTY(Transient) TObjectPtr<UCosmereNativeButton> BackButton;
	UPROPERTY(Transient) TArray<TObjectPtr<UCosmereNativeButton>> CategoryButtons;
	FName FocusedEntryId;
	FName RenderedCategory;
	bool bActionInProgress = false;
};

UCLASS(Blueprintable)
class COSMERERIVALS_API UCosmereNativeShopHubScreen : public UCosmereStoreScreen
{
	GENERATED_BODY()
public: UCosmereNativeShopHubScreen(const FObjectInitializer& ObjectInitializer);
};
UCLASS(Blueprintable)
class COSMERERIVALS_API UCosmereNativeSpiritWebScreen : public UCosmereStoreScreen
{
	GENERATED_BODY()
public: UCosmereNativeSpiritWebScreen(const FObjectInitializer& ObjectInitializer);
};
UCLASS(Blueprintable)
class COSMERERIVALS_API UCosmereNativeArmoryScreen : public UCosmereStoreScreen
{
	GENERATED_BODY()
public: UCosmereNativeArmoryScreen(const FObjectInitializer& ObjectInitializer);
};
UCLASS(Blueprintable)
class COSMERERIVALS_API UCosmereNativeIdentityStoreScreen : public UCosmereStoreScreen
{
	GENERATED_BODY()
public: UCosmereNativeIdentityStoreScreen(const FObjectInitializer& ObjectInitializer);
};
