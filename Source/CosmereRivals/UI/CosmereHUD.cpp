// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/CosmereHUD.h"

#include "Blueprint/UserWidget.h"
#include "CosmereRivals.h"
#include "GameFramework/PlayerController.h"
#include "Character/CosmereCharacter.h"
#include "Character/CosmereRealmComponent.h"
#include "Character/CosmereTinSensesComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Net/CosmereBotController.h"
#include "Net/CosmereGameMode.h"
#include "UI/CosmereMatchHUD.h"

ACosmereHUD::ACosmereHUD()
{
	HUDWidgetClass = TSoftClassPtr<UCosmereMatchHUD>(
		FSoftObjectPath(TEXT("/Game/UI/WBP_MatchHUD.WBP_MatchHUD_C")));
}

void ACosmereHUD::DrawHUD()
{
	// Gate before Super so Blueprint HUD drawing cannot leak into a front-end preview either.
	if (!bGameplayHUDEnabled) return;

	Super::DrawHUD();

	// The same test BeginPlay makes before it creates the widget: only a machine that owns this player has a
	// viewport at all.
	if (!PlayerOwner || !PlayerOwner->IsLocalPlayerController())
	{
		return;
	}

	UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState)
	{
		return;
	}

	// The local camera. A point behind the player has to be rejected before it is projected, because the
	// projection has no screen position to give it and would put a bar on the wrong side of the screen.
	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerOwner->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ViewDirection = ViewRotation.Vector();

	// The local viewer, and which side of the veil they are on. A bar over a body is information about that
	// body, and a player standing in Shadesmar is in a different room from everyone else: a target's health
	// drawn through a wall they have already stepped through would hand back the one advantage the trip costs
	// them. Read off the local pawn's replicated realm rather than off the power that set it, so the suppression
	// follows the state that actually decides what can be seen.
	ACosmereCharacter* LocalPawn = Cast<ACosmereCharacter>(GetOwningPawn());
	const UCosmereRealmComponent* LocalRealm = LocalPawn ? LocalPawn->GetRealmComponent() : nullptr;
	const bool bViewerPhased = LocalRealm && LocalRealm->IsInShadesmar();

	// Sized in pixels rather than world units: a bar is a screen-space readout, so it is not meant to shrink
	// with distance the way a world-space widget would.
	constexpr float BarWidth = 70.f;
	constexpr float BarHeight = 7.f;
	constexpr float BarHeadroom = 30.f;

	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		ACosmereCharacter* Character = PlayerState ? Cast<ACosmereCharacter>(PlayerState->GetPawn()) : nullptr;
		if (!Character || !Character->IsAlive())
		{
			continue;
		}

		// Idle bots only: an opponent that is still fighting does not get a bar, and neither does the pawn the
		// local player is occupying. And nothing at all while the local viewer is phased: a body they cannot
		// reach, shoot or be seen by must not be readable off the screen either.
		const ACosmereBotController* Bot = Cast<ACosmereBotController>(Character->GetController());
		if (bViewerPhased || !Bot || !Bot->IsIdle() || Character == LocalPawn)
		{
			continue;
		}

		const float MaxHealth = Character->GetMaxHealth();
		const float HealthFraction = (FMath::Abs(MaxHealth) > KINDA_SMALL_NUMBER)
			? FMath::Clamp(Character->GetHealth() / MaxHealth, 0.f, 1.f)
			: 0.f;

		const FVector HeadWorld = Character->GetActorLocation()
			+ FVector(0.f, 0.f, Character->GetSimpleCollisionHalfHeight() + BarHeadroom);

		if ((HeadWorld - ViewLocation).Dot(ViewDirection) <= 0.f)
		{
			continue;
		}

		const FVector Projected = Project(HeadWorld, /*bClampToZeroPlane=*/false);
		const float Left = Projected.X - (BarWidth * 0.5f);

		// A plate behind the trough, so the bar still reads against whatever the bot is standing in front of.
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), Left - 1.f, Projected.Y - 1.f, BarWidth + 2.f, BarHeight + 2.f);
		DrawRect(FLinearColor(0.06f, 0.06f, 0.06f, 0.9f), Left, Projected.Y, BarWidth, BarHeight);
		DrawRect(FLinearColor(0.85f, 0.16f, 0.16f, 0.95f), Left, Projected.Y, BarWidth * HealthFraction, BarHeight);
	}

	// Second pass: everything this player has revealed. Kept separate from the loop above because it
	// answers the opposite question - a health bar is about what is already in front of you, a reveal is
	// about what is not. Read off the local pawn, because a reveal is registered there and never replicated.
	if (!LocalPawn)
	{
		return;
	}

	// Sized in pixels like the health bar, and wide enough to bracket a pawn without hiding it.
	constexpr float RevealSize = 36.f;
	constexpr float RevealThickness = 2.f;
	// Red, not the old cyan. Both reasons to bracket somebody are hostile information - seen with tin, or
	// heard through a wall - and one colour for both is what keeps a player from learning two.
	const FLinearColor MarkerColour(0.95f, 0.18f, 0.18f, 0.95f);

	// Tin adds a second reason to bracket somebody: an unbroken line of sight, which is the one case where the
	// mark is a thing the player can already see. A bracket drawn through a wall is a heard noise and nothing
	// else - the reveal list is the only path that reaches around geometry.
	const UCosmereTinSensesComponent* TinSenses = LocalPawn->GetTinSensesComponent();

	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		ACosmereCharacter* Other = PlayerState ? Cast<ACosmereCharacter>(PlayerState->GetPawn()) : nullptr;
		if (!Other || Other == LocalPawn || !Other->IsAlive())
		{
			continue;
		}

		const bool bHeard = LocalPawn->IsRevealed(Other);
		const bool bSeenWithTin = TinSenses && TinSenses->IsVisibleWithTin(Other);
		if (!bHeard && !bSeenWithTin)
		{
			continue;
		}

		const FVector MarkerWorld = Other->GetActorLocation()
			+ FVector(0.f, 0.f, Other->GetSimpleCollisionHalfHeight());

		// Same behind-camera rejection the health bar makes, for the same reason: there is no screen point
		// behind the player, and projecting one would draw the marker on the wrong side of the screen.
		if ((MarkerWorld - ViewLocation).Dot(ViewDirection) <= 0.f)
		{
			continue;
		}

		const FVector Projected = Project(MarkerWorld, /*bClampToZeroPlane=*/false);
		const float Left = Projected.X - (RevealSize * 0.5f);
		const float Top = Projected.Y - (RevealSize * 0.5f);

		// Four edges rather than a filled box, so the marked pawn stays readable through the bracket.
		DrawRect(MarkerColour, Left, Top, RevealSize, RevealThickness);
		DrawRect(MarkerColour, Left, Top + RevealSize - RevealThickness, RevealSize, RevealThickness);
		DrawRect(MarkerColour, Left, Top, RevealThickness, RevealSize);
		DrawRect(MarkerColour, Left + RevealSize - RevealThickness, Top, RevealThickness, RevealSize);
	}
}

void ACosmereHUD::BeginPlay()
{
	Super::BeginPlay();

	const ACosmereGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ACosmereGameMode>() : nullptr;
	if (Mode && Mode->IsFrontEndSession()) bGameplayHUDEnabled = false;
	if (bGameplayHUDEnabled) EnsureGameplayWidget();
}

void ACosmereHUD::SetGameplayHUDEnabled(bool bEnabled)
{
	const bool bChanged = bGameplayHUDEnabled != bEnabled;
	bGameplayHUDEnabled = bEnabled;
	if (bEnabled)
	{
		EnsureGameplayWidget();
	}
	else if (HUDWidget)
	{
		// Off the viewport entirely: the existing NativeTick polling does not run behind a menu.
		HUDWidget->RemoveFromParent();
		HUDWidget = nullptr;
	}
	if (bChanged)
	{
		UE_LOG(LogCosmereUI, Log, TEXT("Gameplay HUD %s for %s."),
			bEnabled ? TEXT("enabled") : TEXT("suppressed"), *GetName());
	}
}

void ACosmereHUD::EnsureGameplayWidget()
{
	if (!bGameplayHUDEnabled || HUDWidget) return;

	// Only a machine that owns this player actually draws. A dedicated server has no viewport, and
	// PlayerOwner is null on a HUD that was never attached to a controller.
	if (!PlayerOwner || !PlayerOwner->IsLocalPlayerController())
	{
		return;
	}

	UClass* WidgetClass = HUDWidgetClass.LoadSynchronous();
	if (!WidgetClass || !WidgetClass->IsChildOf(UCosmereMatchHUD::StaticClass()))
	{
		// A loud failure on purpose: a HUD that silently never appears is far harder to diagnose than a
		// log line naming the asset that would not resolve.
		UE_LOG(LogCosmere, Error,
			TEXT("CosmereHUD: HUD widget class '%s' did not resolve to a UCosmereMatchHUD - no HUD will be drawn."),
			*HUDWidgetClass.ToString());
		return;
	}

	HUDWidget = CreateWidget<UCosmereMatchHUD>(PlayerOwner, WidgetClass);
	if (!HUDWidget)
	{
		UE_LOG(LogCosmere, Error, TEXT("CosmereHUD: CreateWidget failed for '%s'."), *HUDWidgetClass.ToString());
		return;
	}

	if (!HUDWidget->AddToPlayerScreen())
	{
		UE_LOG(LogCosmereUI, Error, TEXT("CosmereHUD: the match widget could not be added to the local player's screen."));
		HUDWidget = nullptr;
		return;
	}

	UE_LOG(LogCosmere, Log, TEXT("CosmereHUD: match HUD created for %s."), *PlayerOwner->GetName());
}

void ACosmereHUD::EndPlay(const EEndPlayReason::Type Reason)
{
 if(HUDWidget) HUDWidget->RemoveFromParent();HUDWidget=nullptr;
 Super::EndPlay(Reason);
}
