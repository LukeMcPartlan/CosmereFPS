// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/CosmereMatchHUD.h"
#include "Blueprint/WidgetTree.h"
#include "CommonInputSubsystem.h"
#include "Components/Image.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "UI/CosmereSettingsSubsystem.h"
#include "UI/CosmereUIPresentationData.h"

#include "Character/CosmereCharacter.h"
#include "Combat/CosmereWeaponComponent.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "CosmereGameInstance.h"
#include "Data/CosmereCatalogSubsystem.h"
#include "Net/CosmereGameState.h"
#include "Net/CosmerePlayerState.h"
#include "Power/CosmerePowerResourceComponent.h"
#include "Power/CosmereSurgeResourceComponent.h"

namespace
{
	/** The colour a power icon sits at when nothing has bounced. */
	const FLinearColor PowerIdleColor(1.f, 1.f, 1.f, 1.f);

	/** The colour it flashes when a press was refused. */
	const FLinearColor PowerRefusedColor(1.f, 0.15f, 0.15f, 1.f);
}

void UCosmereMatchHUD::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	TimeSinceRefresh += InDeltaTime;
	if (RefreshInterval > 0.f && TimeSinceRefresh < RefreshInterval)
	{
		return;
	}
	TimeSinceRefresh = 0.f;

	RefreshFromPawn();
	RefreshFromPowers();
	RefreshFromMatch();
}

void UCosmereMatchHUD::RefreshFromPawn()
{
	ACosmereCharacter* Character = Cast<ACosmereCharacter>(GetOwningPlayerPawn());

	const float Health = Character ? Character->GetHealth() : 0.f;
	const float MaxHealth = Character ? Character->GetMaxHealth() : 0.f;
	const float HealthFraction = (FMath::Abs(MaxHealth) > KINDA_SMALL_NUMBER)
		? FMath::Clamp(Health / MaxHealth, 0.f, 1.f)
		: 0.f;

	if (HealthBar)
	{
		HealthBar->SetPercent(HealthFraction);
	}

	SetTextIfChanged(HealthText,
		FString::Printf(TEXT("%d / %d"), FMath::RoundToInt(Health), FMath::RoundToInt(MaxHealth)));

	UCosmereWeaponComponent* Weapon = Character
		? Character->FindComponentByClass<UCosmereWeaponComponent>()
		: nullptr;

	if (!Weapon)
	{
		SetTextIfChanged(AmmoText, TEXT("-- / --"));
		if (ReloadBar)
		{
			ReloadBar->SetPercent(0.f);
		}
		return;
	}

	// No active weapon is a real state - a bot whose kit failed validation is spawned holding nothing -
	// so it gets its own text rather than being drawn as an empty magazine.
	if (!Weapon->HasActiveWeapon())
	{
		SetTextIfChanged(AmmoText, TEXT("UNARMED"));
	}
	else
	{
		SetTextIfChanged(AmmoText,
			FString::Printf(TEXT("%d / %d"), Weapon->GetActiveAmmo(), Weapon->GetActiveMagazineSize()));
	}

	if (ReloadBar)
	{
		ReloadBar->SetPercent(Weapon->IsReloading() ? FMath::Clamp(Weapon->GetReloadProgress(), 0.f, 1.f) : 0.f);
	}
}

void UCosmereMatchHUD::RefreshFromMatch()
{
	ACosmerePlayerState* PlayerState = Cast<ACosmerePlayerState>(GetOwningPlayerState());

	if (PlayerState)
	{
		SetTextIfChanged(KillsText,
			FString::Printf(TEXT("K %d / D %d"), PlayerState->GetKills(), PlayerState->GetDeaths()));
	}

	UWorld* World = GetWorld();
	ACosmereGameState* GameState = World ? World->GetGameState<ACosmereGameState>() : nullptr;
	if (!GameState)
	{
		// Before the game state replicates, or if the level is not running a Cosmere match, the pawn
		// half of the HUD still works and the match half simply stays blank.
		SetTextIfChanged(ScoreText, TEXT(""));
		SetTextIfChanged(ClockText, TEXT("--:--"));
		SetTextIfChanged(StateText, TEXT(""));
		return;
	}

	// King of the Hill scores the two sides; Free-for-All scores the individual. In Free-for-All
	// ACosmerePlayerState::AddKill raises both Kills and the inherited score together, so the kill
	// count is the score and there is no second number to read.
	if (GameState->GetMatchMode() == ECosmereMatchMode::KingOfTheHill)
	{
		SetTextIfChanged(ScoreText, FString::Printf(TEXT("A %d    B %d    /    %d"),
			GameState->GetTeamScore(ECosmereTeam::TeamA),
			GameState->GetTeamScore(ECosmereTeam::TeamB),
			GameState->GetScoreLimit()));
	}
	else if (PlayerState)
	{
		SetTextIfChanged(ScoreText, FString::Printf(TEXT("SCORE %d / %d"),
			PlayerState->GetKills(), GameState->GetScoreLimit()));
	}

	// The clock is derived on read from the replicated end time, so a negative value simply means the
	// match has not started its clock yet - clamp rather than render a negative time.
	const int32 RemainingSeconds = FMath::Max(0, GameState->GetTimeRemainingSeconds());
	SetTextIfChanged(ClockText, FString::Printf(TEXT("%02d:%02d"), RemainingSeconds / 60, RemainingSeconds % 60));

	// One state line, and death outranks the phase: a dead player needs to know they are dead before
	// they need to know the match is in warmup.
	ACosmereCharacter* Character = Cast<ACosmereCharacter>(GetOwningPlayerPawn());

	FString State;
	if (Character && !Character->IsAlive())
	{
		State = TEXT("RESPAWNING");
	}
	else
	{
		switch (GameState->GetMatchPhase())
		{
		case ECosmereMatchPhase::Warmup:
			State = TEXT("WARMUP");
			break;

		case ECosmereMatchPhase::PostMatch:
			State = GameState->GetWinningPlayerName().IsEmpty()
				? TEXT("MATCH OVER - DRAW")
				: FString::Printf(TEXT("MATCH OVER - %s"), *GameState->GetWinningPlayerName());
			break;

		default:
			break;
		}
	}

	SetTextIfChanged(StateText, State);
}

void UCosmereMatchHUD::SetTextIfChanged(UTextBlock* Field, const FString& Value)
{
	if (!Field)
	{
		return;
	}

	if (Field->GetText().ToString() != Value)
	{
		Field->SetText(FText::FromString(Value));
	}
}

void UCosmereMatchHUD::RefreshFromPowers()
{
	ACosmereCharacter* Character = Cast<ACosmereCharacter>(GetOwningPlayerPawn());

	if (!Character)
	{
		// No pawn yet - a spectator, or the frame before possession. Blanked rather than left stale, so a
		// dead player is not shown a set of powers that look ready to use.
		SetTextIfChanged(PowerAlphaText, FString());
		SetTextIfChanged(PowerBetaText, FString());

		if (PowerAlphaBar)
		{
			PowerAlphaBar->SetPercent(0.f);
		}

		if (PowerBetaBar)
		{
			PowerBetaBar->SetPercent(0.f);
		}

		if (PowerAlphaCooldownBar)
		{
			PowerAlphaCooldownBar->SetPercent(0.f);
		}

		if (PowerBetaCooldownBar)
		{
			PowerBetaCooldownBar->SetPercent(0.f);
		}

		return;
	}

	RefreshPowerSlot(0, Character, PowerAlphaText, PowerAlphaBar, PowerAlphaCooldownBar);
	RefreshPowerSlot(1, Character, PowerBetaText, PowerBetaBar, PowerBetaCooldownBar);
}

void UCosmereMatchHUD::RefreshPowerSlot(int32 SlotIndex, ACosmereCharacter* Character, UTextBlock* NameText, UProgressBar* Bar, UProgressBar* CooldownBar)
{
	if (!Character || (!NameText && !Bar && !CooldownBar))
	{
		// Both halves are optional, so an icon that has not been built yet costs nothing.
		return;
	}

	const EPowerSlot PowerSlot = (SlotIndex == 1) ? EPowerSlot::Beta : EPowerSlot::Alpha;

	// The name comes from the catalog row rather than from the id, so the icon reads what the player
	// actually equipped in the Spirit Web instead of the raw DT_Powers row name. An id with no row falls
	// back to the id itself: a power the catalog cannot describe is still a power the player is carrying.
	FString Name = TEXT("-");

	const FName PowerId = Character->GetPowerSlotId(PowerSlot);
	if (!PowerId.IsNone())
	{
		Name = PowerId.ToString();

		const UWorld* World = GetWorld();
		const UCosmereGameInstance* GameInstance = World ? World->GetGameInstance<UCosmereGameInstance>() : nullptr;
		const UCosmereCatalogSubsystem* Catalog = GameInstance ? GameInstance->GetCatalog() : nullptr;

		FPowerDefinition Row;
		if (Catalog && Catalog->GetPowerRow(PowerId, Row))
		{
			Name = Row.DisplayName.ToString();
		}
	}

	// Each slot draws the reserve its own economy actually spends: a metal its own pool, a Surge the shared
	// stormlight tank, a metalmind its slot reserve on the pawn. Spending Bendalloy must not move Steel's bar,
	// which is exactly what one shared bar behind both icons used to do.
	if (Bar)
	{
		// Each economy draws the bar it actually spends. A Surge slot spends stormlight - 100 units that nothing
		// regenerates and only a stormlight cloud puts back - so drawing a metal pool there would be a bar that
		// never moves when the power is used, which is what a lash looked like from the outside: a cost paid
		// somewhere the player cannot see.
		// PowerSlot is resolved once above and reused here; re-declaring it inside this block is what C4456
		// objects to, and this project promotes shadowing warnings to errors.
		const ACosmerePlayerState* PlayerState = Cast<ACosmerePlayerState>(GetOwningPlayerState());

		if (Character && Character->IsMetalmindSlot(PowerSlot))
		{
			Bar->SetPercent(FMath::Clamp(Character->GetMetalmindReserveFraction(PowerSlot), 0.f, 1.f));
		}
		else if (Character && Character->GetPowerSlotSource(PowerSlot) == EPowerSource::Surge)
		{
			// One stormlight reserve shared by every Surge, so both Surge slots draw the same number: spending it
			// on a lash is spending it on a step.
			const UCosmereSurgeResourceComponent* Stormlight = PlayerState ? PlayerState->GetSurgeResourceComponent() : nullptr;

			Bar->SetPercent(Stormlight ? FMath::Clamp(Stormlight->GetStormlightFraction(), 0.f, 1.f) : 0.f);
		}
		else
		{
			// Keyed by the slot's own row, which is the same DT_Powers row name that metal's ability spends
			// against, so each metal draws its own reserve rather than a bar shared with the other slot.
			const UCosmerePowerResourceComponent* Pool = PlayerState ? PlayerState->GetPowerResourceComponent() : nullptr;
			const FName MetalId = Character ? Character->GetPowerSlotId(PowerSlot) : NAME_None;

			Bar->SetPercent(Pool ? FMath::Clamp(Pool->GetMetalFraction(MetalId), 0.f, 1.f) : 0.f);
		}
	}

	// The cooldown swipe behind the icon. 1 is ready, so the bar fills as the power comes back rather than
	// draining away: a full background is the reading of "you may press this", which is exactly what the
	// refused-press flash denies. The value comes from the same activation stamp CheckCooldown gates on, so
	// the bar cannot drift out of step with whether a press would actually be accepted.
	if (CooldownBar)
	{
		CooldownBar->SetPercent(Character->GetPowerSlotCooldownFraction(PowerSlot));
	}

	if (NameText)
	{
		// A refused press is the only thing that turns an icon red, so this is a flash on input rather than
		// a standing warning that the reserve is low. The age is read against the same clock the refusal
		// was stamped from, so it cannot drift away from the event it dates.
		const float RefusedAge = Character->GetPowerSlotRefusedAge(PowerSlot);
		const bool bFlashing = RefusedAge >= 0.f && RefusedAge < PowerRefusedFlashSeconds;

		if (bPowerSlotFlashing[SlotIndex] != bFlashing)
		{
			bPowerSlotFlashing[SlotIndex] = bFlashing;
			NameText->SetColorAndOpacity(bFlashing ? PowerRefusedColor : PowerIdleColor);
		}

		SetTextIfChanged(NameText, Name);
	}
}

void UCosmereMatchHUD::NativeConstruct()
{
 Super::NativeConstruct();
 if(auto* PC=GetOwningPlayer()) PC->OnPossessedPawnChanged.AddUniqueDynamic(this,&ThisClass::PawnChanged);
 if(auto* LP=GetOwningLocalPlayer())
 {
  BoundSettings=LP->GetSubsystem<UCosmereSettingsSubsystem>();
  if(BoundSettings.IsValid()) {BoundSettings->EnsureSettings();BoundSettings->OnApplied.AddUniqueDynamic(this,&ThisClass::SettingsChanged);BoundSettings->OnPreviewChanged.AddUniqueDynamic(this,&ThisClass::SettingsChanged);}
  if(auto* Input=LP->GetSubsystem<UCommonInputSubsystem>()) Input->OnInputMethodChangedNative.AddUObject(this,&ThisClass::DeviceChanged);
 }
 Presentation=TSoftObjectPtr<UCosmereUIPresentationData>(FSoftObjectPath(TEXT("/Game/UI/Data/DA_UIPresentation.DA_UIPresentation"))).LoadSynchronous();
 PawnChanged(nullptr,GetOwningPlayerPawn());SettingsChanged();
}
void UCosmereMatchHUD::NativeDestruct()
{
 if(auto* PC=GetOwningPlayer()) PC->OnPossessedPawnChanged.RemoveDynamic(this,&ThisClass::PawnChanged);
 if(BoundPawn.IsValid()) BoundPawn->OnBuildApplied.RemoveDynamic(this,&ThisClass::BuildApplied);
 if(BoundSettings.IsValid()) {BoundSettings->OnApplied.RemoveDynamic(this,&ThisClass::SettingsChanged);BoundSettings->OnPreviewChanged.RemoveDynamic(this,&ThisClass::SettingsChanged);}
 if(auto* LP=GetOwningLocalPlayer()) if(auto* Input=LP->GetSubsystem<UCommonInputSubsystem>()) Input->OnInputMethodChangedNative.RemoveAll(this);
 BoundPawn.Reset();BoundSettings.Reset();Super::NativeDestruct();
}
void UCosmereMatchHUD::PawnChanged(APawn*,APawn* NewPawn)
{
 if(BoundPawn.IsValid()) BoundPawn->OnBuildApplied.RemoveDynamic(this,&ThisClass::BuildApplied);
 BoundPawn=Cast<ACosmereCharacter>(NewPawn);
 if(BoundPawn.IsValid()) BoundPawn->OnBuildApplied.AddUniqueDynamic(this,&ThisClass::BuildApplied);
 RefreshPresentation();TimeSinceRefresh=RefreshInterval;
}
void UCosmereMatchHUD::BuildApplied(const FSpiritwebCharacterSave&) {RefreshPresentation();TimeSinceRefresh=RefreshInterval;}
void UCosmereMatchHUD::DeviceChanged(ECommonInputType) {RefreshPresentation();}
void UCosmereMatchHUD::SettingsChanged()
{
 RefreshPresentation();
 if(!WidgetTree) return;
 float Scale=BoundSettings.IsValid()?BoundSettings->GetRuntimePreferences().TextScale:1.f;
 TArray<UWidget*> Widgets;WidgetTree->GetAllWidgets(Widgets);
 for(UWidget* W:Widgets) if(auto* T=Cast<UTextBlock>(W))
 {
  FSlateFontInfo Font=T->GetFont();TWeakObjectPtr<UTextBlock> Key(T);
  int32* Base=BaseTextSizes.Find(Key);if(!Base) Base=&BaseTextSizes.Add(Key,Font.Size);
  int32 Desired=FMath::RoundToInt(*Base*Scale);if(Font.Size!=Desired) {Font.Size=Desired;T->SetFont(Font);}
 }
}
void UCosmereMatchHUD::RefreshPresentation()
{
 UCommonInputSubsystem* Input=GetOwningLocalPlayer()?GetOwningLocalPlayer()->GetSubsystem<UCommonInputSubsystem>():nullptr;
 bool bGamepad=Input && Input->GetCurrentInputType()==ECommonInputType::Gamepad;
 if(BoundSettings.IsValid())
 {
  SetTextIfChanged(PowerAlphaBindingText,BoundSettings->GetBindingLabel(AlphaMappingName,bGamepad).ToString());
  SetTextIfChanged(PowerBetaBindingText,BoundSettings->GetBindingLabel(BetaMappingName,bGamepad).ToString());
 }
 SetTextIfChanged(SharedReserveText,TEXT("Reserve"));
 UImage* Icons[]={PowerAlphaIcon.Get(),PowerBetaIcon.Get()};
 for(int32 I=0;I<2;++I)
 {
  FName Id=BoundPawn.IsValid()?BoundPawn->GetPowerSlotId(I==0?EPowerSlot::Alpha:EPowerSlot::Beta):NAME_None;
  FCosmereUIEntryPresentation P;bool bFound=Presentation && Presentation->FindPresentation(TEXT("Power"),Id,P);
  FSoftObjectPath Path=bFound?P.Icon.ToSoftObjectPath():FSoftObjectPath();
  if(Icons[I])
  {
   if(IconPaths[I]!=Path) {IconPaths[I]=Path;Icons[I]->SetBrushFromSoftTexture(P.Icon,false);}
   const auto IconVisibility=Path.IsNull()?ESlateVisibility::Hidden:ESlateVisibility::HitTestInvisible;
   if(Icons[I]->GetVisibility()!=IconVisibility) Icons[I]->SetVisibility(IconVisibility);
  }
 }
}
