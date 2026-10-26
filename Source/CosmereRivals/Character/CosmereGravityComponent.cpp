// Copyright Epic Games, Inc. All Rights Reserved.

#include "Character/CosmereGravityComponent.h"

#include "Character/CosmereCharacter.h"
#include "Character/CosmereMovementModifierComponent.h"
#include "CollisionQueryParams.h"
#include "CosmereRivals.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

const FName UCosmereGravityComponent::LashModifierSource(TEXT("Gravitation"));

namespace
{
	/** True when A and B point within Degrees of each other. A zero vector never matches. */
	bool IsWithinDegrees(const FVector& A, const FVector& B, float Degrees)
	{
		const FVector NormalA = A.GetSafeNormal();
		const FVector NormalB = B.GetSafeNormal();
		if (NormalA.IsNearlyZero() || NormalB.IsNearlyZero())
		{
			return false;
		}

		return FVector::DotProduct(NormalA, NormalB) >= FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Degrees, 0.f, 180.f)));
	}
}

UCosmereGravityComponent::UCosmereGravityComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UCosmereGravityComponent::BeginPlay()
{
	Super::BeginPlay();

	CurrentGravityDirection = UCharacterMovementComponent::DefaultGravityDirection;

	if (const UCharacterMovementComponent* Movement = GetMovement())
	{
		CurrentGravityDirection = Movement->GetGravityDirection();
	}
}

void UCosmereGravityComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const UCharacterMovementComponent* Movement = GetMovement();
	if (!Movement)
	{
		return;
	}

	// Refined every tick the pawn is standing, not only on the frame it arrives on one: the floor result only
	// exists while the pawn is planted on it, and a lash taken at a wall from across the room has to settle onto
	// the geometry underfoot as soon as it touches down rather than only on the first contact of the press.
	if (Movement->IsMovingOnGround() && FlareLevel > 0)
	{
		RefineToLandedSurface();
	}

	// Clearing a lash points gravity at the world floor again but leaves the capsule lying on the wall, and
	// nothing else stands it back up: FaceRotation only rewrites the axis it owns, so the pitch and roll it was
	// handed stay put. The borrow therefore has to outlive the lash itself, and is handed back only once the
	// body has actually stood - which is also what keeps the view rolling all the way back with it.
	if (bRightingUp && IsCapsuleUpright())
	{
		bRightingUp = false;
		RestoreRotationSettings();
	}
}

int32 UCosmereGravityComponent::Lash(const FVector& Direction)
{
	// Resolved once and reused, so the level this press is priced at, the direction it writes and the direction
	// being logged are all read from the same surface.
	const FVector ResolvedDirection = ResolveSurfaceDirection(Direction);

	const int32 NewLevel = PreviewLashLevel(ResolvedDirection);
	if (NewLevel <= 0)
	{
		ClearLashing();
		return 0;
	}

	CaptureRotationSettings();

	FlareLevel = NewLevel;
	ApplyGravityDirection(ResolvedDirection);

	// Between the direction being written and the rotation rates taking hold. The direction is already the one
	// this lash decided, so a press that points at the floor underfoot can still be refused; the capsule has not
	// begun its slew, so the orientation the flight aim seeds from is still the one on screen.
	if (ACosmereCharacter* CosmereOwner = GetOwnerCosmereCharacter())
	{
		CosmereOwner->BeginLashFreeAim();
	}

	ApplyLashRotationSettings();
	RefreshFlareGravityScale();

	UE_LOG(LogCosmere, Log, TEXT("CosmereGravityComponent: lashing level %d toward %s for %s."),
		FlareLevel,
		*CurrentGravityDirection.ToCompactString(),
		*GetOwner()->GetName());

	return FlareLevel;
}

int32 UCosmereGravityComponent::PreviewLashLevel(const FVector& Direction) const
{
	// The same resolve Lash performs, so a press that is refused as "straight down" is refused because the
	// surface it resolved to is straight down, not because the raw aim happened to be.
	const FVector TargetDirection = ResolveSurfaceDirection(Direction).GetSafeNormal();
	if (TargetDirection.IsNearlyZero())
	{
		return FlareLevel;
	}

	// Straight down is the ground. Aiming back at it is how a lashing is dropped, and it costs nothing.
	if (IsWithinDegrees(TargetDirection, UCharacterMovementComponent::DefaultGravityDirection, WorldDownSnapAngle))
	{
		return 0;
	}

	// A repeat in the same direction is a flare: same floor, more pull, twice the price.
	if (FlareLevel > 0 && IsWithinDegrees(TargetDirection, CurrentGravityDirection, SameDirectionAngle))
	{
		return FlareLevel + 1;
	}

	return 1;
}

void UCosmereGravityComponent::ClearLashing()
{
	const bool bWasLashing = IsLashing();

	FlareLevel = 0;
	ApplyGravityDirection(UCharacterMovementComponent::DefaultGravityDirection);
	RequestRightingUp();
	RefreshFlareGravityScale();

	if (bWasLashing)
	{
		UE_LOG(LogCosmere, Log, TEXT("CosmereGravityComponent: lashing cleared, gravity back to world down for %s."),
			*GetOwner()->GetName());
	}
}

float UCosmereGravityComponent::GetFlareGravityScale() const
{
	return (FlareLevel < 2) ? 1.f : FMath::Pow(2.f, static_cast<float>(FlareLevel - 1));
}

float UCosmereGravityComponent::GetLashCostForLevel(int32 Level) const
{
	return (Level < 1) ? 0.f : LashCostBase * FMath::Pow(2.f, static_cast<float>(Level - 1));
}

FVector UCosmereGravityComponent::ResolveSurfaceDirection(const FVector& Direction) const
{
	const FVector AimDirection = Direction.GetSafeNormal();
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UWorld* World = GetWorld();
	if (!Character || !World || AimDirection.IsNearlyZero() || SurfaceTargetRange <= 0.f)
	{
		return Direction;
	}

	// From the eye, because that is where the player is aiming from. A trace out of the capsule's centre would
	// answer a question about a direction nobody pointed.
	const FVector TraceStart = Character->GetPawnViewLocation();
	const FVector TraceEnd = TraceStart + AimDirection * SurfaceTargetRange;

	// Static geometry only. A trace that answered with a pawn or a pickup would hand back a surface that can
	// walk away, and "I lashed onto that player" is not a floor.
	FCollisionQueryParams QueryParams(TEXT("CosmereSurfaceLash"), /*bTraceComplex=*/false, GetOwner());

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
	{
		return Direction;
	}

	const FVector SurfaceNormal = Hit.ImpactNormal.GetSafeNormal();
	if (SurfaceNormal.IsNearlyZero())
	{
		return Direction;
	}

	// Only a surface the aim is actually pointed into counts. A ray grazing along a wall three metres away hits
	// it too, and that wall's normal would swing the pawn somewhere the player never aimed - so the surface
	// under the crosshair is the only one allowed to become the floor.
	if (FVector::DotProduct(AimDirection, -SurfaceNormal) < FMath::Cos(FMath::DegreesToRadians(45.f)))
	{
		return Direction;
	}

	// Gravity points into the surface, so a surface's own down is its inverted normal.
	return -SurfaceNormal;
}

void UCosmereGravityComponent::ApplyGravityDirection(const FVector& Direction)
{
	UCharacterMovementComponent* Movement = GetMovement();
	if (!Movement)
	{
		return;
	}

	const FVector TargetDirection = Direction.GetSafeNormal();
	if (TargetDirection.IsNearlyZero())
	{
		return;
	}

	Movement->SetGravityDirection(TargetDirection);

	// Read back rather than storing the request: the movement component normalizes and may reject a
	// zero vector, and this has to agree with what is actually driving the simulation.
	CurrentGravityDirection = Movement->GetGravityDirection();
}

void UCosmereGravityComponent::RefreshFlareGravityScale()
{
	const ACosmereCharacter* Character = GetOwnerCosmereCharacter();
	UCosmereMovementModifierComponent* Modifiers = Character ? Character->GetMovementModifiers() : nullptr;
	if (!Modifiers)
	{
		return;
	}

	if (FlareLevel < 2)
	{
		// A plain lashing is not a stat change at all, so its entry comes out of the stack instead of
		// sitting there as a x1 that outlives the flare which registered it.
		Modifiers->ClearModifier(LashModifierSource, ECosmereMovementChannel::GravityScale);
		return;
	}

	Modifiers->SetModifier(LashModifierSource, ECosmereMovementChannel::GravityScale, ECosmereMovementOp::Multiply, GetFlareGravityScale());
}

void UCosmereGravityComponent::RefineToLandedSurface()
{
	if (FlareLevel <= 0)
	{
		return;
	}

	const UCharacterMovementComponent* Movement = GetMovement();
	if (!Movement || !Movement->CurrentFloor.IsWalkableFloor())
	{
		return;
	}

	const FVector SurfaceNormal = Movement->CurrentFloor.HitResult.ImpactNormal;
	if (SurfaceNormal.IsNearlyZero())
	{
		return;
	}

	// Gravity points into whatever the pawn stands on, so a surface's own down is its inverted normal.
	// Landing on a surface already inside SurfaceSnapAngle of that means the surface is this lashing's
	// floor, and snapping to it exactly is what stops the pawn sliding down a slightly-off wall.
	const FVector SurfaceDown = -SurfaceNormal;

	// Already this floor, so there is nothing to write. Without this the component would re-point the movement
	// component every tick the pawn stands on the same surface, which is a write that changes nothing.
	if (IsWithinDegrees(CurrentGravityDirection, SurfaceDown, 0.5f))
	{
		return;
	}

	if (!IsWithinDegrees(CurrentGravityDirection, SurfaceDown, SurfaceSnapAngle))
	{
		return;
	}

	ApplyGravityDirection(SurfaceDown);

	UE_LOG(LogCosmere, Verbose, TEXT("CosmereGravityComponent: landed surface refined gravity to %s for %s."),
		*CurrentGravityDirection.ToCompactString(),
		*GetOwner()->GetName());
}

void UCosmereGravityComponent::CaptureRotationSettings()
{
	if (bRotationSettingsCaptured)
	{
		return;
	}

	const ACosmereCharacter* Character = GetOwnerCosmereCharacter();
	const UCharacterMovementComponent* Movement = GetMovement();
	if (!Character || !Movement)
	{
		return;
	}

	bSavedUseControllerRotationYaw = Character->bUseControllerRotationYaw;
	bSavedUseControllerDesiredRotation = Movement->bUseControllerDesiredRotation;
	SavedRotationRateYaw = Movement->RotationRate.Yaw;
	SavedRotationRatePitch = Movement->RotationRate.Pitch;
	SavedRotationRateRoll = Movement->RotationRate.Roll;
	bRotationSettingsCaptured = true;
}

void UCosmereGravityComponent::ApplyLashRotationSettings()
{
	ACosmereCharacter* Character = GetOwnerCosmereCharacter();
	UCharacterMovementComponent* Movement = GetMovement();
	if (!Character || !Movement)
	{
		return;
	}

	// FaceRotation would rewrite the capsule's yaw in world space every frame, undoing the alignment
	// PhysicsRotation is about to apply. Turning it off hands the capsule's facing to the one path that
	// resolves it in gravity space.
	Character->bUseControllerRotationYaw = false;
	Movement->bUseControllerDesiredRotation = true;
	Movement->RotationRate.Yaw = LashRotationRateYaw;

	// The non-yaw rate is the entire transition: this is how fast PhysicsRotation slews the capsule's world
	// pitch and roll into the new frame, and the view reads the capsule back, so one number paces both. Zero
	// would not mean "do not tilt" - p.PreventNonVerticalOrientationBlock forces a zero non-yaw delta up to a
	// flat 360 for any pawn that should remain vertical, which finishes the tilt in a single frame.
	Movement->RotationRate.Pitch = LashRotationRateTilt;
	Movement->RotationRate.Roll = LashRotationRateTilt;

	// Re-lashing outranks any hand-back still in progress.
	bRightingUp = false;
}

void UCosmereGravityComponent::RestoreRotationSettings()
{
	if (!bRotationSettingsCaptured)
	{
		return;
	}

	if (ACosmereCharacter* Character = GetOwnerCosmereCharacter())
	{
		Character->bUseControllerRotationYaw = bSavedUseControllerRotationYaw;
	}

	if (UCharacterMovementComponent* Movement = GetMovement())
	{
		Movement->bUseControllerDesiredRotation = bSavedUseControllerDesiredRotation;
		Movement->RotationRate.Yaw = SavedRotationRateYaw;
		Movement->RotationRate.Pitch = SavedRotationRatePitch;
		Movement->RotationRate.Roll = SavedRotationRateRoll;
	}

	bRotationSettingsCaptured = false;
	bRightingUp = false;
}

void UCosmereGravityComponent::RequestRightingUp()
{
	if (!bRotationSettingsCaptured)
	{
		// Nothing was borrowed, so there is nothing to hand back and nothing to stand up.
		return;
	}

	if (IsCapsuleUpright())
	{
		RestoreRotationSettings();
		return;
	}

	bRightingUp = true;
}

bool UCosmereGravityComponent::IsCapsuleUpright() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return true;
	}

	// Within one degree of the frame's up. Measured against the current gravity frame rather than world up,
	// so this reads the same whether a lash is still latched or the pawn has just been handed back to world
	// gravity - and once a lash is cleared the two are the same vector anyway.
	constexpr float UprightUpDot = 0.99985f;
	const FVector FrameUp = -CurrentGravityDirection;
	return FVector::DotProduct(Owner->GetActorUpVector(), FrameUp) >= UprightUpDot;
}

UCharacterMovementComponent* UCosmereGravityComponent::GetMovement() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	return Character ? Character->GetCharacterMovement() : nullptr;
}

ACosmereCharacter* UCosmereGravityComponent::GetOwnerCosmereCharacter() const
{
	return Cast<ACosmereCharacter>(GetOwner());
}
