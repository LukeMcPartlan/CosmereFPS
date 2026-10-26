// Copyright Epic Games, Inc. All Rights Reserved.

#include "Character/CosmereAimModeTest.h"

#include "CosmereRivals.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

namespace
{
	/**
	 * The one piece of state. A plain static rather than a console variable, because it is read by the pawn's
	 * look code every frame and typed by a human once: there is nothing to serialise and nothing to tune.
	 */
	CosmereAimMode::EMode GForcedMode = CosmereAimMode::EMode::Auto;

	/** Toggles Mode against Auto, so the same command that engages a model is the one that puts it back. */
	void ToggleAimMode(CosmereAimMode::EMode Mode)
	{
		GForcedMode = GForcedMode == Mode ? CosmereAimMode::EMode::Auto : Mode;

		const FString Message = FString::Printf(TEXT("Aim mode: %s"), CosmereAimMode::ToString(GForcedMode));
		UE_LOG(LogCosmere, Log, TEXT("%s"), *Message);

		// On screen as well, because the answer is being judged by feel while the game runs.
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Yellow, Message);
		}
	}
}

CosmereAimMode::EMode CosmereAimMode::GetForcedMode()
{
	return GForcedMode;
}

void CosmereAimMode::SetForcedMode(EMode Mode)
{
	GForcedMode = Mode;
}

const TCHAR* CosmereAimMode::ToString(EMode Mode)
{
	switch (Mode)
	{
	case EMode::Standard:
		return TEXT("Standard (A: world axes, camera-manager pitch clamp, no roll)");
	case EMode::FreeAim:
		return TEXT("Full quaternion (B: free orientation, single 87 degree stop)");
	case EMode::ScreenRelative:
		return TEXT("Screen relative (C: view right/up axes, surface pitch clamp)");
	default:
		return TEXT("Auto (shipping: the pawn decides from its own frame)");
	}
}

namespace
{
	FAutoConsoleCommand GAimModeStandardCommand(
		TEXT("Cosmere.Aim.Mode.Standard"),
		TEXT("Cosmere look-model test. Forces option A: yaw about world up, pitch about world right, clamped by ")
		TEXT("the camera manager, and a roll-free view. Run again to return to Auto."),
		FConsoleCommandDelegate::CreateLambda([] { ToggleAimMode(CosmereAimMode::EMode::Standard); }));

	FAutoConsoleCommand GAimModeFreeAimCommand(
		TEXT("Cosmere.Aim.Mode.FreeAim"),
		TEXT("Cosmere look-model test. Forces option B: the free quaternion orientation - yaw about the held ")
		TEXT("reference up, pitch about the screen right, roll frozen. Works standing still, not just in flight. ")
		TEXT("Run again to return to Auto."),
		FConsoleCommandDelegate::CreateLambda([] { ToggleAimMode(CosmereAimMode::EMode::FreeAim); }));

	FAutoConsoleCommand GAimModeScreenRelativeCommand(
		TEXT("Cosmere.Aim.Mode.ScreenRelative"),
		TEXT("Cosmere look-model test. Forces option C: yaw about the view's up, pitch about the view's right, ")
		TEXT("clamped against the surface's own up. Run again to return to Auto."),
		FConsoleCommandDelegate::CreateLambda([] { ToggleAimMode(CosmereAimMode::EMode::ScreenRelative); }));
}
