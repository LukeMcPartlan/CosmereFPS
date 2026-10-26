#include "UI/CosmereSessionScreens.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "UI/CosmereNativeMenus.h"
#include "UI/CosmereUIManager.h"
namespace
{
 UCosmereNativeButton* AddButton(UWidgetTree* Tree,UVerticalBox* Box,FName Name,const TCHAR* Text)
 {
  auto* B=Tree->ConstructWidget<UCosmereNativeButton>(UCosmereNativeButton::StaticClass(),MakeUniqueObjectName(Tree,UCosmereNativeButton::StaticClass(),Name));
  B->SetButtonText(FText::FromString(Text));Box->AddChildToVerticalBox(B)->SetPadding(FMargin(4));return B;
 }
 UVerticalBox* Layout(UWidgetTree* Tree)
 {
  auto* Border=Tree->ConstructWidget<UBorder>();Border->SetPadding(FMargin(32));Border->SetBrushColor(FLinearColor(0.015f,0.022f,0.035f,0.98f));Tree->RootWidget=Border;
  auto* Box=Tree->ConstructWidget<UVerticalBox>();Border->SetContent(Box);return Box;
 }
}
UCosmereServerMenuScreen::UCosmereServerMenuScreen(const FObjectInitializer& O):Super(O) {ScreenRoute=ECosmereUIRoute::ServerMenu;DefaultFocusWidgetName=TEXT("BtnHost");}
void UCosmereServerMenuScreen::NativeOnInitialized()
{
 Super::NativeOnInitialized();auto* Box=Layout(WidgetTree);
 Status=WidgetTree->ConstructWidget<UTextBlock>();Status->SetAutoWrapText(true);Box->AddChildToVerticalBox(Status);
 auto* MapTitle=WidgetTree->ConstructWidget<UTextBlock>();MapTitle->SetText(FText::FromString(TEXT("Choose the map to host")));Box->AddChildToVerticalBox(MapTitle);
 for(const FCosmereMultiplayerMap& Map:UCosmereSessionSubsystem::GetMultiplayerMaps())
 {
  auto* B=AddButton(WidgetTree,Box,FName(*FString::Printf(TEXT("BtnMap_%s"),*Map.MapId.ToString())),*Map.DisplayName.ToString());
  MapButtons.Add(B);
  B->OnClicked().AddWeakLambda(this,[this,MapId=Map.MapId]()
  {
   if(!Sessions || Sessions->IsBusy() || Sessions->HasSession()) return;
   SelectedMapId=MapId;RefreshState();
  });
 }
 HostButton=AddButton(WidgetTree,Box,TEXT("BtnHost"),TEXT("LAN Host (Null)"));
 RefreshButton=AddButton(WidgetTree,Box,TEXT("BtnRefresh"),TEXT("Refresh LAN"));
 auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Box->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));List=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(List);
 JoinButton=AddButton(WidgetTree,Box,TEXT("BtnJoin"),TEXT("Join selected LAN session"));
 auto* Back=AddButton(WidgetTree,Box,TEXT("BtnBack"),TEXT("Back / Cancel pending request"));
 HostButton->OnClicked().AddWeakLambda(this,[this](){if(Sessions) Sessions->HostLanSessionOnMap(SelectedMapId);});
 RefreshButton->OnClicked().AddWeakLambda(this,[this](){if(Sessions) Sessions->RefreshLanSessionList();});
 JoinButton->OnClicked().AddWeakLambda(this,[this](){if(Sessions) Sessions->JoinLanListing(SelectedIndex,Selected.SnapshotGeneration,Selected.ListingId);});
 Back->OnClicked().AddWeakLambda(this,[this](){if(auto* M=GetOwningLocalPlayer()->GetSubsystem<UCosmereUIManager>()) M->NavigateBack();});
}
void UCosmereServerMenuScreen::NativeOnActivated()
{
 Super::NativeOnActivated();Sessions=GetGameInstance()?GetGameInstance()->GetSubsystem<UCosmereSessionSubsystem>():nullptr;
 if(Sessions) {Sessions->OnStateChanged.AddUniqueDynamic(this,&ThisClass::RefreshState);Sessions->OnSessionListUpdated.AddUniqueDynamic(this,&ThisClass::ListingsUpdated);ListingsUpdated(true);}
}
void UCosmereServerMenuScreen::Detach()
{
 if(Sessions) {Sessions->OnStateChanged.RemoveDynamic(this,&ThisClass::RefreshState);Sessions->OnSessionListUpdated.RemoveDynamic(this,&ThisClass::ListingsUpdated);Sessions->AbandonBrowserRoute();}Sessions=nullptr;
}
void UCosmereServerMenuScreen::NativeOnDeactivated() {Detach();Super::NativeOnDeactivated();}
void UCosmereServerMenuScreen::NativeDestruct() {Detach();Super::NativeDestruct();}
void UCosmereServerMenuScreen::ListingsUpdated(bool)
{
 SelectedIndex=INDEX_NONE;Selected=FCosmereSessionListing();List->ClearChildren();ListingButtons.Reset();
 if(Sessions)
 {
  auto Listings=Sessions->GetSessionListings();
  for(int32 I=0;I<Listings.Num();++I)
  {
   const auto L=Listings[I];const FString Text=FString::Printf(TEXT("%s  |  %s  |  %d / %d players  |  %d ms"),*L.HostName,*L.MapName,L.CurrentPlayers,L.MaxPlayers,L.PingInMs);
   auto* B=AddButton(WidgetTree,List,*FString::Printf(TEXT("Session_%d"),I),*Text);ListingButtons.Add(B);B->OnClicked().AddWeakLambda(this,[this,I,L](){SelectListing(I,L);});
  }
 }
 RefreshState();
}
void UCosmereServerMenuScreen::SelectListing(int32 I,FCosmereSessionListing L)
{SelectedIndex=I;Selected=L;RefreshState();}
void UCosmereServerMenuScreen::RefreshState()
{
 if(!Sessions) {Status->SetText(FText::FromString(TEXT("LAN subsystem unavailable.")));return;}
 FString Message=TEXT("Local Area Network / OnlineSubsystem Null (not internet matchmaking)\n")+Sessions->GetStatus().ToString();
 if(!Sessions->GetLastError().IsEmpty()) Message+=TEXT("\n")+Sessions->GetLastError().ToString();
 Status->SetText(FText::FromString(Message));const bool bCanConfigure=!Sessions->IsBusy() && !Sessions->HasSession();
 const TArray<FCosmereMultiplayerMap> Maps=UCosmereSessionSubsystem::GetMultiplayerMaps();
 FString SelectedMapName;
 for(int32 I=0;I<Maps.Num();++I)
 {
  const bool bSelected=Maps[I].MapId==SelectedMapId;
  const FString Name=Maps[I].DisplayName.ToString();
  if(bSelected) SelectedMapName=Name;
  if(MapButtons.IsValidIndex(I))
  {
   MapButtons[I]->SetIsEnabled(bCanConfigure);
   MapButtons[I]->SetButtonText(FText::FromString(bSelected?TEXT("SELECTED: ")+Name:Name));
  }
 }
 HostButton->SetButtonText(FText::FromString(TEXT("Host LAN - ")+SelectedMapName));
 HostButton->SetIsEnabled(bCanConfigure && !SelectedMapName.IsEmpty());
 RefreshButton->SetIsEnabled(bCanConfigure);JoinButton->SetIsEnabled(bCanConfigure && SelectedIndex!=INDEX_NONE);
 JoinButton->SetButtonText(FText::FromString(SelectedIndex==INDEX_NONE?TEXT("Select a listing to join"):TEXT("Join SELECTED: ")+Selected.HostName));
 for(const auto& B:ListingButtons) B->SetIsEnabled(bCanConfigure);
}
UCosmereEscapeMenuScreen::UCosmereEscapeMenuScreen(const FObjectInitializer& O):Super(O) {ScreenRoute=ECosmereUIRoute::EscapeMenu;DefaultFocusWidgetName=TEXT("BtnResume");}
void UCosmereEscapeMenuScreen::NativeOnInitialized()
{
 Super::NativeOnInitialized();auto* Box=Layout(WidgetTree);auto* T=WidgetTree->ConstructWidget<UTextBlock>();T->SetText(FText::FromString(TEXT("Local menu - the network match continues")));Box->AddChildToVerticalBox(T);
 AddButton(WidgetTree,Box,TEXT("BtnResume"),TEXT("Resume"))->OnClicked().AddWeakLambda(this,[this](){if(auto* M=GetOwningLocalPlayer()->GetSubsystem<UCosmereUIManager>()) M->ResumeGameplay();});
 AddButton(WidgetTree,Box,TEXT("BtnSettings"),TEXT("Settings"))->OnClicked().AddWeakLambda(this,[this](){if(auto* M=GetOwningLocalPlayer()->GetSubsystem<UCosmereUIManager>()) M->Navigate(ECosmereUIRoute::Settings);});
 AddButton(WidgetTree,Box,TEXT("BtnMainMenu"),TEXT("Main Menu"))->OnClicked().AddWeakLambda(this,[this](){if(auto* M=GetOwningLocalPlayer()->GetSubsystem<UCosmereUIManager>()) M->RequestReturnToMainMenu();});
}
bool UCosmereEscapeMenuScreen::NativeOnHandleBackAction()
{if(auto* M=GetOwningLocalPlayer()->GetSubsystem<UCosmereUIManager>()) M->ResumeGameplay();return true;}
