// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CommonInputBaseTypes.h"
#include "Data/CosmereTypes.h"
#include "CosmereMatchHUD.generated.h"

class ACosmereCharacter;
class UProgressBar;
class UTextBlock;
class UImage;
class UCosmereSettingsSubsystem;
class UCosmereUIPresentationData;

/**
 * The in-match HUD: health, ammo, reload, score, kills, clock and the one state line.
 *
 * This is C++ and not a Blueprint graph on purpose. Everything it draws is already exposed through
 * BlueprintPure accessors (ACosmereCharacter::GetHealth, UCosmereWeaponComponent::GetActiveAmmo,
 * ACosmereGameState::GetTimeRemainingSeconds and so on), so a Blueprint version would be a large graph
 * of property bindings that Slate re-evaluates on every repaint. Here the read pass is throttled to
 * RefreshInterval and each field is only written when its string actually changed.
 *
 * The widget names below are matched by BindWidget at compile time, so WBP_MatchHUD must keep exactly
 * these names. CrosshairText is BindWidgetOptional because a reticle is presentation, not data: losing
 * it must not break the rest of the HUD.
 */
UCLASS()
class COSMERERIVALS_API UCosmereMatchHUD : public UUserWidget
{
	GENERATED_BODY()

public:
	//~ Begin UUserWidget interface
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	//~ End UUserWidget interface

protected:
 virtual void NativeConstruct() override;
 virtual void NativeDestruct() override;
	//~ Begin bound widgets
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> HealthText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> ReloadBar;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> AmmoText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ScoreText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> KillsText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ClockText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> StateText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CrosshairText;

	/** The power icons. Optional like the crosshair: a HUD that has not grown them yet still works. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PowerAlphaText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> PowerAlphaBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PowerBetaText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> PowerBetaBar;
 
 	/** The cooldown wipe drawn behind each icon. Full means the next press would be accepted. */
 	UPROPERTY(meta = (BindWidgetOptional))
 	TObjectPtr<UProgressBar> PowerAlphaCooldownBar;
 
 	UPROPERTY(meta = (BindWidgetOptional))
 	TObjectPtr<UProgressBar> PowerBetaCooldownBar;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UImage> PowerAlphaIcon;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UImage> PowerBetaIcon;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> PowerAlphaBindingText;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> PowerBetaBindingText;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> SharedReserveText;
 UPROPERTY(EditDefaultsOnly,Category="Cosmere|HUD") FName AlphaMappingName=TEXT("PowerAlpha");
 UPROPERTY(EditDefaultsOnly,Category="Cosmere|HUD") FName BetaMappingName=TEXT("PowerBeta");
	//~ End bound widgets

	/** How often the read-and-write pass runs, in seconds. 0 reads every frame. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|HUD", meta = (ClampMin = "0.0"))
	float RefreshInterval = 0.05f;

	/** Seconds a refused press keeps its power icon red. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|HUD", meta = (ClampMin = "0.0"))
	float PowerRefusedFlashSeconds = 0.35f;

private:
 UFUNCTION() void PawnChanged(APawn* OldPawn, APawn* NewPawn);
 UFUNCTION() void BuildApplied(const FSpiritwebCharacterSave& Build);
 UFUNCTION() void SettingsChanged();
 void DeviceChanged(ECommonInputType Type);
 void RefreshPresentation();
 TWeakObjectPtr<ACosmereCharacter> BoundPawn;
 TWeakObjectPtr<UCosmereSettingsSubsystem> BoundSettings;
 UPROPERTY(Transient) TObjectPtr<UCosmereUIPresentationData> Presentation;
 FSoftObjectPath IconPaths[2];
 TMap<TWeakObjectPtr<UTextBlock>,int32> BaseTextSizes;
	/** Health, ammo and the reload bar - everything that lives on the pawn. */
	void RefreshFromPawn();

	/** Score, kills, the clock and the state line - everything that lives on the game state. */
	void RefreshFromMatch();

	/** The two power icons: the equipped name, the shared reserve, and the flash on a refused press. */
	void RefreshFromPowers();

	/** One power icon. SlotIndex 0 is EPowerSlot::Alpha and 1 is Beta, matching the build's PowerIds. */
	void RefreshPowerSlot(int32 SlotIndex, ACosmereCharacter* Character, UTextBlock* NameText, UProgressBar* Bar, UProgressBar* CooldownBar);

	/**
	 * Writes a field only when its text actually differs.
	 *
	 * UTextBlock::SetText invalidates the widget and forces a re-layout even when the string is
	 * identical, so calling it unconditionally every tick is the difference between an idle HUD and one
	 * that re-lays-out itself 60 times a second.
	 */
	static void SetTextIfChanged(UTextBlock* Field, const FString& Value);

	/** Seconds accumulated since the last read pass. */
	float TimeSinceRefresh = 0.f;

	/**
	 * Whether each icon is currently red, so its colour is only written when it has to be.
	 *
	 * SetColorAndOpacity invalidates the widget and forces a re-layout, which is the exact cost
	 * SetTextIfChanged exists to avoid - writing the flash unconditionally every pass would hand back
	 * everything that helper buys.
	 */
	bool bPowerSlotFlashing[2] = { false, false };
};
