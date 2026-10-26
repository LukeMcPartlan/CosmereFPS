#pragma once
#include "CoreMinimal.h"
#include "Components/ComboBoxString.h"
#include "UI/CosmereMenuScreen.h"
#include "UI/CosmereSettingsSubsystem.h"
#include "CosmereSettingsScreen.generated.h"
class USlider;
class UTextBlock;
class UVerticalBox;
class UCosmereNativeButton;
class IInputProcessor;

UENUM()
enum class ECosmereSettingControl : uint8
{ Mouse, Gamepad, InvertMouse, InvertGamepad, Master, SFX, Music, Resolution, Mode, VSync, FPS, Quality, TextScale, ReducedMotion };

/** Typed row: the owning subsystem is the only transaction/persistence backend. */
UCLASS()
class COSMERERIVALS_API UCosmereSettingsControlRow : public UUserWidget
{
 GENERATED_BODY()
public:
 void Configure(UCosmereSettingsSubsystem* Source, ECosmereSettingControl InControl, const FText& Name);
 void Refresh();
protected:
 virtual void NativeOnInitialized() override;
private:
 UFUNCTION() void SliderChanged(float Value);
 UFUNCTION() void ChoiceChanged(FString Value, ESelectInfo::Type Type);
 UPROPERTY(Transient) TObjectPtr<UCosmereSettingsSubsystem> Settings;
 UPROPERTY(Transient) TObjectPtr<USlider> Slider;
 UPROPERTY(Transient) TObjectPtr<UComboBoxString> Choice;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> Label;
 UPROPERTY(Transient) TArray<FIntPoint> Resolutions;
 TArray<float> FrameLimits;
 ECosmereSettingControl Control = ECosmereSettingControl::Mouse;
 bool bRefreshing = false;
 FText RowName;
};

UCLASS(Blueprintable)
class COSMERERIVALS_API UCosmereSettingsScreen : public UCosmereMenuScreen
{
 GENERATED_BODY()
public:
 UCosmereSettingsScreen(const FObjectInitializer& O);
 UFUNCTION(BlueprintCallable, Category="Cosmere|Settings") void RequestBack();
 void BeginCapture(int32 Row, UCosmereNativeButton* Focus);
 void CaptureKey(FKey Key);
 void CancelCapture();
 bool IsCapturing() const { return CaptureRow != INDEX_NONE; }
protected:
 virtual void NativeOnInitialized() override;
 virtual void NativeOnActivated() override;
 virtual void NativeOnDeactivated() override;
 virtual void NativeDestruct() override;
 virtual bool NativeOnHandleBackAction() override;
private:
 void ShowTab(int32 Tab);
 void AddRow(ECosmereSettingControl Control, const TCHAR* Label);
 void Apply();
 void Close();
 void Detach();
 UFUNCTION() void RefreshState();
 UPROPERTY(Transient) TObjectPtr<UCosmereSettingsSubsystem> Settings;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> Content;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
 UPROPERTY(Transient) TArray<TObjectPtr<UCosmereSettingsControlRow>> Rows;
 UPROPERTY(Transient) TArray<TObjectPtr<UCosmereNativeButton>> BindingButtons;
 TWeakObjectPtr<UCosmereNativeButton> CaptureFocus;
 TSharedPtr<IInputProcessor> CaptureProcessor;
 int32 CaptureRow = INDEX_NONE;
 int32 CurrentTab = 0;
 bool bDisplayDialog = false;
};

/** Callbacks are native and one-shot; the coordinator owns this modal's stack. */
UCLASS(Blueprintable)
class COSMERERIVALS_API UCosmereConfirmDialog : public UCosmereMenuScreen
{
 GENERATED_BODY()
public:
 UCosmereConfirmDialog(const FObjectInitializer& O);
 void Configure(const FText& Message, FSimpleDelegate Accept, FSimpleDelegate Decline);
 void SetMessage(const FText& Message);
 void Resolve(bool bAccept);
protected:
 virtual void NativeOnInitialized() override;
 virtual bool NativeOnHandleBackAction() override;
private:
 UPROPERTY(Transient) TObjectPtr<UTextBlock> MessageText;
 FSimpleDelegate AcceptAction;
 FSimpleDelegate DeclineAction;
 bool bResolved = false;
};
