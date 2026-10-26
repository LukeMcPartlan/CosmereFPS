#pragma once
#include "CoreMinimal.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "CosmereInputUserSettings.generated.h"

USTRUCT(BlueprintType)
struct COSMERERIVALS_API FCosmerePreferences
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float MouseSensitivity = 1.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float GamepadSensitivity = 1.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bInvertMouseY = false;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bInvertGamepadY = false;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float MasterVolume = 1.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float SFXVolume = 1.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float MusicVolume = 1.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float TextScale = 1.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bReducedMotion = false;
 bool operator==(const FCosmerePreferences& R) const;
};

/** Uses Enhanced Input's independent save slot, never the character wallet/profile. */
UCLASS()
class COSMERERIVALS_API UCosmereInputUserSettings : public UEnhancedInputUserSettings
{
 GENERATED_BODY()
public:
 UPROPERTY(SaveGame, BlueprintReadOnly, Category="Cosmere|Settings") FCosmerePreferences Preferences;
 bool TrySave();
protected:
 virtual void HandleSettingsClassChangedOnLoad_Impl(UEnhancedInputUserSettings* OldSettings) override;
 virtual bool RegisterKeyMappingsToProfile(UEnhancedPlayerMappableKeyProfile& Profile, const UInputMappingContext* IMC) override;
 virtual FHardwareDeviceIdentifier DetermineHardwareDeviceForActionMapping(const FEnhancedActionKeyMapping& Mapping, const UInputMappingContext* IMC) const override;
private:
 // Per-profile registration guard, not a saved preference. Legacy profiles keep their original slots.
 bool bRegisterLegacyHardware = false;
};
