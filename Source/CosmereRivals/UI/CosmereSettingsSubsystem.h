#pragma once
#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "Scalability.h"
#include "Input/CosmereInputUserSettings.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "CosmereSettingsSubsystem.generated.h"

USTRUCT(BlueprintType)
struct COSMERERIVALS_API FCosmereVideoSettings
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadWrite) FIntPoint Resolution = FIntPoint(1280,720);
 /** 0 fullscreen, 1 borderless, 2 windowed (EWindowMode). */
 UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 WindowMode = 1;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bVSync = false;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float FrameLimit = 0.f;
 /** -1 preserves a custom preset; 0..4 selects a standard engine preset. */
 UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Quality = -1;
 bool operator==(const FCosmereVideoSettings& R) const;
};
USTRUCT(BlueprintType)
struct COSMERERIVALS_API FCosmereBindingEdit
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) FMapPlayerKeyArgs Args;
 UPROPERTY(BlueprintReadOnly) FText DisplayName;
 UPROPERTY(BlueprintReadOnly) FKey DefaultKey;
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCosmereSettingsChanged);

/** One pending transaction per local player. Rebindings are staged until Apply. */
UCLASS(Config=Game, DefaultConfig)
class COSMERERIVALS_API UCosmereSettingsSubsystem : public ULocalPlayerSubsystem
{
 GENERATED_BODY()
public:
 virtual void Deinitialize() override;
 bool EnsureSettings();
 UFUNCTION(BlueprintCallable, Category="Cosmere|Settings") bool BeginEdit();
 UFUNCTION(BlueprintCallable, Category="Cosmere|Settings") bool Apply();
 UFUNCTION(BlueprintCallable, Category="Cosmere|Settings") void Cancel();
 UFUNCTION(BlueprintCallable, Category="Cosmere|Settings") void RestoreDefaults();
 UFUNCTION(BlueprintCallable, Category="Cosmere|Settings") void SetPendingPreferences(FCosmerePreferences Value);
 UFUNCTION(BlueprintCallable, Category="Cosmere|Settings") void SetPendingVideo(FCosmereVideoSettings Value);
 UFUNCTION(BlueprintCallable, Category="Cosmere|Settings") bool StageKey(int32 Row, FKey Key);
 UFUNCTION(BlueprintCallable, Category="Cosmere|Settings") void ConfirmDisplay(bool bKeep);
 UFUNCTION(BlueprintPure, Category="Cosmere|Settings") bool IsDirty() const;
 UFUNCTION(BlueprintPure, Category="Cosmere|Settings") bool IsEditing() const { return bEditing; }
 UFUNCTION(BlueprintPure, Category="Cosmere|Settings") bool IsAwaitingDisplayConfirmation() const { return DisplayDeadline > 0; }
 UFUNCTION(BlueprintPure, Category="Cosmere|Settings") int32 GetDisplaySecondsRemaining() const;
 UFUNCTION(BlueprintPure, Category="Cosmere|Settings") FCosmerePreferences GetRuntimePreferences() const;
 /** Called by the device mapping modifier ONCE, after authored modifiers; do not also wire into BP Aim. */
 UFUNCTION(BlueprintPure, Category="Cosmere|Input") FVector2D AdjustLook(FVector2D Axis, bool bGamepad) const;
 UFUNCTION(BlueprintPure, Category="Cosmere|Input") FText GetBindingLabel(FName MappingName, bool bGamepad) const;
 UFUNCTION(BlueprintPure, Category="Cosmere|Settings") TArray<FIntPoint> GetSupportedResolutions() const;
 UPROPERTY(BlueprintReadOnly, Category="Cosmere|Settings") FCosmerePreferences Pending;
 UPROPERTY(BlueprintReadOnly, Category="Cosmere|Settings") FCosmereVideoSettings PendingVideo;
 UPROPERTY(BlueprintReadOnly, Category="Cosmere|Settings") TArray<FCosmereBindingEdit> Bindings;
 UPROPERTY(BlueprintReadOnly, Category="Cosmere|Settings") FText LastError;
 UPROPERTY(BlueprintAssignable, Category="Cosmere|Settings") FCosmereSettingsChanged OnPreviewChanged;
 UPROPERTY(BlueprintAssignable, Category="Cosmere|Settings") FCosmereSettingsChanged OnApplied;
 UPROPERTY(BlueprintAssignable, Category="Cosmere|Settings") FCosmereSettingsChanged OnStateChanged;
 UPROPERTY(Config, EditAnywhere, Category="Cosmere|Audio") TSoftObjectPtr<USoundClass> MasterClass;
 UPROPERTY(Config, EditAnywhere, Category="Cosmere|Audio") TSoftObjectPtr<USoundClass> SFXClass;
 UPROPERTY(Config, EditAnywhere, Category="Cosmere|Audio") TSoftObjectPtr<USoundClass> MusicClass;
 UPROPERTY(Config, EditAnywhere, Category="Cosmere|Audio") TSoftObjectPtr<USoundMix> SettingsMix;
 UFUNCTION(BlueprintPure, Category="Cosmere|Audio") bool HasAudioRouting() const;
 /** Reapply the runtime mix when a new world/controller arrives. */
 void ApplyRuntimePreferences();
private:
 bool Fail(const FString& Reason);
 void Snapshot();
 void PreviewVideo(bool bResolution);
 void RestoreVideo();
 bool Commit();
 bool WriteKeys(const TArray<FCosmereBindingEdit>& Edits);
 void RebuildInput();
 bool TickDisplay(float Delta);
 void StopDisplayTimer();
 UPROPERTY(Transient) TObjectPtr<UCosmereInputUserSettings> UserSettings;
 UPROPERTY(Transient) FCosmerePreferences Original;
 UPROPERTY(Transient) FCosmereVideoSettings OriginalVideo;
 UPROPERTY(Transient) TArray<FCosmereBindingEdit> OriginalBindings;
 Scalability::FQualityLevels OriginalQuality;
 TWeakObjectPtr<UWorld> MixedWorld;
 FTSTicker::FDelegateHandle DisplayTicker;
 double DisplayDeadline = 0;
 bool bEditing = false;
};
