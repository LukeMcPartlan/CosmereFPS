// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class APlayerController;
class APawn;
class UWorld;
struct FHitResult;

/**
 * TEST SCAFFOLDING - screen-relative aiming, in isolation.
 *
 * Nothing in the game calls any of this. It exists so the aiming question can be answered by measurement
 * before production code moves: UCosmereWeaponComponent::PerformHitscan fires along
 * APlayerController::GetPlayerViewPoint, which is the camera manager's POV, and while a lashing owns the
 * camera that ray is not guaranteed to be the line the crosshair sits on.
 *
 * Three definitions of "where I am aiming" are provided, and they differ in exactly one thing - how the
 * direction is derived:
 *
 *   weapon path  - GetPlayerViewPoint (camera manager POV). Today's behaviour. Measured, not approximated.
 *   screen       - the viewport centre deprojected through the live projection. The only one that goes
 *                  through the projection the renderer actually used, so it is the ray under that pixel by
 *                  construction rather than by agreement, whatever the FOV, aspect, camera offset or
 *                  camera roll happen to be.
 *   pawn eye     - GetPawnViewLocation plus GetViewRotation. The free-aim orientation, unclamped, with the
 *                  roll the body says it has. Included because a lashing makes it and the control rotation
 *                  disagree, and it is useful to see which of the three follows the picture.
 *
 * The verdict metric is pixels, not degrees: each ray is traced, its hit point is projected back to the
 * screen, and the distance from the viewport centre is reported. A shot that is aimed where the crosshair is
 * reads 0 px; anything else is the size of the lie.
 *
 * Console commands (run in PIE):
 *   Cosmere.Aim.Test.Overlay [0|1]   live per-frame draw of all three rays plus an on-screen readout
 *   Cosmere.Aim.Test.Compare         one logged comparison, rays drawn for four seconds
 *
 * If screen-relative aiming turns out to be the right answer, this file's GetScreenRelativeRay body is the
 * whole feature: PerformHitscan replaces its GetPlayerViewPoint block with it.
 */
namespace CosmereScreenAim
{
	/** One candidate aim line: where it starts, and which way it points. */
	struct FAimRay
	{
		bool bValid = false;
		FVector Origin = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
	};

	/**
	 * Where an aim direction sits in the pawn's own frame - the answer to "how far off my body am I aiming",
	 * split into the two angles a player actually feels instead of one combined number. Measured against the
	 * capsule's axes rather than the gravity component's, because the capsule is what the mesh and the camera
	 * are bolted to, so it is the body the picture is attached to when the body is lying on a wall.
	 */
	struct FBodyAim
	{
		/** Azimuth about the body's up. Positive is towards the body's right. */
		float YawDegrees = 0.f;
		/** Elevation off the body's own horizontal plane. Positive is towards the body's up. */
		float PitchDegrees = 0.f;
		/** Total angle between the aim and the body's forward. Pitch and yaw folded back into one. */
		float OffAxisDegrees = 0.f;
	};

	/** How far the comparison traces run. Matches UCosmereWeaponComponent::MaxTraceDistance. */
	inline constexpr float TraceDistance = 20000.f;

	/**
	 * The ray the crosshair actually points down: the viewport centre deprojected through the live projection.
	 * Returns false only when there is no controller and no camera to fall back on.
	 */
	bool GetScreenRelativeRay(APlayerController* PlayerController, FAimRay& OutRay);

	/** The ray UCosmereWeaponComponent::PerformHitscan fires along today. */
	bool GetWeaponPathRay(APawn* Pawn, FAimRay& OutRay);

	/** Pawn eye location along the pawn's own view rotation, unclamped and including its roll. */
	bool GetPawnEyeRay(APawn* Pawn, FAimRay& OutRay);

	/**
	 * Traces one ray on the weapon's channel, skipping bodies on the other side of the veil for the same reason
	 * the weapon does, so a mismatch reported here is one a shot would actually see.
	 */
	bool TraceRay(UWorld* World, const APawn* Shooter, const FAimRay& Ray, float Distance, FHitResult& OutHit);

	/** Angle between two ray directions, in degrees. Zero when either is invalid. */
	float GetDirectionDeltaDegrees(const FAimRay& A, const FAimRay& B);

	/** Splits an aim direction into the pawn's own frame. False when the body has no usable basis. */
	bool GetBodyRelativeAim(const APawn* Pawn, const FVector& AimDirection, FBodyAim& OutAim);
}
