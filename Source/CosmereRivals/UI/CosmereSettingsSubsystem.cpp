#include "UI/CosmereSettingsSubsystem.h"
#include "CosmereRivals.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/GameUserSettings.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetSystemLibrary.h"

bool FCosmereVideoSettings::operator==(const FCosmereVideoSettings& R) const
{ return Resolution == R.Resolution && WindowMode == R.WindowMode && bVSync == R.bVSync && FrameLimit == R.FrameLimit && Quality == R.Quality; }
namespace
{
 FCosmereVideoSettings ReadVideo()
 {
  FCosmereVideoSettings V;
  if (UGameUserSettings* G = UGameUserSettings::GetGameUserSettings())
  { V.Resolution = G->GetScreenResolution(); V.WindowMode = G->GetFullscreenMode(); V.bVSync = G->IsVSyncEnabled(); V.FrameLimit = G->GetFrameRateLimit(); V.Quality = G->GetOverallScalabilityLevel(); }
  return V;
 }
 bool Reserved(FKey Key)
 {
  return Key == EKeys::Escape || Key == EKeys::Tab || Key == EKeys::Enter
   || Key == EKeys::Up || Key == EKeys::Down || Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Tilde
   || Key == EKeys::Gamepad_Special_Right || Key == EKeys::Gamepad_Special_Left
   || Key == EKeys::Gamepad_FaceButton_Bottom || Key == EKeys::Gamepad_FaceButton_Right
   || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right;
 }
}
void UCosmereSettingsSubsystem::Deinitialize()
{ Cancel(); StopDisplayTimer(); Super::Deinitialize(); }
bool UCosmereSettingsSubsystem::EnsureSettings()
{
 if (UserSettings) return true;
 auto* Input = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
 UserSettings = Input ? Cast<UCosmereInputUserSettings>(Input->GetUserSettings()) : nullptr;
 if (!UserSettings) return Fail(TEXT("Enhanced Input user settings are not ready or UserSettingsClass is not CosmereInputUserSettings."));
 for (const TCHAR* Name : {TEXT("IMC_Default"), TEXT("IMC_MouseLook"), TEXT("IMC_Match")})
 {
  TSoftObjectPtr<UInputMappingContext> Ref(FSoftObjectPath(FString::Printf(TEXT("/Game/Input/%s.%s"),Name,Name)));
  if (const UInputMappingContext* IMC = Ref.LoadSynchronous()) UserSettings->RegisterInputMappingContext(IMC);
  else UE_LOG(LogCosmereUI, Warning, TEXT("Settings cannot register missing context %s."),Name);
 }
 ApplyRuntimePreferences();
 return true;
}
void UCosmereSettingsSubsystem::Snapshot()
{
 Original = Pending = UserSettings->Preferences;
 OriginalVideo = PendingVideo = ReadVideo();
 if (auto* G = UGameUserSettings::GetGameUserSettings()) OriginalQuality = G->ScalabilityQuality;
 Bindings.Reset();
 if (auto* Profile = UserSettings->GetActiveKeyProfile())
 {
  for (const auto& Pair : Profile->GetPlayerMappingRows()) for (const FPlayerKeyMapping& M : Pair.Value.Mappings)
  {
   FCosmereBindingEdit E;
   E.Args.MappingName = M.GetMappingName(); E.Args.ProfileIdString = UserSettings->GetActiveKeyProfileId();
   E.Args.Slot = M.GetSlot(); E.Args.HardwareDeviceId = M.GetHardwareDeviceId().HardwareDeviceIdentifier;
   E.Args.NewKey = M.GetCurrentKey(); E.Args.bCreateMatchingSlotIfNeeded = false;
   E.DisplayName = M.GetDisplayName(); E.DefaultKey = M.GetDefaultKey(); Bindings.Add(E);
  }
 }
   Bindings.Sort([](const FCosmereBindingEdit& A,const FCosmereBindingEdit& B)
 {
  const int32 N = A.Args.MappingName.Compare(B.Args.MappingName);
  if (N != 0) return N < 0;
  if (A.Args.NewKey.IsGamepadKey() != B.Args.NewKey.IsGamepadKey()) return !A.Args.NewKey.IsGamepadKey();
  const int32 DeviceOrder = A.Args.HardwareDeviceId.Compare(B.Args.HardwareDeviceId);
  if (DeviceOrder != 0) return DeviceOrder < 0;
  return static_cast<uint8>(A.Args.Slot) < static_cast<uint8>(B.Args.Slot);
 });
 OriginalBindings = Bindings;
}
bool UCosmereSettingsSubsystem::BeginEdit()
{
 if (bEditing) return false;
 if (!EnsureSettings()) return false;
 Snapshot(); bEditing = true; LastError = FText::GetEmpty(); OnStateChanged.Broadcast(); return true;
}
FCosmerePreferences UCosmereSettingsSubsystem::GetRuntimePreferences() const
{ return bEditing ? Pending : UserSettings ? UserSettings->Preferences : FCosmerePreferences(); }
FVector2D UCosmereSettingsSubsystem::AdjustLook(FVector2D Axis, bool bGamepad) const
{
 const auto P = GetRuntimePreferences();
 Axis *= bGamepad ? P.GamepadSensitivity : P.MouseSensitivity;
 if (bGamepad ? P.bInvertGamepadY : P.bInvertMouseY) Axis.Y *= -1;
 return Axis;
}
void UCosmereSettingsSubsystem::SetPendingPreferences(FCosmerePreferences V)
{
 if (!bEditing || DisplayDeadline > 0) return;
 if(!FMath::IsFinite(V.MouseSensitivity) || !FMath::IsFinite(V.GamepadSensitivity) || !FMath::IsFinite(V.MasterVolume) || !FMath::IsFinite(V.SFXVolume) || !FMath::IsFinite(V.MusicVolume) || !FMath::IsFinite(V.TextScale))
 {Fail(TEXT("Settings must contain finite numbers."));return;}
 V.MouseSensitivity = FMath::Clamp(V.MouseSensitivity,0.1f,5.f); V.GamepadSensitivity = FMath::Clamp(V.GamepadSensitivity,0.1f,5.f);
 V.MasterVolume = FMath::Clamp(V.MasterVolume,0.f,1.f); V.SFXVolume = FMath::Clamp(V.SFXVolume,0.f,1.f); V.MusicVolume = FMath::Clamp(V.MusicVolume,0.f,1.f);
 V.TextScale = FMath::Clamp(V.TextScale,1.f,1.5f);
 Pending = V; ApplyRuntimePreferences(); OnPreviewChanged.Broadcast(); OnStateChanged.Broadcast();
}
void UCosmereSettingsSubsystem::SetPendingVideo(FCosmereVideoSettings V)
{
 if (!bEditing || DisplayDeadline > 0) return;
 if(!FMath::IsFinite(V.FrameLimit)) {Fail(TEXT("Frame limit must be a finite number."));return;}
 V.WindowMode = FMath::Clamp(V.WindowMode,0,2); V.Quality = FMath::Clamp(V.Quality,-1,4); V.FrameLimit = FMath::Clamp(V.FrameLimit,0.f,360.f);
 PendingVideo = V; PreviewVideo(false); OnStateChanged.Broadcast();
}
TArray<FIntPoint> UCosmereSettingsSubsystem::GetSupportedResolutions() const
{
 TArray<FIntPoint> Out;
 UKismetSystemLibrary::GetSupportedFullscreenResolutions(Out);
 Out.RemoveAll([](const FIntPoint& R){ return R.X < 640 || R.Y < 480; });
 Out.AddUnique(ReadVideo().Resolution);
 Out.Sort([](const FIntPoint& A,const FIntPoint& B){return A.X == B.X ? A.Y < B.Y : A.X < B.X;});
 return Out;
}
bool UCosmereSettingsSubsystem::StageKey(int32 Row,FKey Key)
{
 if (!bEditing || DisplayDeadline > 0 || !Bindings.IsValidIndex(Row)) return Fail(TEXT("No editable mapping row."));
 const auto& Old = Bindings[Row].Args.NewKey;
 if(Key==Old) {LastError=FText::GetEmpty();OnStateChanged.Broadcast();return true;}
 const UInputAction* Action=UserSettings?UserSettings->FindInputActionForMapping(Bindings[Row].Args.MappingName):nullptr;
 if(Action && (Action->ValueType!=EInputActionValueType::Boolean || Action->GetFName()==TEXT("IA_Escape")))
  return Fail(TEXT("Axis actions and menu-opening mappings retain their authored chains/navigation."));
 if (!Key.IsValid() || Key == EKeys::AnyKey || Key.IsAxis1D() || Key.IsAxis2D() || Key.IsAxis3D() || Key.IsTouch() || Key.IsGesture()) return Fail(TEXT("Choose one physical digital key/button; axis and chord replacement is unsupported."));
 if (Old.IsAxis1D() || Old.IsAxis2D() || Old.IsAxis3D()) return Fail(TEXT("Axis mappings keep their existing modifier chains and cannot be rebound here."));
 if (Old==EKeys::Escape || Old==EKeys::Gamepad_Special_Right || Reserved(Key)) return Fail(TEXT("This key is reserved for reliable menu/navigation. Existing navigation mappings are protected."));
 if (Old.IsGamepadKey() != Key.IsGamepadKey()) return Fail(TEXT("Use the matching keyboard/mouse or gamepad device row."));
 for (int32 I=0; I<Bindings.Num(); ++I) if (I!=Row && Bindings[I].Args.NewKey == Key && Bindings[I].Args.MappingName != Bindings[Row].Args.MappingName)
  return Fail(FString::Printf(TEXT("Conflict: %s already uses %s. Choose a free key; no mapping was replaced."),*Bindings[I].DisplayName.ToString(),*Key.GetDisplayName().ToString()));
 Bindings[Row].Args.NewKey = Key; LastError = FText::GetEmpty(); OnStateChanged.Broadcast(); return true;
}
bool UCosmereSettingsSubsystem::IsDirty() const
{
 if (!bEditing) return false;
 if (!(Pending == Original) || !(PendingVideo == OriginalVideo) || Bindings.Num()!=OriginalBindings.Num()) return true;
 for (int32 I=0; I<Bindings.Num(); ++I) if (Bindings[I].Args.NewKey != OriginalBindings[I].Args.NewKey) return true;
 return false;
}
void UCosmereSettingsSubsystem::PreviewVideo(bool bResolution)
{
 if (auto* G = UGameUserSettings::GetGameUserSettings())
 {
  G->SetVSyncEnabled(PendingVideo.bVSync); G->SetFrameRateLimit(PendingVideo.FrameLimit);
  if (PendingVideo.Quality >= 0) G->SetOverallScalabilityLevel(PendingVideo.Quality); else G->ScalabilityQuality = OriginalQuality;
  G->ApplyNonResolutionSettings();
  if (bResolution) { G->SetScreenResolution(PendingVideo.Resolution); G->SetFullscreenMode(static_cast<EWindowMode::Type>(PendingVideo.WindowMode)); G->ApplyResolutionSettings(false); }
 }
}
void UCosmereSettingsSubsystem::RestoreVideo()
{
 if (auto* G = UGameUserSettings::GetGameUserSettings())
 {
  G->SetScreenResolution(OriginalVideo.Resolution); G->SetFullscreenMode(static_cast<EWindowMode::Type>(OriginalVideo.WindowMode));
  G->SetVSyncEnabled(OriginalVideo.bVSync); G->SetFrameRateLimit(OriginalVideo.FrameLimit); G->ScalabilityQuality = OriginalQuality;
  G->ApplyResolutionSettings(false); G->ApplyNonResolutionSettings();
 }
}
void UCosmereSettingsSubsystem::RestoreDefaults()
{
 if (!bEditing || DisplayDeadline>0) return;
 SetPendingPreferences(FCosmerePreferences());
 for (auto& E : Bindings) E.Args.NewKey = E.DefaultKey;
 auto V = OriginalVideo;
 if (auto* G = UGameUserSettings::GetGameUserSettings()) V.Resolution = G->GetDesktopResolution();
 V.WindowMode=1; V.Quality=3; V.bVSync=false; V.FrameLimit=0; SetPendingVideo(V);
}
bool UCosmereSettingsSubsystem::Apply()
{
 if (!bEditing || DisplayDeadline>0) return false;
 if (!GetSupportedResolutions().Contains(PendingVideo.Resolution)) return Fail(TEXT("Choose a supported resolution."));
 if (PendingVideo.Resolution != OriginalVideo.Resolution || PendingVideo.WindowMode != OriginalVideo.WindowMode)
 {
  PreviewVideo(true); DisplayDeadline = FPlatformTime::Seconds()+15.;
  DisplayTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&ThisClass::TickDisplay),0.05f);
  OnStateChanged.Broadcast(); return true;
 }
 return Commit();
}
void UCosmereSettingsSubsystem::StopDisplayTimer()
{ if (DisplayTicker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(DisplayTicker); DisplayTicker.Reset(); DisplayDeadline=0; }
bool UCosmereSettingsSubsystem::TickDisplay(float)
{ if (FPlatformTime::Seconds()>=DisplayDeadline) { ConfirmDisplay(false); return false; } OnStateChanged.Broadcast(); return true; }
int32 UCosmereSettingsSubsystem::GetDisplaySecondsRemaining() const
{ return DisplayDeadline>0 ? FMath::Max(0,FMath::CeilToInt(DisplayDeadline-FPlatformTime::Seconds())) : 0; }
void UCosmereSettingsSubsystem::ConfirmDisplay(bool bKeep)
{
 if (DisplayDeadline<=0) return;
 bKeep = bKeep && FPlatformTime::Seconds() < DisplayDeadline;
 StopDisplayTimer();
 if (bKeep) Commit();
 else { RestoreVideo(); PendingVideo=OriginalVideo; LastError=FText::FromString(TEXT("Display reverted; other pending changes have not been saved.")); OnStateChanged.Broadcast(); }
}
void UCosmereSettingsSubsystem::RebuildInput()
{
 if (auto* I=GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
 { FModifyContextOptions O; O.bForceImmediately=true; O.bIgnoreAllPressedKeysUntilRelease=true; I->RequestRebuildControlMappings(O,EInputMappingRebuildType::RebuildWithFlush); }
}
bool UCosmereSettingsSubsystem::WriteKeys(const TArray<FCosmereBindingEdit>& Edits)
{
 for (const auto& E:Edits)
 {
  FGameplayTagContainer Reasons; UserSettings->MapPlayerKey(E.Args,Reasons);
  if (!Reasons.IsEmpty()) return Fail(FString::Printf(TEXT("Mapping %s failed: %s"),*E.Args.MappingName.ToString(),*Reasons.ToStringSimple()));
 }
 return true;
}
bool UCosmereSettingsSubsystem::Commit()
{
 // A prior failed commit restores the real engine settings but deliberately retains edits.
 // Reapply them before saving, including the full custom quality snapshot.
 PreviewVideo(true);
 if (!WriteKeys(Bindings)) { WriteKeys(OriginalBindings); RebuildInput(); RestoreVideo(); OnStateChanged.Broadcast(); return false; }
 UserSettings->Preferences=Pending;
 if (!UserSettings->TrySave())
 { UserSettings->Preferences=Original; WriteKeys(OriginalBindings); RebuildInput(); RestoreVideo(); return Fail(TEXT("Settings could not be saved. Applied input/video state was rolled back; edits remain pending.")); }
 if (auto* G=UGameUserSettings::GetGameUserSettings()) { G->ConfirmVideoMode(); G->SaveSettings(); }
 UserSettings->ApplySettings(); RebuildInput(); Snapshot(); ApplyRuntimePreferences();
 UE_LOG(LogCosmereUI,Log,TEXT("Settings committed independently of character profile."));
 OnApplied.Broadcast(); OnStateChanged.Broadcast(); return true;
}
void UCosmereSettingsSubsystem::Cancel()
{
 if (!bEditing) return;
 StopDisplayTimer(); Pending=Original; PendingVideo=OriginalVideo; Bindings=OriginalBindings;
 RestoreVideo(); bEditing=false; ApplyRuntimePreferences(); OnPreviewChanged.Broadcast(); OnStateChanged.Broadcast();
 UE_LOG(LogCosmereUI,Log,TEXT("Pending settings cancelled; reversible previews restored."));
}
bool UCosmereSettingsSubsystem::HasAudioRouting() const
{ return MasterClass.IsValid() && SFXClass.IsValid() && MusicClass.IsValid() && SettingsMix.IsValid(); }
void UCosmereSettingsSubsystem::ApplyRuntimePreferences()
{
 const auto P=GetRuntimePreferences();
 USoundMix* Mix=SettingsMix.LoadSynchronous();
 USoundClass* Master=MasterClass.LoadSynchronous(); USoundClass* SFX=SFXClass.LoadSynchronous(); USoundClass* Music=MusicClass.LoadSynchronous();
 UWorld* World=GetWorld();
 if (!World || !Mix || !Master || !SFX || !Music) return;
 if (MixedWorld.Get()!=World) { UGameplayStatics::PushSoundMixModifier(World,Mix); MixedWorld=World; }
 // Mix adjusters run after static class hierarchy propagation in FAudioDevice.
 // Master must recurse; child overrides are separate multipliers (Master * SFX/Music).
 UGameplayStatics::SetSoundMixClassOverride(World,Mix,Master,P.MasterVolume,1.f,0.05f,true);
 UGameplayStatics::SetSoundMixClassOverride(World,Mix,SFX,P.SFXVolume,1.f,0.05f,false);
 UGameplayStatics::SetSoundMixClassOverride(World,Mix,Music,P.MusicVolume,1.f,0.05f,false);
}
FText UCosmereSettingsSubsystem::GetBindingLabel(FName MappingName,bool bGamepad) const
{
 if (UserSettings && UserSettings->GetActiveKeyProfile())
  if (const auto* Row=UserSettings->GetActiveKeyProfile()->GetPlayerMappingRows().Find(MappingName))
  {
   const FPlayerKeyMapping* Best = nullptr;
   for (const auto& M:Row->Mappings) if (M.GetCurrentKey().IsValid() && M.GetCurrentKey().IsGamepadKey()==bGamepad)
    if (!Best || static_cast<uint8>(M.GetSlot()) < static_cast<uint8>(Best->GetSlot())) Best = &M;
   if (Best) return Best->GetCurrentKey().GetDisplayName();
  }
 return FText::FromString(TEXT("Unregistered"));
}
bool UCosmereSettingsSubsystem::Fail(const FString& Reason)
{ LastError=FText::FromString(Reason); UE_LOG(LogCosmereUI,Warning,TEXT("%s"),*Reason); OnStateChanged.Broadcast(); return false; }
