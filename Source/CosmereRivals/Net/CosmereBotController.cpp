// Copyright Epic Games, Inc. All Rights Reserved.

#include "Net/CosmereBotController.h"

#include "Character/CosmereCharacter.h"
#include "Character/CosmereRealmComponent.h"
#include "CollisionQueryParams.h"
#include "Combat/CosmereWeaponComponent.h"
#include "CosmereRivals.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Net/CosmerePlayerState.h"
#include "TimerManager.h"

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

ACosmereBotController::ACosmereBotController()
{
	// The whole of this bot's behaviour runs from the tick - movement input, control rotation - so it
	// has to be enabled explicitly rather than relying on a base-class default that assumes a
	// PlayerController.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void ACosmereBotController::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// The PlayerState has to exist before the first possess. ACosmereCharacter::PossessedBy reads it to
	// point the ability system's actor info at the pawn and to pull the accepted kit down onto it, and
	// a controller that never had one would leave the bot as an un-scored, unarmed default pawn.
	//
	// This mirrors APlayerController::PostInitializeComponents, which is the engine's own version of
	// the same requirement.
	if (IsValid(this) && GetNetMode() != NM_Client && GetPlayerState<APlayerState>() == nullptr)
	{
		InitPlayerState();
	}
}

void ACosmereBotController::ConfigureBot(const FSpiritwebCharacterSave& InBuild, ECosmereTeam InTeam, const FString& InBotName)
{
	PendingBuild = InBuild;
	PendingTeam = InTeam;
	PendingName = InBotName;

	bHalted = false;
	CurrentTarget = nullptr;

	ApplyIdentityToPlayerState();

	UE_LOG(LogCosmere, Log,
		TEXT("CosmereBotController: %s configured on %s with %d weapon(s) and %d power(s)."),
		*GetName(), *TeamToString(InTeam), InBuild.WeaponIds.Num(), InBuild.PowerIds.Num());
}

void ACosmereBotController::ApplyIdentityToPlayerState()
{
	if (GetPlayerState<APlayerState>() == nullptr)
	{
		// Straggler case: a possess that somehow beat PostInitializeComponents.
		InitPlayerState();
	}

	ACosmerePlayerState* PS = GetPlayerState<ACosmerePlayerState>();
	if (!PS)
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereBotController: %s has no ACosmerePlayerState, so it cannot be scored or equipped."),
			*GetName());
		return;
	}

	PS->SetBotPlayer(true);
	PS->SetMatchTeam(PendingTeam);

	if (!PendingName.IsEmpty())
	{
		PS->SetPlayerName(PendingName);
	}

	// Deliberately routed through the same catalog validation a human client's submission goes through,
	// so a generated bot kit can never be something the server would have refused from a player. The
	// two-and-two shape is what ValidateBuildAgainstCatalog insists on.
	if (PendingBuild.WeaponIds.Num() == 2 && PendingBuild.PowerIds.Num() == 2)
	{
		PS->SetBuildForBot(PendingBuild);
	}
	else
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereBotController: %s was given no usable kit (%d weapon(s), %d power(s)), so it will fight on default stats."),
			*GetName(), PendingBuild.WeaponIds.Num(), PendingBuild.PowerIds.Num());
	}
}

void ACosmereBotController::OnPossess(APawn* InPawn)
{
	// Super first: the default implementation is what actually records the pawn, and everything below
	// reads it back through GetPawn().
	Super::OnPossess(InPawn);

	WeaponComponent = InPawn ? InPawn->FindComponentByClass<UCosmereWeaponComponent>() : nullptr;
	CurrentTarget = nullptr;
	bHalted = false;
	bPatrolReady = false;

	// OnPossess runs before APawn::PossessedBy, which is the callback that reads the PlayerState - so
	// the identity has to be in place here, not in the pawn's own hook. Re-applying is idempotent,
	// which keeps the ordering irrelevant for a respawn or a late-created PlayerState.
	ApplyIdentityToPlayerState();

	GetWorldTimerManager().SetTimer(
		RetargetTimerHandle, this, &ACosmereBotController::RefreshTarget, RetargetInterval, true);

	RefreshTarget();
}

void ACosmereBotController::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(RetargetTimerHandle);
	CurrentTarget = nullptr;
	WeaponComponent = nullptr;

	Super::OnUnPossess();
}

void ACosmereBotController::StopBot()
{
	bHalted = true;
	CurrentTarget = nullptr;

	GetWorldTimerManager().ClearTimer(RetargetTimerHandle);

	// Movement input is not sticky, but the movement component is already carrying whatever the last
	// tick fed it, so it is stopped explicitly rather than left to coast.
	if (APawn* MyPawn = GetPawn())
	{
		if (UCharacterMovementComponent* Move = MyPawn->FindComponentByClass<UCharacterMovementComponent>())
		{
			Move->StopMovementImmediately();
		}
	}

	UE_LOG(LogCosmere, Log, TEXT("CosmereBotController: %s stopped."), *GetName());
}

void ACosmereBotController::SetIdle(bool bInIdle)
{
	bIdle = bInIdle;

	if (bIdle)
	{
		// Idle is StopBot plus the flag that outlives it: this clears the retarget timer and stops the
		// movement the last tick handed the component, and bIdle is what keeps the tick from resuming
		// when a respawn re-possesses this controller.
		StopBot();
	}
	else
	{
		// Only the halt latch is cleared. The retarget timer is not restarted here - the next possess
		// installs it, which is the same order a fresh controller goes through.
		bHalted = false;
	}

	UE_LOG(LogCosmere, Log, TEXT("CosmereBotController: %s is now %s."),
		*GetName(), bIdle ? TEXT("idle") : TEXT("active"));
}

void ACosmereBotController::SetCirclePatrol(float InRadiusCm)
{
	PatrolRadius = FMath::Max(InRadiusCm, 0.f);

	// Re-struck from whichever pawn is standing when the next frame asks, so setting this after a possess and
	// setting it before one both end up circling the pawn that exists.
	bPatrolReady = false;

	UE_LOG(LogCosmere, Log, TEXT("CosmereBotController: %s circle patrol %s."),
		*GetName(),
		PatrolRadius > 0.f
			? *FString::Printf(TEXT("on at %.0f cm radius"), PatrolRadius)
			: TEXT("off"));
}

void ACosmereBotController::UpdateCirclePatrol()
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return;
	}

	const FVector PawnLocation = MyPawn->GetActorLocation();

	// Struck once, from the pawn that is first asked to walk. The centre is set back along the pawn's own right
	// by the radius, so the pawn starts exactly on the rim and already has a radius to turn about - a centre
	// taken directly under the pawn would leave the first frame with no direction to give it at all.
	if (!bPatrolReady)
	{
		PatrolCenter = PawnLocation - MyPawn->GetActorRightVector() * PatrolRadius;
		bPatrolReady = true;
	}

	// Flattened to the ground plane: practice targets stand on a floor, and a tangent with any vertical in it
	// would walk them into it.
	FVector ToCenter = PatrolCenter - PawnLocation;
	ToCenter.Z = 0.0;
	if (ToCenter.SizeSquared() <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// The tangent rather than a point on the rim. The heading that holds a radius is the radius turned a quarter
	// turn, so walking it traces the circle at whatever speed the pawn's own movement allows: no angular rate
	// here to fall out of step with MaxWalkSpeed, and no rim position for a collision to shove the bot off.
	const FVector Tangent = FVector::CrossProduct(FVector::UpVector, ToCenter.GetSafeNormal());
	MyPawn->AddMovementInput(Tangent, MoveThrottle);

	// Facing follows travel, so the bot walks the circle instead of strafing around it.
	SetControlRotation(FRotator(0.f, Tangent.Rotation().Yaw, 0.f));
}

void ACosmereBotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Everything below is server work. Movement input, control rotation and firing all have to happen
	// on the machine that owns the simulation, and a client's copy of a bot pawn is a simulated proxy.
	if (!HasAuthority())
	{
		return;
	}
	
	// A circling practice target walks in spite of being idle - and idle is implemented as a StopBot, so the
	// halt latch is down as well. The patrol is therefore answered before either latch can swallow it; what
	// those two are left to suppress is every branch below, which is the fighting.
	if (bIdle && PatrolRadius > 0.f)
	{
		UpdateCirclePatrol();
		return;
	}
	
	if (bHalted || bIdle)
	{
		return;
	}

	APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return;
	}

	// A corpse stays possessed until the respawn timer replaces it, and it must not keep acting in the
	// meantime - the pawn hides itself, but its input legs would still be live.
	if (const ACosmereCharacter* MyCharacter = Cast<ACosmereCharacter>(MyPawn))
	{
		if (!MyCharacter->IsAlive())
		{
			return;
		}
	}

	if (!CurrentTarget)
	{
		// Nothing to engage until the retarget timer finds somebody.
		return;
	}

	AimAt(*CurrentTarget, DeltaSeconds);
	AdvanceOn(*CurrentTarget);
	ServiceWeapon();
}

void ACosmereBotController::RefreshTarget()
{
	if (bHalted || bIdle)
	{
		CurrentTarget = nullptr;
		return;
	}

	// Always the nearest live opponent, rather than holding the old target while it stays valid. This
	// is standing in for target selection, and "whatever is closest right now" is the behaviour that
	// reads correctly in a firefight.
	CurrentTarget = FindNearestOpponent();
}

APawn* ACosmereBotController::FindNearestOpponent() const
{
	UWorld* World = GetWorld();
	APawn* MyPawn = GetPawn();
	if (!World || !MyPawn)
	{
		return nullptr;
	}

	const AGameStateBase* GS = World->GetGameState();
	if (!GS)
	{
		return nullptr;
	}

	const float MaxDistSq = FMath::Square(SightRange);
	APawn* Best = nullptr;
	float BestDistSq = MaxDistSq;

	// The roster rather than a spatial query. PlayerArray is where the side and the alive flag both
	// live - a pawn on its own cannot answer either question - and with twelve opponents a linear scan
	// is cheaper than anything that would need an index.
	for (APlayerState* PS : GS->PlayerArray)
	{
		APawn* TheirPawn = PS ? PS->GetPawn() : nullptr;
		if (!TheirPawn || TheirPawn == MyPawn || !IsOpponent(TheirPawn))
		{
			continue;
		}

		// A corpse stays possessed for the whole respawn delay, so it has to be excluded explicitly -
		// otherwise a bot would spend those seconds shooting at a body that is not there.
		const ACosmereCharacter* TheirCharacter = Cast<ACosmereCharacter>(TheirPawn);
		if (TheirCharacter && !TheirCharacter->IsAlive())
		{
			continue;
		}

		// A body in the other realm is not a target and not an opponent either - it cannot be shot, and a bot
		// that walked at one would simply stop at a wall of nothing.
		if (!UCosmereRealmComponent::ShareRealm(MyPawn, TheirPawn))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(MyPawn->GetActorLocation(), TheirPawn->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = TheirPawn;
		}
	}

	return Best;
}

bool ACosmereBotController::IsOpponent(const APawn* Other) const
{
	if (!Other || Other == GetPawn())
	{
		return false;
	}

	const ACosmerePlayerState* TheirPS = Cast<ACosmerePlayerState>(Other->GetPlayerState());
	if (!TheirPS)
	{
		return false;
	}

	const ACosmerePlayerState* MyPS = GetPlayerState<ACosmerePlayerState>();

	// Both sides known: the answer is simply whether they differ.
	if (MyPS
		&& MyPS->GetMatchTeam() != ECosmereTeam::None
		&& TheirPS->GetMatchTeam() != ECosmereTeam::None)
	{
		return MyPS->GetMatchTeam() != TheirPS->GetMatchTeam();
	}

	// Free-for-All, or a side nobody has assigned yet: everybody but this bot is fair game.
	return TheirPS != MyPS;
}

bool ACosmereBotController::HasLineOfSightTo(const APawn* Target) const
{
	UWorld* World = GetWorld();
	APawn* MyPawn = GetPawn();
	if (!World || !MyPawn || !Target)
	{
		return false;
	}

	FVector EyeLocation;
	FRotator EyeRotation;
	MyPawn->GetActorEyesViewPoint(EyeLocation, EyeRotation);

	FCollisionQueryParams Params;
	Params.bTraceComplex = true;
	Params.AddIgnoredActor(MyPawn);

	FHitResult Hit;
	const bool bBlocked = World->LineTraceSingleByChannel(
		Hit, EyeLocation, Target->GetActorLocation(), ECC_Visibility, Params);

	// Clear when nothing was hit at all, or when the first thing hit is the target itself.
	return !bBlocked || Hit.GetActor() == Target;
}

void ACosmereBotController::AimAt(const APawn& Target, float DeltaSeconds)
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return;
	}

	// The same view point the hitscan traces from, so the bot aims along the line its own shot will
	// actually take rather than along the mesh's forward vector.
	FVector EyeLocation;
	FRotator EyeRotation;
	MyPawn->GetActorEyesViewPoint(EyeLocation, EyeRotation);

	FVector AimDirection = (Target.GetActorLocation() - EyeLocation).GetSafeNormal();
	if (AimDirection.IsNearlyZero())
	{
		AimDirection = MyPawn->GetActorForwardVector();
	}

	// The error goes on the direction rather than on the finished rotation, so the cone stays round.
	if (AimErrorDegrees > 0.f)
	{
		AimDirection = FMath::VRandCone(AimDirection, FMath::DegreesToRadians(AimErrorDegrees));
	}

	// Interpolated rather than snapped. Without it, every target switch is an instant 180 and the bot
	// reads as a turret rather than a player.
	const FRotator Desired = AimDirection.Rotation();
	SetControlRotation(FMath::RInterpTo(GetControlRotation(), Desired, DeltaSeconds, AimTurnRate));
}

void ACosmereBotController::AdvanceOn(const APawn& Target)
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return;
	}

	const FVector ToTarget = Target.GetActorLocation() - MyPawn->GetActorLocation();
	if (ToTarget.SizeSquared() <= FMath::Square(PreferredRange))
	{
		return;
	}

	// Horizontal only. The vertical component is the movement component's job - feeding it in would
	// fight gravity and stall the character on the spot.
	const FVector Direction = FVector(ToTarget.X, ToTarget.Y, 0.f).GetSafeNormal();
	if (!Direction.IsNearlyZero())
	{
		MyPawn->AddMovementInput(Direction, MoveThrottle);
	}
}

void ACosmereBotController::ServiceWeapon()
{
	if (!WeaponComponent)
	{
		return;
	}

	// A dry magazine is worth a reload ahead of anything else. CanFire is false for an empty slot and
	// for a reload alike, so the two have to be told apart here rather than by CanFire alone - without
	// this the bot would simply stand there holding an empty gun.
	if (WeaponComponent->HasActiveWeapon() && WeaponComponent->GetActiveAmmo() <= 0)
	{
		if (!WeaponComponent->IsReloading())
		{
			WeaponComponent->RequestReload();
		}
		return;
	}

	// A reload owns the weapon until it finishes, and the row's FireInterval paces a burst, so CanFire
	// is the whole of the readiness test for the shot itself.
	if (!WeaponComponent->CanFire())
	{
		return;
	}

	// No firing through geometry. The hitscan would be stopped by the wall anyway, but a bot emptying
	// magazines into cover is both wasteful and plainly wrong to watch.
	if (!HasLineOfSightTo(CurrentTarget))
	{
		return;
	}

	WeaponComponent->RequestFire();
}
