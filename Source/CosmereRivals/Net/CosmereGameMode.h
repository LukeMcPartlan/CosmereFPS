// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Net/CosmereMatchTypes.h"
#include "Templates/SubclassOf.h"
#include "CosmereGameMode.generated.h"

class ACosmereBotController;
class ACosmereGameState;
class ACosmereHillObjective;
struct FSpiritwebCharacterSave;

/**
 * Match GameMode for CosmereRivals.
 *
 * Two jobs, and they are deliberately kept apart.
 *
 * The first is spectator overflow: a connection that arrives once the player slots are full becomes a
 * spectator rather than being turned away, as long as a spectator slot is still free. With the 12
 * playing / 8 spectating split configured in DefaultGame.ini that gives a 20-connection ceiling
 * instead of a hard 12.
 *
 * That is C++ and not Blueprint because the engine exposes no Blueprint hook for it. Verified against
 * the UE 5.8 source:
 *   - AGameModeBase::PreLogin (GameModeBase.cpp:684) is the only PreLogin override in the engine, and
 *     all it does is call GameSession->ApproveLogin(Options).
 *   - AGameSession::ApproveLogin (GameSession.cpp:220) reads SpectatorOnly out of Options and, when
 *     AGameSession::AtCapacity (GameSession.cpp:322) reports the server full, returns "Server full."
 *     It rejects; it never demotes.
 *   - AGameSession::AtCapacity's spectator branch is (NumSpectators >= MaxSpectators) with a
 *     listen-server carve-out, so the [8] MaxSpectators in DefaultGame.ini is the spectator ceiling.
 *
 * The demotion therefore has to work by rewriting the Options string the engine will parse, and BOTH
 * PreLogin and Login are overridden on purpose: Login calls ApproveLogin a second time with the
 * original Options and then hands those same Options to SpawnPlayerController and InitNewPlayer
 * (GameModeBase.cpp:715, :743 and :793), the last of which is what actually calls
 * StartSpectatingOnly(). Rewriting only in PreLogin would admit the connection at the capacity gate
 * and then have Login reject it with "Server full." anyway.
 *
 * The second job is the match itself: which rule set is running, who is on which side, when a kill is
 * worth a point, when a respawn happens, and when the match is over. All of that reads from a single
 * resolved FCosmereMatchSettings, which arrives from the host's menu through the GameInstance - the
 * only object that survives the level travel between the menu and the match.
 *
 * This stays an AGameModeBase rather than being promoted to AGameMode, deliberately. AGameMode brings
 * MatchState and its own StartMatch/EndMatch along with it, and taking it would replace the PreLogin
 * and Login overrides above - which are verified working - with AGameMode's, which do not have the
 * demotion. Match phases are carried by ACosmereGameState instead.
 */
UCLASS()
class COSMERERIVALS_API ACosmereGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACosmereGameMode();

	//~ Begin AActor interface
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor interface

	//~ Begin AGameModeBase interface
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual APlayerController* Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	//~ End AGameModeBase interface

	//~ Begin match flow. These are the two seams ACosmereCharacter calls into.
	/**
	 * Records one death and, when there is a distinct killer, credits the kill.
	 *
	 * Called from ACosmereCharacter::Die before any of the pawn is torn down, so both PlayerStates are
	 * still alive and can still be written to. Either controller may be null: damage from a fall or
	 * from the world has no killer, and a bot's controller is an AController rather than a
	 * PlayerController, which is why neither parameter is typed as one.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Match")
	void RegisterDeath(AController* Victim, AController* Killer);

	/**
	 * Gives a controller that has just lost its pawn a replacement. Called from ACosmereCharacter::Respawn.
	 *
	 * A player goes through the engine's RestartPlayer; a bot does not, for the reason documented on
	 * SpawnAndPossessPawnFor.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Match")
	void RespawnPlayerController(AController* Controller);

	/** The rule set this match resolved to, so a HUD can label the mode without reading the instance. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Match")
	ECosmereMatchMode GetMatchMode() const { return MatchSettings.Mode; }

	/** Ends the match early. A tie is expressed by passing ECosmereTeam::None. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Match")
	void ForceEndMatch(ECosmereTeam WinningTeam);

	/**
	 * True when this GameMode is driving a front-end menu level rather than a playable match.
	 *
	 * This exists because the hub and a playable level are the same map: Lvl_FirstPerson is both the
	 * project's GameDefaultMap and the map UCosmereSessionSubsystem::HostLanSession ServerTravels to.
	 * "Is this machine standalone" therefore cannot tell the front end apart from a playable level, so
	 * the GameMode states which it is and the player controller asks. The menu level's GameMode
	 * Blueprint sets this true; a level whose GameMode override leaves it false gets the match HUD
	 * instead of the hub menu.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Front End")
	bool IsFrontEndSession() const { return bFrontEndSession; }
	//~ End match flow
	
	//~ Begin practice range
	/**
	 * Builds the kit a practice session runs on, when this GameMode is configured to grant one.
	 *
	 * False unless bGrantPracticeBuild is set, so a normal match still takes its kit from the player's save
	 * profile, and the caller needs no branch of its own. The kit is one race, the first two catalog weapons
	 * and up to two resolvable powers out of PracticePowerIds - exactly the shape
	 * ACosmerePlayerState::ValidateBuildAgainstCatalog insists on, so it is a legal build rather than a
	 * special case the match path has to know about.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Practice")
	bool BuildPracticeKit(FSpiritwebCharacterSave& OutBuild) const;
	//~ End practice range
	

protected:
	//~ Begin AGameModeBase interface
	/** Set by the front-end level's GameMode Blueprint. See IsFrontEndSession. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cosmere|Front End")
	bool bFrontEndSession = false;

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void BeginPlay() override;
	//~ End AGameModeBase interface

	//~ Begin spectator overflow
	/**
	 * True when this connection should be demoted: the player slots are full, a spectator slot is
	 * free, and the connection is not already asking to spectate.
	 */
	bool ShouldDemoteToSpectator(const FString& Options);

	/** Rebuilds Options with exactly one SpectatorOnly=1 pair, leaving every other pair intact. */
	static FString MakeSpectatorOptions(const FString& Options);
	//~ End spectator overflow

	/** This match's game state, or null before the game state exists. */
	ACosmereGameState* GetCosmereGameState() const;

	/** The host's choices off the GameInstance, or the struct's own defaults when there are none. */
	FCosmereMatchSettings ResolveMatchSettings() const;

	/** Practice keeps its targets live without score-limit or time-limit match conclusions. */
	virtual bool ShouldRunMatchRules() const { return true; }

	/** Brings the match live: spawns the bots, spawns the hill, starts the clock. Idempotent. */
	void BeginMatch();

	/** Locks a result in and switches the state to PostMatch. Idempotent, so a double call is harmless. */
	void ConcludeMatch(ECosmereTeam WinningTeam, const FString& WinningPlayerName);

	/** Free-for-All: a player at the score limit. King of the Hill: a side at the score limit. */
	void CheckWinConditions();

	/** Whoever leads when the clock runs out. A tie reports no winner rather than picking one. */
	void CheckTimeExpiry();

	/** The side a joining player should land on: the smaller of the two. */
	virtual ECosmereTeam PickBalancedTeam() const;

	/** Creates the bot field for the current mode. */
	virtual void SpawnBots();

	/** Creates one bot, gives it its identity and gives it a pawn. */
	void SpawnBot(ECosmereTeam Team, int32 Index);

	/**
	 * Builds a catalog-valid kit for a bot: one race, exactly two weapons and exactly two powers.
	 *
	 * The two-and-two shape is not decorative - ACosmerePlayerState::ValidateBuildAgainstCatalog
	 * refuses anything else, and the bot path deliberately runs through that same validation so a bot
	 * can never be spawned with a kit a human client would have been refused. Returns false when the
	 * catalog cannot supply one.
	 */
	bool BuildBotKit(FSpiritwebCharacterSave& OutBuild) const;

	/** The name a bot is scored under, e.g. Bot A03. */
	FString MakeBotName(ECosmereTeam Team, int32 Index) const;

	/**
	 * Spawns a bot's pawn and possesses it directly, bypassing RestartPlayer.
	 *
	 * AGameModeBase::RestartPlayer finishes in AController::ClientSetRotation, which is a Client RPC.
	 * A bot has no owning connection, so that call has nowhere to travel; this does the same spawn and
	 * the same possess without it.
	 */
	virtual APawn* SpawnAndPossessPawnFor(AController* Controller);

	/**
	 * A point on the spawn ring, offset from an authored PlayerStart when the level has one.
	 *
	 * Bots are laid out on rings rather than at a single point so twelve of them do not spawn inside
	 * each other's capsules, which would both fail the spawn and stack them into one actor's worth of
	 * collision.
	 */
	FTransform ResolveBotSpawnTransform(int32 Index);

	/** Creates the King of the Hill objective, unless the level already authors one. */
	void SpawnHillObjective();

	//~ Begin tuning
	/** Seconds between the level coming up and the clock starting, so travelling clients arrive first. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Match", meta = (ClampMin = "0.0"))
	float WarmupSeconds = 5.f;

	/**
	 * Which controller class a bot is spawned as. Defaults to the native ACosmereBotController in the
	 * constructor; clearing it leaves bots enabled on paper but unable to be created, which is logged.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Match|Bots")
	TSubclassOf<ACosmereBotController> BotControllerClass;

	/** Bots per side in King of the Hill. Six each is the 6v6 the mode is specified as. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Match|Bots", meta = (ClampMin = "0"))
	int32 BotsPerTeam = 6;

	/** Bots in Free-for-All Deathmatch. Every one of them is their own side, so this is the whole field. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Match|Bots", meta = (ClampMin = "0"))
	int32 FreeForAllBotCount = 8;

	/** Bots placed per ring of the spawn spiral. See ResolveBotSpawnTransform. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Match|Bots", meta = (ClampMin = "1"))
	int32 BotsPerSpawnRing = 8;

	/** Radius of the first spawn ring, in cm. Each further ring adds this much again. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Match|Bots", meta = (ClampMin = "10.0"))
	float BotSpawnRingRadius = 400.f;

	/**
	 * Which hill actor King of the Hill spawns. Defaults to the native ACosmereHillObjective in the
	 * constructor; clearing it leaves the mode running with whatever hill the level authors, or none.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Match|Hill")
	TSubclassOf<ACosmereHillObjective> HillObjectiveClass;

	/** Where the hill is spawned, in world space. Ignored when the level already authors one. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Match|Hill")
	FVector HillLocation = FVector(0.f, 0.f, 200.f);
	
	//~ Begin practice range
	/**
	 * Set by a practice level's GameMode Blueprint. When true, a human reaching the build path is handed the
	 * practice kit instead of their profile's selected character, so the demo needs no saved profile - which
	 * is the point, because a fresh machine has none and would otherwise spawn with no powers at all.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Practice")
	bool bGrantPracticeBuild = false;

	/**
	 * Set by a practice level's GameMode Blueprint. When true every bot is held idle from the moment it is
	 * spawned - no target search, no movement, no firing - and stays idle across a respawn, so the range is
	 * a set of targets to shoot at rather than a firefight to survive.
	 *
	 * Off by default, so every match mode keeps the opponents it was specified with.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Practice")
	bool bHoldBotsIdle = false;
	
	/**
	 * Radius of the circle a held-idle bot walks, in cm. 0 leaves practice targets standing still.
	 *
	 * Small on purpose: spawn slots are laid out on rings of BotSpawnRingRadius with BotsPerSpawnRing to a
	 * ring, so at the defaults neighbours stand about 314 cm apart and a circle much wider than that would have
	 * them walking into each other. This is not a fighting behaviour - it exists so that anything which has to
	 * be walked over has something walking over it.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Practice", meta = (ClampMin = "0.0"))
	float IdleBotCircleRadius = 120.f;
	
	/**
	 * The powers a practice kit is granted, in slot order: index 0 is EPowerSlot::Alpha, index 1 is Beta.
	 *
	 * Read in order rather than at random, so slot 0 is the same power in every session. An id with no row,
	 * or a row with no AbilityClass, is skipped with a log line - a grant path that skipped it silently is
	 * how a power goes missing with nothing on the log to explain it.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Practice")
	TArray<FName> PracticePowerIds;
	//~ End practice range
	//~ End tuning

	/** Resolved once in InitGame and published into the game state in InitGameState. */
	UPROPERTY(Transient)
	FCosmereMatchSettings MatchSettings;

	/** Every bot this match created, held so EndMatch can stop them and so the pawns are not collected. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ACosmereBotController>> BotControllers;

	UPROPERTY(Transient)
	TObjectPtr<ACosmereHillObjective> HillObjective;

	/** Guards BeginMatch against running twice. */
	bool bMatchStarted = false;

	/** Set by ConcludeMatch. Nothing scores, respawns or concludes a second time after it. */
	bool bMatchEnded = false;

	FTimerHandle WarmupTimerHandle;
};
