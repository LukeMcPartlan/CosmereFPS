// Copyright Epic Games, Inc. All Rights Reserved.

#include "Power/StormlightCloud.h"

#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/CosmerePlayerState.h"
#include "Power/CosmereSurgeResourceComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AStormlightCloud::AStormlightCloud()
{
	PrimaryActorTick.bCanEverTick = false;

	InfluenceSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InfluenceSphere"));
	InfluenceSphere->InitSphereRadius(InfluenceRadius);
	InfluenceSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InfluenceSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	InfluenceSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	InfluenceSphere->SetGenerateOverlapEvents(true);
	SetRootComponent(InfluenceSphere);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	LampMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LampMesh"));
	LampMesh->SetupAttachment(InfluenceSphere);
	LampMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LampMesh->SetGenerateOverlapEvents(false);
	LampMesh->SetRelativeScale3D(FVector(0.4f, 0.4f, 0.4f));
	if (SphereMeshAsset.Succeeded())
	{
		LampMesh->SetStaticMesh(SphereMeshAsset.Object);
	}

	LampGlow = CreateDefaultSubobject<UPointLightComponent>(TEXT("LampGlow"));
	LampGlow->SetupAttachment(InfluenceSphere);
	LampGlow->SetMobility(EComponentMobility::Movable);
	LampGlow->SetIntensity(4000.f);
	LampGlow->SetAttenuationRadius(700.f);
	LampGlow->SetLightColor(FLinearColor(0.62f, 0.86f, 1.f));
	LampGlow->SetCastShadows(false);
}

void AStormlightCloud::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// The radius is editable per instance, so it has to be pushed onto the sphere rather than read once.
	if (InfluenceSphere)
	{
		InfluenceSphere->SetSphereRadius(InfluenceRadius);
	}
}

void AStormlightCloud::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		// Surge pools are authority state, so a client-side lamp would only ever fail its own spend check.
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(RefreshTimer, this, &AStormlightCloud::RefreshOverlappingPawns, RefreshInterval, /*bLoop=*/true);
	}
}

void AStormlightCloud::RefreshOverlappingPawns()
{
	if (!InfluenceSphere || RefreshPerSecond <= 0.f)
	{
		return;
	}

	const float Amount = RefreshPerSecond * RefreshInterval;

	TArray<AActor*> OverlappingActors;
	InfluenceSphere->GetOverlappingActors(OverlappingActors, APawn::StaticClass());

	for (AActor* OverlappingActor : OverlappingActors)
	{
		const APawn* Pawn = Cast<APawn>(OverlappingActor);
		const ACosmerePlayerState* PlayerState = Pawn ? Pawn->GetPlayerState<ACosmerePlayerState>() : nullptr;
		UCosmereSurgeResourceComponent* Surges = PlayerState ? PlayerState->GetSurgeResourceComponent() : nullptr;
		if (!Surges)
		{
			continue;
		}

		Surges->AddStormlight(Amount);
	}
}
