// Copyright Epic Games, Inc. All Rights Reserved.

#include "Net/CosmereGameState.h"

#include "Character/CosmereCharacter.h"
#include "Character/CosmereRealmComponent.h"
#include "Character/CosmereTinSensesComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

ACosmereGameState::ACosmereGameState()
{
	// The scoreboard and the clock are read from this actor rather than from the server-only game
	// mode, so every client has to have it. AGameStateBase already replicates; being explicit here
	// keeps that from silently regressing if this constructor is ever edited.
	bReplicates = true;
	bAlwaysRelevant = true;

	// Sized up front so AddTeamScore can index without a validity branch. A client that has not yet
	// received the replicated array still reads zeroes.
	TeamScores.Init(0, NumTeams);
}

void ACosmereGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACosmereGameState, MatchMode);
	DOREPLIFETIME(ACosmereGameState, MatchPhase);
	DOREPLIFETIME(ACosmereGameState, MatchEndTime);
	DOREPLIFETIME(ACosmereGameState, ScoreLimit);
	DOREPLIFETIME(ACosmereGameState, TeamScores);
	DOREPLIFETIME(ACosmereGameState, WinningTeam);
	DOREPLIFETIME(ACosmereGameState, WinningPlayerName);
	DOREPLIFETIME(ACosmereGameState, bBotsEnabled);
}

USoundAttenuation* ACosmereGameState::GetNoiseAttenuation()
{
	if (!NoiseAttenuation)
	{
		NoiseAttenuation = NewObject<USoundAttenuation>(this, TEXT("NoiseAttenuation"));
		FSoundAttenuationSettings& Shape = NoiseAttenuation->Attenuation;

		Shape.bAttenuate = true;
		Shape.bSpatialize = true;
		Shape.AttenuationShape = EAttenuationShape::Sphere;

		// Full volume out to arm's length, then a long falloff. The far end is set against the widest ear in
		// the game - a fully flared tin burner - so a shot across the map still has a sound attached to it
		// when only that ear can still reach it. Perception is decided by the listener, not by this curve.
		Shape.AttenuationShapeExtents = FVector(400.f, 0.f, 0.f);
		Shape.FalloffDistance = 30000.f;
	}

	return NoiseAttenuation;
}

void ACosmereGameState::ReportNoise(AActor* Source, FVector Location, USoundBase* Sound, float Loudness)
{
	// A client asking for the world to hear something is not a thing that can happen: the authority decides.
	if (!HasAuthority())
	{
		return;
	}

	MulticastNoise(Source, Location, Sound, Loudness);
}

void ACosmereGameState::MulticastNoise_Implementation(AActor* Source, FVector Location, USoundBase* Sound, float Loudness)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// A noise is made on one side of the veil and heard on that side. The marker this feeds is a position, and
	// a position the player cannot otherwise act on is still a target - so the shot fired in Shadesmar is not
	// a shot anybody in the physical world can place, and the reverse holds too.
	const APawn* ListenerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (ListenerPawn && !UCosmereRealmComponent::ShareRealm(ListenerPawn, Source))
	{
		return;
	}

	if (Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Location, 1.f, 1.f, 0.f, GetNoiseAttenuation());
	}

	// Offered to every pawn, and each machine decides for itself: the senses component checks that it is the
	// local player, that tin is burning, and that the noise is inside its own hearing radius.
	for (APlayerState* ListenerState : PlayerArray)
	{
		ACosmereCharacter* Listener = ListenerState ? Cast<ACosmereCharacter>(ListenerState->GetPawn()) : nullptr;
		UCosmereTinSensesComponent* Senses = Listener ? Listener->GetTinSensesComponent() : nullptr;
		if (Senses)
		{
			Senses->NotifyHeardNoise(Source);
		}
	}
}

void ACosmereGameState::MulticastBronzeReveal_Implementation(AActor* Seer, const TArray<AActor*>& Burners, float Duration)
{
	if (!Seer || Burners.Num() == 0)
	{
		return;
	}

	// Only the seer's own machine does anything with this. The sweep ran on the authority because only the
	// authority can see everybody's burn level, but the mark itself is a local drawing fact: it lives on the
	// pawn's own reveal list and never replicates, so every other client is told and quietly ignores it.
	ACosmereCharacter* Local = Cast<ACosmereCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Local || Local != Seer)
	{
		return;
	}

	Local->RevealActors(Burners, Duration);
}

int32 ACosmereGameState::GetTimeRemainingSeconds() const
{
	if (MatchEndTime <= 0.f)
	{
		// No clock running (warmup, or a mode with no time limit).
		return 0;
	}

	// Both sides use GetServerWorldTimeSeconds, so the two timestamps share one base on the server
	// and on every client. Subtracting the local world clock here instead would drift by the
	// client's own uptime.
	const float Remaining = MatchEndTime - GetServerWorldTimeSeconds();
	return FMath::Max(0, FMath::CeilToInt(Remaining));
}

int32 ACosmereGameState::GetTeamScore(ECosmereTeam Team) const
{
	// TeamA maps to index 0 and TeamB to index 1. None maps to -1 and falls through to 0.
	const int32 Index = static_cast<int32>(Team) - static_cast<int32>(ECosmereTeam::TeamA);
	return TeamScores.IsValidIndex(Index) ? TeamScores[Index] : 0;
}

void ACosmereGameState::SetMatchMode(ECosmereMatchMode InMode)
{
	MatchMode = InMode;
}

void ACosmereGameState::SetMatchPhase(ECosmereMatchPhase InPhase)
{
	MatchPhase = InPhase;
}

void ACosmereGameState::SetScoreLimit(int32 InLimit)
{
	ScoreLimit = InLimit;
}

void ACosmereGameState::SetBotsEnabled(bool bInEnabled)
{
	bBotsEnabled = bInEnabled;
}

void ACosmereGameState::SetMatchDuration(float DurationSeconds)
{
	if (DurationSeconds <= 0.f)
	{
		// Clearing the clock is distinct from starting one, and GetTimeRemainingSeconds reads a zero
		// MatchEndTime as "no clock".
		MatchEndTime = 0.f;
		return;
	}

	// Server-side only, so GetServerWorldTimeSeconds is the authority's own world clock here. Clients
	// compare against the same function, which is what keeps the two ends in one time base.
	MatchEndTime = GetServerWorldTimeSeconds() + DurationSeconds;
}

void ACosmereGameState::AddTeamScore(ECosmereTeam Team, int32 Delta)
{
	const int32 Index = static_cast<int32>(Team) - static_cast<int32>(ECosmereTeam::TeamA);
	if (TeamScores.IsValidIndex(Index))
	{
		TeamScores[Index] += Delta;
	}
}

void ACosmereGameState::SetWinningTeam(ECosmereTeam InTeam)
{
	WinningTeam = InTeam;
}

void ACosmereGameState::SetWinningPlayerName(const FString& InName)
{
	WinningPlayerName = InName;
}
