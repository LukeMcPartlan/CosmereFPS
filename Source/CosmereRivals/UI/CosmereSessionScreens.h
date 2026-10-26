#pragma once
#include "CoreMinimal.h"
#include "UI/CosmereMenuScreen.h"
#include "Net/CosmereSessionSubsystem.h"
#include "CosmereSessionScreens.generated.h"
class UCosmereNativeButton;
class UTextBlock;
class UVerticalBox;

UCLASS(Blueprintable)
class COSMERERIVALS_API UCosmereServerMenuScreen:public UCosmereMenuScreen
{
 GENERATED_BODY()
public:
 UCosmereServerMenuScreen(const FObjectInitializer& O);
protected:
 virtual void NativeOnInitialized() override;
 virtual void NativeOnActivated() override;
 virtual void NativeOnDeactivated() override;
 virtual void NativeDestruct() override;
private:
 UFUNCTION() void RefreshState();
 UFUNCTION() void ListingsUpdated(bool bSuccess);
 void Detach();
 void SelectListing(int32 Index,FCosmereSessionListing Listing);
 UPROPERTY(Transient) TObjectPtr<UCosmereSessionSubsystem> Sessions;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> List;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
 UPROPERTY(Transient) TObjectPtr<UCosmereNativeButton> HostButton;
 UPROPERTY(Transient) TObjectPtr<UCosmereNativeButton> RefreshButton;
 UPROPERTY(Transient) TObjectPtr<UCosmereNativeButton> JoinButton;
 UPROPERTY(Transient) TArray<TObjectPtr<UCosmereNativeButton>> MapButtons;
 UPROPERTY(Transient) TArray<TObjectPtr<UCosmereNativeButton>> ListingButtons;
 FCosmereSessionListing Selected;
 FName SelectedMapId=FName(TEXT("Urithiru"));
 int32 SelectedIndex=INDEX_NONE;
};
UCLASS(Blueprintable)
class COSMERERIVALS_API UCosmereEscapeMenuScreen:public UCosmereMenuScreen
{
 GENERATED_BODY()
public:
 UCosmereEscapeMenuScreen(const FObjectInitializer& O);
protected:
 virtual void NativeOnInitialized() override;
 virtual bool NativeOnHandleBackAction() override;
};
