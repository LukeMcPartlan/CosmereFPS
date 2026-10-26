#pragma once
#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "GameFramework/Actor.h"
#include "CosmereMenuScene.generated.h"

class USkeletalMeshComponent;
class USkeletalMesh;
class UAnimationAsset;

/** Presentation only. Poses are authored by scene placement, not invented here. */
UCLASS(Blueprintable)
class COSMERERIVALS_API ACosmereMenuCameraRig : public ACameraActor
{
	GENERATED_BODY()
public:
	ACosmereMenuCameraRig(const FObjectInitializer& ObjectInitializer);
	virtual void Tick(float DeltaSeconds) override;
	UFUNCTION(BlueprintCallable, Category="Cosmere|UI|Scene")
	void RequestPose(FName Pose);
	UFUNCTION(BlueprintCallable, Category="Cosmere|UI|Scene")
	void ConfigurePoses(const FTransform& Main, const FTransform& Shop);
	UFUNCTION(BlueprintCallable, Category="Cosmere|UI|Scene")
	void SetReducedMotion(bool bReduced);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cosmere|UI|Scene")
	FTransform ShopPoseOffset;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cosmere|UI|Scene")
	bool bReducedMotion = false;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Cosmere|UI|Scene")
	float Alpha = 1.f;
protected:
	virtual void BeginPlay() override;
private:
	FTransform MainPose, ShopPose, SourcePose, TargetPose;
	FName RequestedPose;
	float Elapsed = 0.f;
};

/** Separate animated full-body visual; never possesses, collides, grants builds, or registers bots. */
UCLASS(Blueprintable)
class COSMERERIVALS_API ACosmereMenuCharacterPreview : public AActor
{
	GENERATED_BODY()
public:
	ACosmereMenuCharacterPreview();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cosmere|UI|Scene")
	TObjectPtr<USkeletalMeshComponent> PreviewMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cosmere|UI|Scene")
	TSoftObjectPtr<USkeletalMesh> MeshAsset;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cosmere|UI|Scene")
	TSoftObjectPtr<UAnimationAsset> IdleAnimation;
protected:
	virtual void BeginPlay() override;
};
