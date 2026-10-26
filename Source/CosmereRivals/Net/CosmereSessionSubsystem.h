#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "CosmereSessionSubsystem.generated.h"
class UNetDriver;

USTRUCT(BlueprintType)
struct FCosmereMultiplayerMap
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly, Category="Cosmere|Session") FName MapId;
 UPROPERTY(BlueprintReadOnly, Category="Cosmere|Session") FText DisplayName;
 UPROPERTY(BlueprintReadOnly, Category="Cosmere|Session") FString LevelPath;
};

USTRUCT(BlueprintType)
struct FCosmereSessionListing
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly, Category="Cosmere|Session") FString HostName;
 UPROPERTY(BlueprintReadOnly, Category="Cosmere|Session") FString MapName;
 UPROPERTY(BlueprintReadOnly, Category="Cosmere|Session") int32 PingInMs=0;
 UPROPERTY(BlueprintReadOnly, Category="Cosmere|Session") int32 CurrentPlayers=0;
 UPROPERTY(BlueprintReadOnly, Category="Cosmere|Session") int32 MaxPlayers=0;
 UPROPERTY(BlueprintReadOnly, Category="Cosmere|Session") FString ListingId;
 UPROPERTY(BlueprintReadOnly, Category="Cosmere|Session") int32 SnapshotGeneration=0;
};
UENUM(BlueprintType)
enum class ECosmereSessionOperation:uint8 { Idle, Hosting, Searching, Joining, Leaving };
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCosmereSessionListUpdated,bool,bWasSuccessful);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCosmereSessionStateChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCosmereSessionLeft,bool,bCleanupSucceeded);

/** Null LAN, not internet matchmaking. Async callbacks cannot resurrect an abandoned route. */
UCLASS()
class COSMERERIVALS_API UCosmereSessionSubsystem:public UGameInstanceSubsystem
{
 GENERATED_BODY()
public:
 virtual void Initialize(FSubsystemCollectionBase& Collection) override;
 virtual void Deinitialize() override;
 UFUNCTION(BlueprintCallable,Category="Cosmere|Session") bool HostLanSession(int32 MaxPlayers=12);
 UFUNCTION(BlueprintCallable,Category="Cosmere|Session") bool HostLanSessionOnMap(FName MapId,int32 MaxPlayers=12);
 UFUNCTION(BlueprintPure,Category="Cosmere|Session") static TArray<FCosmereMultiplayerMap> GetMultiplayerMaps();
 UFUNCTION(BlueprintCallable,Category="Cosmere|Session") bool RefreshLanSessionList(int32 MaxResults=32);
 UFUNCTION(BlueprintCallable,Category="Cosmere|Session") bool JoinLanSessionByIndex(int32 ListingIndex);
 UFUNCTION(BlueprintCallable,Category="Cosmere|Session") bool JoinLanListing(int32 Index,int32 SnapshotGeneration,const FString& ListingId);
 UFUNCTION(BlueprintCallable,Category="Cosmere|Session") bool LeaveSession(bool bReturnToMenu=true);
 UFUNCTION(BlueprintCallable,Category="Cosmere|Session") void AbandonBrowserRoute();
 UFUNCTION(BlueprintPure,Category="Cosmere|Session") bool IsBusy() const {return Operation!=ECosmereSessionOperation::Idle;}
 UFUNCTION(BlueprintPure,Category="Cosmere|Session") bool HasSession() const;
 UFUNCTION(BlueprintPure,Category="Cosmere|Session") TArray<FCosmereSessionListing> GetSessionListings() const {return Listings;}
 UFUNCTION(BlueprintPure,Category="Cosmere|Session") FText GetLastError() const {return LastError;}
 UFUNCTION(BlueprintPure,Category="Cosmere|Session") FText GetStatus() const {return Status;}
 UFUNCTION(BlueprintPure,Category="Cosmere|Session") ECosmereSessionOperation GetOperation() const {return Operation;}
 /** Retained across travel and consumed only when the practice front-end is ready. */
 bool ConsumeReturnToMainMenu();
 UPROPERTY(BlueprintAssignable,Category="Cosmere|Session") FOnCosmereSessionListUpdated OnSessionListUpdated;
 UPROPERTY(BlueprintAssignable,Category="Cosmere|Session") FOnCosmereSessionStateChanged OnStateChanged;
 UPROPERTY(BlueprintAssignable,Category="Cosmere|Session") FOnCosmereSessionLeft OnLeaveComplete;
private:
 bool EnsureSessionInterface();
 bool BeginOperation(ECosmereSessionOperation Op,const TCHAR* Message);
 void SetState(ECosmereSessionOperation Op,const FString& Message);
 bool Fail(const FString& Message);
 void ClearHandles();
 void CancelBrowserSearch();
 void DisableAdvertisement();
 void CreateComplete(uint64 Token,FName Name,bool bSuccess);
 void FindComplete(uint64 Token,bool bSuccess);
 void JoinComplete(uint64 Token,FName Name,EOnJoinSessionCompleteResult::Type Result);
 void DestroyComplete(uint64 Token,FName Name,bool bSuccess);
 void FinishLeave(bool bSuccess);
 bool TickDeadline(float Delta);
 void NetworkFailure(UWorld* World,UNetDriver* Driver,ENetworkFailure::Type Type,const FString& Error);
 void TravelFailure(UWorld* World,ETravelFailure::Type Type,const FString& Error);
 void Recover(UWorld* World,const FString& Error);
 bool ValidateLocalCharacter();
 FString PendingHostLevelPath;
 FString PendingHostMapName;
 int32 PendingHostMaxPlayers=12;
 IOnlineSessionPtr SessionInterface;
 TSharedPtr<FOnlineSessionSearch> SessionSearch;
 TArray<FOnlineSessionSearchResult> RawResults;
 TArray<FCosmereSessionListing> Listings;
 FText LastError;
 FText Status;
 ECosmereSessionOperation Operation=ECosmereSessionOperation::Idle;
 uint64 Generation=0;
 int32 ListingGeneration=0;
 bool bAbandoned=false;
 bool bReturnAfterLeave=false;
 bool bReturnMainMenu=false;
 bool bRecovering=false;
 double Deadline=0;
 FDelegateHandle CreateHandle,FindHandle,JoinHandle,DestroyHandle,NetworkHandle,TravelHandle;
 FTSTicker::FDelegateHandle DeadlineTicker;
};
