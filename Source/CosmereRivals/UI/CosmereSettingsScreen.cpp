#include "UI/CosmereSettingsScreen.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "UI/CosmereNativeMenus.h"
#include "UI/CosmereUIManager.h"

namespace
{
 UCosmereNativeButton* Button(UWidgetTree* Tree,UVerticalBox* Parent,const TCHAR* Name,const FText& Text,FSimpleDelegate Click)
 {
  auto* B=Tree->ConstructWidget<UCosmereNativeButton>(UCosmereNativeButton::StaticClass(),MakeUniqueObjectName(Tree,UCosmereNativeButton::StaticClass(),Name));
  B->SetButtonText(Text); Parent->AddChildToVerticalBox(B)->SetPadding(FMargin(4)); B->OnClicked().Add(Click); return B;
 }
 class FCosmereCaptureProcessor final : public IInputProcessor
 {
  TWeakObjectPtr<UCosmereSettingsScreen> Screen;
  FKey ReleaseKey;
  bool bDraining = false;
 public:
  explicit FCosmereCaptureProcessor(UCosmereSettingsScreen* S):Screen(S){}
  virtual void Tick(const float,FSlateApplication&,TSharedRef<ICursor>) override {}
  void ResetDrain() { bDraining=false;ReleaseKey=EKeys::Invalid; }
    bool Active() const { return Screen.IsValid() && (Screen->IsCapturing() || bDraining); }
    bool MatchesUser(int32 Index) const
    {
     auto* LP=Screen.IsValid()?Screen->GetOwningLocalPlayer():nullptr;
     auto User=LP?LP->GetSlateUser():TSharedPtr<FSlateUser>();
     return User.IsValid() && User->GetUserIndex()==Index;
    }
  virtual bool HandleKeyDownEvent(FSlateApplication&,const FKeyEvent& E) override
  {
     if (!Active() || !MatchesUser(E.GetUserIndex())) return false;
   if (bDraining) return E.GetKey()==ReleaseKey;
   if (!bDraining && !E.IsRepeat()) { ReleaseKey=E.GetKey(); bDraining=true; Screen->CaptureKey(ReleaseKey); }
   return true;
  }
  virtual bool HandleKeyUpEvent(FSlateApplication&,const FKeyEvent& E) override
  {
   if (!Active() || !MatchesUser(E.GetUserIndex())) return false;
   if (bDraining && E.GetKey()!=ReleaseKey) return false;
   if (E.GetKey()==ReleaseKey) bDraining=false;
   return true;
  }
    virtual bool HandleAnalogInputEvent(FSlateApplication&,const FAnalogInputEvent& E) override { return Screen.IsValid() && Screen->IsCapturing() && MatchesUser(E.GetUserIndex()); }
    virtual bool HandleMouseButtonDownEvent(FSlateApplication&,const FPointerEvent& E) override
  {
   if (!Active() || !MatchesUser(E.GetUserIndex())) return false;
   if (bDraining) return E.GetEffectingButton()==ReleaseKey;
   ReleaseKey=E.GetEffectingButton(); bDraining=true; Screen->CaptureKey(ReleaseKey); return true;
  }
    virtual bool HandleMouseButtonUpEvent(FSlateApplication&,const FPointerEvent& E) override
  {
   if (!Active() || !MatchesUser(E.GetUserIndex())) return false;
   if (bDraining && E.GetEffectingButton()!=ReleaseKey) return false;
   if (E.GetEffectingButton()==ReleaseKey) bDraining=false;
   return true;
  }
    virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& App,const FPointerEvent& E) override {return HandleMouseButtonDownEvent(App,E);}
    virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication&,const FPointerEvent& E,const FPointerEvent*) override { return Screen.IsValid() && Screen->IsCapturing() && MatchesUser(E.GetUserIndex()); }
 };
}
void UCosmereSettingsControlRow::NativeOnInitialized()
{
 Super::NativeOnInitialized();
 auto* Box=WidgetTree->ConstructWidget<UVerticalBox>(); WidgetTree->RootWidget=Box;
 Label=WidgetTree->ConstructWidget<UTextBlock>(); Label->SetAutoWrapText(true); Box->AddChildToVerticalBox(Label);
 Slider=WidgetTree->ConstructWidget<USlider>(); Box->AddChildToVerticalBox(Slider);
 Choice=WidgetTree->ConstructWidget<UComboBoxString>(); Box->AddChildToVerticalBox(Choice);
 Slider->OnValueChanged.AddDynamic(this,&ThisClass::SliderChanged); Choice->OnSelectionChanged.AddDynamic(this,&ThisClass::ChoiceChanged);
}
void UCosmereSettingsControlRow::Configure(UCosmereSettingsSubsystem* S,ECosmereSettingControl C,const FText& Name)
{
 Settings=S; Control=C; RowName=Name;
 bRefreshing=true; Choice->ClearOptions();
 const bool bSlider=C==ECosmereSettingControl::Mouse || C==ECosmereSettingControl::Gamepad || C==ECosmereSettingControl::Master || C==ECosmereSettingControl::SFX || C==ECosmereSettingControl::Music;
 Slider->SetVisibility(bSlider?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 Choice->SetVisibility(bSlider?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
 Slider->SetMinValue(C==ECosmereSettingControl::Mouse || C==ECosmereSettingControl::Gamepad?0.1f:0.f);
 Slider->SetMaxValue(C==ECosmereSettingControl::Mouse || C==ECosmereSettingControl::Gamepad?5.f:1.f); Slider->SetStepSize(0.05f);
 if (!bSlider)
 {
  if (C==ECosmereSettingControl::Resolution) { Resolutions=S->GetSupportedResolutions(); for (auto R:Resolutions) Choice->AddOption(FString::Printf(TEXT("%d x %d"),R.X,R.Y)); }
  else if (C==ECosmereSettingControl::Mode) for (const TCHAR* V:{TEXT("Fullscreen"),TEXT("Borderless"),TEXT("Windowed")}) Choice->AddOption(V);
  else if (C==ECosmereSettingControl::Quality) for (const TCHAR* V:{TEXT("Custom (preserve)"),TEXT("Low"),TEXT("Medium"),TEXT("High"),TEXT("Epic"),TEXT("Cinematic")}) Choice->AddOption(V);
  else if (C==ECosmereSettingControl::FPS)
  {
   FrameLimits={0,30,60,120,144,240};FrameLimits.AddUnique(S->PendingVideo.FrameLimit);
   for(float Limit:FrameLimits) Choice->AddOption(Limit==0?FString(TEXT("Unlimited")):FString::Printf(TEXT("%.0f FPS"),Limit));
  }
  else if (C==ECosmereSettingControl::TextScale) for (const TCHAR* V:{TEXT("100%"),TEXT("125%"),TEXT("150%")}) Choice->AddOption(V);
  else { Choice->AddOption(TEXT("Off")); Choice->AddOption(TEXT("On")); }
 }
 bRefreshing=false; Refresh();
}
void UCosmereSettingsControlRow::Refresh()
{
 if (!Settings) return;
 TGuardValue<bool> Guard(bRefreshing,true);
 const auto P=Settings->Pending; const auto V=Settings->PendingVideo;
 float F=0; int32 I=0;
 switch(Control)
 {
 case ECosmereSettingControl::Mouse:F=P.MouseSensitivity;break;
 case ECosmereSettingControl::Gamepad:F=P.GamepadSensitivity;break;
 case ECosmereSettingControl::Master:F=P.MasterVolume;break;
 case ECosmereSettingControl::SFX:F=P.SFXVolume;break;
 case ECosmereSettingControl::Music:F=P.MusicVolume;break;
 case ECosmereSettingControl::InvertMouse:I=P.bInvertMouseY;break;
 case ECosmereSettingControl::InvertGamepad:I=P.bInvertGamepadY;break;
 case ECosmereSettingControl::Resolution:I=Resolutions.IndexOfByKey(V.Resolution);break;
 case ECosmereSettingControl::Mode:I=V.WindowMode;break;
 case ECosmereSettingControl::VSync:I=V.bVSync;break;
 case ECosmereSettingControl::FPS:I=FrameLimits.IndexOfByKey(V.FrameLimit);break;
 case ECosmereSettingControl::Quality:I=V.Quality+1;break;
 case ECosmereSettingControl::TextScale:I=FMath::RoundToInt((P.TextScale-1.f)*4);break;
 case ECosmereSettingControl::ReducedMotion:I=P.bReducedMotion;break;
 }
 if (Slider->GetVisibility()==ESlateVisibility::Visible) { Slider->SetValue(F); Label->SetText(FText::FromString(FString::Printf(TEXT("%s: %.2f"),*RowName.ToString(),F))); }
 else { Choice->SetSelectedIndex(I); Label->SetText(RowName); }
 SetIsEnabled(Settings->IsEditing() && !Settings->IsAwaitingDisplayConfirmation());
}
void UCosmereSettingsControlRow::SliderChanged(float V)
{
 if (bRefreshing || !Settings) return;
 auto P=Settings->Pending;
 switch(Control) { case ECosmereSettingControl::Mouse:P.MouseSensitivity=V;break; case ECosmereSettingControl::Gamepad:P.GamepadSensitivity=V;break; case ECosmereSettingControl::Master:P.MasterVolume=V;break; case ECosmereSettingControl::SFX:P.SFXVolume=V;break; case ECosmereSettingControl::Music:P.MusicVolume=V;break; default:return; }
 Settings->SetPendingPreferences(P);
}
void UCosmereSettingsControlRow::ChoiceChanged(FString,ESelectInfo::Type)
{
 if (bRefreshing || !Settings) return;
 int32 I=Choice->GetSelectedIndex(); auto P=Settings->Pending; auto V=Settings->PendingVideo;
 switch(Control)
 {
 case ECosmereSettingControl::InvertMouse:P.bInvertMouseY=I==1;break;
 case ECosmereSettingControl::InvertGamepad:P.bInvertGamepadY=I==1;break;
 case ECosmereSettingControl::TextScale:P.TextScale=1.f+I*0.25f;break;
 case ECosmereSettingControl::ReducedMotion:P.bReducedMotion=I==1;break;
 case ECosmereSettingControl::Resolution:if(Resolutions.IsValidIndex(I)) V.Resolution=Resolutions[I];break;
 case ECosmereSettingControl::Mode:V.WindowMode=I;break;
 case ECosmereSettingControl::VSync:V.bVSync=I==1;break;
 case ECosmereSettingControl::Quality:V.Quality=I-1;break;
 case ECosmereSettingControl::FPS:if(FrameLimits.IsValidIndex(I)) V.FrameLimit=FrameLimits[I];break;
 default:return;
 }
 if(Control==ECosmereSettingControl::Resolution || Control==ECosmereSettingControl::Mode || Control==ECosmereSettingControl::VSync || Control==ECosmereSettingControl::Quality || Control==ECosmereSettingControl::FPS) Settings->SetPendingVideo(V); else Settings->SetPendingPreferences(P);
}
UCosmereSettingsScreen::UCosmereSettingsScreen(const FObjectInitializer& O):Super(O)
{ ScreenRoute=ECosmereUIRoute::Settings; DefaultFocusWidgetName=TEXT("TabControls"); }
void UCosmereSettingsScreen::NativeOnInitialized()
{
 Super::NativeOnInitialized();
 auto* Border=WidgetTree->ConstructWidget<UBorder>(); Border->SetPadding(FMargin(32)); Border->SetBrushColor(FLinearColor(0.015f,0.022f,0.035f,0.98f)); WidgetTree->RootWidget=Border;
 auto* Box=WidgetTree->ConstructWidget<UVerticalBox>(); Border->SetContent(Box);
 const TCHAR* Names[]={TEXT("Controls"),TEXT("Audio"),TEXT("Video"),TEXT("Accessibility")};
 for(int32 I=0;I<4;++I) Button(WidgetTree,Box,*FString::Printf(TEXT("Tab%s"),Names[I]),FText::FromString(Names[I]),FSimpleDelegate::CreateWeakLambda(this,[this,I](){ if(!IsCapturing()) ShowTab(I); }));
 auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(); Box->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
 Content=WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(Content);
 Status=WidgetTree->ConstructWidget<UTextBlock>(); Status->SetAutoWrapText(true); Box->AddChildToVerticalBox(Status);
 Button(WidgetTree,Box,TEXT("BtnApply"),FText::FromString(TEXT("Apply")),FSimpleDelegate::CreateUObject(this,&ThisClass::Apply));
 Button(WidgetTree,Box,TEXT("BtnDefaults"),FText::FromString(TEXT("Restore Defaults (pending)")),FSimpleDelegate::CreateWeakLambda(this,[this](){if(Settings && !IsCapturing()) Settings->RestoreDefaults();}));
 Button(WidgetTree,Box,TEXT("BtnCancel"),FText::FromString(TEXT("Cancel / Back")),FSimpleDelegate::CreateUObject(this,&ThisClass::RequestBack));
}
void UCosmereSettingsScreen::NativeOnActivated()
{
 Super::NativeOnActivated();
 Settings=GetOwningLocalPlayer()?GetOwningLocalPlayer()->GetSubsystem<UCosmereSettingsSubsystem>():nullptr;
 if (Settings)
 {
  const bool bNewEdit=!Settings->IsEditing();if(bNewEdit) Settings->BeginEdit();
  Settings->OnStateChanged.AddUniqueDynamic(this,&ThisClass::RefreshState);
  if(bNewEdit || Content->GetChildrenCount()==0) ShowTab(CurrentTab);
  RefreshState();
 }
}
void UCosmereSettingsScreen::NativeOnDeactivated() { CancelCapture(); Super::NativeOnDeactivated(); }
void UCosmereSettingsScreen::Detach()
{
 CaptureRow=INDEX_NONE;CaptureFocus.Reset();
 if(CaptureProcessor.IsValid() && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(CaptureProcessor);
 CaptureProcessor.Reset();
 if(Settings) { Settings->OnStateChanged.RemoveDynamic(this,&ThisClass::RefreshState); Settings->Cancel(); } Settings=nullptr;
}
void UCosmereSettingsScreen::NativeDestruct() { Detach(); Super::NativeDestruct(); }
void UCosmereSettingsScreen::AddRow(ECosmereSettingControl C,const TCHAR* Name)
{
 auto* R=CreateWidget<UCosmereSettingsControlRow>(GetOwningPlayer()); R->Configure(Settings,C,FText::FromString(Name));
 Content->AddChildToVerticalBox(R)->SetPadding(FMargin(4,8)); Rows.Add(R);
}
void UCosmereSettingsScreen::ShowTab(int32 Tab)
{
 if(!Settings || IsCapturing() || Settings->IsAwaitingDisplayConfirmation()) return;
 CurrentTab=Tab; Content->ClearChildren(); Rows.Reset(); BindingButtons.Reset();
 if(Tab==0)
 {
  AddRow(ECosmereSettingControl::Mouse,TEXT("Mouse multiplier (baseline 1)")); AddRow(ECosmereSettingControl::Gamepad,TEXT("Controller multiplier (baseline 1)"));
  AddRow(ECosmereSettingControl::InvertMouse,TEXT("Invert mouse Y")); AddRow(ECosmereSettingControl::InvertGamepad,TEXT("Invert controller Y"));
  if(Settings->Bindings.IsEmpty()) { auto* T=WidgetTree->ConstructWidget<UTextBlock>(); T->SetText(FText::FromString(TEXT("No registered player-mappable rows. Context metadata must be authored before rebinding is available."))); T->SetAutoWrapText(true); Content->AddChild(T); }
  for(int32 I=0;I<Settings->Bindings.Num();++I)
  {
   auto* B=Button(WidgetTree,Content,*FString::Printf(TEXT("Binding_%d"),I),FText::GetEmpty(),FSimpleDelegate());
   BindingButtons.Add(B); B->OnClicked().AddWeakLambda(this,[this,I,B](){BeginCapture(I,B);});
  }
 }
 else if(Tab==1) { AddRow(ECosmereSettingControl::Master,TEXT("Master")); AddRow(ECosmereSettingControl::SFX,TEXT("SFX")); AddRow(ECosmereSettingControl::Music,TEXT("Music (existing sources only)")); }
 else if(Tab==2) { AddRow(ECosmereSettingControl::Resolution,TEXT("Supported resolution")); AddRow(ECosmereSettingControl::Mode,TEXT("Window mode")); AddRow(ECosmereSettingControl::VSync,TEXT("VSync")); AddRow(ECosmereSettingControl::FPS,TEXT("Frame limit")); AddRow(ECosmereSettingControl::Quality,TEXT("Quality preset")); }
 else { AddRow(ECosmereSettingControl::TextScale,TEXT("Local UI text scale")); AddRow(ECosmereSettingControl::ReducedMotion,TEXT("Reduced Motion")); }
 RefreshState();
 if(auto* M=GetOwningLocalPlayer()->GetSubsystem<UCosmereUIManager>()) M->RefreshLocalPreferences();
}
void UCosmereSettingsScreen::RefreshState()
{
 if(!Settings) return;
 for(const auto& R:Rows) if(R) R->Refresh();
 for(int32 I=0;I<BindingButtons.Num();++I) if(Settings->Bindings.IsValidIndex(I))
 {
  const auto& E=Settings->Bindings[I];
  BindingButtons[I]->SetButtonText(FText::FromString(FString::Printf(TEXT("%s [%s / slot %d / %s]: %s"),*E.DisplayName.ToString(),E.Args.NewKey.IsGamepadKey()?TEXT("Gamepad"):TEXT("Keyboard/Mouse"),static_cast<int32>(E.Args.Slot)+1,E.Args.HardwareDeviceId.IsNone()?TEXT("legacy shared slots"):*E.Args.HardwareDeviceId.ToString(),*E.Args.NewKey.GetDisplayName().ToString())));
 }
 FString Message=IsCapturing()?TEXT("Press one key/button. Escape or controller Back cancels capture."):Settings->IsDirty()?TEXT("Pending changes; Apply commits, Cancel restores."):TEXT("No pending changes.");
 if(CurrentTab==1 && !Settings->HasAudioRouting()) Message+=TEXT(" Audio routing assets are not configured; audible preview is unavailable.");
 if(!Settings->LastError.IsEmpty()) Message+=TEXT("\n")+Settings->LastError.ToString(); Status->SetText(FText::FromString(Message));
 if(Settings->IsAwaitingDisplayConfirmation())
 {
  auto* Manager=GetOwningLocalPlayer()->GetSubsystem<UCosmereUIManager>();
  if(!bDisplayDialog && Manager)
  {
   bDisplayDialog=true; // Reserve before CommonUI can synchronously reactivate this screen.
   if(!Manager->ShowConfirmation(FText::FromString(TEXT("Keep this display mode? Automatically reverts after 15 real seconds.")),
    FSimpleDelegate::CreateWeakLambda(this,[this](){bDisplayDialog=false; if(Settings) Settings->ConfirmDisplay(true);}),
    FSimpleDelegate::CreateWeakLambda(this,[this](){bDisplayDialog=false; if(Settings) Settings->ConfirmDisplay(false);}))) bDisplayDialog=false;
  }
  if(Manager && bDisplayDialog) Manager->UpdateConfirmationMessage(FText::FromString(FString::Printf(TEXT("Keep display mode? Reverting in %d real seconds."),Settings->GetDisplaySecondsRemaining())));
 }
 else if(bDisplayDialog)
 { bDisplayDialog=false; if(auto* M=GetOwningLocalPlayer()->GetSubsystem<UCosmereUIManager>()) M->DismissConfirmation(); }
}
void UCosmereSettingsScreen::Apply() { if(Settings && !IsCapturing()) Settings->Apply(); }
void UCosmereSettingsScreen::BeginCapture(int32 Row,UCosmereNativeButton* Focus)
{
 if(!Settings || !Settings->IsEditing() || Settings->IsAwaitingDisplayConfirmation() || !FSlateApplication::IsInitialized()) return;
 CaptureRow=Row; CaptureFocus=Focus;
 if(!CaptureProcessor) { CaptureProcessor=MakeShared<FCosmereCaptureProcessor>(this); FSlateApplication::Get().RegisterInputPreProcessor(CaptureProcessor,0); }
 else StaticCastSharedPtr<FCosmereCaptureProcessor>(CaptureProcessor)->ResetDrain();
 RefreshState();
}
void UCosmereSettingsScreen::CaptureKey(FKey Key)
{
 if(!IsCapturing()) return;
 if(Key==EKeys::Escape || Key==EKeys::Gamepad_FaceButton_Right || Key==EKeys::Gamepad_Special_Left) { CancelCapture(); return; }
 if(Settings) Settings->StageKey(CaptureRow,Key);
 CancelCapture();
}
void UCosmereSettingsScreen::CancelCapture()
 { CaptureRow=INDEX_NONE; if(IsActivated() && CaptureFocus.IsValid()) CaptureFocus->SetUserFocus(GetOwningPlayer()); CaptureFocus.Reset(); if(IsActivated() && Settings && Status) RefreshState(); }
void UCosmereSettingsScreen::Close()
{ if(Settings) Settings->Cancel(); if(auto* M=GetOwningLocalPlayer()->GetSubsystem<UCosmereUIManager>()) M->PopScreen(); }
void UCosmereSettingsScreen::RequestBack()
{
 if(IsCapturing()) {CancelCapture();return;}
 if(Settings && Settings->IsAwaitingDisplayConfirmation()) {Settings->ConfirmDisplay(false);return;}
 if(Settings && Settings->IsDirty())
 {
  if(auto* M=GetOwningLocalPlayer()->GetSubsystem<UCosmereUIManager>()) M->ShowConfirmation(FText::FromString(TEXT("Discard all pending settings and reversible previews?")),FSimpleDelegate::CreateUObject(this,&ThisClass::Close),FSimpleDelegate());
 }
 else Close();
}
bool UCosmereSettingsScreen::NativeOnHandleBackAction() {RequestBack();return true;}
UCosmereConfirmDialog::UCosmereConfirmDialog(const FObjectInitializer& O):Super(O) { DefaultFocusWidgetName=TEXT("BtnDecline"); }
void UCosmereConfirmDialog::NativeOnInitialized()
{
 Super::NativeOnInitialized(); auto* Border=WidgetTree->ConstructWidget<UBorder>(); Border->SetPadding(FMargin(40)); Border->SetBrushColor(FLinearColor(0.02f,0.03f,0.05f,1)); WidgetTree->RootWidget=Border;
 auto* Box=WidgetTree->ConstructWidget<UVerticalBox>(); Border->SetContent(Box);
 MessageText=WidgetTree->ConstructWidget<UTextBlock>(); MessageText->SetAutoWrapText(true); Box->AddChildToVerticalBox(MessageText);
 Button(WidgetTree,Box,TEXT("BtnAccept"),FText::FromString(TEXT("Confirm")),FSimpleDelegate::CreateWeakLambda(this,[this](){Resolve(true);}));
 Button(WidgetTree,Box,TEXT("BtnDecline"),FText::FromString(TEXT("Cancel / Back")),FSimpleDelegate::CreateWeakLambda(this,[this](){Resolve(false);}));
}
void UCosmereConfirmDialog::Configure(const FText& Message,FSimpleDelegate A,FSimpleDelegate D) { bResolved=false; AcceptAction=A; DeclineAction=D; SetMessage(Message); }
void UCosmereConfirmDialog::SetMessage(const FText& Message) {if(MessageText) MessageText->SetText(Message);}
void UCosmereConfirmDialog::Resolve(bool bAccept)
{
 if(bResolved) return; bResolved=true; FSimpleDelegate Action=bAccept?AcceptAction:DeclineAction;
 DeactivateWidget(); Action.ExecuteIfBound();
}
bool UCosmereConfirmDialog::NativeOnHandleBackAction() {Resolve(false);return true;}
