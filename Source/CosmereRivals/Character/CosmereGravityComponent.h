// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CosmereGravityComponent.generated.h"

class ACosmereCharacter;
class UCharacterMovementComponent;

/**
 * The pawn's own gravity: one latched direction, the flare level it was reached at, and the
 * re-orientation that turns a wall or a ceiling into a floor.
 *
 * Why this lives on the pawn rather than in the ability that sets it: a lashing outlives the press that
 * created it - that is the whole point, since the new floor is only useful once the hand comes off the
 * key - so the state has to survive the ability ending. It also has to be re-examined after the ability
 * is long over, because landing on a surface is what refines the direction.
 *
 * The direction is still CharacterMovementComponent::GravityDirection, not a second copy of it. This
 * component owns the policy - when to point it somewhere, when to refine it, when to drop it - and never
 * a parallel simulation. CurrentGravityDirection is only the last value this component wrote, kept so a
 * press can be compared against it without reaching through the movement component on every read.
 *
 * Camera: the view rolls with the body, about its own forward axis only. The capsule has to lie onto the
 * wall for that wall to read as a floor, and a view left world-upright would instead show the pawn sliding
 * sideways down a vertical surface - the body turns, so the picture turns with it.
 *
 * The roll is read from the capsule's own up vector (see ACosmereCharacter::GetViewRotation), which is the
 * vector PhysicsRotation is already slewing into the new frame. That is what keeps the two in step: there is
 * no second rate here that could reach the new up before or after the body does, and LashRotationRateTilt
 * below sets the pace for both by setting it for the body alone. And because a roll about the view's forward
 * axis cannot move that forward axis, where the player is already aiming survives the change untouched - the
 * world turns, the crosshair does not.
 *
 * The control rotation is never written. Only the rotation the view reports is rolled.
 *
 * Rotation: the capsule can only be brought upright in the new frame by
 * UCharacterMovementComponent::PhysicsRotation, and that runs only for one of the movement component's
 * rotation modes. A lashing therefore turns on bUseControllerDesiredRotation and turns off the pawn's
 * bUseControllerRotationYaw, so the capsule's facing is resolved in gravity space instead of being
 * overwritten with a world-space yaw every frame by FaceRotation. Both are restored exactly as found
 * when the lashing ends.
 */
UCLASS(ClassGroup = (Cosmere), meta = (BlueprintSpawnableComponent))
class COSMERERIVALS_API UCosmereGravityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCosmereGravityComponent();

	//~ Begin UActorComponent interface
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent interface

	/**
	 * Points gravity at Direction, or clears the lashing when Direction is close enough to straight down
	 * to read as the ground. Returns the level this press reached, or 0 when it cleared.
	 *
	 * Direction is where gravity should point - the aim direction, straight into the surface the player
	 * wants to stand on. Callers are expected to have paid for the level PreviewLashLevel reported.
	 */
	int32 Lash(const FVector& Direction);

	/** The level Lash would reach for Direction, without changing anything. 0 means it would clear. */
	int32 PreviewLashLevel(const FVector& Direction) const;

	/** Drops the lashing, restores world gravity, and hands the capsule's rotation settings back. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Gravity")
	void ClearLashing();

	UFUNCTION(BlueprintPure, Category = "Cosmere|Gravity")
	bool IsLashing() const { return FlareLevel > 0; }

	/** 1 for a plain lashing, 2 after one repeat, 3 after two. 0 whenever nothing is latched. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Gravity")
	int32 GetFlareLevel() const { return FlareLevel; }

	/** The direction gravity points in, in world space. World down when nothing is latched. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Gravity")
	FVector GetGravityDirection() const { return CurrentGravityDirection; }

	/** Multiplier on the pawn's gravity at the current flare level: 1, 2, 4, 8. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Gravity")
	float GetFlareGravityScale() const;

	/** Surge units the level costs. Level 1 costs the base, and every level above it doubles. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Gravity")
	float GetLashCostForLevel(int32 Level) const;

protected:
	/** A press whose direction lands this close to world down is a request to stop, not to lash. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Gravity", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float WorldDownSnapAngle = 5.f;

	/**
	 * How far off a surface's own down the lashing may be and still have that surface become the floor exactly.
	 *
	 * Wide on purpose: this is re-read every tick the pawn is standing rather than only on the frame it lands,
	 * so a lash taken at a wall from across the room settles onto the real geometry the moment the pawn touches
	 * it. The cone matches the one the player aims in, which is what keeps what they pressed and what they end
	 * up standing on from being two different surfaces.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Gravity", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float SurfaceSnapAngle = 45.f;

	/**
	 * How far a lash looks ahead for a surface to stand on, in cm.
	 *
	 * A press that finds one inside this range points gravity straight into it, so the wall the player is aiming
	 * at three metres away becomes the floor they asked for instead of whichever direction the camera happened to
	 * hold at that instant. Zero disables the search and the aim is used verbatim.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Gravity", meta = (ClampMin = "0.0"))
	float SurfaceTargetRange = 300.f;

	/** Two presses this close in direction flare one lashing rather than starting a new one. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Gravity", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float SameDirectionAngle = 5.f;

	/** Surge cost of the first press. Every repeat doubles it: 5, 10, 20, 40. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Gravity", meta = (ClampMin = "1.0"))
	float LashCostBase = 5.f;

	/**
	 * Yaw rate, in degrees per second, used while a lashing is latched. High so the capsule reaches the
	 * new up within a frame or two instead of visibly turning onto the wall.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Gravity", meta = (ClampMin = "0.0"))
	float LashRotationRateYaw = 900.f;

	/**
	 * Pitch and roll rate, in degrees per second, used while a lashing is latched. This is how fast the body
	 * actually lies onto the new floor, and the camera inherits it - a 90 degree wall at 200 takes about half
	 * a second, slow enough to read as the pawn falling into place rather than the world cutting.
	 *
	 * Zero here would not mean "do not tilt". p.PreventNonVerticalOrientationBlock is on by default and
	 * overrides a zero non-yaw rotation rate up to a flat 360 for any pawn that should remain vertical, which
	 * completes the whole tilt in a single frame - the snap this component exists to avoid.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Gravity", meta = (ClampMin = "0.0"))
	float LashRotationRateTilt = 200.f;

private:
	/** Writes Direction to the movement component and records it here. */
	void ApplyGravityDirection(const FVector& Direction);

	/**
	 * The direction a lash should actually point: Direction itself, or the normal of the surface it is aiming
	 * into when one is inside SurfaceTargetRange. Read by both Lash and PreviewLashLevel so the level a press is
	 * priced at and the direction it applies cannot disagree about which surface was picked.
	 */
	FVector ResolveSurfaceDirection(const FVector& Direction) const;

	/** Re-asserts the flare's gravity multiplier on the pawn's movement modifier stack. */
	void RefreshFlareGravityScale();

	/** Re-points gravity at the floor just landed on, when that floor sits inside SurfaceSnapAngle. */
	void RefineToLandedSurface();

	/** Remembers what a lashing has to borrow from the capsule, once, so it can be handed back. */
	void CaptureRotationSettings();
	void ApplyLashRotationSettings();
	void RestoreRotationSettings();

	/** Stands the capsule back up after a clear, handing the borrow back only once it is actually upright. */
	void RequestRightingUp();

	/** Whether the capsule's up has reached the frame it is supposed to be vertical in. */
	bool IsCapsuleUpright() const;

	UCharacterMovementComponent* GetMovement() const;
	ACosmereCharacter* GetOwnerCosmereCharacter() const;

	/** The last direction written to the movement component. */
	FVector CurrentGravityDirection = FVector(0.f, 0.f, -1.f);

	/** 0 when nothing is latched. */
	int32 FlareLevel = 0;

	bool bRotationSettingsCaptured = false;
	bool bSavedUseControllerRotationYaw = true;
	bool bSavedUseControllerDesiredRotation = false;
	float SavedRotationRateYaw = 0.f;
	float SavedRotationRatePitch = 0.f;
	float SavedRotationRateRoll = 0.f;
	bool bRightingUp = false;

	/** The stack entry this component's gravity multiplier lives under. One lashing per pawn, so fixed. */
	static const FName LashModifierSource;
};
