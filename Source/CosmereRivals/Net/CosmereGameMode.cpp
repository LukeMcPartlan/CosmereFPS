// Copyright Epic Games, Inc. All Rights Reserved.

#include "Net/CosmereGameMode.h"

#include "Abilities/GameplayAbility.h"
#include "CosmereGameInstance.h"
#include "CosmereRivals.h"
#include "Data/CosmereCatalogSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Net/CosmereBotController.h"
#include "Net/CosmereGameState.h"
#include "Net/CosmereHillObjective.h"
#include "Net/CosmerePlayerState.h"
#include "TimerManager.h"
#include "UI/CosmereHUD.h"

namespace
{
	/** Local rather than UEnum::GetValueAsString, so a log line reads the same as the scoreboard does. */
	FString TeamToString(ECosmereTeam Team)
	{
		switch (Team)
		{
		case ECosmereTeam::TeamA: return TEXT("Team A");
		case ECosmereTeam::TeamB: return TEXT("Team B");
		default: return TEXT("no team");
		}
	}
}

ACosmereGameMode::ACosmereGameMode()
{
	// Consumed by AController::InitPlayerState (Controller.cpp:676) when the PlayerState is spawned.
	PlayerStateClass = ACosmerePlayerState::StaticClass();

	// The scoreboard, the clock and the phase are replicated state, so they belong on the game state - a
	// client has no game mode at all. On AGameStateBase every write below would have nowhere to land.
	GameStateClass = ACosmereGameState::StaticClass();

	// The match HUD. This is assigned here rather than on the Blueprint so every level running this game
	// mode gets the HUD without needing its own wiring; ACosmereHUD creates the widget per local player.
	HUDClass = ACosmereHUD::StaticClass();

	// The match runs a clock and polls the hill's score, so the tick has to be on. A GameMode that never
	// ticked would never end a match on time.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// Defaults, so a mode dropped into a level with no Blueprint wiring still produces opponents and a
	// hill. Clearing either field disables that part of the match rather than breaking it.
	BotControllerClass = ACosmereBotController::StaticClass();
	HillObjectiveClass = ACosmereHillObjective::StaticClass();
}

bool ACosmereGameMode::ShouldDemoteToSpectator(const FString& Options)
{
	// Already asking to spectate, or there is no session to ask: leave it to the engine.
	if (UGameplayStatics::GetIntOption(Options, TEXT("SpectatorOnly"), 0) == 1)
	{
		return false;
	}

	if (!GameSession)
	{
		return false;
	}

	// Room among the players - this connection joins normally.
	if (!GameSession->AtCapacity(false))
	{
		return false;
	}

	// Player slots are full. Demote only while a spectator slot is still free; once both are full,
	// the engine's own "Server full." rejection is the correct answer.
	return !GameSession->AtCapacity(true);
}

FString ACosmereGameMode::MakeSpectatorOptions(const FString& Options)
{
	// UGameplayStatics::GrabOption (GameplayStatics.cpp:3410) only parses a string that BEGINS with
	// '?', and ParseOption returns the FIRST matching pair. Simply appending "?SpectatorOnly=1" would
	// therefore be ignored whenever the client already sent SpectatorOnly=0, so the pair list is
	// rebuilt with ours replacing any existing one.
	FString Remaining = Options;
	if (!Remaining.IsEmpty() && Remaining[0] != TEXT('?'))
	{
		Remaining = TEXT("?") + Remaining;
	}

	FString Rebuilt;
	FString Pair;
	while (UGameplayStatics::GrabOption(Remaining, Pair))
	{
		FString Key;
		FString Value;
		UGameplayStatics::GetKeyValue(Pair, Key, Value);

		if (Key.Equals(TEXT("SpectatorOnly"), ESearchCase::CaseSensitive))
		{
			continue;
		}

		Rebuilt += TEXT("?");
		Rebuilt += Pair;
	}

	Rebuilt += TEXT("?SpectatorOnly=1");
	return Rebuilt;
}

void ACosmereGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	if (ShouldDemoteToSpectator(Options))
	{
		const FString SpectatorOptions = MakeSpectatorOptions(Options);

		UE_LOG(LogCosmere, Log,
			TEXT("CosmereGameMode: %s arrived to a full server (%d player(s), %d spectator(s)); admitting as a spectator."),
			*Address, GetNumPlayers(), GetNumSpectators());

		Super::PreLogin(SpectatorOptions, Address, UniqueId, ErrorMessage);
		return;
	}

	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
}

APlayerController* ACosmereGameMode::Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	// Re-checked here because Login runs ApproveLogin a second time against the original Options and
	// then passes those same Options to SpawnPlayerController and InitNewPlayer.
	if (ShouldDemoteToSpectator(Options))
	{
		const FString SpectatorOptions = MakeSpectatorOptions(Options);

		UE_LOG(LogCosmere, Log,
			TEXT("CosmereGameMode: login re-checked with the players full (%d player(s)); completing it as a spectator."),
			GetNumPlayers());

		return Super::Login(NewPlayer, InRemoteRole, Portal, SpectatorOptions, UniqueId, ErrorMessage);
	}

	return Super::Login(NewPlayer, InRemoteRole, Portal, Options, UniqueId, ErrorMessage);
}

void ACosmereGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	if (!NewPlayer)
	{
		return;
	}

	ACosmerePlayerState* CosmerePlayerState = NewPlayer->GetPlayerState<ACosmerePlayerState>();
	if (!CosmerePlayerState)
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereGameMode: %s logged in with no ACosmerePlayerState, so it has no side and is not scored."),
			*NewPlayer->GetName());
		return;
	}

	// A connection the overflow path demoted keeps its camera and watches. It is deliberately left off a
	// side: it does not play, so it must not be scored, and it must not tip the team balance either.
	if (CosmerePlayerState->IsSpectator())
	{
		UE_LOG(LogCosmere, Log,
			TEXT("CosmereGameMode: %s joined as a spectator (%d playing, %d spectating)."),
			*CosmerePlayerState->GetPlayerName(), GetNumPlayers(), GetNumSpectators());
		return;
	}

	CosmerePlayerState->SetBotPlayer(false);
	CosmerePlayerState->SetMatchTeam(PickBalancedTeam());

	UE_LOG(LogCosmere, Log,
		TEXT("CosmereGameMode: %s joined on %s (%d playing)."),
		*CosmerePlayerState->GetPlayerName(), *TeamToString(CosmerePlayerState->GetMatchTeam()), GetNumPlayers());
}

void ACosmereGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	// Resolve during InitGame, before the engine's later PreInitializeComponents dispatches
	// InitGameState and publishes these settings into the game state.
	MatchSettings = ResolveMatchSettings();

	Super::InitGame(MapName, Options, ErrorMessage);

	UE_LOG(LogCosmere, Log,
		TEXT("CosmereGameMode: %s - score limit %d, clock %.0fs, bots %s."),
		MatchSettings.Mode == ECosmereMatchMode::KingOfTheHill ? TEXT("King of the Hill") : TEXT("Free-for-All"),
		MatchSettings.ScoreLimit,
		MatchSettings.MatchDuration,
		MatchSettings.bBotsEnabled ? TEXT("on") : TEXT("off"));
}

void ACosmereGameMode::InitGameState()
{
	Super::InitGameState();

	ACosmereGameState* CosmereGameState = GetCosmereGameState();
	if (!CosmereGameState)
	{
		// A Blueprint's GameStateClass wins over this class's constructor default, so this is the
		// tripwire for a BP_CosmereGameMode left pointing at AGameStateBase: without it the score, the
		// clock and the phase would all silently have nowhere to go.
		UE_LOG(LogCosmere, Error,
			TEXT("CosmereGameMode: the game state is not an ACosmereGameState, so nothing about this match will replicate. Check GameStateClass on the game mode Blueprint."));
		return;
	}

	CosmereGameState->SetMatchMode(MatchSettings.Mode);
	CosmereGameState->SetScoreLimit(MatchSettings.ScoreLimit);
	CosmereGameState->SetBotsEnabled(MatchSettings.bBotsEnabled);

	// Explicit, so a game state reused across a travel cannot carry a previous match's result.
	CosmereGameState->SetWinningTeam(ECosmereTeam::None);
	CosmereGameState->SetWinningPlayerName(FString());
	CosmereGameState->SetMatchPhase(ECosmereMatchPhase::Warmup);
}

void ACosmereGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (bFrontEndSession)
	{
		UE_LOG(LogCosmereUI, Log, TEXT("Front-end preview: automatic match startup suppressed."));
		return;
	}

	// The warmup exists so a travelling client finishes loading the match level before the clock starts
	// and before the bots start shooting.
	if (WarmupSeconds > 0.f)
	{
		GetWorldTimerManager().SetTimer(WarmupTimerHandle, this, &ACosmereGameMode::BeginMatch, WarmupSeconds, false);
	}
	else
	{
		BeginMatch();
	}
}

void ACosmereGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bMatchStarted || bMatchEnded || bFrontEndSession || !ShouldRunMatchRules())
	{
		return;
	}

	const ACosmereGameState* CosmereGameState = GetCosmereGameState();
	if (!CosmereGameState || CosmereGameState->GetMatchPhase() != ECosmereMatchPhase::InProgress)
	{
		return;
	}

	// The hill pays team score itself rather than routing through a kill, so the win check has to be
	// polled. A kill-driven check alone would never see a King of the Hill win.
	CheckWinConditions();
	CheckTimeExpiry();
}

void ACosmereGameMode::BeginMatch()
{
	if (bMatchStarted || bMatchEnded || bFrontEndSession || !HasAuthority())
	{
		return;
	}

	bMatchStarted = true;

	// The hill exists before the clock starts: in King of the Hill the score comes from holding it, so a
	// match that began with no hill would be unwinnable.
	if (MatchSettings.Mode == ECosmereMatchMode::KingOfTheHill)
	{
		SpawnHillObjective();
	}

	if (MatchSettings.bBotsEnabled)
	{
		SpawnBots();
	}

	if (ACosmereGameState* CosmereGameState = GetCosmereGameState())
	{
		CosmereGameState->SetMatchPhase(ECosmereMatchPhase::InProgress);
		CosmereGameState->SetMatchDuration(MatchSettings.MatchDuration);
	}

	UE_LOG(LogCosmere, Log,
		TEXT("CosmereGameMode: the match is live - %d player(s), %d bot(s), clock %.0fs."),
		GetNumPlayers(), BotControllers.Num(), MatchSettings.MatchDuration);
}

void ACosmereGameMode::RegisterDeath(AController* Victim, AController* Killer)
{
	if (!HasAuthority() || bMatchEnded || bFrontEndSession)
	{
		return;
	}

	ACosmerePlayerState* VictimState = Victim ? Victim->GetPlayerState<ACosmerePlayerState>() : nullptr;
	if (VictimState)
	{
		VictimState->AddDeath();
	}

	// No killer, or a controller killing its own pawn: the death is recorded above but no point is
	// awarded. A fall or a world hazard arrives here with Killer null.
	ACosmerePlayerState* KillerState = Killer ? Killer->GetPlayerState<ACosmerePlayerState>() : nullptr;
	if (!KillerState || KillerState == VictimState)
	{
		UE_LOG(LogCosmere, Log,
			TEXT("CosmereGameMode: %s died with %s to credit, so no kill was scored."),
			VictimState ? *VictimState->GetPlayerName() : TEXT("an unknown pawn"),
			KillerState ? TEXT("its own controller") : TEXT("nobody"));
		return;
	}

	// Friendly fire is a death on the scoreboard and nothing else. Checked against the victim state only
	// when there is one, so an unattributed death in a team mode cannot look like a team kill.
	const bool bTeamMode = MatchSettings.Mode == ECosmereMatchMode::KingOfTheHill;
	if (bTeamMode && VictimState && KillerState->GetMatchTeam() == VictimState->GetMatchTeam())
	{
		UE_LOG(LogCosmere, Log,
			TEXT("CosmereGameMode: %s killed a team-mate (%s); no kill was scored."),
			*KillerState->GetPlayerName(), *VictimState->GetPlayerName());
		return;
	}

	// AddKill also raises the inherited APlayerState::Score, which is the number Free-for-All compares
	// against the score limit and the point column the scoreboard shows.
	KillerState->AddKill();

	UE_LOG(LogCosmere, Log,
		TEXT("CosmereGameMode: %s killed %s (now %d kill(s), limit %d)."),
		*KillerState->GetPlayerName(),
		VictimState ? *VictimState->GetPlayerName() : TEXT("an unknown pawn"),
		KillerState->GetKills(),
		MatchSettings.ScoreLimit);

	// Checked here as well as in the tick, so a kill that reaches the limit ends the match on the frame
	// it lands rather than up to a frame later.
	CheckWinConditions();
}

void ACosmereGameMode::RespawnPlayerController(AController* Controller)
{
	if (!HasAuthority() || !Controller || bFrontEndSession)
	{
		return;
	}

	// Once the result is in there is nothing to respawn into, and the corpse-and-replacement churn
	// would only fight the post-match screen for the player's camera.
	if (bMatchEnded)
	{
		UE_LOG(LogCosmere, Log,
			TEXT("CosmereGameMode: %s asked for a respawn after the match ended; refused."),
			*Controller->GetName());
		return;
	}

	// A human is handed to the engine, which owns the pawn-class resolution, the PlayerStart choice and
	// the possession. A bot is not, because that path ends in a Client RPC a bot has no connection to
	// travel over.
	if (Controller->IsA<APlayerController>())
	{
		RestartPlayer(Controller);
		return;
	}

	SpawnAndPossessPawnFor(Controller);
}

void ACosmereGameMode::ForceEndMatch(ECosmereTeam WinningTeam)
{
	// A game mode only exists on the server, so authority is implied - but this is BlueprintCallable and
	// the guard is cheap insurance against a client-side call from a widget.
	if (!HasAuthority())
	{
		return;
	}

	ConcludeMatch(WinningTeam, FString());
}

ACosmereGameState* ACosmereGameMode::GetCosmereGameState() const
{
	return GetGameState<ACosmereGameState>();
}

FCosmereMatchSettings ACosmereGameMode::ResolveMatchSettings() const
{
	UWorld* World = GetWorld();
	UCosmereGameInstance* GameInstance = World ? World->GetGameInstance<UCosmereGameInstance>() : nullptr;
	if (!GameInstance)
	{
		// Nothing wrote a choice - a direct PIE session opened on the match level, for instance. The
		// struct's own defaults describe a playable match, so that is what runs.
		UE_LOG(LogCosmere, Log,
			TEXT("CosmereGameMode: no GameInstance of ours, so the match runs on the default rule set."));
		return FCosmereMatchSettings();
	}

	return GameInstance->PendingMatchSettings;
}

void ACosmereGameMode::ConcludeMatch(ECosmereTeam WinningTeam, const FString& WinningPlayerName)
{
	if (bMatchEnded || bFrontEndSession || !ShouldRunMatchRules())
	{
		return;
	}

	bMatchEnded = true;

	if (ACosmereGameState* CosmereGameState = GetCosmereGameState())
	{
		// Phase and clock first: a stale InProgress with a running clock would let the next frame's tick
		// conclude the match a second time.
		CosmereGameState->SetMatchPhase(ECosmereMatchPhase::PostMatch);
		CosmereGameState->SetMatchDuration(0.f);
		CosmereGameState->SetWinningTeam(WinningTeam);
		CosmereGameState->SetWinningPlayerName(WinningPlayerName);
	}

	// The AI is frozen, not the players: whoever is still alive should be able to sit in their own pawn
	// and read their own result.
	for (const TObjectPtr<ACosmereBotController>& Bot : BotControllers)
	{
		if (IsValid(Bot))
		{
			Bot->StopBot();
		}
	}

	if (!WinningPlayerName.IsEmpty())
	{
		UE_LOG(LogCosmere, Log, TEXT("CosmereGameMode: match over - %s wins."), *WinningPlayerName);
	}
	else if (WinningTeam != ECosmereTeam::None)
	{
		UE_LOG(LogCosmere, Log, TEXT("CosmereGameMode: match over - %s wins."), *TeamToString(WinningTeam));
	}
	else
	{
		UE_LOG(LogCosmere, Log, TEXT("CosmereGameMode: match over - nobody won."));
	}
}

void ACosmereGameMode::CheckWinConditions()
{
	if (bMatchEnded || bFrontEndSession || !ShouldRunMatchRules())
	{
		return;
	}

	ACosmereGameState* CosmereGameState = GetCosmereGameState();
	if (!CosmereGameState)
	{
		return;
	}

	if (MatchSettings.Mode == ECosmereMatchMode::KingOfTheHill)
	{
		// A side's score is the hill's, so this is the only check the mode needs.
		if (CosmereGameState->GetTeamScore(ECosmereTeam::TeamA) >= MatchSettings.ScoreLimit)
		{
			ConcludeMatch(ECosmereTeam::TeamA, FString());
		}
		else if (CosmereGameState->GetTeamScore(ECosmereTeam::TeamB) >= MatchSettings.ScoreLimit)
		{
			ConcludeMatch(ECosmereTeam::TeamB, FString());
		}

		return;
	}

	// Free-for-All is won by a single player, so the winner is reported by name. PlayerArray is the list
	// to walk rather than the controller iterator, because a bot's controller is an AController and the
	// controller iterator would not see it - a bot winning would never end the match.
	for (const TObjectPtr<APlayerState>& Entry : CosmereGameState->PlayerArray)
	{
		const ACosmerePlayerState* CosmerePlayerState = Cast<ACosmerePlayerState>(Entry.Get());
		if (CosmerePlayerState && CosmerePlayerState->GetScore() >= MatchSettings.ScoreLimit)
		{
			ConcludeMatch(ECosmereTeam::None, CosmerePlayerState->GetPlayerName());
			return;
		}
	}
}

void ACosmereGameMode::CheckTimeExpiry()
{
	if (bMatchEnded || bFrontEndSession || !ShouldRunMatchRules())
	{
		return;
	}

	// A mode with no time limit never expires, and GetTimeRemainingSeconds reads a cleared clock as zero,
	// so this has to be checked before the remaining time is.
	if (MatchSettings.MatchDuration <= 0.f)
	{
		return;
	}

	ACosmereGameState* CosmereGameState = GetCosmereGameState();
	if (!CosmereGameState || CosmereGameState->GetTimeRemainingSeconds() > 0)
	{
		return;
	}

	if (MatchSettings.Mode == ECosmereMatchMode::KingOfTheHill)
	{
		const int32 ScoreA = CosmereGameState->GetTeamScore(ECosmereTeam::TeamA);
		const int32 ScoreB = CosmereGameState->GetTeamScore(ECosmereTeam::TeamB);

		if (ScoreA == ScoreB)
		{
			// Nobody leads, so nobody wins. Naming a side here would invent a result the scoreboard
			// plainly contradicts.
			ConcludeMatch(ECosmereTeam::None, FString());
			return;
		}

		ConcludeMatch(ScoreA > ScoreB ? ECosmereTeam::TeamA : ECosmereTeam::TeamB, FString());
		return;
	}

	// Free-for-All: whoever leads on kills. A tie at the top is reported as no winner rather than being
	// broken by PlayerArray's order.
	const ACosmerePlayerState* Leader = nullptr;
	bool bTiedAtTheTop = false;

	for (const TObjectPtr<APlayerState>& Entry : CosmereGameState->PlayerArray)
	{
		const ACosmerePlayerState* CosmerePlayerState = Cast<ACosmerePlayerState>(Entry.Get());
		if (!CosmerePlayerState)
		{
			continue;
		}

		if (!Leader || CosmerePlayerState->GetScore() > Leader->GetScore())
		{
			Leader = CosmerePlayerState;
			bTiedAtTheTop = false;
		}
		else if (CosmerePlayerState->GetScore() == Leader->GetScore())
		{
			bTiedAtTheTop = true;
		}
	}

	if (!Leader || bTiedAtTheTop)
	{
		ConcludeMatch(ECosmereTeam::None, FString());
		return;
	}

	ConcludeMatch(ECosmereTeam::None, Leader->GetPlayerName());
}

ECosmereTeam ACosmereGameMode::PickBalancedTeam() const
{
	// Free-for-All has no sides at all: every player is their own, which is what None means throughout
	// this match layer.
	if (MatchSettings.Mode == ECosmereMatchMode::FreeForAll)
	{
		return ECosmereTeam::None;
	}

	int32 CountA = 0;
	int32 CountB = 0;

	if (AGameStateBase* CurrentGameState = GameState)
	{
		for (const TObjectPtr<APlayerState>& Entry : CurrentGameState->PlayerArray)
		{
			const ACosmerePlayerState* CosmerePlayerState = Cast<ACosmerePlayerState>(Entry.Get());
			if (!CosmerePlayerState)
			{
				continue;
			}

			if (CosmerePlayerState->GetMatchTeam() == ECosmereTeam::TeamA)
			{
				++CountA;
			}
			else if (CosmerePlayerState->GetMatchTeam() == ECosmereTeam::TeamB)
			{
				++CountB;
			}
		}
	}

	// Ties go to Team A, which only ever happens on the first player in.
	return CountA <= CountB ? ECosmereTeam::TeamA : ECosmereTeam::TeamB;
}

void ACosmereGameMode::SpawnBots()
{
	if (!BotControllerClass)
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereGameMode: bots are enabled but no BotControllerClass is set, so the match runs with no AI."));
		return;
	}

	if (MatchSettings.Mode == ECosmereMatchMode::KingOfTheHill)
	{
		// An even split, and the two sides are numbered off the same ring walk so no two bots share a
		// spawn slot.
		for (int32 Index = 0; Index < BotsPerTeam; ++Index)
		{
			SpawnBot(ECosmereTeam::TeamA, Index);
			SpawnBot(ECosmereTeam::TeamB, BotsPerTeam + Index);
		}

		return;
	}

	for (int32 Index = 0; Index < FreeForAllBotCount; ++Index)
	{
		SpawnBot(ECosmereTeam::None, Index);
	}
}

void ACosmereGameMode::SpawnBot(ECosmereTeam Team, int32 Index)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	// The ring deliberately overlaps whatever the level already has there, so collision must not be
	// allowed to refuse the spawn - a silently missing bot would break the field size the mode promised.
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FTransform SpawnTransform = ResolveBotSpawnTransform(Index);

	ACosmereBotController* Bot = World->SpawnActor<ACosmereBotController>(
		BotControllerClass, SpawnTransform.GetLocation(), SpawnTransform.Rotator(), SpawnParameters);

	if (!Bot)
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereGameMode: failed to spawn a bot at slot %d for %s."), Index, *TeamToString(Team));
		return;
	}

	// Held before the kit is built, so a respawn returns to this slot even if the kit could not resolve.
	Bot->SetSpawnIndex(Index);

	FSpiritwebCharacterSave BotKit;
	const bool bBuiltKit = BuildBotKit(BotKit);

	// Configured either way: without a kit a bot still needs its side and its name, or it would be an
	// unscored row on the scoreboard fighting on default stats.
	Bot->ConfigureBot(BotKit, Team, MakeBotName(Team, Index));

	if (bHoldBotsIdle)
	{
		// Held idle before the pawn exists, because the possess that follows re-enables the tick - so the
		// freeze has to be a property of the controller rather than something done to the pawn once it is
		// up. SetIdle is sticky precisely so the respawn that follows a death does not undo it.
		Bot->SetIdle(true);
		
		// Idle targets walk a small circle around their own spawn slot, so anything that has to be walked over
		// has something walking over it. Set here because this is the one place that knows the bot is a target
		// and not a fighter; a radius of 0 leaves the range exactly as it was.
		Bot->SetCirclePatrol(IdleBotCircleRadius);
	}

	BotControllers.Add(Bot);

	if (!bBuiltKit)
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereGameMode: %s was spawned with no resolvable kit and will fight on default stats."),
			*Bot->GetName());
	}

	SpawnAndPossessPawnFor(Bot);
}

bool ACosmereGameMode::BuildBotKit(FSpiritwebCharacterSave& OutBuild) const
{
	UWorld* World = GetWorld();
	UCosmereGameInstance* GameInstance = World ? World->GetGameInstance<UCosmereGameInstance>() : nullptr;
	const UCosmereCatalogSubsystem* Catalog = GameInstance ? GameInstance->GetCatalog() : nullptr;
	if (!Catalog || !Catalog->IsCatalogReady())
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereGameMode: the catalog is not loaded, so no bot kit could be generated."));
		return false;
	}

	const TArray<FName> RaceIds = Catalog->GetAllRaceIds();
	const TArray<FName> WeaponIds = Catalog->GetAllWeaponIds();
	const TArray<FName> PowerIds = Catalog->GetAllPowerIds();

	// Two distinct weapons and two distinct powers is not a preference - it is what
	// ACosmerePlayerState::ValidateBuildAgainstCatalog insists on, so a catalog that cannot supply it
	// cannot produce a legal kit at all and the bot is better left unarmed than given a refused one.
	if (RaceIds.Num() == 0 || WeaponIds.Num() < 2 || PowerIds.Num() < 2)
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereGameMode: the catalog cannot build a legal bot kit (%d race(s), %d weapon(s), %d power(s)); two distinct weapons and two distinct powers are required."),
			RaceIds.Num(), WeaponIds.Num(), PowerIds.Num());
		return false;
	}

	// Varied rather than fixed, so the field is not a dozen identical opponents. Server-side only, so
	// there is no determinism the clients could disagree about.
	const int32 FirstWeapon = FMath::RandRange(0, WeaponIds.Num() - 1);
	int32 SecondWeapon = FMath::RandRange(0, WeaponIds.Num() - 2);
	if (SecondWeapon >= FirstWeapon)
	{
		++SecondWeapon;
	}

	const int32 FirstPower = FMath::RandRange(0, PowerIds.Num() - 1);
	int32 SecondPower = FMath::RandRange(0, PowerIds.Num() - 2);
	if (SecondPower >= FirstPower)
	{
		++SecondPower;
	}

	OutBuild = FSpiritwebCharacterSave();
	OutBuild.CharacterId = FGuid::NewGuid();
	OutBuild.RaceId = RaceIds[FMath::RandRange(0, RaceIds.Num() - 1)];
	OutBuild.WeaponIds.Add(WeaponIds[FirstWeapon]);
	OutBuild.WeaponIds.Add(WeaponIds[SecondWeapon]);
	OutBuild.PowerIds.Add(PowerIds[FirstPower]);
	OutBuild.PowerIds.Add(PowerIds[SecondPower]);

	// Left empty on purpose: a preset hero would override the hand-built kit assembled above, and a bot
	// is specified as a built character rather than a hero.
	OutBuild.EquippedHeroId = NAME_None;

	return true;
}

bool ACosmereGameMode::BuildPracticeKit(FSpiritwebCharacterSave& OutBuild) const
{
	if (!bGrantPracticeBuild)
	{
		return false;
	}

	UWorld* World = GetWorld();
	UCosmereGameInstance* GameInstance = World ? World->GetGameInstance<UCosmereGameInstance>() : nullptr;
	const UCosmereCatalogSubsystem* Catalog = GameInstance ? GameInstance->GetCatalog() : nullptr;
	if (!Catalog || !Catalog->IsCatalogReady())
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereGameMode: the catalog is not loaded, so no practice kit could be generated."));
		return false;
	}

	const TArray<FName> RaceIds = Catalog->GetAllRaceIds();
	const TArray<FName> WeaponIds = Catalog->GetAllWeaponIds();

	// The first race and the first two weapons, unlike a bot's randomised kit: a practice session is meant
	// to be the same every time, so a bug that only shows on one race or one weapon is reproducible.
	if (RaceIds.Num() == 0 || WeaponIds.Num() < 2)
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereGameMode: the catalog cannot build a legal practice kit (%d race(s), %d weapon(s)); one race and two distinct weapons are required."),
			RaceIds.Num(), WeaponIds.Num());
		return false;
	}

	FSpiritwebCharacterSave Kit;
	Kit.CharacterId = FGuid::NewGuid();
	Kit.RaceId = RaceIds[0];
	Kit.WeaponIds.Add(WeaponIds[0]);
	Kit.WeaponIds.Add(WeaponIds[1]);

	for (const FName& PowerId : PracticePowerIds)
	{
		if (Kit.PowerIds.Num() >= 2)
		{
			break;
		}

		FPowerDefinition Power;
		if (!Catalog->GetPowerRow(PowerId, Power))
		{
			UE_LOG(LogCosmere, Warning,
				TEXT("CosmereGameMode: practice power id '%s' is not a row in DT_Powers, so it was skipped."),
				*PowerId.ToString());
			continue;
		}

		// This is the same condition ACosmerePlayerState::GrantPowersFromBuild skips on, so an id named here
		// without an AbilityClass is reported now, at the place it was configured, rather than later as a
		// power that silently never appears.
		if (!Power.AbilityClass)
		{
			UE_LOG(LogCosmere, Warning,
				TEXT("CosmereGameMode: practice power id '%s' has no AbilityClass, so it was skipped."),
				*PowerId.ToString());
			continue;
		}

		if (Kit.PowerIds.Contains(PowerId))
		{
			continue;
		}

		Kit.PowerIds.Add(PowerId);
	}

	// Two powers is the shape ValidateBuildAgainstCatalog insists on, so a kit short of one would be refused
	// on the way back in and the player would spawn with nothing. Better to grant nothing and say so.
	if (Kit.PowerIds.Num() < 2)
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereGameMode: only %d of the practice powers resolved, and two are required, so no practice kit was granted."),
			Kit.PowerIds.Num());
		return false;
	}

	// Left empty on purpose: a preset hero carries its own powers and would override the two slots set above.
	Kit.EquippedHeroId = NAME_None;

	UE_LOG(LogCosmere, Log,
		TEXT("CosmereGameMode: built a practice kit (race %s, weapon(s) %s / %s, power(s) %s / %s)."),
		*Kit.RaceId.ToString(),
		*Kit.WeaponIds[0].ToString(),
		*Kit.WeaponIds[1].ToString(),
		*Kit.PowerIds[0].ToString(),
		*Kit.PowerIds[1].ToString());

	OutBuild = Kit;
	return true;
}

FString ACosmereGameMode::MakeBotName(ECosmereTeam Team, int32 Index) const
{
	// A and B for the two sides, F for a free-for-all field where every bot is its own side.
	const TCHAR* Prefix = Team == ECosmereTeam::TeamA ? TEXT("A")
		: Team == ECosmereTeam::TeamB ? TEXT("B")
		: TEXT("F");

	return FString::Printf(TEXT("Bot %s%02d"), Prefix, Index);
}

APawn* ACosmereGameMode::SpawnAndPossessPawnFor(AController* Controller)
{
	UWorld* World = GetWorld();
	if (!World || !Controller)
	{
		return nullptr;
	}

	// The same class resolution RestartPlayer uses, so a bot is handed the same pawn a player gets
	// rather than a second, divergent default.
	UClass* PawnClass = GetDefaultPawnClassForController(Controller);
	if (!PawnClass)
	{
		PawnClass = DefaultPawnClass;
	}

	if (!PawnClass)
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereGameMode: there is no pawn class to give %s."), *Controller->GetName());
		return nullptr;
	}

	// A bot returns to the slot it started on, so a respawned field is laid out the way the opening one
	// was instead of piling every replacement onto the PlayerStart.
	const ACosmereBotController* Bot = Cast<ACosmereBotController>(Controller);
	const FTransform SpawnTransform = ResolveBotSpawnTransform(Bot ? Bot->GetSpawnIndex() : 0);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = Controller;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	APawn* NewPawn = World->SpawnActor<APawn>(
		PawnClass, SpawnTransform.GetLocation(), SpawnTransform.Rotator(), SpawnParameters);

	if (!NewPawn)
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereGameMode: failed to spawn a pawn for %s."), *Controller->GetName());
		return nullptr;
	}

	// The possess itself, without the AController::ClientSetRotation at the end of RestartPlayer. That
	// call is a Client RPC, and a bot has no owning connection for it to travel over - which is the
	// whole reason this exists alongside RestartPlayer rather than replacing it.
	Controller->Possess(NewPawn);

	return NewPawn;
}

FTransform ACosmereGameMode::ResolveBotSpawnTransform(int32 Index)
{
	// Anchored on an authored PlayerStart when the level has one, so a bot field lands where the level
	// designer put the players rather than at the world origin. Walked directly rather than through
	// FindPlayerStart, which logs a warning apiece when a level has no start to find.
	FVector Origin = FVector::ZeroVector;
	float BaseYaw = 0.f;

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			Origin = It->GetActorLocation();
			BaseYaw = It->GetActorRotation().Yaw;
			break;
		}
	}

	const int32 SafeBotsPerRing = FMath::Max(1, BotsPerSpawnRing);
	const int32 Ring = Index / SafeBotsPerRing;
	const int32 Slot = Index % SafeBotsPerRing;

	// Later bots go onto an outer ring, so a field larger than one ring does not wrap back onto the
	// first and stack.
	const float Radius = BotSpawnRingRadius * static_cast<float>(Ring + 1);
	const float Angle = ((2.f * PI) / static_cast<float>(SafeBotsPerRing)) * static_cast<float>(Slot);

	const FVector Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);

	return FTransform(FRotator(0.f, BaseYaw, 0.f), Origin + Offset, FVector::OneVector);
}

void ACosmereGameMode::SpawnHillObjective()
{
	if (HillObjective)
	{
		return;
	}

	// A hill authored into the level wins over a spawned one: the level author knows where the capture
	// zone belongs, and a spawned hill on top of one would give the mode two scoring volumes.
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ACosmereHillObjective> It(World); It; ++It)
		{
			HillObjective = *It;

			UE_LOG(LogCosmere, Log,
				TEXT("CosmereGameMode: using the level's %s as the hill."), *HillObjective->GetName());
			return;
		}
	}

	if (!HillObjectiveClass)
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereGameMode: King of the Hill has no HillObjectiveClass and the level authors no hill, so neither side can score."));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	HillObjective = World->SpawnActor<ACosmereHillObjective>(
		HillObjectiveClass, HillLocation, FRotator::ZeroRotator, SpawnParameters);

	if (HillObjective)
	{
		UE_LOG(LogCosmere, Log,
			TEXT("CosmereGameMode: spawned %s as the hill at %s."),
			*HillObjective->GetName(), *HillLocation.ToCompactString());
	}
	else
	{
		UE_LOG(LogCosmere, Warning, TEXT("CosmereGameMode: failed to spawn the hill objective."));
	}
}
