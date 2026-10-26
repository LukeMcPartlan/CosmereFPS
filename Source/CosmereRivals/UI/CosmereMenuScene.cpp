#include "UI/CosmereMenuScene.h"
#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "CosmereRivals.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"

ACosmereMenuCameraRig::ACosmereMenuCameraRig(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetActorEnableCollision(false);
	bReplicates = false;
}

void ACosmereMenuCameraRig::BeginPlay()
{
	Super::BeginPlay();
	MainPose = GetActorTransform();
	ShopPose = ShopPoseOffset * MainPose;
	SourcePose = TargetPose = MainPose;
}

void ACosmereMenuCameraRig::ConfigurePoses(const FTransform& Main, const FTransform& Shop)
{
	MainPose = Main;
	ShopPose = Shop;
	RequestedPose = NAME_None;
}

void ACosmereMenuCameraRig::RequestPose(FName Pose)
{
	if (!GetWorld() || GetWorld()->GetNetMode() != NM_Standalone) return;
	if (Pose != TEXT("MainMenu") && Pose != TEXT("Shop")) return;
	if (RequestedPose == Pose) return;
	RequestedPose = Pose;
	SourcePose = GetActorTransform(); // Interrupts capture the current visual position.
	TargetPose = Pose == TEXT("Shop") ? ShopPose : MainPose;
	Elapsed = 0.f;
	Alpha = bReducedMotion ? 1.f : 0.f;
	if (bReducedMotion) SetActorTransform(TargetPose);
	SetActorTickEnabled(!bReducedMotion);
	UE_LOG(LogCosmereUI, Log, TEXT("Menu camera requested %s, reduced motion=%d"), *Pose.ToString(), bReducedMotion);
}

void ACosmereMenuCameraRig::SetReducedMotion(bool bReduced)
{
	bReducedMotion = bReduced;
	if (bReduced && IsActorTickEnabled())
	{
		Alpha = 1.f;
		SetActorTransform(TargetPose);
		SetActorTickEnabled(false);
	}
}

void ACosmereMenuCameraRig::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += FMath::Max(0.f, DeltaSeconds);
	Alpha = FMath::Clamp(Elapsed / 0.6f, 0.f, 1.f);
	FTransform Pose;
	Pose.Blend(SourcePose, TargetPose, Alpha);
	SetActorTransform(Pose);
	if (Alpha >= 1.f) SetActorTickEnabled(false);
}

ACosmereMenuCharacterPreview::ACosmereMenuCharacterPreview()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	PreviewMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PreviewMesh"));
	SetRootComponent(PreviewMesh);
	PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMesh->SetGenerateOverlapEvents(false);
	PreviewMesh->SetOnlyOwnerSee(false);
	PreviewMesh->SetOwnerNoSee(false);
	SetActorEnableCollision(false);
	MeshAsset = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple")));
	IdleAnimation = TSoftObjectPtr<UAnimationAsset>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle")));
}

void ACosmereMenuCharacterPreview::BeginPlay()
{
	Super::BeginPlay();
	if (GetWorld()->GetNetMode() != NM_Standalone) { SetActorHiddenInGame(true); return; }
	USkeletalMesh* Mesh = MeshAsset.LoadSynchronous();
	UAnimationAsset* Idle = IdleAnimation.LoadSynchronous();
	if (!Mesh || !Idle || !Mesh->GetSkeleton() || Mesh->GetSkeleton() != Idle->GetSkeleton())
	{
		UE_LOG(LogCosmereUI, Error, TEXT("Preview mesh/idle missing or skeleton mismatch; no live pawn mesh was changed."));
		SetActorHiddenInGame(true);
		return;
	}
	PreviewMesh->SetSkeletalMeshAsset(Mesh);
	PreviewMesh->PlayAnimation(Idle, true);
	UE_LOG(LogCosmereUI, Log, TEXT("Menu preview mesh and idle share skeleton %s."), *Mesh->GetSkeleton()->GetPathName());
}
