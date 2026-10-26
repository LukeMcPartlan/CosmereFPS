// Copyright Epic Games, Inc. All Rights Reserved.

#include "Net/CosmerePlayerState.h"

#include "Character/CosmereCharacter.h"
#include "Abilities/GameplayAbility.h"
#include "Ability/CosmereAbilitySystemComponent.h"
#include "Ability/CosmereAttributeSet.h"
#include "CosmereRivals.h"
#include "Data/CosmereCatalogSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Net/CosmereGameMode.h"
#include "Net/CosmerePracticeGameMode.h"
#include "Net/UnrealNetwork.h"
#include "Power/CosmerePowerResourceComponent.h"
#include "Power/CosmereSurgeResourceComponent.h"
#include "Save/CosmereSaveSubsystem.h"
#include "UObject/UnrealType.h"

ACosmerePlayerState::ACosmerePlayerState()
{
	AbilitySystemComponent =
		CreateDefaultSubobject<UCosmereAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	// Replication is enabled inside UCosmereAbilitySystemComponent's own constructor, where the
	// protected UActorComponent::SetIsReplicatedByDefault is reachable. It is not callable here.

	// Mixed, not Full and not Minimal. The owning client predicts its own effects and has to receive
	// them; the other clients only need the resulting tags and cue data.
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	AttributeSet = CreateDefaultSubobject<UCosmereAttributeSet>(TEXT("AttributeSet"));

	// Player-scoped like the ASC and the attribute set. A respawn replaces the pawn, and the reserves
	// must survive it. UCosmerePowerResourceComponent's own constructor enables replication,
	// because SetIsReplicatedByDefault is protected and is not callable from here.
	PowerResourceComponent =
		CreateDefaultSubobject<UCosmerePowerResourceComponent>(TEXT("PowerResourceComponent"));

	// Stormlight is a second economy, not a second bar on the first. It never regenerates - it is refreshed only
	// by standing in a stormlight cloud - so it cannot share the per-metal pools' regen. Replication lives in
	// UCosmereSurgeResourceComponent's own constructor for the same reason as the pools above.
	SurgeResourceComponent =
		CreateDefaultSubobject<UCosmereSurgeResourceComponent>(TEXT("SurgeResourceComponent"));
}

UAbilitySystemComponent* ACosmerePlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ACosmerePlayerState::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// The ASC discovers attribute sets that are subobjects of its owner during its own
	// InitializeComponent, which has already run by this point. This check exists so that if that
	// discovery ever stops working we get one warning here, rather than health silently reading 0
	// in the match with nothing on the log to explain it.
	if (AbilitySystemComponent && AttributeSet &&
		AbilitySystemComponent->GetSet<UCosmereAttributeSet>() == nullptr)
	{
		AbilitySystemComponent->AddSpawnedAttribute(AttributeSet);
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmerePlayerState: the ASC did not auto-discover UCosmereAttributeSet; added it explicitly."));
	}

	// Verification signal for "health only, no shield layer". Enumerating the set's
	// FGameplayAttributeData properties by reflection means a stray third attribute shows up here in
	// the log, rather than only in the editor's details panel where nobody is looking during a match.
	if (AbilitySystemComponent)
	{
		int32 AttributeCount = 0;
		FString AttributeNames;
		for (TFieldIterator<FStructProperty> It(UCosmereAttributeSet::StaticClass()); It; ++It)
		{
			if (It->Struct != FGameplayAttributeData::StaticStruct())
			{
				continue;
			}

			++AttributeCount;
			AttributeNames += AttributeNames.IsEmpty()
				? FString(It->GetName())
				: FString::Printf(TEXT(", %s"), *It->GetName());
		}

		UE_LOG(LogCosmere, Log,
			TEXT("CosmerePlayerState: UCosmereAttributeSet declares %d attribute(s): %s."),
			AttributeCount, *AttributeNames);
	}
}

void ACosmerePlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACosmerePlayerState, SelectedBuild);
	DOREPLIFETIME(ACosmerePlayerState, bHasSelectedBuild);
	DOREPLIFETIME(ACosmerePlayerState, Kills);
	DOREPLIFETIME(ACosmerePlayerState, Deaths);
	DOREPLIFETIME(ACosmerePlayerState, MatchTeam);
	DOREPLIFETIME(ACosmerePlayerState, bIsBotPlayer);
}

void ACosmerePlayerState::ClientInitialize(AController* C)
{
	Super::ClientInitialize(C);
	// PossessedBy only runs on authority. A joining client must send its own profile, not the host's.
	if (!HasAuthority() && C && C->IsLocalController() && !HasSelectedBuild())
	{
		SubmitSelectedCharacterFromProfile();
	}
}

bool ACosmerePlayerState::SubmitSelectedCharacterFromProfile()
{
	const ACosmerePracticeGameMode* Hub = GetWorld()
		? GetWorld()->GetAuthGameMode<ACosmerePracticeGameMode>() : nullptr;
	if (Hub && Hub->IsFrontEndSession())
	{
		// Possession during preview is not consent to play or to grant the previously saved kit.
		return false;
	}

	// A practice session hands the local player a fixed kit rather than reading the profile, so the demo needs
	// nothing on disk. This is checked before the save subsystem on purpose: a machine with no saved
	// character returns false there, and the practice path would never be reached.
	if (const ACosmereGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACosmereGameMode>() : nullptr)
	{
		FSpiritwebCharacterSave PracticeBuild;
		if (GameMode->BuildPracticeKit(PracticeBuild))
		{
			UE_LOG(LogCosmere, Log,
				TEXT("CosmerePlayerState: %s is running the GameMode's practice kit instead of a saved character."),
				*GetPlayerName());
			ServerSubmitCharacter(PracticeBuild);
			return true;
		}
	}

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UCosmereSaveSubsystem* SaveSubsystem = GameInstance ? GameInstance->GetSubsystem<UCosmereSaveSubsystem>() : nullptr;
	if (!SaveSubsystem)
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmerePlayerState: no save subsystem on this machine, so no build can be submitted."));
		return false;
	}

	FSpiritwebCharacterSave Build;
	if (!SaveSubsystem->GetSelectedCharacter(Build))
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmerePlayerState: the local profile has no selected character to submit."));
		return false;
	}

	ServerSubmitCharacter(Build);
	return true;
}

void ACosmerePlayerState::ServerSubmitCharacter_Implementation(const FSpiritwebCharacterSave& Build)
{
	FString Reason;
	if (!ValidateBuildAgainstCatalog(Build, Reason))
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmerePlayerState: refused a build from %s - %s."), *GetPlayerName(), *Reason);
		return;
	}

	ApplyBuildOnServer(Build);
}

void ACosmerePlayerState::ApplyBuildOnServer(const FSpiritwebCharacterSave& Build)
{
	SelectedBuild = Build;
	bHasSelectedBuild = true;

	const FString HeroLabel = Build.IsHeroBuild() ? Build.EquippedHeroId.ToString() : FString(TEXT("none"));

	UE_LOG(LogCosmere, Log,
		TEXT("CosmerePlayerState: %s submitted a build (race %s, %d weapon(s), %d power(s), hero %s)."),
		*GetPlayerName(), *Build.RaceId.ToString(), Build.WeaponIds.Num(), Build.PowerIds.Num(), *HeroLabel);

	// Powers come from the same build that was just accepted, so re-submitting a kit or respawning
	// cannot leave the player holding something they are no longer carrying.
	GrantPowersFromBuild(Build);

	// OnRep does not run on the authority, so the server reaches the same point here.
	OnBuildReplicated.Broadcast(true);
}

void ACosmerePlayerState::GrantPowersFromBuild(const FSpiritwebCharacterSave& Build)
{
	if (!HasAuthority() || !AbilitySystemComponent)
	{
		return;
	}

	// Idempotent: replacing the kit must not leave a second spec of the same power behind, which would
	// make a power both activatable twice and un-clearable by a single cancel.
	AbilitySystemComponent->ClearAllAbilities();

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UCosmereCatalogSubsystem* Catalog =
		GameInstance ? GameInstance->GetSubsystem<UCosmereCatalogSubsystem>() : nullptr;

	if (!Catalog || !Catalog->IsCatalogReady())
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmerePlayerState: no catalog on this machine, so no powers were granted to %s."),
			*GetPlayerName());
		return;
	}

	for (const FName& PowerId : Build.PowerIds)
	{
		FPowerDefinition Power;
		if (!Catalog->GetPowerRow(PowerId, Power))
		{
			// ValidateBuildAgainstCatalog already resolved every id, so this only happens if the catalog
			// was reloaded underneath a live match.
			UE_LOG(LogCosmere, Warning,
				TEXT("CosmerePlayerState: power id '%s' no longer resolves in DT_Powers."),
				*PowerId.ToString());
			continue;
		}

		if (!Power.AbilityClass)
		{
			// An inert row is a documented state rather than an error: the power exists in the catalog but
			// has no ability wired to it yet, so selecting it simply does nothing.
			UE_LOG(LogCosmere, Log,
				TEXT("CosmerePlayerState: power id '%s' has no AbilityClass, so nothing was granted for it."),
				*PowerId.ToString());
			continue;
		}

		// This PlayerState is the spec's source object, so a power can reach both the pool and the build
		// that granted it from its own spec rather than from a per-ability lookup table.
		AbilitySystemComponent->GiveAbility(
			FGameplayAbilitySpec(Power.AbilityClass, 1, INDEX_NONE, this));

		UE_LOG(LogCosmere, Log,
			TEXT("CosmerePlayerState: granted '%s' (%s) to %s."),
			*PowerId.ToString(), *Power.AbilityClass->GetName(), *GetPlayerName());
	}
}

void ACosmerePlayerState::OnRep_SelectedBuild()
{
	UE_LOG(LogCosmere, Log,
		TEXT("CosmerePlayerState: build replicated in for %s (valid=%s)."),
		*GetPlayerName(), bHasSelectedBuild ? TEXT("true") : TEXT("false"));

	OnBuildReplicated.Broadcast(bHasSelectedBuild);
}

void ACosmerePlayerState::SetMatchTeam(ECosmereTeam NewTeam)
{
	MatchTeam = NewTeam;
}

void ACosmerePlayerState::SetBotPlayer(bool bNewIsBot)
{
	bIsBotPlayer = bNewIsBot;
}

void ACosmerePlayerState::AddKill()
{
	++Kills;

	// Score and Kills are the same number in Free-for-All, and Score is what the inherited
	// APlayerState machinery already replicates and notifies on.
	//
	// SetScore rather than Score += 1: APlayerState::Score is private as of UE 5.8 and is only
	// reachable through its getter / setter pair.
	SetScore(GetScore() + 1.f);
}

void ACosmerePlayerState::AddDeath()
{
	++Deaths;
}

void ACosmerePlayerState::SetBuildForBot(const FSpiritwebCharacterSave& Build)
{
	// Authority only. A bot has no owning connection, so the server assigns the kit directly rather
	// than routing through ServerSubmitCharacter - but it still goes through the same catalog
	// validation the RPC path uses, so a generated bot kit cannot be something a human would have
	// been refused.
	if (!HasAuthority())
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmerePlayerState: SetBuildForBot was called on %s without authority; ignored."),
			*GetName());
		return;
	}

	FString Reason;
	if (!ValidateBuildAgainstCatalog(Build, Reason))
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmerePlayerState: refusing a generated bot build for %s - %s."),
			*GetName(), *Reason);
		return;
	}

	ApplyBuildOnServer(Build);
}

bool ACosmerePlayerState::ApplyLocalPracticeBuild(const FSpiritwebCharacterSave& Build, FText& OutError)
{
	OutError = FText::GetEmpty();
	UWorld* World = GetWorld();
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!HasAuthority() || !World || World->GetNetMode() != NM_Standalone
		|| !World->GetAuthGameMode<ACosmerePracticeGameMode>()
		|| !Controller || !Controller->IsLocalController())
	{
		OutError = NSLOCTEXT("CosmereUI", "PracticeBuildScope", "A practice build can only be applied to the local hub player.");
		return false;
	}

	UGameInstance* Instance = World->GetGameInstance();
	const UCosmereSaveSubsystem* Save = Instance ? Instance->GetSubsystem<UCosmereSaveSubsystem>() : nullptr;
	const UCosmereCatalogSubsystem* Catalog = Instance ? Instance->GetSubsystem<UCosmereCatalogSubsystem>() : nullptr;
	FSpiritwebCharacterSave EffectiveBuild;
	if (!Save || !Save->GetSelectedCharacter(EffectiveBuild) || !Build.CharacterId.IsValid()
		|| EffectiveBuild.CharacterId != Build.CharacterId)
	{
		OutError = NSLOCTEXT("CosmereUI", "PracticeBuildUnconfirmed", "Confirm a saved character before entering practice.");
		return false;
	}
	if (!Catalog || !Catalog->IsCatalogReady())
	{
		OutError = NSLOCTEXT("CosmereUI", "PracticeCatalogMissing", "The character catalogs are not ready.");
		return false;
	}
	if (EffectiveBuild.IsHeroBuild())
	{
		FHeroDefinition Hero;
		if (!Catalog->GetHeroRow(EffectiveBuild.EquippedHeroId, Hero))
		{
			OutError = NSLOCTEXT("CosmereUI", "PracticeHeroMissing", "The selected hero is unavailable.");
			return false;
		}
		EffectiveBuild.RaceId = Hero.RaceId;
		EffectiveBuild.WeaponIds = Hero.WeaponIds;
		EffectiveBuild.PowerIds = Hero.PowerIds;
	}

	// Validate all effective slots, not just the existence of a preset hero row.
	FSpiritwebCharacterSave ValidationBuild = EffectiveBuild;
	ValidationBuild.EquippedHeroId = NAME_None;
	FString Reason;
	if (!ValidateBuildAgainstCatalog(ValidationBuild, Reason))
	{
		OutError = FText::FromString(Reason);
		UE_LOG(LogCosmereUI, Warning, TEXT("Practice build refused: %s."), *Reason);
		return false;
	}
	if (EffectiveBuild.WeaponIds[0] == EffectiveBuild.WeaponIds[1]
		|| EffectiveBuild.PowerIds[0] == EffectiveBuild.PowerIds[1])
	{
		OutError = NSLOCTEXT("CosmereUI", "PracticeDuplicateSlots", "Practice requires two distinct weapons and two distinct powers.");
		return false;
	}
	ACosmereCharacter* Character = Cast<ACosmereCharacter>(Controller->GetPawn());
	if (!Character || !Character->IsAlive())
	{
		OutError = NSLOCTEXT("CosmereUI", "PracticeBuildNoPawn", "A live practice pawn is required before applying the build.");
		return false;
	}
	Character->SetPowerSlotHeld(EPowerSlot::Alpha, false);
	Character->SetPowerSlotHeld(EPowerSlot::Beta, false);
	if (AbilitySystemComponent) AbilitySystemComponent->CancelAllAbilities();
	ApplyBuildOnServer(EffectiveBuild);
	if (!Character->HasAppliedBuild() || Character->GetAppliedBuild().CharacterId != EffectiveBuild.CharacterId)
	{
		OutError = NSLOCTEXT("CosmereUI", "PracticeBuildApplyFailed", "The confirmed character build did not reach the practice pawn.");
		return false;
	}
	UE_LOG(LogCosmereUI, Log, TEXT("Practice build ready: character %s, race %s, two weapons and two powers."),
		*EffectiveBuild.CharacterId.ToString(), *EffectiveBuild.RaceId.ToString());
	return true;
}

void ACosmerePlayerState::ResetPracticeStatistics()
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World || World->GetNetMode() != NM_Standalone
		|| !World->GetAuthGameMode<ACosmerePracticeGameMode>()) return;
	Kills = 0;
	Deaths = 0;
	SetScore(0.f);
	MatchTeam = ECosmereTeam::None;
	ForceNetUpdate();
}

bool ACosmerePlayerState::ValidateBuildAgainstCatalog(const FSpiritwebCharacterSave& Build, FString& OutReason) const
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UCosmereCatalogSubsystem* Catalog =
		GameInstance ? GameInstance->GetSubsystem<UCosmereCatalogSubsystem>() : nullptr;

	if (!Catalog || !Catalog->IsCatalogReady())
	{
		// Fail loudly rather than accepting data nothing on this server can interpret.
		OutReason = TEXT("the catalog is not loaded on the server");
		return false;
	}

	// A prebuilt hero replaces the hand-built kit, so only the hero row has to resolve. The manual
	// kit is kept underneath it so clearing the hero restores it, and need not be filled in.
	if (Build.IsHeroBuild())
	{
		FHeroDefinition Hero;
		if (!Catalog->GetHeroRow(Build.EquippedHeroId, Hero))
		{
			OutReason = FString::Printf(TEXT("hero id '%s' is not in DT_Heroes"),
				*Build.EquippedHeroId.ToString());
			return false;
		}

		return true;
	}

	FRaceDefinition Race;
	if (!Catalog->GetRaceRow(Build.RaceId, Race))
	{
		OutReason = FString::Printf(TEXT("race id '%s' is not in DT_Races"), *Build.RaceId.ToString());
		return false;
	}

	if (Build.WeaponIds.Num() != 2)
	{
		OutReason = FString::Printf(TEXT("expected 2 weapons, got %d"), Build.WeaponIds.Num());
		return false;
	}

	for (const FName& WeaponId : Build.WeaponIds)
	{
		FWeaponDefinition Weapon;
		if (!Catalog->GetWeaponRow(WeaponId, Weapon))
		{
			OutReason = FString::Printf(TEXT("weapon id '%s' is not in DT_Weapons"), *WeaponId.ToString());
			return false;
		}
	}

	if (Build.PowerIds.Num() != 2)
	{
		OutReason = FString::Printf(TEXT("expected 2 powers, got %d"), Build.PowerIds.Num());
		return false;
	}

	for (const FName& PowerId : Build.PowerIds)
	{
		FPowerDefinition Power;
		if (!Catalog->GetPowerRow(PowerId, Power))
		{
			OutReason = FString::Printf(TEXT("power id '%s' is not in DT_Powers"), *PowerId.ToString());
			return false;
		}
	}

	return true;
}
