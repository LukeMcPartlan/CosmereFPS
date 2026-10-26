#pragma once

#include "CoreMinimal.h"
#include "Net/CosmereGameMode.h"
#include "CosmereKredikShawGameMode.generated.h"

class APlayerStart;
class ACosmereBotController;
struct FKredikShawStartDescriptor;

/** Opt-in map policy. Inherits session admission, spectator overflow, scoring and capture unchanged. */
UCLASS(Blueprintable)
class COSMERERIVALS_API ACosmereKredikShawGameMode : public ACosmereGameMode
{
	GENERATED_BODY()

public:
	ACosmereKredikShawGameMode();
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual AActor* FindPlayerStart_Implementation(AController* Player, const FString& IncomingName) override;
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;

	/** Must be set from the measured source-world datum; no automatic origin inference. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Kredik Shaw|Heights")
	float GroundDatumZCM = 0.f;

	/** Host settings win unless this map-local override is explicitly enabled. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Kredik Shaw|Match")
	bool bOverrideMatchMode = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Kredik Shaw|Match")
	ECosmereMatchMode MapMatchMode = ECosmereMatchMode::KingOfTheHill;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Kredik Shaw|Spawn", meta=(ClampMin="0"))
	float MinimumOpponentDistanceCM = 600.f;

	UFUNCTION(BlueprintCallable, Category="Kredik Shaw|Diagnostics")
	void AuditSpawnCatalog();

protected:
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual bool UpdatePlayerStartSpot(AController* Player, const FString& Portal, FString& OutErrorMessage) override;
	virtual void BeginPlay() override;
	virtual ECosmereTeam PickBalancedTeam() const override;
	virtual void SpawnBots() override;
	virtual APawn* SpawnAndPossessPawnFor(AController* Controller) override;

private:
	static constexpr int32 ParticipantsPerSide = 6;
	static constexpr int32 FFAParticipants = 12;
	int32 CountParticipants(ECosmereTeam Team, bool bHumansOnly) const;
	bool RetireOneBot(ECosmereTeam Team);
	bool IsSafeStart(APlayerStart* Start, AController* Controller, const FKredikShawStartDescriptor& Descriptor) const;
	void ReconcilePopulation();
	TMap<FName, uint64> LastStartUse;
	TMap<int32, uint64> LastTierUse;
	uint64 SelectionSerial = 0;
	int32 NextBotIndex = 0;
	float MaintenanceElapsed = 0.f;
};
