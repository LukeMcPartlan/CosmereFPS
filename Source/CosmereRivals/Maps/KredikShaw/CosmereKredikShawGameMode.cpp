#include "Maps/KredikShaw/CosmereKredikShawGameMode.h"

#include "Maps/KredikShaw/CosmereKredikShawPlayerStart.h"
#include "Components/CapsuleComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Net/CosmereBotController.h"
#include "Net/CosmerePlayerState.h"

DEFINE_LOG_CATEGORY_STATIC(LogKredikShaw, Log, All);

ACosmereKredikShawGameMode::ACosmereKredikShawGameMode()
{
	bFrontEndSession = false;
}

void ACosmereKredikShawGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	// InitGameState runs later; keep every other resolved host setting unchanged.
	if (bOverrideMatchMode)
	{
		MatchSettings.Mode = MapMatchMode;
	}
	UE_LOG(LogKredikShaw, Log, TEXT("Map policy: mode=%d datum=%.1f side-cap=6 (humans+bots), FFA-cap=12. No session admission override."),
		static_cast<int32>(GetMatchMode()), GroundDatumZCM);
}

bool ACosmereKredikShawGameMode::UpdatePlayerStartSpot(AController* Player, const FString& Portal, FString& OutErrorMessage)
{
	// InitNewPlayer calls this BEFORE it parses SpectatorOnly, and BEFORE PostLogin assigns teams.
	// A full/blocked catalog must not turn successful session admission into a Login error.
	// No pawn is spawned here; RestartPlayer selects after identity is resolved.
	OutErrorMessage.Reset();
	if (Player)
	{
		Player->StartSpot = nullptr;
		return true;
	}
	OutErrorMessage = TEXT("Missing controller");
	return false;
}

void ACosmereKredikShawGameMode::BeginPlay()
{
	AuditSpawnCatalog();
	Super::BeginPlay();
}

int32 ACosmereKredikShawGameMode::CountParticipants(ECosmereTeam Team, bool bHumansOnly) const
{
	int32 Count = 0;
	for (TActorIterator<AController> It(GetWorld()); It; ++It)
	{
		if (!IsValid(*It) || It->IsActorBeingDestroyed())
		{
			continue;
		}
		const ACosmerePlayerState* PS = It->GetPlayerState<ACosmerePlayerState>();
		if (PS && !PS->IsSpectator() && !PS->IsOnlyASpectator() && PS->GetMatchTeam() == Team
			&& (!bHumansOnly || !PS->IsBotPlayer()))
		{
			++Count;
		}
	}
	return Count;
}

ECosmereTeam ACosmereKredikShawGameMode::PickBalancedTeam() const
{
	if (GetMatchMode() == ECosmereMatchMode::FreeForAll)
	{
		return ECosmereTeam::None;
	}
	// Bots yield their seat. Counting bots here would put late humans on an already full side.
	return CountParticipants(ECosmereTeam::TeamA, true) <= CountParticipants(ECosmereTeam::TeamB, true)
		? ECosmereTeam::TeamA : ECosmereTeam::TeamB;
}

bool ACosmereKredikShawGameMode::RetireOneBot(ECosmereTeam Team)
{
	for (int32 Index = BotControllers.Num() - 1; Index >= 0; --Index)
	{
		ACosmereBotController* Bot = BotControllers[Index];
		const ACosmerePlayerState* PS = IsValid(Bot) ? Bot->GetPlayerState<ACosmerePlayerState>() : nullptr;
		if (!PS || PS->GetMatchTeam() != Team)
		{
			continue;
		}
		UE_LOG(LogKredikShaw, Log, TEXT("Retiring fill bot %s from side=%d before participant admission."),
			*Bot->GetName(), static_cast<int32>(Team));
		BotControllers.RemoveAt(Index);
		Bot->SetIdle(true);
		if (APawn* Pawn = Bot->GetPawn())
		{
			Bot->UnPossess();
			Pawn->Destroy();
		}
		// AController::Destroyed cleans PlayerState and removes it from GameState.
		Bot->Destroy();
		return true;
	}
	return false;
}

void ACosmereKredikShawGameMode::PostLogin(APlayerController* NewPlayer)
{
	const ACosmerePlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<ACosmerePlayerState>() : nullptr;
	if (PS && !PS->IsSpectator() && !PS->IsOnlyASpectator())
	{
		const ECosmereTeam Team = PickBalancedTeam();
		const int32 Cap = GetMatchMode() == ECosmereMatchMode::FreeForAll ? FFAParticipants : ParticipantsPerSide;
		// A new FFA human is already counted as None. Team humans are assigned by Super below.
		const int32 Pending = GetMatchMode() == ECosmereMatchMode::FreeForAll ? 0 : 1;
		while (CountParticipants(Team, false) + Pending > Cap && RetireOneBot(Team)) {}
	}
	// Keeps overflow spectators unassigned and uses the virtual, human-balanced team policy above.
	Super::PostLogin(NewPlayer);
	if (bMatchStarted && !bMatchEnded && !bFrontEndSession)
	{
		ReconcilePopulation();
	}
}

void ACosmereKredikShawGameMode::SpawnBots()
{
	ReconcilePopulation();
}

void ACosmereKredikShawGameMode::ReconcilePopulation()
{
	if (!HasAuthority() || bFrontEndSession || bMatchEnded)
	{
		return;
	}
	BotControllers.RemoveAll([](const TObjectPtr<ACosmereBotController>& Bot) { return !IsValid(Bot.Get()); });
	const bool bFFA = GetMatchMode() == ECosmereMatchMode::FreeForAll;
	const ECosmereTeam Teams[] = { ECosmereTeam::TeamA, ECosmereTeam::TeamB };
	for (int32 Side = 0; Side < (bFFA ? 1 : 2); ++Side)
	{
		const ECosmereTeam Team = bFFA ? ECosmereTeam::None : Teams[Side];
		const int32 Cap = bFFA ? FFAParticipants : ParticipantsPerSide;
		while (CountParticipants(Team, false) > Cap && RetireOneBot(Team)) {}
		if (!MatchSettings.bBotsEnabled || !BotControllerClass)
		{
			continue;
		}
		// Bounded even if controller creation fails; never six bots in addition to humans.
		const int32 Missing = FMath::Max(0, Cap - CountParticipants(Team, false));
		for (int32 Index = 0; Index < Missing; ++Index)
		{
			SpawnBot(Team, NextBotIndex++);
		}
		UE_LOG(LogKredikShaw, Verbose, TEXT("Population side=%d human=%d total=%d target=%d"),
			static_cast<int32>(Team), CountParticipants(Team, true), CountParticipants(Team, false), Cap);
	}
}

void ACosmereKredikShawGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !bMatchStarted || bMatchEnded || bFrontEndSession)
	{
		return;
	}
	MaintenanceElapsed += DeltaSeconds;
	if (MaintenanceElapsed < 1.f)
	{
		return;
	}
	MaintenanceElapsed = 0.f;
	ReconcilePopulation();
	// A blocked start fails closed, then is retried without using a first-start ring or stale StartSpot.
	for (TActorIterator<AController> It(GetWorld()); It; ++It)
	{
		const ACosmerePlayerState* PS = It->GetPlayerState<ACosmerePlayerState>();
		if (IsValid(*It) && !It->IsActorBeingDestroyed() && !IsValid(It->GetPawn())
			&& PS && !PS->IsSpectator() && !PS->IsOnlyASpectator())
		{
			if (It->IsA<ACosmereBotController>())
			{
				SpawnAndPossessPawnFor(*It);
			}
			else if (It->IsA<APlayerController>())
			{
				RestartPlayer(*It);
			}
		}
	}
}

bool ACosmereKredikShawGameMode::IsSafeStart(APlayerStart* Start, AController* Controller,
	const FKredikShawStartDescriptor& Descriptor) const
{
	UClass* PawnClass = const_cast<ACosmereKredikShawGameMode*>(this)->GetDefaultPawnClassForController(Controller);
	const ACharacter* PawnCDO = PawnClass ? Cast<ACharacter>(PawnClass->GetDefaultObject()) : nullptr;
	if (!PawnCDO || !PawnCDO->GetCapsuleComponent() || !FMath::IsFinite(GroundDatumZCM))
	{
		return false;
	}
	float Radius = 0.f, HalfHeight = 0.f;
	const UCapsuleComponent* Capsule = PawnCDO->GetCapsuleComponent();
	Capsule->GetScaledCapsuleSize(Radius, HalfHeight);
	if (Radius <= 0.f || HalfHeight < Radius)
	{
		return false;
	}
	const FVector Center = Start->GetActorLocation();
	if (Center.ContainsNaN())
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KredikShawSpawn), false, Start);
	if (Controller->GetPawn())
	{
		Params.AddIgnoredActor(Controller->GetPawn());
	}
	if (GetWorld()->OverlapBlockingTestByProfile(Center, FQuat::Identity, Capsule->GetCollisionProfileName(),
		FCollisionShape::MakeCapsule(Radius, HalfHeight), Params))
	{
		return false;
	}
	// Check actual movement/shot collision, never the separate NAV representation.
	const FVector ProbeOffsets[] = { FVector::ZeroVector, FVector(Radius * .8f, 0, 0),
		FVector(-Radius * .8f, 0, 0), FVector(0, Radius * .8f, 0), FVector(0, -Radius * .8f, 0) };
	for (const FVector& Offset : ProbeOffsets)
	{
		FHitResult Floor;
		if (!GetWorld()->LineTraceSingleByChannel(Floor, Center + Offset,
			Center + Offset - FVector(0, 0, HalfHeight + 20.f), ECC_Pawn, Params)
			|| Floor.ImpactNormal.Z < PawnCDO->GetCharacterMovement()->GetWalkableFloorZ()
			|| FMath::Abs((Center.Z - HalfHeight) - Floor.ImpactPoint.Z) > 10.f
			|| Floor.ImpactPoint.Z - GroundDatumZCM > 3200.1f
			|| FMath::Abs(Floor.ImpactPoint.Z - GroundDatumZCM - Descriptor.TierCM) > 50.f)
		{
			return false;
		}
	}
	return true;
}

AActor* ACosmereKredikShawGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (!Player || !GetWorld())
	{
		return nullptr;
	}
	const ACosmerePlayerState* PS = Player->GetPlayerState<ACosmerePlayerState>();
	if (PS && (PS->IsSpectator() || PS->IsOnlyASpectator()))
	{
		return nullptr;
	}
	const bool bFFA = GetMatchMode() == ECosmereMatchMode::FreeForAll;
	const ECosmereTeam Team = bFFA ? ECosmereTeam::None
		: PS && PS->GetMatchTeam() != ECosmereTeam::None ? PS->GetMatchTeam() : PickBalancedTeam();
	APlayerStart* Best = nullptr;
	FKredikShawStartDescriptor BestDescriptor;
	double BestSafety = -1.;
	uint64 BestTierUse = MAX_uint64;
	uint64 BestStartUse = MAX_uint64;
	TMap<FName, int32> IdCounts;
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		FKredikShawStartDescriptor D;
		if (ACosmereKredikShawPlayerStart::Describe(*It, D))
		{
			++IdCounts.FindOrAdd(D.StableId);
		}
	}
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		FKredikShawStartDescriptor D;
		if (!ACosmereKredikShawPlayerStart::Describe(*It, D) || IdCounts.FindRef(D.StableId) != 1
			|| D.bFreeForAll != bFFA || D.Team != Team || !IsSafeStart(*It, Player, D))
		{
			continue;
		}
		double Safety = 1.e12;
		for (TActorIterator<APawn> PawnIt(GetWorld()); PawnIt; ++PawnIt)
		{
			const ACosmerePlayerState* Other = PawnIt->GetPlayerState<ACosmerePlayerState>();
			if (*PawnIt == Player->GetPawn() || !Other || Other->IsSpectator()
				|| (!bFFA && Other->GetMatchTeam() == Team))
			{
				continue;
			}
			Safety = FMath::Min(Safety, FVector::DistSquared(It->GetActorLocation(), PawnIt->GetActorLocation()));
		}
		if (Safety < FMath::Square(FMath::Max(0.f, MinimumOpponentDistanceCM)))
		{
			continue;
		}
		const uint64 TierUse = bFFA ? LastTierUse.FindRef(D.TierCM) : 0;
		const uint64 StartUse = LastStartUse.FindRef(D.StableId);
		const bool bBetter = !Best || TierUse < BestTierUse
			|| (TierUse == BestTierUse && (Safety > BestSafety
				|| (Safety == BestSafety && (StartUse < BestStartUse
					|| (StartUse == BestStartUse && D.StableId.ToString() < BestDescriptor.StableId.ToString())))));
		if (bBetter)
		{
			Best = *It;
			BestDescriptor = D;
			BestSafety = Safety;
			BestTierUse = TierUse;
			BestStartUse = StartUse;
		}
	}
	if (Best)
	{
		LastStartUse.Add(BestDescriptor.StableId, ++SelectionSerial);
		LastTierUse.Add(BestDescriptor.TierCM, SelectionSerial);
		UE_LOG(LogKredikShaw, Log, TEXT("Start controller=%s mode=%d team=%d id=%s tier=%d center=%s"),
			*Player->GetName(), static_cast<int32>(GetMatchMode()), static_cast<int32>(Team),
			*BestDescriptor.StableId.ToString(), BestDescriptor.TierCM, *Best->GetActorLocation().ToCompactString());
	}
	else
	{
		UE_LOG(LogKredikShaw, Warning, TEXT("No safe unique authored start: controller=%s mode=%d team=%d; spawn refused (no ring/origin fallback)."),
			*Player->GetName(), static_cast<int32>(GetMatchMode()), static_cast<int32>(Team));
	}
	return Best;
}

AActor* ACosmereKredikShawGameMode::FindPlayerStart_Implementation(AController* Player, const FString& IncomingName)
{
	// Portal and cached StartSpot cannot bypass mode/team/occupancy checks.
	return ChoosePlayerStart(Player);
}

void ACosmereKredikShawGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!IsValid(NewPlayer) || NewPlayer->IsActorBeingDestroyed())
	{
		return;
	}
	if (AActor* Start = FindPlayerStart(NewPlayer))
	{
		RestartPlayerAtPlayerStart(NewPlayer, Start);
	}
	// Do not call Super::RestartPlayer: it falls back to a potentially occupied old StartSpot.
}

APawn* ACosmereKredikShawGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer);
	if (!PawnClass || !NewPlayer || SpawnTransform.ContainsNaN())
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.Owner = NewPlayer;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
	return GetWorld()->SpawnActor<APawn>(PawnClass,
		FTransform(FRotator(0.f, SpawnTransform.Rotator().Yaw, 0.f), SpawnTransform.GetLocation()), Params);
}

APawn* ACosmereKredikShawGameMode::SpawnAndPossessPawnFor(AController* Controller)
{
	if (!IsValid(Controller) || IsValid(Controller->GetPawn()))
	{
		return Controller ? Controller->GetPawn() : nullptr;
	}
	AActor* Start = ChoosePlayerStart(Controller);
	APawn* Pawn = Start ? SpawnDefaultPawnAtTransform(Controller, Start->GetActorTransform()) : nullptr;
	if (Pawn)
	{
		Controller->Possess(Pawn);
		Controller->SetControlRotation(FRotator(0.f, Start->GetActorRotation().Yaw, 0.f));
	}
	return Pawn;
}

void ACosmereKredikShawGameMode::AuditSpawnCatalog()
{
	TSet<FName> Ids;
	TSet<int32> FFATiers;
	int32 A = 0, B = 0, FFA = 0, Duplicates = 0;
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		FKredikShawStartDescriptor D;
		if (!ACosmereKredikShawPlayerStart::Describe(*It, D))
		{
			continue;
		}
		if (Ids.Contains(D.StableId))
		{
			++Duplicates;
			UE_LOG(LogKredikShaw, Error, TEXT("Duplicate spawn ID %s; duplicates are excluded from selection."), *D.StableId.ToString());
		}
		Ids.Add(D.StableId);
		if (D.bFreeForAll)
		{
			++FFA;
			FFATiers.Add(D.TierCM);
		}
		else if (D.Team == ECosmereTeam::TeamA) { ++A; }
		else if (D.Team == ECosmereTeam::TeamB) { ++B; }
	}
	UE_LOG(LogKredikShaw, Log, TEXT("Authored catalog A=%d B=%d FFA=%d FFA-tiers=%d duplicate-IDs=%d. Counts are not clearance acceptance."),
		A, B, FFA, FFATiers.Num(), Duplicates);
	if (A < 6 || B < 6 || FFA < 16 || FFATiers.Num() < 4 || Duplicates > 0)
	{
		UE_LOG(LogKredikShaw, Error, TEXT("Incomplete map start catalog: require 6 per team and 16 FFA candidates across at least four bands, unique IDs."));
	}
}
