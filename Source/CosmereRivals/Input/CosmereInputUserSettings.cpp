#include "Input/CosmereInputUserSettings.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedActionKeyMapping.h"
#include "GameFramework/InputSettings.h"
#include "Kismet/GameplayStatics.h"

bool FCosmerePreferences::operator==(const FCosmerePreferences& R) const
{
 return MouseSensitivity == R.MouseSensitivity && GamepadSensitivity == R.GamepadSensitivity
  && bInvertMouseY == R.bInvertMouseY && bInvertGamepadY == R.bInvertGamepadY
  && MasterVolume == R.MasterVolume && SFXVolume == R.SFXVolume && MusicVolume == R.MusicVolume
  && TextScale == R.TextScale && bReducedMotion == R.bReducedMotion;
}
bool UCosmereInputUserSettings::TrySave()
{
 ULocalPlayer* LP = GetLocalPlayer();
 return LP && UGameplayStatics::SaveGameToSlot(this, GetSaveFilename(LP), LP->GetLocalPlayerIndex());
}
void UCosmereInputUserSettings::HandleSettingsClassChangedOnLoad_Impl(UEnhancedInputUserSettings* OldSettings)
{
 // 5.8's base implementation retains EVERY string-ID profile and the active profile.
 Super::HandleSettingsClassChangedOnLoad_Impl(OldSettings);
 if (const UCosmereInputUserSettings* Old = Cast<UCosmereInputUserSettings>(OldSettings)) Preferences = Old->Preferences;
}
bool UCosmereInputUserSettings::RegisterKeyMappingsToProfile(UEnhancedPlayerMappableKeyProfile& Profile, const UInputMappingContext* IMC)
{
 // The old engine default put keyboard and controller mappings into one unspecified-device
 // slot sequence. Re-keying it would add default bindings alongside customized old bindings.
 // Preserve that sequence for existing profiles; new profiles use per-device slot sequences.
 bool bLegacy = false;
 for (const auto& Row : Profile.GetPlayerMappingRows())
  for (const FPlayerKeyMapping& Mapping : Row.Value.Mappings)
   bLegacy |= Mapping.GetHardwareDeviceId() == FHardwareDeviceIdentifier::Invalid;
 TGuardValue<bool> Guard(bRegisterLegacyHardware, bLegacy);
 return Super::RegisterKeyMappingsToProfile(Profile, IMC);
}
FHardwareDeviceIdentifier UCosmereInputUserSettings::DetermineHardwareDeviceForActionMapping(
 const FEnhancedActionKeyMapping& Mapping, const UInputMappingContext*) const
{
 if (bRegisterLegacyHardware) return FHardwareDeviceIdentifier::Invalid;
 if (Mapping.Key.IsTouch()) return FHardwareDeviceIdentifier::DefaultMobileTouch;
 return Mapping.Key.IsGamepadKey() ? FHardwareDeviceIdentifier::DefaultGamepad
  : FHardwareDeviceIdentifier::DefaultKeyboardAndMouse;
}
