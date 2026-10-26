// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * TEST SCAFFOLDING - forces one of the three camera look models, so they can be compared in the same
 * situation rather than only in the one geometry each was written for.
 *
 * ACosmereCharacter picks its frame per frame from the body's orientation and from whether a lash is in
 * flight, which means the three models are normally reachable only in different states: Standard on the
 * floor, screen-relative once the capsule is off world-up, free orientation only while lashed and falling.
 * This override replaces that decision so any of them can be tried anywhere.
 *
 *
 *   Standard        yaw about world up, pitch about world right, clamped by the camera manager. The view is
 *                   taken straight from Super::GetViewRotation, so it carries no gravity-frame roll - which
 *                   is what makes it option A rather than a fourth model.
 *
 *   FreeAim         the quaternion orientation: yaw about a held reference up, pitch about the screen right
 *                   that reference implies, roll untouched. Six degrees of freedom, single pitch stop.
 *
 *   ScreenRelative  yaw about the view's up, pitch about the view's right, clamped against the surface's own
 *                   up. This is the surface-frame model the pawn already uses on a wall or a ceiling.
 *
 * Auto is the shipping behaviour and the default; the module never forces anything until a command is run.
 *
 * The mode is a local preference, not replicated state: it lives in this module's own storage and is read by
 * the locally controlled pawn's look code. In PIE the server and client share one process, so the listen
 * server's view is affected too - fine for testing, and the reason this cannot ship as-is.
 *
 * Console commands, each a toggle back to Auto when run a second time:
 *
 *   Cosmere.Aim.Mode.Standard
 *   Cosmere.Aim.Mode.FreeAim
 *   Cosmere.Aim.Mode.ScreenRelative
 */
namespace CosmereAimMode
{
	enum class EMode : uint8
	{
		/** Shipping behaviour: the pawn decides from its own geometry and flight state. */
		Auto,

		/** Option A - world-axis yaw and pitch, no gravity-frame roll. */
		Standard,

		/** Option B - free quaternion orientation about a held reference up. */
		FreeAim,

		/** Option C - screen-axis yaw and pitch in the body's surface frame. */
		ScreenRelative,
	};

	/** The mode currently forced. EMode::Auto unless a command has been run. */
	EMode GetForcedMode();

	/** Forces Mode, or clears the override. Called by the console commands; also usable from a test harness. */
	void SetForcedMode(EMode Mode);

	/** Human-readable name, for the log line each command prints. */
	const TCHAR* ToString(EMode Mode);
}
