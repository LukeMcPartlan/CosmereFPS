#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerStart.h"
#include "Net/CosmereMatchTypes.h"
#include "CosmereKredikShawPlayerStart.generated.h"

/** Native descriptor also accepts explicitly tagged ordinary PlayerStarts after the native build. */
struct COSMERERIVALS_API FKredikShawStartDescriptor
{
	FName StableId;
	ECosmereTeam Team = ECosmereTeam::None;
	int32 TierCM = 0;
	bool bFreeForAll = false;
};

/** Place at the intended pawn capsule CENTER, not at floor Z. Floor is checked against the pawn CDO. */
UCLASS(Blueprintable)
class COSMERERIVALS_API ACosmereKredikShawPlayerStart : public APlayerStart
{
	GENERATED_BODY()

public:
	ACosmereKredikShawPlayerStart(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kredik Shaw|Spawn")
	FName StableId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kredik Shaw|Spawn")
	ECosmereTeam Team = ECosmereTeam::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kredik Shaw|Spawn")
	bool bFreeForAllOnly = false;

	/** Allowed authored bands: -1200, 0, 800, 1600, 2400, 3200. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kredik Shaw|Spawn")
	int32 TierHeightCM = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kredik Shaw|Spawn")
	bool bSpawnEnabled = true;

	static bool Describe(const APlayerStart* Start, FKredikShawStartDescriptor& Out);
	static bool IsTier(int32 HeightCM);
};
