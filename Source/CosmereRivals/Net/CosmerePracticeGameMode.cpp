// Copyright Epic Games, Inc. All Rights Reserved.

#include "Net/CosmerePracticeGameMode.h"

#include "AbilitySystemComponent.h"
#include "Character/CosmereCharacter.h"
#include "Combat/CosmereWeaponComponent.h"
#include "CosmereRivals.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Net/CosmereBotController.h"
#include "Net/CosmereGameState.h"
#include "Net/CosmereHillObjective.h"
#include "Net/CosmerePlayerState.h"
#include "TimerManager.h"

ACosmerePracticeGameMode::ACosmerePracticeGameMode()
{
	bFrontEndSession = true;
	bHoldBotsIdle = true;
	bGrantPracticeBuild = false;
	bPauseable = false;
}

void ACosmerePracticeGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	// Enforce preview even if an older Blueprint serialized the previous parent's false default.
	bFrontEndSession = true;
	bPracticeRunning = false;
	bHoldBotsIdle = true;
	bGrantPracticeBuild = false;
	bPauseable = false;
	Super::InitGame(MapName, Options, ErrorMessage);
}

void ACosmerePracticeGameMode::InitGameState()
{
	// The engine dispatches this from PreInitializeComponents, after InitGame resolves pending settings.
	// Publish practice settings here so a previous King-of-the-Hill selection cannot leak into the hub.
	MatchSettings.Mode = ECosmereMatchMode::FreeForAll;
	MatchSettings.bBotsEnabled = true;
	MatchSettings.MatchDuration = 0.f;
	Super::InitGameState();
	if (ACosmereGameState* State = GetCosmereGameState())
	{
		State->SetMatchDuration(0.f);
	}
}

bool ACosmerePracticeGameMode::StartPractice(APlayerController* Controller,
	const FSpiritwebCharacterSave& Build, FText& OutError)
{
	OutError = FText::GetEmpty();
	if (!HasAuthority() || !Controller || !Controller->IsLocalController()
		|| !GetWorld() || GetWorld()->GetNetMode() != NM_Standalone)
	{
		OutError = NSLOCTEXT("CosmereUI", "PracticeLocalOnly", "Practice must run in the local standalone hub.");
		return false;
	}
	if (bPracticeRunning) return true;
	ACosmerePlayerState* PlayerState = Controller->GetPlayerState<ACosmerePlayerState>();
	if (!PlayerState)
	{
		OutError = NSLOCTEXT("CosmereUI", "PracticeNoState", "The player is not ready to enter practice.");
		return false;
	}
	ACosmereCharacter* Character = Cast<ACosmereCharacter>(Controller->GetPawn());
	if (!Character || !Character->IsAlive())
	{
		if (Character)
		{
			GetWorldTimerManager().ClearAllTimersForObject(Character);
			Controller->UnPossess();
			Character->Destroy();
		}
		RestartPlayer(Controller);
		Character = Cast<ACosmereCharacter>(Controller->GetPawn());
	}
	if (!Character)
	{
		OutError = NSLOCTEXT("CosmereUI", "PracticeNoPawn", "The practice pawn could not be spawned.");
		return false;
	}
	if (!PlayerState->ApplyLocalPracticeBuild(Build, OutError)) return false;
	if (!Character->HasAppliedBuild() || Character->GetAppliedBuild().CharacterId != Build.CharacterId)
	{
		OutError = NSLOCTEXT("CosmereUI", "PracticeBuildNotApplied", "The selected build did not reach the practice pawn.");
		return false;
	}
	PlayerState->ResetPracticeStatistics();
	Character->SetActorHiddenInGame(false);
	Character->SetActorEnableCollision(true);
	Character->GetCharacterMovement()->StopMovementImmediately();
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	if (UCosmereWeaponComponent* Weapon = Character->FindComponentByClass<UCosmereWeaponComponent>())
	{
		Weapon->SetTriggerHeld(false);
		Weapon->SetWeaponEnabled(true);
	}
	bFrontEndSession = false;
	bMatchStarted = false;
	bMatchEnded = false;
	bPracticeRunning = true;
	GetWorldTimerManager().ClearTimer(WarmupTimerHandle);
	BeginMatch();
	UE_LOG(LogCosmereUI, Log, TEXT("Practice started with character %s; %d idle targets; no map travel."),
		*Build.CharacterId.ToString(), BotControllers.Num());
	return true;
}

void ACosmerePracticeGameMode::StopPractice(APlayerController* Controller)
{
	if (!HasAuthority() || !GetWorld() || GetWorld()->GetNetMode() != NM_Standalone) return;
	// Close the scoring/respawn gate before cancelling anything that can end in a death callback.
	bFrontEndSession = true;
	bPracticeRunning = false;
	bMatchStarted = false;
	bMatchEnded = false;
	GetWorldTimerManager().ClearAllTimersForObject(this);
	if (Controller)
	{
		if (ACosmerePlayerState* PlayerState = Controller->GetPlayerState<ACosmerePlayerState>())
		{
			if (UAbilitySystemComponent* ASC = PlayerState->GetAbilitySystemComponent()) ASC->CancelAllAbilities();
		}
		if (ACosmereCharacter* Character = Cast<ACosmereCharacter>(Controller->GetPawn()))
		{
			Character->SetPowerSlotHeld(EPowerSlot::Alpha, false);
			Character->SetPowerSlotHeld(EPowerSlot::Beta, false);
			GetWorldTimerManager().ClearAllTimersForObject(Character);
			Character->GetCharacterMovement()->StopMovementImmediately();
			Character->GetCharacterMovement()->DisableMovement();
			if (UCosmereWeaponComponent* Weapon = Character->FindComponentByClass<UCosmereWeaponComponent>())
			{
				Weapon->SetTriggerHeld(false);
				Weapon->SetWeaponEnabled(false);
			}
			Character->SetActorHiddenInGame(true);
			Character->SetActorEnableCollision(false);
		}
	}
	for (ACosmereBotController* Bot : BotControllers)
	{
		if (!IsValid(Bot)) continue;
		Bot->StopBot();
		GetWorldTimerManager().ClearAllTimersForObject(Bot);
		if (ACosmerePlayerState* BotState = Bot->GetPlayerState<ACosmerePlayerState>())
		{
			if (UAbilitySystemComponent* ASC = BotState->GetAbilitySystemComponent()) ASC->CancelAllAbilities();
			BotState->Destroy();
		}
		if (APawn* Pawn = Bot->GetPawn())
		{
			GetWorldTimerManager().ClearAllTimersForObject(Pawn);
			Bot->UnPossess();
			Pawn->Destroy();
		}
		Bot->Destroy();
	}
	BotControllers.Reset();
	if (IsValid(HillObjective)) HillObjective->Destroy();
	HillObjective = nullptr;
	if (ACosmereGameState* State = GetCosmereGameState())
	{
		State->SetWinningTeam(ECosmereTeam::None);
		State->SetWinningPlayerName(FString());
		State->SetMatchPhase(ECosmereMatchPhase::Warmup);
		State->SetMatchDuration(0.f);
	}
	UE_LOG(LogCosmereUI, Log, TEXT("Practice stopped; targets and combat timers cleared; preview restored without travel."));
}
