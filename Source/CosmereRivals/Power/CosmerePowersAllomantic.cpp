// Copyright Epic Games, Inc. All Rights Reserved.

#include "Power/CosmerePowersAllomantic.h"

#include "Character/CosmereCharacter.h"
#include "Character/CosmereInvestitureComponent.h"
#include "Character/CosmereRealmComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Net/CosmereGameState.h"
#include "TimerManager.h"

//~ Begin Iron

UCosmereAllomanticIronAbility::UCosmereAllomanticIronAbility()
{
	Range = 3500.f;
	Damage = 0.f;
	Impulse = -1400.f;

	// Held, because a pull is something you lean on rather than a single blow. The Strike archetype reads
	// this and keeps the ability alive until the input layer releases it.
	bChannelWhileHeld = true;
	ChannelTickInterval = 0.4f;
}

//~ End Iron

//~ Begin Steel

UCosmereAllomanticSteelAbility::UCosmereAllomanticSteelAbility()
{
	Range = 3500.f;
	Damage = 0.f;
	Impulse = 1400.f;
	bChannelWhileHeld = true;
	ChannelTickInterval = 0.4f;
}

//~ End Steel

//~ Begin Tin

UCosmereAllomanticTinAbility::UCosmereAllomanticTinAbility()
{
	// A slow burn: the DT_Powers row pays half a unit a second, which is around a light out of a full pool and
	// a cost the regen delay cannot outrun. Everything above level 1 is charged by the ladder itself.
	MaxTinLevel = 4;
	FlareSeconds = 4.f;
	FlareSurchargePerSecond = 0.5f;
	bSenseLevelFromMetalmind = false;
}

//~ End Tin

//~ Begin Pewter

UCosmereAllomanticPewterAbility::UCosmereAllomanticPewterAbility()
{
	Duration = 8.f;
	SpeedMultiplier = 1.35f;
	JumpMultiplier = 1.2f;
	DamageResistance = 0.35f;
	AirControlBonus = 0.2f;
}

//~ End Pewter

//~ Begin Zinc

UCosmereAllomanticZincAbility::UCosmereAllomanticZincAbility()
{
	Range = 3000.f;
	Damage = 0.f;

	// Nothing in this build reacts to an emotion, so a riot is spent as a pull: the target comes to you.
	Impulse = -1000.f;
}

//~ End Zinc

//~ Begin Brass

UCosmereAllomanticBrassAbility::UCosmereAllomanticBrassAbility()
{
	Range = 3000.f;
	Damage = 0.f;
	SlowMultiplier = 0.45f;
	SlowDuration = 5.f;
}

//~ End Brass

//~ Begin Copper

UCosmereAllomanticCopperAbility::UCosmereAllomanticCopperAbility()
{
	// A cloud is cheap to hold, because its whole job is to be up before anything happens. The surcharge buys
	// reach rather than concealment: a flared copper covers four times the ground of a level 1 cloud.
	FlareSurchargePerSecond = 0.1f;

	// One second sooner than bronze's, so a copper who starts burning a moment late still gets covered first.
	FlareSeconds = 5.f;
}

void UCosmereAllomanticCopperAbility::ApplyBurnLevel(int32 NewLevel)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FTimerManager& Timers = World->GetTimerManager();

	if (NewLevel <= 0)
	{
		Timers.ClearTimer(CloudTimer);
		DropCloud();
		return;
	}

	// The flare step only fires every FlareSeconds, which is far too slow for a shroud that lapses after one
	// second. So the cloud re-asserts itself on its own clock, and the burn level only decides how big it is.
	AssertCloud();

	if (!Timers.IsTimerActive(CloudTimer))
	{
		Timers.SetTimer(CloudTimer, this, &UCosmereAllomanticCopperAbility::AssertCloud, ShieldInterval, true);
	}
}

void UCosmereAllomanticCopperAbility::AssertCloud()
{
	ACosmereCharacter* Character = GetOwnerCosmereCharacter();
	const int32 Level = GetBurnLevel();
	if (!Character || Level <= 0)
	{
		return;
	}

	const FName Source = GetModifierSource();
	const float ReachSquared = FMath::Square(GetScaledRadius(ShieldRadius));
	const FVector Origin = Character->GetActorLocation();

	// Drop the dead before trusting the list. A pawn that left a match must not keep a wall standing on a body
	// nobody can clear, which is exactly what an unchecked list would leave behind.
	Covered.RemoveAll([](const TWeakObjectPtr<ACosmereCharacter>& Entry) { return !Entry.IsValid(); });

	// The burner stands inside their own cloud - that is what makes copper a defence and not just a curtain.
	TArray<ACosmereCharacter*> Targets;
	Targets.Add(Character);

	if (UWorld* World = GetWorld())
	{
		if (AGameStateBase* GameState = World->GetGameState())
		{
			for (APlayerState* Entry : GameState->PlayerArray)
			{
				ACosmereCharacter* Other = Entry ? Cast<ACosmereCharacter>(Entry->GetPawn()) : nullptr;
				if (!Other || Other == Character)
				{
					continue;
				}

				if (FVector::DistSquared(Origin, Other->GetActorLocation()) > ReachSquared)
				{
					continue;
				}

				Targets.Add(Other);
			}
		}
	}

	for (ACosmereCharacter* Target : Targets)
	{
		UCosmereInvestitureComponent* Investiture = Target->GetInvestitureComponent();
		if (!Investiture)
		{
			continue;
		}

		Investiture->SetCopperShroud(Source, Level, ShroudSeconds);

		Covered.AddUnique(Target);
	}

	// The seal is the burner's alone, and it goes up through the standard immunity pathway so that every invested
	// power - the ones that exist and the ones that do not yet - asks one question about them, rather than each
	// keeping its own list of what copper stops.
	if (UCosmereInvestitureComponent* Self = Character->GetInvestitureComponent())
	{
		Self->SetInvestitureImmunity(Source);
	}
}

void UCosmereAllomanticCopperAbility::DropCloud()
{
	const FName Source = GetModifierSource();

	for (const TWeakObjectPtr<ACosmereCharacter>& Entry : Covered)
	{
		ACosmereCharacter* Target = Entry.Get();
		UCosmereInvestitureComponent* Investiture = Target ? Target->GetInvestitureComponent() : nullptr;
		if (!Investiture)
		{
			continue;
		}

		// Keyed by this burner's own source name, so a second copper's cloud on the same body survives this one
		// ending. Nothing here may be keyed by the metal: two coppers burning at once are two clouds.
		Investiture->ClearCopperShroud(Source);
	}

	Covered.Reset();

	// Taken back off the burner whether or not they were in the list - the pawn outlives the ability, and the
	// source name is this burner's own, so a second copper's seal on them is untouched.
	if (ACosmereCharacter* Character = GetOwnerCosmereCharacter())
	{
		if (UCosmereInvestitureComponent* Self = Character->GetInvestitureComponent())
		{
			Self->ClearInvestitureImmunity(Source);
		}
	}
}

//~ End Copper

//~ Begin Bronze

UCosmereAllomanticBronzeAbility::UCosmereAllomanticBronzeAbility()
{
	// A seer holds bronze up for as long as they can afford it. The surcharge buys reach, not detection: a
	// level 1 bronze already finds a level 1 copper, and only the flare above that burns through.
	FlareSurchargePerSecond = 0.1f;
	FlareSeconds = 5.f;
}

void UCosmereAllomanticBronzeAbility::ApplyBurnLevel(int32 NewLevel)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FTimerManager& Timers = World->GetTimerManager();

	if (NewLevel <= 0)
	{
		Timers.ClearTimer(ScanTimer);
		return;
	}

	ScanForBurners();

	if (!Timers.IsTimerActive(ScanTimer))
	{
		Timers.SetTimer(ScanTimer, this, &UCosmereAllomanticBronzeAbility::ScanForBurners, ScanInterval, true);
	}
}

void UCosmereAllomanticBronzeAbility::ScanForBurners()
{
	ACosmereCharacter* Seer = GetOwnerCosmereCharacter();
	const int32 Level = GetBurnLevel();
	if (!Seer || Level <= 0)
	{
		return;
	}

	// Whether a pawn is burning is a number on that pawn's ability instance, and an instance only exists on its
	// owner's machine - a client-side sweep would find nobody. The authority owns every level, so it is the only
	// side that can answer, and it hands the answer back through the game state.
	if (!Seer->HasAuthority())
	{
		return;
	}

	UWorld* World = GetWorld();
	ACosmereGameState* GameState = World ? World->GetGameState<ACosmereGameState>() : nullptr;
	if (!GameState)
	{
		return;
	}

	const float ReachSquared = FMath::Square(GetScaledRadius(SearchRadius));
	const FVector Origin = Seer->GetActorLocation();

	TArray<AActor*> Burners;
	for (APlayerState* Entry : GameState->PlayerArray)
	{
		ACosmereCharacter* Other = Entry ? Cast<ACosmereCharacter>(Entry->GetPawn()) : nullptr;
		if (!Other || Other == Seer)
		{
			continue;
		}

		if (FVector::DistSquared(Origin, Other->GetActorLocation()) > ReachSquared)
		{
			continue;
		}

		// Bronze listens for the pulse of metal being burned, and a burner who has stepped into Shadesmar is not
		// burning anywhere the seer can reach. Same rule as a bullet: no crossing the veil either way.
		if (!UCosmereRealmComponent::ShareRealm(Seer, Other))
		{
			continue;
		}

		if (!Other->IsBurningPower())
		{
			continue;
		}

		// Copper is the only thing that answers bronze, and it holds one level below: only a harder flare burns
		// through. A hidden burner is simply not reported, so a cloud costs nothing when nobody is listening.
		if (UCosmereInvestitureComponent* Theirs = Other->GetInvestitureComponent())
		{
			if (Theirs->IsHiddenFromBronze(Level))
			{
				continue;
			}
		}

		Burners.Add(Other);
	}

	if (Burners.Num() > 0)
	{
		GameState->MulticastBronzeReveal(Seer, Burners, MarkDuration);
	}
}

//~ End Bronze

//~ Begin Cadmium

UCosmereAllomanticCadmiumAbility::UCosmereAllomanticCadmiumAbility()
{
	Radius = 1100.f;
	Duration = 8.f;

	// Below 1 stalls whoever is standing in it. The bubble is placed at the crosshair rather than on the
	// caster, and the caster is explicitly left out of its own effect.
	SpeedMultiplier = 0.35f;
	Range = 2500.f;
	bAffectSelf = false;
	bAffectOthers = true;
}

//~ End Cadmium

//~ Begin Bendalloy

UCosmereAllomanticBendalloyAbility::UCosmereAllomanticBendalloyAbility()
{
	// One number doing both jobs: 1100 is who the power reaches and how far each of them is carried. The
	// metal's own reach is short, so a slider pulls the people beside them along rather than crossing an
	// arena with it.
	Radius = 1100.f;

	// Both, because the power is written as "you and everyone near you". The caster is the first person
	// it is meant to move - a slider who only moved other people would be a strange cooldown to spend.
	//
	// There is no team filter, so enemies standing in the radius are carried too. That is the honest
	// reading of the power in a free-for-all, where everyone in range is an enemy, and it is the one
	// thing about this shape worth revisiting once team modes exist.
	bAffectSelf = true;
	bAffectOthers = true;
}

//~ End Bendalloy

//~ Begin Gold

UCosmereAllomanticGoldAbility::UCosmereAllomanticGoldAbility()
{
	Radius = 4000.f;
	Duration = 6.f;
	bIncludeSelf = false;
}

//~ End Gold

//~ Begin Electrum

UCosmereAllomanticElectrumAbility::UCosmereAllomanticElectrumAbility()
{
	Duration = 8.f;
	SpeedMultiplier = 1.15f;
	DamageResistance = 0.2f;
}

//~ End Electrum

//~ Begin Chromium

UCosmereAllomanticChromiumAbility::UCosmereAllomanticChromiumAbility()
{
	Range = 2500.f;
	Damage = 0.f;
	ResourceDrain = 8.f;
}

//~ End Chromium

//~ Begin Nicrosil

UCosmereAllomanticNicrosilAbility::UCosmereAllomanticNicrosilAbility()
{
	Range = 2500.f;

	// Reserve only, no health: nicrosil boosts what someone is burning rather than mending them.
	HealAmount = 0.f;
	ResourceRefill = 12.f;
}

//~ End Nicrosil

//~ Begin Aluminum

UCosmereAllomanticAluminumAbility::UCosmereAllomanticAluminumAbility()
{
	// One second, because the point of aluminum is the purge itself and not a lasting buff.
	Duration = 1.f;
	bCleanse = true;
}

//~ End Aluminum

//~ Begin Duralumin

UCosmereAllomanticDuraluminAbility::UCosmereAllomanticDuraluminAbility()
{
	// Half of Pewter's window at roughly twice the numbers. A flare is meant to be spent, not sustained.
	Duration = 4.f;
	SpeedMultiplier = 1.8f;
	JumpMultiplier = 1.3f;
	DamageResistance = 0.4f;
}

//~ End Duralumin
