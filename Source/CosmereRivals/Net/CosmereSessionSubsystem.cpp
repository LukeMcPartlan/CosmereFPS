#include "Net/CosmereSessionSubsystem.h"
#include "CosmereRivals.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "Save/CosmereSaveSubsystem.h"
namespace
{
 const FName SessionName(TEXT("GameSession"));
 const FName MapNameSetting(TEXT("COSMERE_MAP_NAME"));
 const FName MapPathSetting(TEXT("COSMERE_MAP_PATH"));
}
TArray<FCosmereMultiplayerMap> UCosmereSessionSubsystem::GetMultiplayerMaps()
{
 FCosmereMultiplayerMap Urithiru;
 Urithiru.MapId=FName(TEXT("Urithiru"));
 Urithiru.DisplayName=FText::FromString(TEXT("Urithiru"));
 Urithiru.LevelPath=TEXT("/Game/Maps/Lvl_Urithiru");
 FCosmereMultiplayerMap KendrikShaw;
 KendrikShaw.MapId=FName(TEXT("KendrikShaw"));
 KendrikShaw.DisplayName=FText::FromString(TEXT("Kendrik Shaw"));
 KendrikShaw.LevelPath=TEXT("/Game/Maps/Lvl_KendrikShaw");
 return {Urithiru,KendrikShaw};
}
bool UCosmereSessionSubsystem::ValidateLocalCharacter()
{
 UGameInstance* GI=GetGameInstance();
 UCosmereSaveSubsystem* Save=GI?GI->GetSubsystem<UCosmereSaveSubsystem>():nullptr;
 FSpiritwebCharacterSave Build;
 if(!Save || !Save->GetSelectedCharacter(Build))
  return Fail(TEXT("Select and confirm a character before hosting or joining a LAN match."));
 return true;
}
void UCosmereSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
 Super::Initialize(Collection);
 if(GEngine) { NetworkHandle=GEngine->OnNetworkFailure().AddUObject(this,&ThisClass::NetworkFailure); TravelHandle=GEngine->OnTravelFailure().AddUObject(this,&ThisClass::TravelFailure); }
 DeadlineTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&ThisClass::TickDeadline),0.1f);
 SetState(ECosmereSessionOperation::Idle,TEXT("LAN (OnlineSubsystem Null); no internet matchmaking."));
}
void UCosmereSessionSubsystem::Deinitialize()
{
 DisableAdvertisement();
 ++Generation; ClearHandles();
 if(GEngine) {GEngine->OnNetworkFailure().Remove(NetworkHandle);GEngine->OnTravelFailure().Remove(TravelHandle);}
 FTSTicker::GetCoreTicker().RemoveTicker(DeadlineTicker); SessionInterface.Reset(); SessionSearch.Reset();
 Super::Deinitialize();
}
bool UCosmereSessionSubsystem::EnsureSessionInterface()
{
 if(!SessionInterface.IsValid()) if(auto* OSS=Online::GetSubsystem(GetWorld())) SessionInterface=OSS->GetSessionInterface();
 return SessionInterface.IsValid() || Fail(TEXT("No LAN session interface. Configure OnlineSubsystem Null."));
}
bool UCosmereSessionSubsystem::HasSession() const {return SessionInterface.IsValid() && SessionInterface->GetNamedSession(SessionName)!=nullptr;}
void UCosmereSessionSubsystem::ClearHandles()
{
 if(SessionInterface.IsValid())
 {
  SessionInterface->OnCreateSessionCompleteDelegates.Remove(CreateHandle); SessionInterface->OnFindSessionsCompleteDelegates.Remove(FindHandle);
  SessionInterface->OnJoinSessionCompleteDelegates.Remove(JoinHandle); SessionInterface->OnDestroySessionCompleteDelegates.Remove(DestroyHandle);
 }
 CreateHandle.Reset();FindHandle.Reset();JoinHandle.Reset();DestroyHandle.Reset();
}
void UCosmereSessionSubsystem::SetState(ECosmereSessionOperation Op,const FString& Message)
{ Operation=Op; Status=FText::FromString(Message); UE_LOG(LogCosmereUI,Log,TEXT("LAN state %d: %s"),static_cast<int32>(Op),*Message); OnStateChanged.Broadcast(); }
bool UCosmereSessionSubsystem::Fail(const FString& Message)
{ LastError=FText::FromString(Message); UE_LOG(LogCosmereUI,Warning,TEXT("LAN: %s"),*Message); OnStateChanged.Broadcast();return false; }
bool UCosmereSessionSubsystem::BeginOperation(ECosmereSessionOperation Op,const TCHAR* Message)
{
 if(IsBusy() || CreateHandle.IsValid() || FindHandle.IsValid() || JoinHandle.IsValid() || DestroyHandle.IsValid()) return Fail(TEXT("An earlier LAN operation is still pending; wait for cleanup before retrying."));
 if(SessionSearch.IsValid() && SessionSearch->SearchState==EOnlineAsyncTaskState::InProgress)
  return Fail(TEXT("The previous backend search is still completing; retry after it finishes."));
 if(!EnsureSessionInterface()) return false;
 ++Generation; bAbandoned=false; LastError=FText::GetEmpty(); Deadline=FPlatformTime::Seconds()+20.; SetState(Op,Message);return true;
}
bool UCosmereSessionSubsystem::HostLanSession(int32 MaxPlayers)
{
 return HostLanSessionOnMap(FName(TEXT("Urithiru")),MaxPlayers);
}
bool UCosmereSessionSubsystem::HostLanSessionOnMap(FName MapId,int32 MaxPlayers)
{
 if(HasSession()) return Fail(TEXT("Leave the existing session before hosting another."));
 if(!GetWorld() || GetWorld()->GetNetMode()==NM_Client)
  return Fail(TEXT("Hosting requires a local standalone or listen-server world."));
 const TArray<FCosmereMultiplayerMap> Maps=GetMultiplayerMaps();
 const FCosmereMultiplayerMap* Map=Maps.FindByPredicate([MapId](const FCosmereMultiplayerMap& Choice){return Choice.MapId==MapId;});
 if(!Map) return Fail(TEXT("Choose Urithiru or Kendrik Shaw before hosting."));
 if(!FPackageName::DoesPackageExist(Map->LevelPath))
  return Fail(FString::Printf(TEXT("The selected map is not saved or included in this build: %s."),*Map->LevelPath));
 if(!ValidateLocalCharacter()) return false;
 if(!BeginOperation(ECosmereSessionOperation::Hosting,TEXT("Creating LAN host..."))) return false;
 PendingHostLevelPath=Map->LevelPath;PendingHostMapName=Map->DisplayName.ToString();PendingHostMaxPlayers=FMath::Clamp(MaxPlayers,2,64);
 FOnlineSessionSettings S; S.bIsLANMatch=true;S.bShouldAdvertise=true;S.bAllowJoinInProgress=true;S.bAllowInvites=false;S.bUsesPresence=false;S.bUseLobbiesIfAvailable=false;S.NumPublicConnections=PendingHostMaxPlayers;
 S.Set(MapNameSetting,PendingHostMapName,EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
 S.Set(MapPathSetting,PendingHostLevelPath,EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
 uint64 Token=Generation;
 CreateHandle=SessionInterface->OnCreateSessionCompleteDelegates.AddWeakLambda(this,[this,Token](FName N,bool B){CreateComplete(Token,N,B);});
 if(!SessionInterface->CreateSession(0,SessionName,S)) {SessionInterface->OnCreateSessionCompleteDelegates.Remove(CreateHandle);CreateHandle.Reset();Deadline=0;SetState(ECosmereSessionOperation::Idle,TEXT("Host request rejected."));return Fail(TEXT("LAN host request was rejected."));}
 return true;
}
void UCosmereSessionSubsystem::CreateComplete(uint64 Token,FName Name,bool bSuccess)
{
 if(Token!=Generation || Name!=SessionName) return;
 if(!bAbandoned && Deadline>0 && FPlatformTime::Seconds()>=Deadline)
 {
  bAbandoned=true;
  Fail(TEXT("LAN host completed after its deadline; cleaning up without travel."));
 }
 SessionInterface->OnCreateSessionCompleteDelegates.Remove(CreateHandle);CreateHandle.Reset();Deadline=0;
 if(bAbandoned) {LeaveSession(bReturnAfterLeave);return;}
 if(!bSuccess) {if(HasSession()) LeaveSession(false);else SetState(ECosmereSessionOperation::Idle,TEXT("Host failed."));Fail(TEXT("LAN host could not be created."));return;}
 SetState(ECosmereSessionOperation::Idle,FString::Printf(TEXT("LAN host ready; travelling to %s."),*PendingHostMapName));
 const FString TravelURL=FString::Printf(TEXT("%s?listen?MaxPlayers=%d"),*PendingHostLevelPath,PendingHostMaxPlayers);
 // Absolute travel must not carry a front-end GameMode override into the selected match map.
 if(!GetWorld() || PendingHostLevelPath.IsEmpty() || !GetWorld()->ServerTravel(TravelURL,true)) Recover(GetWorld(),TEXT("Host created but selected match travel failed."));
}
bool UCosmereSessionSubsystem::RefreshLanSessionList(int32 MaxResults)
{
 if(!BeginOperation(ECosmereSessionOperation::Searching,TEXT("Searching LAN..."))) return false;
 ++ListingGeneration; Listings.Reset();RawResults.Reset();OnSessionListUpdated.Broadcast(true);
 SessionSearch=MakeShared<FOnlineSessionSearch>();SessionSearch->bIsLanQuery=true;SessionSearch->MaxSearchResults=FMath::Clamp(MaxResults,1,200);
 uint64 Token=Generation;
 FindHandle=SessionInterface->OnFindSessionsCompleteDelegates.AddWeakLambda(this,[this,Token](bool B){FindComplete(Token,B);});
 if(!SessionInterface->FindSessions(0,SessionSearch.ToSharedRef())) {SessionInterface->OnFindSessionsCompleteDelegates.Remove(FindHandle);FindHandle.Reset();Deadline=0;SetState(ECosmereSessionOperation::Idle,TEXT("Search rejected."));OnSessionListUpdated.Broadcast(false);return Fail(TEXT("LAN search request was rejected."));}
 return true;
}
void UCosmereSessionSubsystem::FindComplete(uint64 Token,bool bSuccess)
{
 if(Token!=Generation) return;
 if(SessionSearch.IsValid() && SessionSearch->SearchState==EOnlineAsyncTaskState::InProgress) return;
 if(!bAbandoned && Deadline>0 && FPlatformTime::Seconds()>=Deadline)
 {
  bAbandoned=true;
  Fail(TEXT("LAN search completed after its deadline; late results were discarded."));
 }
 SessionInterface->OnFindSessionsCompleteDelegates.Remove(FindHandle);FindHandle.Reset();
 if(Operation==ECosmereSessionOperation::Searching) Deadline=0;
 Listings.Reset();RawResults.Reset();
 if(bAbandoned)
 {
  // A late cancelled search must not publish results or finish a newer leave/recovery route.
  if(Operation==ECosmereSessionOperation::Searching)
   SetState(ECosmereSessionOperation::Idle,TEXT("LAN search cancelled."));
  return;
 }
 if(bSuccess && !bAbandoned && SessionSearch.IsValid())
 {
  RawResults=SessionSearch->SearchResults;
  for(const auto& R:RawResults)
  {
   FCosmereSessionListing L;L.HostName=R.Session.OwningUserName;L.PingInMs=R.PingInMs;L.MaxPlayers=R.Session.SessionSettings.NumPublicConnections;
   R.Session.SessionSettings.Get(MapNameSetting,L.MapName);
   if(L.MapName.IsEmpty()) L.MapName=TEXT("Unknown map");
   L.CurrentPlayers=FMath::Max(0,L.MaxPlayers-R.Session.NumOpenPublicConnections);L.SnapshotGeneration=ListingGeneration;
   L.ListingId=R.GetSessionIdStr();if(L.ListingId.IsEmpty()) L.ListingId=FGuid::NewGuid().ToString();Listings.Add(L);
  }
 }
 SetState(ECosmereSessionOperation::Idle,!bSuccess?TEXT("LAN search failed."):Listings.IsEmpty()?TEXT("No LAN sessions found."):TEXT("LAN search complete."));
 if(!bSuccess) Fail(TEXT("LAN search failed. Check local network and firewall."));OnSessionListUpdated.Broadcast(bSuccess);
}
bool UCosmereSessionSubsystem::JoinLanListing(int32 Index,int32 Snapshot,const FString& Id)
{
 if(!Listings.IsValidIndex(Index) || Listings[Index].SnapshotGeneration!=Snapshot || Listings[Index].ListingId!=Id) return Fail(TEXT("Stale session selection. Refresh and select the current listing."));
 return JoinLanSessionByIndex(Index);
}
bool UCosmereSessionSubsystem::JoinLanSessionByIndex(int32 Index)
{
 if(!RawResults.IsValidIndex(Index)) return Fail(TEXT("That session is no longer listed. Refresh first."));
 if(HasSession()) return Fail(TEXT("Leave the current session before joining."));
 if(!ValidateLocalCharacter()) return false;
 FString HostMapPath;
 RawResults[Index].Session.SessionSettings.Get(MapPathSetting,HostMapPath);
 if(!HostMapPath.IsEmpty() && !FPackageName::DoesPackageExist(HostMapPath))
  return Fail(FString::Printf(TEXT("This build does not include the host's map: %s."),*HostMapPath));
 if(!BeginOperation(ECosmereSessionOperation::Joining,TEXT("Joining LAN host..."))) return false;
 uint64 Token=Generation;
 JoinHandle=SessionInterface->OnJoinSessionCompleteDelegates.AddWeakLambda(this,[this,Token](FName N,EOnJoinSessionCompleteResult::Type R){JoinComplete(Token,N,R);});
 if(!SessionInterface->JoinSession(0,SessionName,RawResults[Index])) {SessionInterface->OnJoinSessionCompleteDelegates.Remove(JoinHandle);JoinHandle.Reset();Deadline=0;SetState(ECosmereSessionOperation::Idle,TEXT("Join rejected."));return Fail(TEXT("LAN join request was rejected."));}
 return true;
}
void UCosmereSessionSubsystem::JoinComplete(uint64 Token,FName Name,EOnJoinSessionCompleteResult::Type Result)
{
 if(Token!=Generation || Name!=SessionName) return;
 if(!bAbandoned && Deadline>0 && FPlatformTime::Seconds()>=Deadline)
 {
  bAbandoned=true;
  Fail(TEXT("LAN join completed after its deadline; cleaning up without travel."));
 }
 SessionInterface->OnJoinSessionCompleteDelegates.Remove(JoinHandle);JoinHandle.Reset();Deadline=0;
 if(bAbandoned) {LeaveSession(bReturnAfterLeave);return;}
 if(Result!=EOnJoinSessionCompleteResult::Success) {if(HasSession()) LeaveSession(false);else SetState(ECosmereSessionOperation::Idle,TEXT("Join failed."));Fail(FString::Printf(TEXT("Could not join LAN (%s)."),LexToString(Result)));return;}
 FString Address;APlayerController* PC=GetGameInstance()->GetFirstLocalPlayerController();
 if(!PC || !SessionInterface->GetResolvedConnectString(Name,Address)) {Recover(GetWorld(),TEXT("Joined but no host address/controller was available."));return;}
 SetState(ECosmereSessionOperation::Idle,TEXT("Joined LAN; travelling to host."));PC->ClientTravel(Address,TRAVEL_Absolute);
}
void UCosmereSessionSubsystem::CancelBrowserSearch()
{
 if(!SessionInterface.IsValid() || !FindHandle.IsValid()) return;
 if(SessionSearch.IsValid() && SessionSearch->SearchState==EOnlineAsyncTaskState::InProgress)
  SessionInterface->CancelFindSessions();
 // Null emits CancelFindComplete, NOT FindComplete. Do not leave a dead find delegate behind.
 // Backends whose search is still running retain this delegate as a quarantine barrier.
 if(!SessionSearch.IsValid() || SessionSearch->SearchState!=EOnlineAsyncTaskState::InProgress)
 {
  SessionInterface->OnFindSessionsCompleteDelegates.Remove(FindHandle);FindHandle.Reset();
  if(Operation==ECosmereSessionOperation::Searching) {Deadline=0;SetState(ECosmereSessionOperation::Idle,TEXT("LAN search cancelled."));}
 }
}
void UCosmereSessionSubsystem::DisableAdvertisement()
{
 if(!SessionInterface.IsValid()) return;
 if(FNamedOnlineSession* Session=SessionInterface->GetNamedSession(SessionName))
 {
  FOnlineSessionSettings Hidden=Session->SessionSettings;
  Hidden.bShouldAdvertise=false;Hidden.bAllowJoinInProgress=false;
  SessionInterface->UpdateSession(SessionName,Hidden,true);
 }
}
void UCosmereSessionSubsystem::AbandonBrowserRoute()
{
 bAbandoned=true;
 ++ListingGeneration;Listings.Reset();RawResults.Reset();
 if(Operation==ECosmereSessionOperation::Searching && SessionInterface.IsValid())
 {
  CancelBrowserSearch();
  if(FindHandle.IsValid()) SetState(ECosmereSessionOperation::Searching,TEXT("LAN route abandoned; waiting for backend search cancellation."));
 }
 else if(CreateHandle.IsValid() || JoinHandle.IsValid()) LeaveSession(false);
}
bool UCosmereSessionSubsystem::LeaveSession(bool bReturnToMenu)
{
 bAbandoned=true;bReturnAfterLeave|=bReturnToMenu;
 // Duplicate clicks may request menu return, but cannot keep extending bounded cleanup.
 if(Operation==ECosmereSessionOperation::Leaving && Deadline>0) return true;
 CancelBrowserSearch();
 DisableAdvertisement();
 if(DestroyHandle.IsValid()) {SetState(ECosmereSessionOperation::Leaving,TEXT("Waiting for prior LAN destroy..."));Deadline=FPlatformTime::Seconds()+8.;return true;}
 SetState(ECosmereSessionOperation::Leaving,TEXT("Leaving/destroying LAN session..."));Deadline=FPlatformTime::Seconds()+8.;
 if(!EnsureSessionInterface()) {FinishLeave(false);return false;}
 // A pending create/join retains its callback solely to clean up any late result, never to travel.
 // Even a named session can still be Creating/Joining. Destroy only after its completion.
 if(CreateHandle.IsValid() || JoinHandle.IsValid()) return true;
 if(!HasSession()) {FinishLeave(true);return true;}
 uint64 Token=Generation;
 DestroyHandle=SessionInterface->OnDestroySessionCompleteDelegates.AddWeakLambda(this,[this,Token](FName N,bool B){DestroyComplete(Token,N,B);});
 if(!SessionInterface->DestroySession(SessionName))
 {SessionInterface->OnDestroySessionCompleteDelegates.Remove(DestroyHandle);DestroyHandle.Reset();FinishLeave(false);return false;}
 return true;
}
void UCosmereSessionSubsystem::DestroyComplete(uint64 Token,FName Name,bool bSuccess)
{
 if(Token!=Generation || Name!=SessionName) return;
 SessionInterface->OnDestroySessionCompleteDelegates.Remove(DestroyHandle);DestroyHandle.Reset();FinishLeave(bSuccess);
}
void UCosmereSessionSubsystem::FinishLeave(bool bSuccess)
{
 Deadline=0;
 if(!bSuccess) Fail(TEXT("LAN cleanup failed or timed out. Returning locally; backend cleanup is not confirmed."));
 const bool bTravel=bReturnAfterLeave;bReturnAfterLeave=false; bReturnMainMenu|=bTravel;
 SetState(ECosmereSessionOperation::Idle,bSuccess?TEXT("LAN session left."):TEXT("Returned with a LAN cleanup warning."));OnLeaveComplete.Broadcast(bSuccess);
 if(bTravel && GetWorld()) UGameplayStatics::OpenLevel(GetWorld(),FName(TEXT("/Game/Maps/Lvl_PracticeRange")),true);
 bRecovering=false;
}
bool UCosmereSessionSubsystem::ConsumeReturnToMainMenu() {const bool B=bReturnMainMenu;bReturnMainMenu=false;return B;}
bool UCosmereSessionSubsystem::TickDeadline(float)
{
 if(bAbandoned && FindHandle.IsValid() && SessionSearch.IsValid() && SessionSearch->SearchState!=EOnlineAsyncTaskState::InProgress)
  CancelBrowserSearch();
 if(Deadline<=0 || FPlatformTime::Seconds()<Deadline) return true;
 Deadline=0;
 if(Operation==ECosmereSessionOperation::Leaving) FinishLeave(false);
 else if(Operation==ECosmereSessionOperation::Searching)
 {
  bAbandoned=true;CancelBrowserSearch();++ListingGeneration;Listings.Reset();RawResults.Reset();
  SetState(ECosmereSessionOperation::Idle,TEXT("LAN search timed out."));Fail(TEXT("LAN search timed out."));OnSessionListUpdated.Broadcast(false);
 }
 else
 {
  bAbandoned=true;Fail(TEXT("LAN host/join timed out; late results are quarantined for cleanup without travel."));
  LeaveSession(false); // Bounded local leave, retaining callbacks only for eventual cleanup.
 }
 return true;
}
void UCosmereSessionSubsystem::NetworkFailure(UWorld* W,UNetDriver*,ENetworkFailure::Type,const FString& E) {Recover(W,TEXT("Network failure: ")+E);}
void UCosmereSessionSubsystem::TravelFailure(UWorld* W,ETravelFailure::Type,const FString& E) {Recover(W,TEXT("Travel failure: ")+E);}
void UCosmereSessionSubsystem::Recover(UWorld* W,const FString& E)
{
 if(!W || W->GetGameInstance()!=GetGameInstance() || bRecovering) return;
 bRecovering=true;Fail(E);LeaveSession(true);
}
