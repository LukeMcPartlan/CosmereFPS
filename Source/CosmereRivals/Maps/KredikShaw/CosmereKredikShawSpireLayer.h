#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CosmereKredikShawSpireLayer.generated.h"

UENUM(BlueprintType)
enum class EKredikShawLayer : uint8
{
	L1Playable,
	L2Near,
	L3MidFar,
	L4Horizon
};

UENUM(BlueprintType)
enum class EKredikShawAnchorRole : uint8
{
	WalkableFloor,
	StandardTip,
	CentralTip,
	Architecture
};

/** References already-authored source-world roots; never recreates meshes, actors or pinned placements. */
USTRUCT(BlueprintType)
struct COSMERERIVALS_API FKredikShawLayerEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kredik Shaw")
	FName StableId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kredik Shaw")
	TObjectPtr<AActor> Actor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kredik Shaw")
	bool bPinned = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kredik Shaw")
	EKredikShawAnchorRole AnchorRole = EKredikShawAnchorRole::Architecture;

	/** Explicit floor/tip datum in the source actor's local coordinates, not a guessed bounds center. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kredik Shaw")
	FVector LocalAnchor = FVector::ZeroVector;
};

/** One registry per owned layer/source level. No automatic construction or cross-layer regeneration. */
UCLASS(Blueprintable)
class COSMERERIVALS_API ACosmereKredikShawSpireLayer : public AActor
{
	GENERATED_BODY()

public:
	ACosmereKredikShawSpireLayer();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kredik Shaw|Ownership")
	EKredikShawLayer OwnedLayer = EKredikShawLayer::L1Playable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kredik Shaw|Ownership")
	TArray<FKredikShawLayerEntry> Entries;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kredik Shaw|Heights")
	float GroundDatumZCM = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kredik Shaw|Skyline")
	int32 LayerSeed = 731;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kredik Shaw|Skyline", meta=(ClampMin="0"))
	float MinimumRadiusCM = 12000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kredik Shaw|Skyline", meta=(ClampMin="0"))
	float MaximumRadiusCM = 18000.f;

	/** Skyline base anchor relative to the measured datum, not an additional roof elevation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kredik Shaw|Skyline")
	float SkylineBaseHeightCM = 0.f;

	UFUNCTION(BlueprintCallable, CallInEditor, Category="Kredik Shaw|Ownership")
	bool ValidateOwnership() const;

	/** Background transforms only. L1, pinned entries and unowned/attached roots cannot be regenerated. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Kredik Shaw|Skyline")
	bool RegenerateLayer();

	/** Absolute relative-to-ground target, applied once to an explicit anchor in its owning source World. */
	UFUNCTION(BlueprintCallable, Category="Kredik Shaw|Heights")
	bool TrySetL1AnchorHeight(FName StableId, float RelativeHeightCM);

	UFUNCTION(BlueprintPure, Category="Kredik Shaw|Heights")
	bool IsAuthoredFloorHeightValid(float WorldFloorZCM) const;

private:
	bool HasOnlyDecorativeGeometry(const AActor* Actor) const;
};
