// Copyright Epic Games, Inc. All Rights Reserved.

#include "Character/CosmereCharacter.h"
#include "Character/CosmereAimModeTest.h"
#include "Character/CosmereGravityComponent.h"
#include "Character/CosmereInvestitureComponent.h"
#include "Character/CosmereRealmComponent.h"
#include "Character/CosmereMovementModifierComponent.h"
#include "Character/CosmereTinSensesComponent.h"
#include "Net/CosmereGameState.h"
#include "Sound/SoundBase.h"

#include "Ability/CosmereAbilitySystemComponent.h"
#include "Ability/CosmereAttributeSet.h"
#include "Combat/CosmereWeaponComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/LineBatchComponent.h"
#include "CosmereGameInstance.h"
#include "CosmereRivals.h"
#include "Data/CosmereCatalogSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "InputMappingContext.h"
#include "Net/CosmereGameMode.h"
#include "Net/CosmerePlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Power/CosmereBurnAbility.h"
#include "Power/CosmerePowerAbility.h"
#include "Power/CosmereSurgeResourceComponent.h"
#include "TimerManager.h"
#include "UI/CosmereUIManager.h"

ACosmereCharacter::ACosmereCharacter()
{
	// Ticked for one reason only: the mass-driven fall ceiling is clamped in Tick, which is the only place it
	// can live once the movement component stopped offering a fall-speed property. Tick early-outs while
	// grounded, so the cost is one branch a frame on a pawn that is not falling.
	PrimaryActorTick.bCanEverTick = true;

	// Movement defaults are deliberately left at the engine values. The race row is the single
	// source of truth for them and is applied in ApplyBuildToPawn, so baking a second copy here
	// would just be a second thing to keep in sync.
	//
	// The modifier stack is the only thing that writes those values once they are applied, so every speed
	// source on the pawn - the build itself, a metalmind, a slow, a bubble - composes instead of overwriting.
	MovementModifiers = CreateDefaultSubobject<UCosmereMovementModifierComponent>(TEXT("MovementModifiers"));

	// Gravity is its own component rather than part of the stack: it owns a direction, not a number, and it has
	// to keep re-examining that direction long after the ability that set it has ended - landing on a surface is
	// what refines a lashing. It reaches GravityScale only through the stack, like every other source.
	GravityComponent = CreateDefaultSubobject<UCosmereGravityComponent>(TEXT("GravityComponent"));
	
	// The gravity-axis reference line. A line batcher rather than a mesh so it needs no material and can be
	// drawn through the surface it runs into, which is exactly the moment the answer is worth having: a lashing
	// is about to make that surface the floor. Nothing is ever added to it on a machine that is not looking
	// through this pawn's eyes, so it is owner-only without needing a visibility flag.
	GravityReferenceLines = CreateDefaultSubobject<ULineBatchComponent>(TEXT("GravityReferenceLines"));
	GravityReferenceLines->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GravityReferenceLines->SetGenerateOverlapEvents(false);

	// Tin lives on the pawn for the same reason gravity does: it is a state of this body rather than a number
	// that composes with other sources. Authored here so every pawn carries one - a build that never puts tin in
	// a slot simply never pushes a level into it, and nothing below runs.
	TinSenses = CreateDefaultSubobject<UCosmereTinSensesComponent>(TEXT("TinSenses"));

	// Resistances and the copper cloud are facts about this body, not about whoever is working on it, so they
	// live beside tin for the same reason. Every pawn gets an empty one, which is what lets a copper burn write
	// into a body it has never met without checking whether that body bothered to opt in.
	Investiture = CreateDefaultSubobject<UCosmereInvestitureComponent>(TEXT("Investiture"));

	// Realm lives here for the same reason tin and investiture do: it is a fact about this body. Every pawn gets
	// one answering Material, so an unphased match costs one replicated byte and nothing else, and a pawn that
	// phases has somewhere to keep the answer that every other machine reads.
	RealmComponent = CreateDefaultSubobject<UCosmereRealmComponent>(TEXT("Realm"));
}

void ACosmereCharacter::UpdateFootsteps(float DeltaSeconds)
{
	// Only the machine that simulates this pawn reports it. A remote pawn's motion arrives as a replication
	// result, so every client would otherwise count the same step and the whole match would hear four of it.
	if (!IsLocallyControlled() || !IsAlive())
	{
		FootstepAccumulator = 0.f;
		return;
	}

	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const float Speed = GetVelocity().Size();
	const bool bWalking = Movement && Movement->IsMovingOnGround() && Speed > FootstepMinSpeed;
	if (!bWalking)
	{
		// Reset rather than hold, so the first step after stopping is a full stride away instead of the
		// half-stride that was left over from the last walk.
		FootstepAccumulator = 0.f;
		return;
	}

	FootstepAccumulator += Speed * DeltaSeconds;
	if (FootstepAccumulator < FootstepStride)
	{
		return;
	}

	FootstepAccumulator = 0.f;

	// Sent rather than played locally: a step has to be heard by the players who are not the one taking it,
	// and the authority is the only side that can put one sound on every machine.
	ServerReportNoise(FootstepSound, GetActorLocation(), FootstepLoudness);
}

void ACosmereCharacter::ServerReportNoise_Implementation(USoundBase* Sound, FVector Location, float Loudness)
{
	ACosmereGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACosmereGameState>() : nullptr;
	if (!GameState)
	{
		return;
	}

	GameState->ReportNoise(this, Location, Sound, Loudness);
}

UCosmereMovementModifierComponent* ACosmereCharacter::GetMovementModifiers() const
{
	return MovementModifiers;
}

UCosmereGravityComponent* ACosmereCharacter::GetGravityComponent() const
{
	return GravityComponent;
}

UCosmereTinSensesComponent* ACosmereCharacter::GetTinSensesComponent() const
{
	return TinSenses;
}

UCosmereInvestitureComponent* ACosmereCharacter::GetInvestitureComponent() const
{
	return Investiture;
}

UCosmereRealmComponent* ACosmereCharacter::GetRealmComponent() const
{
	return RealmComponent;
}

bool ACosmereCharacter::IsBurningPower() const
{
	const ACosmerePlayerState* PS = GetCosmerePlayerState();
	UAbilitySystemComponent* ASC = PS ? PS->GetAbilitySystemComponent() : nullptr;
	if (!ASC)
	{
		return false;
	}

	// Asked of every granted power rather than answered from a flag on the pawn: a burn that ran dry, or one
	// the input layer put out, has already stepped itself down to level 0 by the time this looks, so there is
	// no separate state for anything to keep in sync.
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		UGameplayAbility* Ability = Spec.GetPrimaryInstance();
		if (!Ability)
		{
			Ability = Spec.Ability;
		}

		const UCosmereBurnAbility* Burn = Cast<UCosmereBurnAbility>(Ability);
		if (Burn && Burn->GetBurnLevel() > 0)
		{
			return true;
		}
	}

	return false;
}

void ACosmereCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	
	// All four are resolved before anything reads the view this frame. A reserve that has run out ends the lash
	// first, so the frame it empties is also the frame the aim hands back to the surface; which frame the aim
	// belongs in can change on any frame, and the seed has to be taken from the picture still on screen rather
	// than after it has moved. The horizon is slewed after the free aim because which frame it belongs to was
	// decided by that call.
	EnforceLashReserve();
	RefreshFreeAim();
	UpdateSurfaceHorizon(DeltaSeconds);
	UpdateGravityReferenceLine();

	// Steps are counted before the falling early-out below, because a pawn on the floor is exactly the pawn
	// that is walking - the early-out is about fall speed, not about whether anything else has to happen.
	UpdateFootsteps(DeltaSeconds);
	
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement || !Movement->IsFalling())
	{
		return;
	}

	// Measured along the pawn's own gravity axis rather than world Z. After a Gravitation lashing the wall it
	// stands on is the floor, so a clamp against Z would let a lashing aimed at a wall accelerate forever.
	const FVector GravityDirection = Movement->GetGravityDirection();
	const float SpeedDown = FVector::DotProduct(Movement->Velocity, GravityDirection);
	const float TerminalSpeed = GetMaxFallSpeed();
	if (SpeedDown <= TerminalSpeed)
	{
		return;
	}

	// Removes only the excess along that axis, so lateral speed built up before the ceiling was reached survives.
	Movement->Velocity -= GravityDirection * (SpeedDown - TerminalSpeed);
}

void ACosmereCharacter::UpdateSurfaceHorizon(float DeltaSeconds)
{
	// Only while the surface frame is the one on screen. A flight holds its own roll, and a pawn in the air has
	// no floor to follow, so the horizon is put out of use rather than left running underneath either of them.
	const UCosmereGravityComponent* GravityState = GetGravityComponent();
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const bool bSurfaceFrameInUse = !bFreeAimActive
		&& CosmereAimMode::GetForcedMode() != CosmereAimMode::EMode::Standard
		&& GravityState && GravityState->IsLashing()
		&& Movement && Movement->IsMovingOnGround();

	if (!bSurfaceFrameInUse)
	{
		// Forgotten rather than held. Whatever floor comes next, the frame comes back seeded from the body that
		// is on screen at that moment, so nothing crosses a flight or a jump.
		bSurfaceHorizonValid = false;
		return;
	}

	const FVector TargetUp = -GravityState->GetGravityDirection();
	if (!bSurfaceHorizonValid)
	{
		// Seeded from the body, not from the floor. That is what makes the hand-over from the body's own roll
		// invisible: the first horizon this slews is the horizon already on screen.
		SurfaceHorizonUp = GetActorUpVector();
		bSurfaceHorizonValid = true;
	}

	// Rate limited rather than read off the floor, because the floor is only re-examined where the capsule is,
	// and on anything curved that arrives as one discrete step per facet. This is the part of the turn the body
	// cannot supply, filled in at a rate the world can be seen following.
	SurfaceHorizonUp = FMath::VInterpNormalRotationTo(
		SurfaceHorizonUp, TargetUp, DeltaSeconds, SurfaceHorizonDegreesPerSecond).GetSafeNormal();
}

FRotator ACosmereCharacter::GetViewRotation() const
{
	// Free aim replaces the whole chain rather than adding a roll to it. Nothing below runs while it is engaged,
	// so the pitch limit the camera manager applies to the control rotation cannot reach the picture, and the
	// aim is free to pass straight over the pole instead of stalling at it.
	if (bFreeAimActive)
	{
		return FreeAimRotation.Rotator();
	}
	
	// A forced Standard mode is the whole of option A, and the gravity-frame roll derived below is exactly what
	// makes screen axes differ from world axes - keeping it would make this a fourth model rather than A.
	// Auto never reaches this line, so the shipping picture is untouched.
	if (CosmereAimMode::GetForcedMode() == CosmereAimMode::EMode::Standard)
	{
		return Super::GetViewRotation();
	}

	const FRotator BaseViewRotation = Super::GetViewRotation();

	// The capsule is what PhysicsRotation slews into the gravity frame, so its up is read back here rather
	// than the gravity component being asked where down is. Mid-transition the body is only partway round,
	// and reading it back is what puts the picture partway round with it instead of arriving before it.
	const FVector BodyUp = GetActorUpVector();
	if (FVector::DotProduct(BodyUp, FVector::UpVector) >= 0.99985f)
	{
		return BaseViewRotation;
	}
	
	// While a lash has the pawn standing on a floor the horizon is the slewed one instead, because the body only
	// re-points in the discrete steps a floor check hands back and the picture would otherwise show those steps.
	// Everywhere else this is the body's own up, which is the shipped behaviour unchanged.
	const FVector RollUp = bSurfaceHorizonValid ? SurfaceHorizonUp : BodyUp;
	
	const FVector ViewForward = BaseViewRotation.Vector();
	const FVector FlatUp = RollUp - ViewForward * FVector::DotProduct(RollUp, ViewForward);
	if (FlatUp.SizeSquared() <= UE_KINDA_SMALL_NUMBER)
	{
		// Aiming straight along the body's own up axis, where no roll is defined. Leave the view alone.
		return BaseViewRotation;
	}

	// MakeFromXZ takes X exactly as given and puts Z as close to FlatUp as it can while staying perpendicular
	// to it. The forward vector therefore comes out untouched - the player keeps aiming exactly where they
	// were - and the only thing that differs is the roll about it, which is the entire point.
	return FRotationMatrix::MakeFromXZ(ViewForward, FlatUp.GetSafeNormal()).Rotator();
}

void ACosmereCharacter::AddControllerYawInput(float Val)
{
	Super::AddControllerYawInput(Val);
	ApplyFramedLookInput();
}

void ACosmereCharacter::AddControllerPitchInput(float Val)
{
	Super::AddControllerPitchInput(Val);
	ApplyFramedLookInput();
}

void ACosmereCharacter::ApplyFramedLookInput()
{
	// Resolved before the input is spent, because the two frames consume the same batch in different ways.
	RefreshFreeAim();
	
	// The same test GetViewRotation uses to decide the body is upright. While it is - and while the aim is not
	// free - world axes and screen axes are the same axes and Super has already spent the input correctly.
	// Nothing is consumed on this path, so UpdateRotation still gets to spend it.
	// A forced mode replaces that decision outright: Standard is this path unconditionally, ScreenRelative
	// never is, and FreeAim arrives here with bFreeAimActive already set by the refresh above.
	const CosmereAimMode::EMode ForcedMode = CosmereAimMode::GetForcedMode();
	const bool bBodyUpright = FVector::DotProduct(GetActorUpVector(), FVector::UpVector) >= 0.99985f;
	const bool bStandardFrame = ForcedMode == CosmereAimMode::EMode::Standard
		|| (ForcedMode == CosmereAimMode::EMode::Auto && !bFreeAimActive && bBodyUpright);
	if (bStandardFrame)
	{
		return;
	}

	APlayerController* OwningController = Cast<APlayerController>(GetController());
	if (!OwningController)
	{
		return;
	}

	// Super has already turned this batch of mouse into degrees and has already applied the ignore-look
	// gate, so the amount is taken exactly as the engine would have spent it. Clearing it is what stops
	// UpdateRotation spending it a second time, about the world axes.
	const FRotator PendingLook = OwningController->RotationInput;
	OwningController->RotationInput = FRotator::ZeroRotator;
	
	if (bFreeAimActive)
	{
		if (FMath::IsNearlyZero(PendingLook.Yaw, KINDA_SMALL_NUMBER) && FMath::IsNearlyZero(PendingLook.Pitch, KINDA_SMALL_NUMBER))
		{
			return;
		}
		
		// Both axes are taken from the held roll reference rather than from the aim's own up. The aim's own up
		// *is* its roll, so turning about it turned the horizon every time the player was pitched off level and
		// the picture drifted for as long as the ability was up. Turning about the reference instead leaves the
		// roll exactly where the lash found it: the player can look anywhere and the world stays the same way
		// up, which is the camera not changing. The reference owns the roll; the player owns only the forward.
		const FVector AimForward = FreeAimRotation.GetForwardVector();
		
		// Pitch still travels with the screen, so it is taken about the screen's right. Rebuilt here from the
		// aim and the reference rather than read off the quaternion, because that is what keeps it roll-free.
		FVector AimRight = FVector::CrossProduct(FreeAimRollReference, AimForward);
		if (AimRight.SizeSquared() <= KINDA_SMALL_NUMBER)
		{
			// The lash engaged with the aim already on the reference's pole, where that cross product collapses.
			// The aim's own right is carried for the step; it resolves on the first turn away from the pole.
			AimRight = FreeAimRotation.GetRightVector();
		}
		AimRight = AimRight.GetSafeNormal();
		
		// Negated, because positive look-pitch in the engine's own convention carries the forward vector towards
		// the frame's up - which is a rotation about the *negative* right axis, exactly as FRotator::Quaternion
		// encodes it. Turning about the right axis instead stands the mouse on its head: up would look down, and
		// down would look up, for as long as the lash keeps the aim free of the body.
		const FQuat PitchStep(-AimRight, FMath::DegreesToRadians(PendingLook.Pitch));
		FQuat PitchedRotation = (PitchStep * FreeAimRotation).GetNormalized();
		
		// Roll-free means the aim cannot cross the reference's own pole: going over it would flip the horizon,
		// which is the one thing the player was told would not move. So the pitch stops short of it, at
		// CameraPitchLimitDegrees off the reference's horizon.
		//
		// The step is projected onto the limit rather than thrown away. Discarding an overshooting step stops the
		// aim wherever the previous frame left it, so how far the player could look depended on how fast they
		// moved the mouse; rebuilding the orientation at exactly the limit makes 87 degrees a stop they can reach
		// and feel, and it is the only angle at which the axis this turns about cannot collapse.
		const float PitchLimitRadians = FMath::DegreesToRadians(CameraPitchLimitDegrees);
		const float AlongReference = FVector::DotProduct(PitchedRotation.GetForwardVector(), FreeAimRollReference);
		if (FMath::Abs(AlongReference) > FMath::Sin(PitchLimitRadians))
		{
			const FVector FlatForward = PitchedRotation.GetForwardVector() - FreeAimRollReference * AlongReference;
			if (FlatForward.SizeSquared() > KINDA_SMALL_NUMBER)
			{
				// Rebuilt about the reference, so the roll is exactly where the lash found it and only the pitch
				// has moved - the same construction GetViewRotation uses to hold the horizon.
				const FVector ClampedForward = (FlatForward.GetSafeNormal() * FMath::Cos(PitchLimitRadians)
					+ FreeAimRollReference * (FMath::Sign(AlongReference) * FMath::Sin(PitchLimitRadians))).GetSafeNormal();
				PitchedRotation = FRotationMatrix::MakeFromXZ(ClampedForward, FreeAimRollReference).Rotator().Quaternion();
			}
		}

		FreeAimRotation = PitchedRotation;
		
		// Yaw about the reference up turns the aim in the plane the player sees as horizontal, and cannot change
		// the roll, so the horizon holds still while they swing round.
		const FQuat YawStep(FreeAimRollReference, FMath::DegreesToRadians(PendingLook.Yaw));
		FreeAimRotation = (YawStep * FreeAimRotation).GetNormalized();
		
		// The control rotation still has to carry the aim, because the pawn's facing and every authority-side
		// read expect a plain direction. Built from the forward vector alone, so it stays roll-free and the free
		// aim keeps sole ownership of the roll - the roll is the picture's, never the capsule's.
		OwningController->SetControlRotation(FRotationMatrix::MakeFromX(FreeAimRotation.GetForwardVector()).Rotator());
		return;
	}
	
	if (FMath::IsNearlyZero(PendingLook.Yaw, KINDA_SMALL_NUMBER) && FMath::IsNearlyZero(PendingLook.Pitch, KINDA_SMALL_NUMBER))
	{
		return;
	}

	// Turn the aim about the axes the player can actually see: yaw about the screen's up, pitch about the
	// screen's right. On a ceiling both point opposite their world counterparts, which is the inversion
	// being removed; on a wall both are horizontal, which is why the mouse stops dragging the horizon
	// diagonally. These two axes are the view's own, so neither can be expressed as a world pitch or yaw.
	const FQuat ViewQuat = GetViewRotation().Quaternion();
	FVector AimDirection = OwningController->GetControlRotation().Vector();
	// The same negation as the free branch, and for the same reason: a wall or a ceiling has a frame whose right
	// axis is not world Y, so pitching about the axis itself inverts up and down the moment the body is no longer
	// upright. About the negative right it is screen-correct in any orientation.
	AimDirection = FQuat(-ViewQuat.GetRightVector(), FMath::DegreesToRadians(PendingLook.Pitch)).RotateVector(AimDirection);
	AimDirection = FQuat(ViewQuat.GetUpVector(), FMath::DegreesToRadians(PendingLook.Yaw)).RotateVector(AimDirection);

	// The same stop as the free frame, measured against the pawn's own up instead of the held horizon. A wall or
	// a ceiling has no pole in world pitch space - aiming "straight down" on a wall is a horizontal direction -
	// so the camera manager's world-space limit cannot see this case, and the aim would otherwise be free to run
	// straight into the frame's up axis, where yaw collapses and the picture rolls over. Only reached while the
	// body is off vertical, which is exactly when a relative down exists.
	const UCosmereGravityComponent* GravityState = GetGravityComponent();
	const FVector SurfaceUp = GravityState ? -GravityState->GetGravityDirection() : FVector::UpVector;
	if (!SurfaceUp.IsNearlyZero())
	{
		const float SurfacePitchLimit = FMath::DegreesToRadians(CameraPitchLimitDegrees);
		const float AimAlongUp = FVector::DotProduct(AimDirection, SurfaceUp);
		if (FMath::Abs(AimAlongUp) > FMath::Sin(SurfacePitchLimit))
		{
			const FVector AimFlat = AimDirection - SurfaceUp * AimAlongUp;
			if (AimFlat.SizeSquared() > KINDA_SMALL_NUMBER)
			{
				// Azimuth kept, elevation clamped: the player keeps looking the way they were looking and only
				// the part that would have crossed the pole is given back.
				AimDirection = (AimFlat.GetSafeNormal() * FMath::Cos(SurfacePitchLimit)
					+ SurfaceUp * (FMath::Sign(AimAlongUp) * FMath::Sin(SurfacePitchLimit))).GetSafeNormal();
			}
		}
	}

	// Roll-free by construction. GetViewRotation re-derives the roll from the body every frame so nothing is
	// lost, and the control rotation stays a plain aim direction that the camera manager's pitch limit and
	// the pawn's gravity-space facing both keep reading the way they always did.
	OwningController->SetControlRotation(AimDirection.Rotation());
}
	
bool ACosmereCharacter::ShouldUseFreeAim() const
{
	// A forced mode answers before the geometry does. Standard and ScreenRelative are both surface-frame
	// models, so free aim has to stay off for them even in mid-air; FreeAim has to stay on even standing
	// still. Auto falls through to the rule below, which is the shipping answer.
	const CosmereAimMode::EMode ForcedMode = CosmereAimMode::GetForcedMode();
	if (ForcedMode == CosmereAimMode::EMode::FreeAim)
	{
		return true;
	}
	if (ForcedMode != CosmereAimMode::EMode::Auto)
	{
		return false;
	}

	// Only a press arms the flight aim. This is the whole of "not while jumping": a lash stays latched long
	// after the pawn is standing on the wall it was aimed at, so without this every later jump off that wall is
	// spent off the ground with a lash latched and would read as flight.
	if (!bFreeAimArmed)
	{
		return false;
	}

	const UCosmereGravityComponent* GravityState = GetGravityComponent();
	if (!GravityState || !GravityState->IsLashing())
	{
		return false;
	}
	
	// The flight ends where it was aimed. Standing on the surface the lash points into means that surface is the
	// pawn's ground now, so the horizon it took off with stops being the right one - and the aim belongs in the
	// surface frame again, which is the snap. This and a lash ending are the only two things allowed to move the
	// picture once a flight has started.
	//
	// Ten degrees of tolerance rather than none, because the direction and the geometry are not the same number:
	// RefineToLandedSurface re-points gravity at the real floor normal on touch, so the two agree closely once the
	// pawn is standing. The floor the lash was taken from is a whole quarter turn away and cannot be mistaken for
	// the one aimed at, so a press that starts on the ground still flies.
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (Movement && Movement->IsMovingOnGround() && Movement->CurrentFloor.IsWalkableFloor())
	{
		constexpr double ArrivedDot = 0.985;
		const FVector SurfaceDown = -Movement->CurrentFloor.HitResult.ImpactNormal;
		if (FVector::DotProduct(SurfaceDown, GravityState->GetGravityDirection()) >= ArrivedDot)
		{
			return false;
		}
	}

	return true;
}
	
void ACosmereCharacter::BeginLashFreeAim()
{
	// The picture is local, so a machine that is not rendering this pawn has no flight to seed. The lash itself
	// still runs on both - the direction is movement state - but the aim it would engage belongs to eyes.
	if (!IsLocallyControlled())
	{
		return;
	}

	// A forced mode replaces the rule outright, exactly as ShouldUseFreeAim says it does: Standard and
	// ScreenRelative are surface-frame models and never fly, and FreeAim is already flying everywhere.
	if (CosmereAimMode::GetForcedMode() != CosmereAimMode::EMode::Auto)
	{
		return;
	}

	// A press that only levels a lash already in flight is not a new flight. Re-seeding here would be the camera
	// moving on a press, which is the one thing a press must never do.
	if (bFreeAimActive)
	{
		bFreeAimArmed = true;
		return;
	}

	// Taken here, between the gravity direction being written and PhysicsRotation acting on it. The capsule has
	// not turned a degree yet, so this is the orientation the player is looking at - and holding its up vector as
	// the roll reference freezes the horizon they took off with for the whole flight.
	FreeAimRotation = GetViewRotation().Quaternion();
	FreeAimRollReference = FreeAimRotation.GetUpVector().GetSafeNormal();

	bFreeAimArmed = true;

	// A press that points at the floor underfoot is not a flight, so the ordinary rule still gets to refuse it.
	bFreeAimActive = ShouldUseFreeAim();
}

void ACosmereCharacter::RefreshFreeAim()
{
	const bool bWantFreeAim = ShouldUseFreeAim();
	if (bWantFreeAim == bFreeAimActive)
	{
		return;
	}
	
	if (bWantFreeAim)
	{
		// Read while the old frame is still the one on screen, which is what makes the hand-off invisible: the
		// first free orientation is the last surface one. The forward vector is the same either way and the roll
		// carries over, so nothing on screen moves at the instant the ability takes over.
		FreeAimRotation = GetViewRotation().Quaternion();
		
		// The roll it is frozen at, taken from the same read so the two agree exactly. Held for the whole lash:
		// this is the axis the aim is turned about, so nothing the player does can roll the picture while flying.
		FreeAimRollReference = FreeAimRotation.GetUpVector().GetSafeNormal();
	}
	
	if (!bWantFreeAim)
	{
		// The flight is over, so the next one has to be asked for. Leaving this armed is exactly what would put
		// the pawn back into free aim on the next jump off the surface it has just landed on.
		bFreeAimArmed = false;
	}

	bFreeAimActive = bWantFreeAim;
}

void ACosmereCharacter::UpdateGravityReferenceLine()
{
	if (!GravityReferenceLines)
	{
		return;
	}
	
	// Owner-only by construction rather than by a visibility flag: the line is only ever added on the machine
	// whose eyes this pawn is behind, so it never reaches another player's picture. Anything left over from a
	// frame this pawn was the local one is cleared here.
	if (!IsLocallyControlled())
	{
		if (GravityReferenceLines->BatchedLines.Num() > 0)
		{
			GravityReferenceLines->Flush();
		}
		return;
	}
	
	// Cleared first, so the batcher only ever holds this frame's segment.
	GravityReferenceLines->Flush();
	
	const UCosmereGravityComponent* GravityState = GetGravityComponent();
	if (!GravityState || !GravityState->IsLashing())
	{
		return;
	}
	
	// Along gravity rather than along the body. Mid-transition the capsule is still partway round, and the point
	// of the line is to show where down has actually gone, not where the body has got to.
	const FVector GravityUp = -GravityState->GetGravityDirection();
	if (GravityUp.IsNearlyZero())
	{
		return;
	}
	
	float HalfHeight = 88.f;
	if (const UCapsuleComponent* CapsuleNow = GetCapsuleComponent())
	{
		HalfHeight = CapsuleNow->GetScaledCapsuleHalfHeight();
	}
	
	// Foreground, so it reads through the surface it is running into. That is exactly the moment the answer is
	// worth having: the line is saying which surface is about to become the floor.
	// The reach past the capsule is what the surge pays for, so that is the part that is scaled: at full reserve
	// the line runs its whole overhang, and as the reserve drains it draws in towards the capsule. The capsule
	// length is kept as a floor, so the line still answers which way down is when there is nothing left to spend.
	const float SurgeFraction = GetGravitationSurgeFraction();
	
	const FVector CenterLine = GetActorLocation();
	const FVector Reach = GravityUp.GetSafeNormal() * (HalfHeight + GravityReferenceOverhang * SurgeFraction);
	GravityReferenceLines->DrawLine(CenterLine - Reach, CenterLine + Reach, GravityReferenceColor, SDPG_Foreground, 2.f, 0.f);
}
	
void ACosmereCharacter::RegisterLashTarget(UCosmereGravityComponent* Target)
{
	if (!Target)
	{
		return;
	}
	
	// One entry per component. A press that flares an existing lashing offers the same target again, so the list
	// has to behave as a set or a flare would look like a second lash to take down later.
	LashTargets.AddUnique(TWeakObjectPtr<UCosmereGravityComponent>(Target));
}
	
float ACosmereCharacter::GetGravitationSurgeFraction() const
{
	// A reserve that cannot be read is not an empty one. Every path that cannot answer here is a pawn that could
	// never have paid for a lash in the first place, so reporting it as empty would only ever drop a lash that
	// something else is already responsible for.
	const ACosmerePlayerState* OwnerState = GetCosmerePlayerState();
	if (!OwnerState)
	{
		return 1.f;
	}
	
	const UCosmereSurgeResourceComponent* OwnerSurges = OwnerState->GetSurgeResourceComponent();
	if (!OwnerSurges)
	{
		return 1.f;
	}
	
	return OwnerSurges->GetStormlightFraction();
}
	
void ACosmereCharacter::EnforceLashReserve()
{
	if (LashTargets.Num() == 0)
	{
		return;
	}
	
	// Anything destroyed, or already unlashed by its own hand, is no longer being paid for. Pruned before the
	// reserve is read so a stale entry can never be the reason the list is treated as still live.
	LashTargets.RemoveAll([](const TWeakObjectPtr<UCosmereGravityComponent>& Target)
	{
		return !Target.IsValid() || !Target->IsLashing();
	});
	
	if (LashTargets.Num() == 0 || GetGravitationSurgeFraction() > 0.f)
	{
		return;
	}
	
	// Paid for, and now empty. Everything the reserve was holding up comes down at once, the player's own lash
	// included: a lash that outlived its reserve would be free flight, and there is no partial state worth
	// keeping between full gravitation and none.
	for (const TWeakObjectPtr<UCosmereGravityComponent>& Target : LashTargets)
	{
		if (Target.IsValid())
		{
			Target->ClearLashing();
		}
	}
	
	LashTargets.Reset();
}
	
UAbilitySystemComponent* ACosmereCharacter::GetAbilitySystemComponent() const
{
	const ACosmerePlayerState* PS = GetCosmerePlayerState();
	return PS ? PS->GetAbilitySystemComponent() : nullptr;
}

ACosmerePlayerState* ACosmereCharacter::GetCosmerePlayerState() const
{
	return Cast<ACosmerePlayerState>(GetPlayerState());
}

void ACosmereCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Authority path. The ASC lives on the PlayerState, so its actor info has to point at
	// (PlayerState, this pawn) before any ability can run.
	BindToPlayerState();
	RegisterMatchMappingContext();

	ACosmerePlayerState* PS = GetCosmerePlayerState();
	if (!PS)
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereCharacter: %s was possessed but has no ACosmerePlayerState, so no build can be applied."),
			*GetName());
		return;
	}

	// If the build is already here - a respawn, or a submission that beat possession - apply it now.
	// Otherwise only the local controller submits. On a listen server the other pawns' controllers
	// are remote, and each of those clients submits its own build from its own machine's profile.
	if (PS->HasSelectedBuild())
	{
		HandleBuildReplicated(true);
	}
	else if (NewController && NewController->IsLocalController())
	{
		PS->SubmitSelectedCharacterFromProfile();
	}
}

void ACosmereCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	// PossessedBy is authority-only; the owning client needs the same existing match context.
	RegisterMatchMappingContext();
}

void ACosmereCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// Client path. On a client the PlayerState arrives after possession, so this is where a
	// remotely-controlled pawn first has an ASC to point at.
	BindToPlayerState();

	ACosmerePlayerState* PS = GetCosmerePlayerState();
	if (PS && PS->HasSelectedBuild())
	{
		HandleBuildReplicated(true);
	}
}

void ACosmereCharacter::BindToPlayerState()
{
	ACosmerePlayerState* PS = GetCosmerePlayerState();
	if (!PS)
	{
		return;
	}

	if (UCosmereAbilitySystemComponent* ASC =
			Cast<UCosmereAbilitySystemComponent>(PS->GetAbilitySystemComponent()))
	{
		ASC->InitAbilityActorInfo(PS, this);
	}

	// RemoveDynamic first so that a re-possess cannot leave two subscriptions behind and apply the
	// same build twice.
	PS->OnBuildReplicated.RemoveDynamic(this, &ACosmereCharacter::HandleBuildReplicated);
	PS->OnBuildReplicated.AddDynamic(this, &ACosmereCharacter::HandleBuildReplicated);
}

void ACosmereCharacter::HandleBuildReplicated(bool bHasBuild)
{
	if (!bHasBuild || bAppliedBuild)
	{
		return;
	}

	ACosmerePlayerState* PS = GetCosmerePlayerState();
	if (!PS)
	{
		return;
	}

	ApplyBuildToPawn(PS->GetSelectedBuild());
}

void ACosmereCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// The metalmind is earned and spent on the authority only, so the bar every client draws is the server's.
	DOREPLIFETIME(ACosmereCharacter, MetalmindReserve);
}

bool ACosmereCharacter::IsMetalmindSlot(EPowerSlot Slot) const
{
	const ACosmerePlayerState* PS = GetCosmerePlayerState();
	if (!PS)
	{
		return false;
	}

	const FSpiritwebCharacterSave Build = PS->GetSelectedBuild();
	const int32 SlotIndex = (Slot == EPowerSlot::Beta) ? 1 : 0;
	if (!Build.PowerIds.IsValidIndex(SlotIndex))
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const UCosmereGameInstance* GameInstance = World ? World->GetGameInstance<UCosmereGameInstance>() : nullptr;
	const UCosmereCatalogSubsystem* Catalog = GameInstance ? GameInstance->GetCatalog() : nullptr;

	FPowerDefinition Row;
	return Catalog && Catalog->GetPowerRow(Build.PowerIds[SlotIndex], Row) && Row.Source == EPowerSource::Feruchemical;
}

EPowerSource ACosmereCharacter::GetPowerSlotSource(EPowerSlot Slot) const
{
	const ACosmerePlayerState* PS = GetCosmerePlayerState();
	if (!PS)
	{
		return EPowerSource::Allomantic;
	}

	const FSpiritwebCharacterSave Build = PS->GetSelectedBuild();
	const int32 SlotIndex = (Slot == EPowerSlot::Beta) ? 1 : 0;
	if (!Build.PowerIds.IsValidIndex(SlotIndex))
	{
		return EPowerSource::Allomantic;
	}

	const UWorld* World = GetWorld();
	const UCosmereGameInstance* GameInstance = World ? World->GetGameInstance<UCosmereGameInstance>() : nullptr;
	const UCosmereCatalogSubsystem* Catalog = GameInstance ? GameInstance->GetCatalog() : nullptr;

	// Unresolvable reads as Allomantic, which is the shared-pool answer: an unknown slot then draws exactly what
	// it drew before surges had reserves of their own, rather than claiming a reserve it can never name.
	FPowerDefinition Row;
	if (!Catalog || !Catalog->GetPowerRow(Build.PowerIds[SlotIndex], Row))
	{
		return EPowerSource::Allomantic;
	}

	return Row.Source;
}

float ACosmereCharacter::GetMetalmindReserveFraction(EPowerSlot Slot) const
{
	if (MetalmindCapacity <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}

	const int32 SlotIndex = (Slot == EPowerSlot::Beta) ? 1 : 0;
	return FMath::Clamp(MetalmindReserve[SlotIndex] / MetalmindCapacity, 0.f, 1.f);
}

float ACosmereCharacter::GetMetalmindReserveUnits(EPowerSlot Slot) const
{
	const int32 SlotIndex = (Slot == EPowerSlot::Beta) ? 1 : 0;
	return MetalmindReserve[SlotIndex];
}

float ACosmereCharacter::AddMetalmindReserve(EPowerSlot Slot, float DeltaUnits)
{
	// Authority only, for the same reason the power pool is: a client that spent metal locally could disagree
	// with the server about whether the buff was affordable.
	if (!HasAuthority())
	{
		return 0.f;
	}

	const int32 SlotIndex = (Slot == EPowerSlot::Beta) ? 1 : 0;
	const float Before = MetalmindReserve[SlotIndex];
	MetalmindReserve[SlotIndex] = FMath::Clamp(Before + DeltaUnits, 0.f, MetalmindCapacity);
	return MetalmindReserve[SlotIndex] - Before;
}

void ACosmereCharacter::AccrueMetalmindPassive()
{
	if (!HasAuthority())
	{
		return;
	}

	// Standing still means not walking, not falling: the test is horizontal and on the ground, so a pawn in
	// mid-air or riding a moving platform still reads as stationary.
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!Move || !Move->IsMovingOnGround())
	{
		return;
	}

	if (GetVelocity().SizeSquared2D() > FMath::Square(MetalmindPassiveStillSpeed))
	{
		return;
	}

	for (int32 Index = 0; Index < 2; ++Index)
	{
		const EPowerSlot Slot = (Index == 1) ? EPowerSlot::Beta : EPowerSlot::Alpha;

		const UCosmerePowerAbility* Power = FindPowerSlotAbility(Slot);
		const float Rate = Power ? Power->GetMetalmindPassiveStandRate() : 0.f;
		if (Rate <= 0.f)
		{
			continue;
		}

		// A spend owns the slot. While the metalmind is being spent there is no passive income on top of it,
		// which is what stops a tap from paying for itself by standing still with it running. Storing is not a
		// spend - it is the other direction - so a hold still earns the passive as well as its own rate.
		const EMetalmindMode Mode = Power->GetMetalmindMode();
		if (Mode == EMetalmindMode::Tapping || Mode == EMetalmindMode::Flaring)
		{
			continue;
		}

		// AddMetalmindReserve clamps at capacity, so a full metalmind simply stops earning.
		AddMetalmindReserve(Slot, Rate * MetalmindPassiveInterval);
	}
}

void ACosmereCharacter::StartMetalmindStore(EPowerSlot Slot, FGameplayAbilitySpecHandle Handle)
{
	const int32 SlotIndex = (Slot == EPowerSlot::Beta) ? 1 : 0;

	ACosmerePlayerState* PS = GetCosmerePlayerState();
	UAbilitySystemComponent* ASC = PS ? PS->GetAbilitySystemComponent() : nullptr;
	FGameplayAbilitySpec* Spec = ASC ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
	if (!Spec)
	{
		return;
	}

	UGameplayAbility* Ability = Spec->GetPrimaryInstance();
	if (!Ability)
	{
		Ability = Spec->Ability;
	}

	UCosmerePowerAbility* Power = Cast<UCosmerePowerAbility>(Ability);

	// Never gate this on Power->IsMetalmind(): that answers false until the row resolves, and the row only
	// resolves on the first activation - which is the one thing this callback exists to start. Gating on it
	// deadlocks a fresh pawn, because an empty metalmind cannot fill and an empty metalmind refuses every
	// tap before TryActivateAbility, so the row would never resolve at all. The timer that reaches this
	// callback is only ever armed from SetPowerSlotHeld's Feruchemical branch.
	if (!Power)
	{
		return;
	}

	// Already running - a tap landed before the hold clock elapsed - so a hold is nothing on top of it.
	// The classification flag stays down here on purpose: it is what tells the release "this press was a
	// store, stop it". Raising it without a store would swallow the release and leave a live buff with no
	// way to be cancelled, which is the bug this guard has to keep open.
	if (Power->GetMetalmindMode() != EMetalmindMode::Idle)
	{
		return;
	}

	// Nothing to store into is not a store. A full metalmind only has the metal's own downside left to pay -
	// half a Steelrunner's speed, a Skimmer's weight - for metal that has nowhere to go, so the hold is declined.
	//
	// Consumed rather than ignored: raising the classification flag makes the release read as "this press was a
	// store, stop it", which is a no-op on an ability that never started. Leaving it down would let the release
	// fall through into the tap branch and spend the very reserve the player was trying to fill.
	if (GetMetalmindReserveFraction(Slot) >= 1.f - KINDA_SMALL_NUMBER)
	{
		bPowerHoldClassified[SlotIndex] = true;
		return;
	}

	// Queued before activating so ActivateAbility knows this press is a store: a store only fills the metal,
	// so the archetype's own effect has to stay out of it.
	Power->QueueMetalmindMode(EMetalmindMode::Storing, SlotIndex);

	if (!ASC->TryActivateAbility(Handle))
	{
		Power->QueueMetalmindMode(EMetalmindMode::Idle, INDEX_NONE);

		if (UWorld* World = GetWorld())
		{
			LastRefusedPowerTime[SlotIndex] = World->GetTimeSeconds();
		}
		return;
	}

	Power->BeginMetalmind(EMetalmindMode::Storing, SlotIndex);

	// Raised only once a store is genuinely running, never on entry to this callback. A hold that gets
	// refused above therefore leaves the release to be classified as a tap instead of being eaten.
	bPowerHoldClassified[SlotIndex] = true;
}

bool ACosmereCharacter::SetPowerSlotHeld(EPowerSlot Slot, bool bHeld)
{
	ACosmerePlayerState* PS = GetCosmerePlayerState();
	UAbilitySystemComponent* ASC = PS ? PS->GetAbilitySystemComponent() : nullptr;
	if (!ASC)
	{
		return false;
	}

	// The build is the only thing that knows which ability landed in which slot. Index 0 is
	// EPowerSlot::Alpha and index 1 is Beta, matching FSpiritwebCharacterSave::PowerIds.
	const FSpiritwebCharacterSave Build = PS->GetSelectedBuild();
	const int32 SlotIndex = (Slot == EPowerSlot::Beta) ? 1 : 0;
	if (!Build.PowerIds.IsValidIndex(SlotIndex))
	{
		return false;
	}

	const FName PowerId = Build.PowerIds[SlotIndex];

	UWorld* World = GetWorld();
	UCosmereGameInstance* GameInstance = World ? World->GetGameInstance<UCosmereGameInstance>() : nullptr;
	const UCosmereCatalogSubsystem* Catalog = GameInstance ? GameInstance->GetCatalog() : nullptr;

	FPowerDefinition Row;
	if (!Catalog || !Catalog->GetPowerRow(PowerId, Row) || !Row.AbilityClass)
	{
		return false;
	}

	// The grant path stores the row's AbilityClass as the spec's ability class verbatim, so the spec is
	// found by matching class rather than by remembering a handle that a re-grant would invalidate.
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		UGameplayAbility* Ability = Spec.GetPrimaryInstance();
		if (!Ability)
		{
			Ability = Spec.Ability;
		}

		if (!Ability || Ability->GetClass() != Row.AbilityClass)
		{
			continue;
		}

		UCosmerePowerAbility* Power = Cast<UCosmerePowerAbility>(Ability);

		// Everything that is not a metalmind keeps the contract it always had: press activates, release ends.
		if (!Power || Row.Source != EPowerSource::Feruchemical)
		{
			if (bHeld)
			{
				const bool bActivated = ASC->TryActivateAbility(Spec.Handle);

				// A refused press is the only thing that separates "this power is unavailable" from "this power
				// is available and I chose not to use it". TryActivateAbility answers false for both gates the
				// row itself defines - an unaffordable cost and a running cooldown - which is exactly the pair
				// the HUD flashes on. A slot with no granted spec returns above this, so an unequipped power
				// never flashes.
				if (!bActivated && World)
				{
					LastRefusedPowerTime[SlotIndex] = World->GetTimeSeconds();
				}

				return bActivated;
			}

			if (Power)
			{
				Power->EndPower();
				return true;
			}

			return false;
		}

		//~ Begin metalmind press classification
		if (!World)
		{
			return false;
		}

		if (bHeld)
		{
			// The press starts a short clock instead of a power: released before it elapses it was a tap, and
			// still held when it elapses it is a store. Nothing is activated yet either way, which is what
			// lets one key mean three different things.
			bPowerHoldClassified[SlotIndex] = false;
			FTimerDelegate HoldDelegate = FTimerDelegate::CreateUObject(this, &ACosmereCharacter::StartMetalmindStore, Slot, Spec.Handle);
			World->GetTimerManager().SetTimer(PowerHoldTimers[SlotIndex], HoldDelegate, Power->GetMetalmindHoldThreshold(), /*bLoop=*/false);
			return true;
		}

		// Release. A store simply stops and never consults the buff cooldown, which is what lets a player keep
		// filling the metalmind while the buff is still cooling down.
		const bool bWasStoring = bPowerHoldClassified[SlotIndex];
		bPowerHoldClassified[SlotIndex] = false;
		World->GetTimerManager().ClearTimer(PowerHoldTimers[SlotIndex]);

		if (bWasStoring)
		{
			if (Power->GetMetalmindMode() == EMetalmindMode::Storing)
			{
				Power->EndPower();
			}
			return true;
		}

		const double Now = World->GetTimeSeconds();
		const bool bWithinWindow = (Now - LastPowerTapTime[SlotIndex]) <= Power->GetMetalmindDoubleTapWindow();
		const EMetalmindMode Mode = Power->GetMetalmindMode();
		const bool bBuffRunning = (Mode == EMetalmindMode::Tapping || Mode == EMetalmindMode::Flaring);
		const float ReserveUnits = GetMetalmindReserveUnits(Slot);

		if (bBuffRunning)
		{
			// Every tap inside the window doubles the level, upgraded in place: ending and re-activating would
			// have to clear the 2 second gate the end itself just started. A tap outside it is the off switch.
			if (bWithinWindow)
			{
				const int32 NextLevel = Power->GetMetalmindLevel() + 1;
				const float NextDrain = Power->GetMetalmindDrainRateForLevel(NextLevel);

				// One second of use has to be in the tank before a level may open, so a nearly empty metalmind
				// cannot be spent on a level it could only hold for a fraction of a second.
				if (ReserveUnits < NextDrain)
				{
					LastRefusedPowerTime[SlotIndex] = World->GetTimeSeconds();
					return true;
				}

				// Stamped rather than cleared, so the next tap keeps escalating instead of starting over.
				LastPowerTapTime[SlotIndex] = Now;
				Power->BeginMetalmind(EMetalmindMode::Flaring, SlotIndex, NextLevel);
				return true;
			}

			LastPowerTapTime[SlotIndex] = -1000.0;
			Power->EndPower();
			return true;
		}

		// No cooldown to wait out: a metalmind is ready the moment its last buff ends, so the reserve is the only
		// gate on spending metal. One second of use at level 1 is the cheapest thing a tap can buy, so anything
		// under that is refused and the icon flashes.
		if (ReserveUnits < Power->GetMetalmindDrainRateForLevel(1))
		{
			LastRefusedPowerTime[SlotIndex] = World->GetTimeSeconds();
			return false;
		}

		LastPowerTapTime[SlotIndex] = Now;

		// Queue before activating: ActivateAbility needs the mode to tell a metalmind press apart from the
		// archetype's one-shot effect.
		Power->QueueMetalmindMode(EMetalmindMode::Tapping, SlotIndex, 1);

		if (!ASC->TryActivateAbility(Spec.Handle))
		{
			Power->QueueMetalmindMode(EMetalmindMode::Idle, INDEX_NONE);
			LastRefusedPowerTime[SlotIndex] = World->GetTimeSeconds();
			return false;
		}

		// The ability activates first and is told its mode second, so a refused activation never leaves a
		// metalmind running on a power the ASC turned down.
		Power->BeginMetalmind(EMetalmindMode::Tapping, SlotIndex, 1);
		return true;
		//~ End metalmind press classification
	}

	return false;
}

UCosmerePowerAbility* ACosmereCharacter::FindPowerSlotAbility(EPowerSlot Slot) const
{
	const ACosmerePlayerState* PS = GetCosmerePlayerState();
	UAbilitySystemComponent* ASC = PS ? PS->GetAbilitySystemComponent() : nullptr;
	if (!ASC)
	{
		return nullptr;
	}

	const FSpiritwebCharacterSave Build = PS->GetSelectedBuild();
	const int32 SlotIndex = (Slot == EPowerSlot::Beta) ? 1 : 0;
	if (!Build.PowerIds.IsValidIndex(SlotIndex))
	{
		return nullptr;
	}

	const FName PowerId = Build.PowerIds[SlotIndex];

	UWorld* World = GetWorld();
	UCosmereGameInstance* GameInstance = World ? World->GetGameInstance<UCosmereGameInstance>() : nullptr;
	const UCosmereCatalogSubsystem* Catalog = GameInstance ? GameInstance->GetCatalog() : nullptr;

	FPowerDefinition Row;
	if (!Catalog || !Catalog->GetPowerRow(PowerId, Row) || !Row.AbilityClass)
	{
		return nullptr;
	}

	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		UGameplayAbility* Ability = Spec.GetPrimaryInstance();
		if (!Ability)
		{
			Ability = Spec.Ability;
		}

		if (Ability && Ability->GetClass() == Row.AbilityClass)
		{
			return Cast<UCosmerePowerAbility>(Ability);
		}
	}

	return nullptr;
}

float ACosmereCharacter::GetPowerSlotCooldownFraction(EPowerSlot Slot) const
{
	const UCosmerePowerAbility* Ability = FindPowerSlotAbility(Slot);
	if (!Ability)
	{
		// Nothing granted for this slot yet, so nothing is gating a press.
		return 1.f;
	}

	// A metalmind has no row cooldown at all - CheckCooldown skips it - so drawing one would show a gate the
	// player cannot hit. The metal bar is the constraint, and the 2 second tap gate is deliberately short.
	if (IsMetalmindSlot(Slot))
	{
		return 1.f;
	}

	const float Total = Ability->GetRowCooldownSeconds();
	const UWorld* World = GetWorld();
	if (Total <= 0.f || !World)
	{
		// A row with no cooldown, or one that has never been activated and so has not resolved its row yet.
		// Either way the next press would not be refused, so the bar reads ready.
		return 1.f;
	}

	const double Elapsed = World->GetTimeSeconds() - Ability->GetLastActivatedWorldTime();
	const float Remaining = FMath::Clamp(Total - static_cast<float>(Elapsed), 0.f, Total);
	return 1.f - (Remaining / Total);
}

FName ACosmereCharacter::GetPowerSlotId(EPowerSlot Slot) const
{
	const ACosmerePlayerState* PS = GetCosmerePlayerState();
	if (!PS)
	{
		return NAME_None;
	}

	// Index 0 is EPowerSlot::Alpha and index 1 is Beta, the same pairing SetPowerSlotHeld resolves its
	// ability through, so both reads agree about which slot is which.
	const FSpiritwebCharacterSave Build = PS->GetSelectedBuild();
	const int32 SlotIndex = (Slot == EPowerSlot::Beta) ? 1 : 0;

	return Build.PowerIds.IsValidIndex(SlotIndex) ? Build.PowerIds[SlotIndex] : NAME_None;
}

float ACosmereCharacter::GetPowerSlotRefusedAge(EPowerSlot Slot) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 1.0e9f;
	}

	// The same clock the refusal was stamped from, so the age cannot drift away from the event it dates.
	return static_cast<float>(World->GetTimeSeconds() - LastRefusedPowerTime[(Slot == EPowerSlot::Beta) ? 1 : 0]);
}

void ACosmereCharacter::RegisterMatchMappingContext()
{
	if (!MatchMappingContext)
	{
		// Expected until the match input context asset exists. The hub contexts the player controller
		// registers are a separate registration and are untouched by this.
		UE_LOG(LogCosmere, Log,
			TEXT("CosmereCharacter: no MatchMappingContext set on %s, so match-specific input is not registered."),
			*GetName());
		return;
	}

	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController())
	{
		// Only the machine actually driving this pawn has a local input subsystem to register into.
		return;
	}

	const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	const UCosmereUIManager* UI = LocalPlayer ? LocalPlayer->GetSubsystem<UCosmereUIManager>() : nullptr;
	// A respawn may complete while Escape is open. Resume restores its suspended contexts.
	if (UI && UI->IsMenuOpen()) return;

	if (UEnhancedInputLocalPlayerSubsystem* Input =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		int32 ExistingPriority = 0;
		if (Input->HasMappingContext(MatchMappingContext, ExistingPriority)) return;
		Input->AddMappingContext(MatchMappingContext, MatchMappingContextPriority);
		UE_LOG(LogCosmere, Log,
			TEXT("CosmereCharacter: registered %s at priority %d for %s."),
			*MatchMappingContext->GetName(), MatchMappingContextPriority, *GetName());
	}
}

void ACosmereCharacter::ApplyBuildToPawn(const FSpiritwebCharacterSave& Build)
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UCosmereCatalogSubsystem* Catalog =
		GameInstance ? GameInstance->GetSubsystem<UCosmereCatalogSubsystem>() : nullptr;

	if (!Catalog || !Catalog->IsCatalogReady())
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereCharacter: the catalog is not loaded, so no race stats can be applied to %s."),
			*GetName());
		return;
	}

	// Resolve which race this build actually is. See the header note on hero builds: the hero row is
	// the authority there, because the manual kit stays underneath it and RaceId can be stale.
	FName RaceId = Build.RaceId;
	if (Build.IsHeroBuild())
	{
		FHeroDefinition Hero;
		if (!Catalog->GetHeroRow(Build.EquippedHeroId, Hero))
		{
			UE_LOG(LogCosmere, Warning,
				TEXT("CosmereCharacter: hero id '%s' is not in DT_Heroes, so no race can be resolved for %s."),
				*Build.EquippedHeroId.ToString(), *GetName());
			return;
		}
		RaceId = Hero.RaceId;
	}

	FRaceDefinition Race;
	if (!Catalog->GetRaceRow(RaceId, Race))
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereCharacter: race id '%s' is not in DT_Races, so no stats can be applied to %s."),
			*RaceId.ToString(), *GetName());
		return;
	}

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		// The race row moves the bases, not the live values. Everything that changes movement - a slow, a
		// metalmind, a field - registers on the modifier stack and resolves against these, so a build arriving
		// mid-buff re-derives the stack instead of overwriting it, and the buff no longer overwrites the build.
		BaseWalkSpeed = Race.RunSpeed;
		BaseJumpVelocity = Race.JumpVelocity;
		BaseGravityScale = Race.GravityScale;
		BaseAirControl = Race.AirControl;

		if (MovementModifiers && Move)
		{
			MovementModifiers->Apply();
		}

	}
	else
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereCharacter: %s has no movement component, so no movement stats were applied."),
			*GetName());
	}

	// Health is the attribute-side half. SetNumericAttributeBase deliberately does not clear
	// modifiers, which makes it the right call both for establishing the base value at spawn and for
	// a respawn reset.
	if (UCosmereAbilitySystemComponent* ASC =
			Cast<UCosmereAbilitySystemComponent>(GetAbilitySystemComponent()))
	{
		ASC->SetNumericAttributeBase(UCosmereAttributeSet::GetMaxHealthAttribute(), Race.BaseHealth);
		ASC->SetNumericAttributeBase(UCosmereAttributeSet::GetHealthAttribute(), Race.BaseHealth);

		if (ASC->GetSet<UCosmereAttributeSet>() == nullptr)
		{
			UE_LOG(LogCosmere, Warning,
				TEXT("CosmereCharacter: the ASC on %s has no UCosmereAttributeSet, so health will not read back."),
				*GetName());
		}
	}
	else
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereCharacter: %s has no ability system, so health was not set. Movement stats were applied."),
			*GetName());
	}

	bAppliedBuild = true;
	AppliedBuild = Build;

	// The verification line for this chunk. One of these must appear on the host and on the client.
	UE_LOG(LogCosmere, Log,
		TEXT("CosmereCharacter: applied build to %s - race %s, MaxWalkSpeed=%.1f, JumpZVelocity=%.1f, ")
			 TEXT("GravityScale=%.2f, AirControl=%.2f, MaxHealth=%.1f."),
		*GetName(), *RaceId.ToString(), Race.RunSpeed, Race.JumpVelocity,
		Race.GravityScale, Race.AirControl, Race.BaseHealth);

	// The modular seam. Everything downstream - weapons, powers, HUD - is driven from here, so none
	// of them depend on which path delivered the build or on how the build replicates.
	OnBuildApplied.Broadcast(AppliedBuild);
}

void ACosmereCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Cached once here rather than searched on every death. The weapon component is a subobject of
	// this pawn, so it exists by the time BeginPlay runs on the authority and on clients alike.
	CachedWeaponComponent = FindComponentByClass<UCosmereWeaponComponent>();

	// ACharacter ships the capsule on the Pawn profile, and that profile ignores the Visibility channel
	// (BaseEngine.ini). Every hitscan in the project traces Visibility - the weapon, a bot's line of
	// sight, and Division's decay reach - so a body was never what stopped one: the ray passed through
	// the player and landed on whatever stood behind it. Set here rather than in the constructor,
	// because the profile is serialized on the capsule and the Blueprint value would undo it.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	}

	// The only automatic source of metal in the game. Started here rather than on possession so it survives a
	// respawn without re-registering, and gated on authority because the reserve only exists there meaningfully.
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(MetalmindPassiveTimer, this, &ACosmereCharacter::AccrueMetalmindPassive,
			MetalmindPassiveInterval, /*bLoop=*/true);
	}
}

float ACosmereCharacter::GetHealth() const
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC)
	{
		return 0.f;
	}

	const UCosmereAttributeSet* Set = ASC->GetSet<UCosmereAttributeSet>();
	return Set ? Set->GetHealth() : 0.f;
}

float ACosmereCharacter::GetMaxHealth() const
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC)
	{
		return 0.f;
	}

	const UCosmereAttributeSet* Set = ASC->GetSet<UCosmereAttributeSet>();
	return Set ? Set->GetMaxHealth() : 0.f;
}

void ACosmereCharacter::SetDamageResistanceForSource(FName Source, float Fraction)
{
	// Add rather than FindRef-then-assign: TMap::Add replaces the value for an existing key, which is exactly
	// the "re-assert costs nothing" behaviour a per-tick metalmind needs.
	DamageResistanceBySource.Add(Source, FMath::Clamp(Fraction, 0.f, 1.f));
}

void ACosmereCharacter::ClearDamageResistanceSource(FName Source)
{
	DamageResistanceBySource.Remove(Source);
}

void ACosmereCharacter::ClearAllDamageResistance()
{
	DamageResistanceBySource.Reset();
}

void ACosmereCharacter::SetDamageResistance(float NewResistance)
{
	SetDamageResistanceForSource(FName(TEXT("Legacy")), NewResistance);
}

float ACosmereCharacter::GetDamageResistance() const
{
	// One minus the product of what each source lets through. An empty stack reads 0, which is the same
	// answer a single entry of zero would give, so an unscathed pawn and a pawn with nothing open agree.
	float ThroughFraction = 1.f;
	for (const TPair<FName, float>& Entry : DamageResistanceBySource)
	{
		ThroughFraction *= (1.f - FMath::Clamp(Entry.Value, 0.f, 1.f));
	}

	return 1.f - ThroughFraction;
}

void ACosmereCharacter::SetMassMultiplierForSource(FName Source, float Multiplier)
{
	const float Safe = FMath::Max(Multiplier, KINDA_SMALL_NUMBER);

	// Two early outs, both of which exist because a metalmind re-asserts itself ten times a second. A metal with
	// no weight effect never enters the map at all, and an unchanged number never rewrites the movement
	// component, so Pewter burning costs nothing here.
	const float* Existing = MassMultiplierBySource.Find(Source);
	if (!Existing)
	{
		if (FMath::IsNearlyEqual(Safe, 1.f))
		{
			return;
		}
	}
	else if (FMath::IsNearlyEqual(*Existing, Safe))
	{
		return;
	}

	// A neutral multiplier is a removal rather than an entry, which keeps "nothing open" and "open at 1" the same
	// state and stops the map filling with no-ops as a store and a tap trade places.
	if (FMath::IsNearlyEqual(Safe, 1.f))
	{
		MassMultiplierBySource.Remove(Source);
	}
	else
	{
		MassMultiplierBySource.Add(Source, Safe);
	}

	// Nothing has to be pushed anywhere. Mass feeds GetMaxFallSpeed directly, which Tick reads every frame, so
	// the change is live as soon as the map is written - and mass deliberately does not touch the movement
	// stack, so re-resolving it here would only rewrite four values with the numbers already in them.
}

void ACosmereCharacter::ClearMassMultiplierSource(FName Source)
{
	// Removal needs no follow-up for the same reason the add needs none: GetMaxFallSpeed is derived on read.
	MassMultiplierBySource.Remove(Source);
}

float ACosmereCharacter::GetMass() const
{
	float Multiplied = 1.f;
	for (const TPair<FName, float>& Entry : MassMultiplierBySource)
	{
		Multiplied *= FMath::Max(Entry.Value, KINDA_SMALL_NUMBER);
	}

	return BaseMass * Multiplied;
}

float ACosmereCharacter::GetMassRatio() const
{
	// Guarded rather than divided blindly: a build with no mass behind it should read as normal weight, not as a
	// divide by zero that turns every derived number into infinity.
	return (BaseMass > KINDA_SMALL_NUMBER) ? (GetMass() / BaseMass) : 1.f;
}

float ACosmereCharacter::GetFallSpeedScale() const
{
	return FMath::Pow(GetMassRatio(), FallSpeedMassExponent);
}

float ACosmereCharacter::GetMaxFallSpeed() const
{
	// Mass enters here and only here, so a stored metalmind floats down and a flared one drops. Floored so a
	// wildly light pawn still falls at all rather than hovering in place on a zero ceiling.
	return FMath::Max(BaseMaxFallSpeed * GetFallSpeedScale(), 60.f);
}

float ACosmereCharacter::GetLaunchScale() const
{
	// Floored at a hundredth of normal weight so a wildly light pawn is thrown a great deal rather than into a
	// launch velocity that overflows the movement solver.
	return 1.f / FMath::Max(GetMassRatio(), 0.01f);
}

void ACosmereCharacter::SetMeleeDamageMultiplier(float NewMultiplier)
{
	MeleeDamageMultiplier = FMath::Clamp(NewMultiplier, 0.1f, 5.f);
}

void ACosmereCharacter::RevealActors(const TArray<AActor*>& Actors, float Duration)
{
	UWorld* World = GetWorld();
	if (!World || Duration <= 0.f)
	{
		return;
	}

	const double Expiry = World->GetTimeSeconds() + Duration;

	for (AActor* Actor : Actors)
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		bool bAlreadyMarked = false;
		for (TPair<TWeakObjectPtr<AActor>, double>& Entry : RevealedUntil)
		{
			if (Entry.Key.Get() == Actor)
			{
				// Refreshed rather than duplicated, so casting twice on the same target extends one mark and the
				// list stays the size of what is actually revealed.
				Entry.Value = FMath::Max(Entry.Value, Expiry);
				bAlreadyMarked = true;
				break;
			}
		}

		if (!bAlreadyMarked)
		{
			RevealedUntil.Emplace(Actor, Expiry);
		}
	}
}

bool ACosmereCharacter::IsRevealed(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}

	// Read against the world clock rather than a tick count, so a reveal that outlives a hitch or a
	// time-dilation effect still expires when it said it would.
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	for (const TPair<TWeakObjectPtr<AActor>, double>& Entry : RevealedUntil)
	{
		if (Entry.Key.Get() == Actor)
		{
			return Now < Entry.Value;
		}
	}

	return false;
}

float ACosmereCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// Super first so anything the engine or a child Blueprint does with the damage event still runs.
	float AppliedDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	
	if (AppliedDamage <= 0.f || bIsDead)
	{
		return AppliedDamage;
	}

	// Health is server-authoritative and replicates from the PlayerState's attribute set, so a client
	// must not write it locally - Super already let its own damage events run above.
	if (!HasAuthority())
	{
		return AppliedDamage;
	}

	// Every open window scales what is left of the hit, in turn: two 50% sources leave a quarter of it rather
	// than cancelling it out entirely. There is no ceiling here or on the composed result, so enough stored
	// metal really can make a pawn untouchable - the only cap left is the 0..1 clamp on each source inside
	// GetDamageResistance, which is what stops a fraction above 1 from healing its owner.
	if (DamageResistanceBySource.Num() > 0)
	{
		AppliedDamage *= (1.f - GetDamageResistance());
	}
	
	UCosmereAbilitySystemComponent* ASC = Cast<UCosmereAbilitySystemComponent>(GetAbilitySystemComponent());
	if (!ASC)
	{
		// No ability system, so this pawn has no health to lose. Movement stats were what mattered.
		return AppliedDamage;
	}

	const UCosmereAttributeSet* Set = ASC->GetSet<UCosmereAttributeSet>();
	if (!Set)
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereCharacter: %s took damage but its ASC has no UCosmereAttributeSet."),
			*GetName());
		return AppliedDamage;
	}

	// The same loose additive mod the weapon used to apply itself, moved here so that health loss has
	// exactly one owner and death has exactly one trigger. ApplyModToAttribute is authority-gated
	// internally as well, which is why the HasAuthority check above is about intent and not safety.
	ASC->ApplyModToAttribute(
		UCosmereAttributeSet::GetHealthAttribute(),
		EGameplayModOp::Additive,
		-AppliedDamage);

	const float NewHealth = Set->GetHealth();

	UE_LOG(LogCosmere, Log,
		TEXT("CosmereCharacter: %s took %.1f damage from %s, credited to %s, and is now at %.1f health."),
		*GetName(), AppliedDamage, *GetNameSafe(DamageCauser), *GetNameSafe(EventInstigator), NewHealth);

	if (NewHealth <= 0.f)
	{
		Die(EventInstigator);
	}

	return AppliedDamage;
}

void ACosmereCharacter::Die(AController* Killer)
{
	if (bIsDead)
	{
		// Two shots landing on the same frame must not credit two kills or start two respawn timers.
		return;
	}

	bIsDead = true;

	UE_LOG(LogCosmere, Log,
		TEXT("CosmereCharacter: %s was killed by %s."),
		*GetName(), Killer ? *Killer->GetName() : TEXT("nobody (unattributed)"));

	// Credited before anything is torn down, while both PlayerStates still exist and can be scored.
	if (ACosmereGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACosmereGameMode>() : nullptr)
	{
		GameMode->RegisterDeath(GetController(), Killer);
	}
	else
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereCharacter: %s died with no Cosmere game mode, so the kill was not scored."),
			*GetName());
	}

	EnterDeadState();

	GetWorldTimerManager().SetTimer(
		RespawnTimerHandle, this, &ACosmereCharacter::Respawn, RespawnDelay, false);

	OnCosmereDied.Broadcast(Killer);
}

void ACosmereCharacter::EnterDeadState()
{
	// Input keeps arriving from a client that has not yet learned it died, so the weapon has to be
	// switched off explicitly as well as the movement - otherwise a dead player keeps firing with the
	// camera left where it fell.
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->DisableMovement();
	}

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		// Out of the world's traces, so a corpse cannot absorb a shot aimed through it.
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (CachedWeaponComponent)
	{
		CachedWeaponComponent->SetWeaponEnabled(false);
	}

	// Hidden rather than destroyed: the controller still possesses this pawn until the respawn timer
	// fires and the game mode hands it a replacement.
	SetActorHiddenInGame(true);
}

void ACosmereCharacter::Respawn()
{
	// Authority only. A client learns about the replacement pawn the same way it learned about this
	// one - by replication - so it has nothing to do here.
	if (!HasAuthority())
	{
		return;
	}

	AController* OwningController = GetController();
	ACosmereGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACosmereGameMode>() : nullptr;
	if (!OwningController || !GameMode)
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereCharacter: %s cannot respawn - it has no controller or no Cosmere game mode."),
			*GetName());
		return;
	}

	// Release the controller before this pawn goes away, otherwise the game mode would be asked to
	// possess a replacement while the controller still owns the corpse.
	OwningController->UnPossess();

	// Destroy is deferred, so calling it from inside this pawn's own timer callback is safe.
	Destroy();

	GameMode->RespawnPlayerController(OwningController);
}
