// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CosmereScreenAimTest.h"

#include "Character/CosmereAimModeTest.h"
#include "Character/CosmereCharacter.h"
#include "Character/CosmereRealmComponent.h"
#include "Containers/Ticker.h"
#include "CosmereRivals.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// The channel the weapon actually fires on. Duplicated rather than read out of the weapon component, so
	// this file stays deletable: nothing in production may depend on it while the test is undecided.
	constexpr ECollisionChannel TestTraceChannel = ECC_Visibility;

	/** One colour per candidate, used by both the draw and the readout. */
	const FColor WeaponRayColour(230, 60, 60);
	const FColor ScreenRayColour(60, 220, 90);
	const FColor PawnEyeRayColour(80, 150, 240);
	/** The body-relative readout, which is not one of the three rays and should not borrow their colour. */
	const FColor BodyAimColour(235, 200, 70);

	/** Stable key, so the per-frame readout replaces itself instead of stacking up the screen. */
	constexpr uint64 ReadoutMessageKey = 0x5C0AE7A1;
	/** Its own key, so the body-relative angle is a line of its own and survives on its own. */
	constexpr uint64 BodyAimMessageKey = 0x5C0AE7A2;
	/** One-shot key, so running the angles command repeatedly replaces the readout instead of stacking it. */
	constexpr uint64 AimAnglesMessageKey = 0x5C0AE7A3;

	FString DescribeHit(const FHitResult* Hit)
	{
		if (!Hit || !Hit->GetActor())
		{
			return TEXT("no hit");
		}
		return FString::Printf(TEXT("%s @ %.0f cm"), *Hit->GetActor()->GetName(), Hit->Distance);
	}

	/** Pixels between a world point and the viewport centre. Negative when it cannot be projected. */
	float GetScreenOffsetPixels(APlayerController* PlayerController, const FVector& WorldLocation)
	{
		if (!PlayerController)
		{
			return -1.f;
		}

		int32 ViewportX = 0;
		int32 ViewportY = 0;
		PlayerController->GetViewportSize(ViewportX, ViewportY);
		if (ViewportX <= 0 || ViewportY <= 0)
		{
			return -1.f;
		}

		FVector2D Projected = FVector2D::ZeroVector;
		if (!PlayerController->ProjectWorldLocationToScreen(WorldLocation, Projected))
		{
			return -1.f;
		}

		const FVector2D Centre(ViewportX * 0.5f, ViewportY * 0.5f);
		return static_cast<float>(FVector2D::Distance(Projected, Centre));
	}

	/** Where a ray ends if it hit nothing, so an unobstructed ray still has a point to project. */
	FVector GetRayEndPoint(const CosmereScreenAim::FAimRay& Ray, const FHitResult& Hit, bool bHit)
	{
		return bHit ? FVector(Hit.ImpactPoint) : Ray.Origin + Ray.Direction * CosmereScreenAim::TraceDistance;
	}

	/**
	 * The whole measurement: build the three rays, trace the two that matter, log the numbers, and optionally
	 * draw them. Called once by the compare command and every frame by the overlay.
	 */
	void ReportComparison(UWorld* World, APawn* Pawn, bool bDraw, float DrawLifetime)
	{
		APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
		if (!World || !PlayerController || !Pawn)
		{
			UE_LOG(LogCosmere, Warning,
				TEXT("ScreenAim: no local player controller or pawn to measure. Run this in PIE."));
			return;
		}

		CosmereScreenAim::FAimRay WeaponRay;
		CosmereScreenAim::FAimRay ScreenRay;
		CosmereScreenAim::FAimRay EyeRay;
		CosmereScreenAim::GetWeaponPathRay(Pawn, WeaponRay);
		CosmereScreenAim::GetScreenRelativeRay(PlayerController, ScreenRay);
		CosmereScreenAim::GetPawnEyeRay(Pawn, EyeRay);

		FHitResult WeaponHit;
		FHitResult ScreenHit;
		const bool bWeaponHit = CosmereScreenAim::TraceRay(World, Pawn, WeaponRay, CosmereScreenAim::TraceDistance, WeaponHit);
		const bool bScreenHit = CosmereScreenAim::TraceRay(World, Pawn, ScreenRay, CosmereScreenAim::TraceDistance, ScreenHit);

		// The two numbers that decide the argument. The angle says how far apart the directions are; the pixel
		// offset says how far from the crosshair each ray's impact actually lands, which is the thing a player
		// sees and the thing a lashing is accused of breaking.
		const float WeaponToScreenDegrees = CosmereScreenAim::GetDirectionDeltaDegrees(WeaponRay, ScreenRay);
		const float WeaponToEyeDegrees = CosmereScreenAim::GetDirectionDeltaDegrees(WeaponRay, EyeRay);
		const float OriginApart = WeaponRay.bValid && ScreenRay.bValid
			? static_cast<float>(FVector::Dist(WeaponRay.Origin, ScreenRay.Origin))
			: -1.f;

		const float WeaponPixels = GetScreenOffsetPixels(PlayerController,
			GetRayEndPoint(WeaponRay, WeaponHit, bWeaponHit));
		const float ScreenPixels = GetScreenOffsetPixels(PlayerController,
			GetRayEndPoint(ScreenRay, ScreenHit, bScreenHit));

		// How far off the body the crosshair is pointing. Taken from the deprojected screen ray and not from the
		// weapon path, because the screen ray is the crosshair: it is the line under that pixel whatever the
		// camera is doing, which is the thing being asked about. The weapon path is reported above it for
		// contrast, and the two disagreeing is itself the finding.
		CosmereScreenAim::FBodyAim BodyAim;
		const bool bBodyAimValid = CosmereScreenAim::GetBodyRelativeAim(Pawn,
			ScreenRay.bValid ? ScreenRay.Direction : WeaponRay.Direction, BodyAim);

		UE_LOG(LogCosmere, Log,
			TEXT("ScreenAim: weapon->screen %.2f deg (eye %.2f deg), origins %.1f cm apart | weapon %s | screen %s | ")
			TEXT("offset from crosshair: weapon %.1f px, screen %.1f px | crosshair vs body: pitch %+.1f deg, ")
			TEXT("yaw %+.1f deg, off-axis %.1f deg."),
			WeaponToScreenDegrees, WeaponToEyeDegrees, OriginApart,
			*DescribeHit(bWeaponHit ? &WeaponHit : nullptr),
			*DescribeHit(bScreenHit ? &ScreenHit : nullptr),
			WeaponPixels, ScreenPixels,
			BodyAim.PitchDegrees, BodyAim.YawDegrees, BodyAim.OffAxisDegrees);

		if (bDraw)
		{
			DrawDebugLine(World, WeaponRay.Origin, WeaponRay.Origin + WeaponRay.Direction * CosmereScreenAim::TraceDistance,
				WeaponRayColour, false, DrawLifetime, 0, 1.5f);
			DrawDebugLine(World, ScreenRay.Origin, ScreenRay.Origin + ScreenRay.Direction * CosmereScreenAim::TraceDistance,
				ScreenRayColour, false, DrawLifetime, 0, 1.5f);
			DrawDebugLine(World, EyeRay.Origin, EyeRay.Origin + EyeRay.Direction * CosmereScreenAim::TraceDistance,
				PawnEyeRayColour, false, DrawLifetime, 0, 1.f);

			// A dot where each ray stops. If the picture and the weapon agree, the red dot sits under the
			// crosshair; if they do not, the gap is the bug and the readout is its size in pixels.
			if (bWeaponHit)
			{
				DrawDebugSphere(World, WeaponHit.ImpactPoint, 6.f, 8, WeaponRayColour, false, DrawLifetime);
			}
			if (bScreenHit)
			{
				DrawDebugSphere(World, ScreenHit.ImpactPoint, 6.f, 8, ScreenRayColour, false, DrawLifetime);
			}

			DrawDebugString(World, WeaponRay.Origin + FVector(0.f, 0.f, 12.f),
				FString::Printf(TEXT("weapon: %.1f deg / %.0f px"), WeaponToScreenDegrees, WeaponPixels),
				nullptr, WeaponRayColour, DrawLifetime);
		}

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(ReadoutMessageKey, 0.f, WeaponRayColour,
				FString::Printf(
					TEXT("aim  red=weapon %.1f px  green=screen %.1f px  delta %.2f deg  origin %.1f cm"),
					WeaponPixels, ScreenPixels, WeaponToScreenDegrees, OriginApart));

			// Signed, so which way the aim leans off the body is visible and not just by how much. Pitch is off
			// the body's own horizontal plane and yaw is towards its right, so on a wall or a ceiling both read
			// the way the player would describe them rather than in world axes.
			GEngine->AddOnScreenDebugMessage(BodyAimMessageKey, 0.f, BodyAimColour,
				bBodyAimValid
					? FString::Printf(
						TEXT("crosshair vs body   pitch %+.1f deg   yaw %+.1f deg   off-axis %.1f deg"),
						BodyAim.PitchDegrees, BodyAim.YawDegrees, BodyAim.OffAxisDegrees)
					: FString(TEXT("crosshair vs body   (no body basis)")));
		}
	}

	//~ Begin overlay state
	//
	// Static, because this is scaffolding: one overlay, one console toggle, no owner, and no object to keep
	// alive across a PIE session. The ticker is registered on first enable and then idles behind a bool, so
	// the cost when it is off is that bool.
	bool GScreenAimOverlayEnabled = false;
	TWeakObjectPtr<UWorld> GScreenAimOverlayWorld;
	// FTSTicker hands back its own handle type (a weak pointer to the ticker element), not FDelegateHandle.
	FTSTicker::FDelegateHandle GScreenAimOverlayTicker;

	bool TickScreenAimOverlay(float DeltaTime)
	{
		UWorld* OverlayWorld = GScreenAimOverlayWorld.Get();
		if (!GScreenAimOverlayEnabled || !OverlayWorld)
		{
			return true;
		}

		APlayerController* PlayerController = OverlayWorld->GetFirstPlayerController();
		ReportComparison(OverlayWorld, PlayerController ? PlayerController->GetPawn() : nullptr,
			true, FMath::Max(DeltaTime * 1.5f, 0.02f));
		return true;
	}
	//~ End overlay state

	//~ Begin live aim-angle readout
	//
	// Its own toggle and its own state rather than a second mode of the overlay: the overlay traces two rays every
	// frame to measure where they land, and this is one line of angles worth reading with the overlay off. Same
	// shape as the overlay otherwise - registered on first enable, then idling behind a bool.
	bool GAimAnglesEnabled = false;
	TWeakObjectPtr<UWorld> GAimAnglesWorld;
	FTSTicker::FDelegateHandle GAimAnglesTicker;

	/**
	 * One sample of the aim: the crosshair split into the pawn's own frame, the world angles of the view, and what
	 * is driving it. Called every frame by the ticker, so nothing here logs - a per-frame line would bury the log.
	 */
	void ReportAimAngles(UWorld* World, APawn* Pawn)
	{
		APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
		if (!PlayerController || !Pawn)
		{
			return;
		}

		// The crosshair itself, deprojected, so these are the angles of the line the player is looking down and not
		// of a reconstruction of it.
		CosmereScreenAim::FAimRay ScreenRay;
		CosmereScreenAim::GetScreenRelativeRay(PlayerController, ScreenRay);

		CosmereScreenAim::FBodyAim BodyAim;
		const bool bHasBody = CosmereScreenAim::GetBodyRelativeAim(Pawn, ScreenRay.Direction, BodyAim);

		// GetViewRotation rather than the control rotation: on this pawn it is what the camera actually uses, so
		// its roll is the roll on screen and its forward is the aim, free aim included.
		const FRotator ViewRotation = Pawn->GetViewRotation();
		const ACosmereCharacter* CosmereCharacter = Cast<ACosmereCharacter>(Pawn);
		const bool bFreeAim = CosmereCharacter && CosmereCharacter->HasFreeAim();

		const FString Text = FString::Printf(
			TEXT("aim  body(%s) pitch %+.1f  yaw %+.1f  off-axis %.1f  |  world pitch %+.1f  yaw %+.1f  ")
			TEXT("view roll %+.1f  |  free aim %s  |  forced %s"),
			bHasBody ? TEXT("ok") : TEXT("none"),
			BodyAim.PitchDegrees, BodyAim.YawDegrees, BodyAim.OffAxisDegrees,
			ViewRotation.Pitch, ViewRotation.Yaw, ViewRotation.Roll,
			bFreeAim ? TEXT("on") : TEXT("off"),
			CosmereAimMode::ToString(CosmereAimMode::GetForcedMode()));

		if (GEngine)
		{
			// Zero lifetime, so the key is rewritten every frame and the line vanishes the frame the ticker stops
			// feeding it. That is the whole of "stays up until it is turned off".
			GEngine->AddOnScreenDebugMessage(AimAnglesMessageKey, 0.f, BodyAimColour, Text);
		}
	}

	bool TickAimAngles(float DeltaTime)
	{
		UWorld* AnglesWorld = GAimAnglesWorld.Get();
		if (!GAimAnglesEnabled || !AnglesWorld)
		{
			return true;
		}

		APlayerController* PlayerController = AnglesWorld->GetFirstPlayerController();
		ReportAimAngles(AnglesWorld, PlayerController ? PlayerController->GetPawn() : nullptr);
		return true;
	}
	//~ End live aim-angle readout
}

bool CosmereScreenAim::GetScreenRelativeRay(APlayerController* PlayerController, FAimRay& OutRay)
{
	OutRay = FAimRay();
	if (!PlayerController)
	{
		return false;
	}

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	PlayerController->GetViewportSize(ViewportX, ViewportY);

	// Deprojected rather than read off a rotation, because deprojection is the only route back through the
	// projection the renderer used. FOV, aspect, the camera's own offset from the pawn and the roll a lashing
	// gives it are all already folded in, so this is the line under that pixel by construction - which is the
	// entire difference between this and GetPlayerViewPoint.
	if (ViewportX > 0 && ViewportY > 0 &&
		PlayerController->DeprojectScreenPositionToWorld(ViewportX * 0.5f, ViewportY * 0.5f,
			OutRay.Origin, OutRay.Direction))
	{
		OutRay.Direction = OutRay.Direction.GetSafeNormal();
		OutRay.bValid = !OutRay.Direction.IsNearlyZero();
		if (OutRay.bValid)
		{
			return true;
		}
	}

	// No viewport yet - a dedicated server, or a client still joining. The camera POV is the next best answer:
	// still a real line from a real camera, so the comparison degrades instead of inventing something.
	FRotator FallbackRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(OutRay.Origin, FallbackRotation);
	OutRay.Direction = FallbackRotation.Vector().GetSafeNormal();
	OutRay.bValid = !OutRay.Direction.IsNearlyZero();
	return OutRay.bValid;
}

bool CosmereScreenAim::GetWeaponPathRay(APawn* Pawn, FAimRay& OutRay)
{
	OutRay = FAimRay();
	if (!Pawn)
	{
		return false;
	}

	// Mirrors UCosmereWeaponComponent::PerformHitscan line for line. That is the point of this ray: it is the
	// behaviour being measured, not a reconstruction of it.
	FRotator ViewRotation = FRotator::ZeroRotator;
	if (APlayerController* PlayerController = Cast<APlayerController>(Pawn->GetController()))
	{
		PlayerController->GetPlayerViewPoint(OutRay.Origin, ViewRotation);
	}
	else
	{
		Pawn->GetActorEyesViewPoint(OutRay.Origin, ViewRotation);
	}

	OutRay.Direction = ViewRotation.Vector().GetSafeNormal();
	OutRay.bValid = !OutRay.Direction.IsNearlyZero();
	return OutRay.bValid;
}

bool CosmereScreenAim::GetPawnEyeRay(APawn* Pawn, FAimRay& OutRay)
{
	OutRay = FAimRay();
	if (!Pawn)
	{
		return false;
	}

	// GetViewRotation is virtual, so on ACosmereCharacter this is the free-aim orientation: unclamped, and at
	// whatever roll the body believes it has. Included so the readout can say which of the three follows the
	// picture when the body is on a wall.
	OutRay.Origin = Pawn->GetPawnViewLocation();
	OutRay.Direction = Pawn->GetViewRotation().Vector().GetSafeNormal();
	OutRay.bValid = !OutRay.Direction.IsNearlyZero();
	return OutRay.bValid;
}

bool CosmereScreenAim::TraceRay(UWorld* World, const APawn* Shooter, const FAimRay& Ray, float Distance, FHitResult& OutHit)
{
	OutHit = FHitResult();
	if (!World || !Ray.bValid)
	{
		return false;
	}

	const FVector TraceEnd = Ray.Origin + Ray.Direction * Distance;

	FCollisionQueryParams Params;
	Params.bTraceComplex = true;
	Params.AddIgnoredActor(Shooter);

	// Bodies on the other side of the veil are stepped over, exactly as the weapon steps over them: they are
	// neither a target nor cover, so a comparison that stopped on one would report a mismatch a shot never sees.
	FHitResult Hit;
	while (World->LineTraceSingleByChannel(Hit, Ray.Origin, TraceEnd, TestTraceChannel, Params))
	{
		if (UCosmereRealmComponent::ShareRealm(Shooter, Hit.GetActor()))
		{
			OutHit = Hit;
			return true;
		}

		Params.AddIgnoredActor(Hit.GetActor());
	}

	return false;
}

bool CosmereScreenAim::GetBodyRelativeAim(const APawn* Pawn, const FVector& AimDirection, FBodyAim& OutAim)
{
	OutAim = FBodyAim();
	if (!Pawn)
	{
		return false;
	}

	// The capsule's own axes, orthonormalised by hand. GetActorRightVector is defined as a cross product of
	// the other two, so it is only correct while forward and up are square to each other, and a pawn can carry
	// a non-uniform scale or a rotated component hierarchy. Re-projecting forward off up first is what turns
	// three hints into a frame, without which a pitched aim would be reported at the wrong azimuth.
	const FVector BodyUp = Pawn->GetActorUpVector().GetSafeNormal();
	if (BodyUp.IsNearlyZero())
	{
		return false;
	}

	const FVector RawForward = Pawn->GetActorForwardVector().GetSafeNormal();
	const FVector BodyForward =
		(RawForward - BodyUp * FVector::DotProduct(RawForward, BodyUp)).GetSafeNormal();
	if (BodyForward.IsNearlyZero())
	{
		return false;
	}

	const FVector BodyRight = FVector::CrossProduct(BodyUp, BodyForward).GetSafeNormal();
	const FVector Aim = AimDirection.GetSafeNormal();
	if (BodyRight.IsNearlyZero() || Aim.IsNearlyZero())
	{
		return false;
	}

	const float AlongForward = static_cast<float>(FVector::DotProduct(Aim, BodyForward));
	const float AlongRight = static_cast<float>(FVector::DotProduct(Aim, BodyRight));
	const float AlongUp = static_cast<float>(FVector::DotProduct(Aim, BodyUp));

	// Asin rather than a pitch difference, because pitch in this frame is elevation off the body's own
	// horizontal plane. Atan2 keeps the quadrant, so aiming behind the body reads as 180 and not as 0.
	OutAim.PitchDegrees = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(AlongUp, -1.f, 1.f)));
	OutAim.YawDegrees = FMath::RadiansToDegrees(FMath::Atan2(AlongRight, AlongForward));
	OutAim.OffAxisDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(AlongForward, -1.f, 1.f)));
	return true;
}

float CosmereScreenAim::GetDirectionDeltaDegrees(const FAimRay& A, const FAimRay& B)
{
	if (!A.bValid || !B.bValid)
	{
		return 0.f;
	}

	const float Dot = FMath::Clamp(static_cast<float>(FVector::DotProduct(A.Direction, B.Direction)), -1.f, 1.f);
	return FMath::RadiansToDegrees(FMath::Acos(Dot));
}

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GScreenAimOverlayCommand(
		TEXT("Cosmere.Aim.Test.Overlay"),
		TEXT("Cosmere screen-relative aiming test. 1 (or no argument, to toggle) draws the red weapon ray, the ")
		TEXT("green screen-relative ray and the blue pawn-eye ray every frame, with a live pixel readout. 0 stops it. ")
		TEXT("Run in PIE on the client."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				const bool bWantsOn = Args.Num() == 0 ? !GScreenAimOverlayEnabled : Args[0] != TEXT("0");
				const bool bEnable = bWantsOn && World != nullptr;

				GScreenAimOverlayEnabled = bEnable;
				GScreenAimOverlayWorld = bEnable ? World : nullptr;

				if (bEnable && !GScreenAimOverlayTicker.IsValid())
				{
					GScreenAimOverlayTicker = FTSTicker::GetCoreTicker().AddTicker(
						FTickerDelegate::CreateStatic(&TickScreenAimOverlay));
				}

				UE_LOG(LogCosmere, Log, TEXT("ScreenAim: overlay %s."), bEnable ? TEXT("on") : TEXT("off"));
			}));

	FAutoConsoleCommandWithWorldAndArgs GScreenAimCompareCommand(
		TEXT("Cosmere.Aim.Test.Compare"),
		TEXT("Cosmere screen-relative aiming test. Logs one comparison of the weapon ray, the screen-relative ray ")
		TEXT("and the pawn-eye ray, and draws them for four seconds. Run in PIE on the client."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				(void)Args;

				APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
				ReportComparison(World, PlayerController ? PlayerController->GetPawn() : nullptr, true, 4.f);
			}));

	FAutoConsoleCommandWithWorldAndArgs GAimAnglesCommand(
		TEXT("Cosmere.Aim.Angles"),
		TEXT("Cosmere aiming. 1 (or no argument, to toggle) shows a live readout of the crosshair's angle in the ")
		TEXT("pawn's own frame - pitch off the body's own horizontal plane, yaw towards the body's right, and the ")
		TEXT("total off-axis angle - alongside the view's world pitch, yaw and roll, whether quaternion free aim is ")
		TEXT("driving, and which look model is forced. It refreshes every frame and stays up until the command is ")
		TEXT("run again, or run with 0. Run in PIE."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				const bool bWantsOn = Args.Num() == 0 ? !GAimAnglesEnabled : Args[0] != TEXT("0");
				const bool bEnable = bWantsOn && World != nullptr;

				GAimAnglesEnabled = bEnable;
				GAimAnglesWorld = bEnable ? World : nullptr;

				if (bEnable && !GAimAnglesTicker.IsValid())
				{
					GAimAnglesTicker = FTSTicker::GetCoreTicker().AddTicker(
						FTickerDelegate::CreateStatic(&TickAimAngles));
				}

				if (!bEnable && GEngine)
				{
					// Cleared by hand, because nothing is left to overwrite the line on the next frame.
					GEngine->RemoveOnScreenDebugMessage(AimAnglesMessageKey);
				}

				UE_LOG(LogCosmere, Log, TEXT("AimAngles: readout %s."), bEnable ? TEXT("on") : TEXT("off"));
			}));
}
