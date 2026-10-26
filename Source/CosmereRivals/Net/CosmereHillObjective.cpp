// Copyright Epic Games, Inc. All Rights Reserved.

#include "Net/CosmereHillObjective.h"

#include "Character/CosmereCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CosmereRivals.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/CosmereGameState.h"
#include "Net/CosmerePlayerState.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ACosmereHillObjective::ACosmereHillObjective()
{
	// Capture and scoring are server work; every other machine only reads the replicated state, so the
	// tick is gated on authority rather than on a net role.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	bReplicates = true;

	CaptureVolume = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CaptureVolume"));
	SetRootComponent(CaptureVolume);

	// A sensor, not geometry: it must not stop a shot, a body or a fall. Only pawns are sensed at all.
	CaptureVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CaptureVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	CaptureVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CaptureVolume->SetGenerateOverlapEvents(true);
	CaptureVolume->SetHiddenInGame(true);

	HillMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HillMarker"));
	HillMarker->SetupAttachment(CaptureVolume);
	HillMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HillMarker->SetGenerateOverlapEvents(false);
	HillMarker->SetCastShadow(false);

	// The engine's basic cylinder is 100cm across at unit scale, so a radius of CaptureRadius cm is
	// CaptureRadius / 50 in X and Y. OnConstruction re-applies this, so editing the tuning in the
	// details panel moves the disc with it instead of leaving it behind at the constructor's size.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		HillMarker->SetStaticMesh(CylinderMesh.Object);
	}
	else
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereHillObjective: the engine's basic cylinder mesh did not resolve, so the hill has no visible marker. Capture is unaffected."));
	}
}

void ACosmereHillObjective::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Driven from the tuning rather than set once in the constructor, so a designer changing the
	// radius in the details panel sees the volume and the marker move together.
	if (CaptureVolume)
	{
		CaptureVolume->SetCapsuleRadius(CaptureRadius);
		CaptureVolume->SetCapsuleHalfHeight(CaptureHalfHeight);
	}

	if (HillMarker)
	{
		// Flattened to a few centimetres and dropped to the bottom of the volume, so it reads as a pad
		// on the floor rather than as a pillar in the middle of the fight.
		HillMarker->SetRelativeLocation(FVector(0.f, 0.f, -CaptureHalfHeight + 2.f));
		HillMarker->SetRelativeScale3D(FVector(CaptureRadius / 50.f, CaptureRadius / 50.f, 0.04f));
	}
}

void ACosmereHillObjective::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACosmereHillObjective, OwningTeam);
	DOREPLIFETIME(ACosmereHillObjective, ContestingTeam);
	DOREPLIFETIME(ACosmereHillObjective, CaptureProgress);
}

int32 ACosmereHillObjective::GetTeamPresence(ECosmereTeam Team) const
{
	if (Team == ECosmereTeam::None || !CaptureVolume)
	{
		return 0;
	}

	TArray<AActor*> Overlapping;
	CaptureVolume->GetOverlappingActors(Overlapping, APawn::StaticClass());

	int32 Count = 0;
	for (const AActor* Actor : Overlapping)
	{
		const APawn* Pawn = Cast<APawn>(Actor);
		if (!Pawn)
		{
			continue;
		}

		// A corpse stays where it fell for the respawn delay, and it must not hold the point. The
		// capsule is dropped to NoCollision on death, which usually removes it from the overlap list,
		// but the list is cached and a stale entry would otherwise let a body contest the hill.
		const ACosmereCharacter* Character = Cast<ACosmereCharacter>(Pawn);
		if (Character && !Character->IsAlive())
		{
			continue;
		}

		const ACosmerePlayerState* PS = Cast<ACosmerePlayerState>(Pawn->GetPlayerState());
		if (PS && PS->GetMatchTeam() == Team)
		{
			++Count;
		}
	}

	return Count;
}

ECosmereTeam ACosmereHillObjective::ResolveSoleOccupant() const
{
	const int32 CountA = GetTeamPresence(ECosmereTeam::TeamA);
	const int32 CountB = GetTeamPresence(ECosmereTeam::TeamB);

	if (CountA > 0 && CountB == 0)
	{
		return ECosmereTeam::TeamA;
	}

	if (CountB > 0 && CountA == 0)
	{
		return ECosmereTeam::TeamB;
	}

	// Empty, or both sides on it. Either way nobody is taking it this frame.
	return ECosmereTeam::None;
}

void ACosmereHillObjective::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ACosmereGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACosmereGameState>() : nullptr;

	// Warmup and Post-Match both mean "do not score". Gating on the phase here is what stops the hill
	// from having to know anything about how the match is scheduled.
	if (!HasAuthority() || !GameState || GameState->GetMatchPhase() != ECosmereMatchPhase::InProgress)
	{
		return;
	}

	const ECosmereTeam SoleOccupant = ResolveSoleOccupant();

	if (SoleOccupant == ECosmereTeam::None)
	{
		// A contest freezes the meter where it stands rather than draining it. That is what makes
		// contesting the hill a real answer to somebody holding it.
		AwardHeldScore(DeltaSeconds);
		return;
	}

	if (SoleOccupant == OwningTeam)
	{
		// Already theirs and nobody is taking it back.
		ContestingTeam = ECosmereTeam::None;
		CaptureProgress = 1.f;
		AwardHeldScore(DeltaSeconds);
		return;
	}

	// A different side is on the hill. A new challenger restarts the meter; a continuing challenge
	// keeps filling it.
	if (ContestingTeam != SoleOccupant)
	{
		ContestingTeam = SoleOccupant;
		CaptureProgress = 0.f;
	}

	CaptureProgress += DeltaSeconds / FMath::Max(CaptureSeconds, 0.1f);

	if (CaptureProgress < 1.f)
	{
		// The capture itself counts as unheld time, so a side that has just taken the point starts
		// being paid from the tick after the flip rather than retroactively.
		return;
	}

	const ECosmereTeam Previous = OwningTeam;
	OwningTeam = SoleOccupant;
	ContestingTeam = ECosmereTeam::None;
	CaptureProgress = 1.f;
	ScoreAccumulator = 0.f;

	UE_LOG(LogCosmere, Log,
		TEXT("CosmereHillObjective: the hill was taken by %s (was %d)."),
		SoleOccupant == ECosmereTeam::TeamA ? TEXT("Team A") : TEXT("Team B"),
		static_cast<int32>(Previous));

	AwardHeldScore(DeltaSeconds);
}

void ACosmereHillObjective::AwardHeldScore(float DeltaSeconds)
{
	if (OwningTeam == ECosmereTeam::None || SecondsPerPoint <= 0.f)
	{
		return;
	}

	// Paid only while the holder is actually on the hill and nobody is contesting it. Without the
	// presence check the hill would keep paying after the holder died, which would make holding the
	// ground optional and the whole mode pointless.
	if (GetTeamPresence(OwningTeam) == 0)
	{
		return;
	}

	const ECosmereTeam Other =
		(OwningTeam == ECosmereTeam::TeamA) ? ECosmereTeam::TeamB : ECosmereTeam::TeamA;
	if (GetTeamPresence(Other) > 0)
	{
		return;
	}

	ScoreAccumulator += DeltaSeconds / SecondsPerPoint;

	const int32 Whole = FMath::FloorToInt(ScoreAccumulator);
	if (Whole <= 0)
	{
		return;
	}

	ScoreAccumulator -= static_cast<float>(Whole);

	if (ACosmereGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACosmereGameState>() : nullptr)
	{
		GameState->AddTeamScore(OwningTeam, Whole);
	}
}
