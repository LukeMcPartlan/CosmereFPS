// Copyright Epic Games, Inc. All Rights Reserved.

#include "Character/CosmereInvestitureComponent.h"

#include "Character/CosmereCharacter.h"
#include "Engine/World.h"

UCosmereInvestitureComponent::UCosmereInvestitureComponent()
{
	// Pure bookkeeping. Nothing here advances on a frame of its own; the powers that own each entry re-assert
	// it, and the expiry stamp is read against the world clock rather than counted down.
	PrimaryComponentTick.bCanEverTick = false;
}

double UCosmereInvestitureComponent::GetNowSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

void UCosmereInvestitureComponent::SetResistancesForSource(FName Source, float Physical, float Spiritual)
{
	if (Source.IsNone())
	{
		return;
	}

	const float ClampedPhysical = FMath::Clamp(Physical, 0.f, 1.f);
	const float ClampedSpiritual = FMath::Clamp(Spiritual, 0.f, 1.f);

	if (ClampedPhysical <= 0.f && ClampedSpiritual <= 0.f)
	{
		// A neutral contribution is a removal, not an entry. Keeps the array the size of what is actually up.
		ClearResistanceSource(Source);
		return;
	}

	for (FCosmereResistanceSource& Entry : Resistances)
	{
		if (Entry.Source == Source)
		{
			Entry.Physical = ClampedPhysical;
			Entry.Spiritual = ClampedSpiritual;
			return;
		}
	}

	FCosmereResistanceSource& Added = Resistances.AddDefaulted_GetRef();
	Added.Source = Source;
	Added.Physical = ClampedPhysical;
	Added.Spiritual = ClampedSpiritual;
}

void UCosmereInvestitureComponent::ClearResistanceSource(FName Source)
{
	Resistances.RemoveAll([Source](const FCosmereResistanceSource& Entry)
	{
		return Entry.Source == Source;
	});
}

void UCosmereInvestitureComponent::ClearAllResistances()
{
	Resistances.Reset();
}

float UCosmereInvestitureComponent::GetPhysicalResistance()
{
	float Remaining = 1.f;
	for (const FCosmereResistanceSource& Entry : Resistances)
	{
		Remaining *= (1.f - Entry.Physical);
	}

	return FMath::Clamp(1.f - Remaining, 0.f, 1.f);
}

float UCosmereInvestitureComponent::GetSpiritualResistance()
{
	float Remaining = 1.f;
	for (const FCosmereResistanceSource& Entry : Resistances)
	{
		Remaining *= (1.f - Entry.Spiritual);
	}

	return FMath::Clamp(1.f - Remaining, 0.f, 1.f);
}

float UCosmereInvestitureComponent::ScaleForPhysical(float Incoming)
{
	return Incoming * (1.f - GetPhysicalResistance());
}

float UCosmereInvestitureComponent::ScaleForSpiritual(float Incoming)
{
	return Incoming * (1.f - GetSpiritualResistance());
}

void UCosmereInvestitureComponent::SetInvestitureImmunity(FName Source)
{
	if (Source.IsNone())
	{
		return;
	}

	// Idempotent, because a burn re-asserts its own seal several times a second and that has to cost one entry
	// rather than one per assertion.
	Immunities.AddUnique(Source);
}

void UCosmereInvestitureComponent::ClearInvestitureImmunity(FName Source)
{
	Immunities.Remove(Source);
}

void UCosmereInvestitureComponent::ClearAllImmunity()
{
	Immunities.Reset();
}

bool UCosmereInvestitureComponent::IsInvestitureImmune() const
{
	return Immunities.Num() > 0;
}

bool UCosmereInvestitureComponent::IsTargetInvestitureImmune(AActor* Target)
{
	const ACosmereCharacter* Character = Cast<ACosmereCharacter>(Target);
	const UCosmereInvestitureComponent* Investiture = Character ? Character->GetInvestitureComponent() : nullptr;

	return Investiture && Investiture->IsInvestitureImmune();
}

void UCosmereInvestitureComponent::SetCopperShroud(FName Source, int32 Level, float DurationSeconds)
{
	if (Source.IsNone() || Level <= 0)
	{
		return;
	}

	const double Expiry = GetNowSeconds() + FMath::Max(DurationSeconds, 0.05f);

	for (FCosmereCopperShroud& Entry : Shrouds)
	{
		if (Entry.Source == Source)
		{
			Entry.Level = Level;
			Entry.ExpiresAt = Expiry;
			return;
		}
	}

	FCosmereCopperShroud& Added = Shrouds.AddDefaulted_GetRef();
	Added.Source = Source;
	Added.Level = Level;
	Added.ExpiresAt = Expiry;
}

void UCosmereInvestitureComponent::ClearCopperShroud(FName Source)
{
	Shrouds.RemoveAll([Source](const FCosmereCopperShroud& Entry)
	{
		return Entry.Source == Source;
	});
}

void UCosmereInvestitureComponent::PruneExpiredShrouds()
{
	const double Now = GetNowSeconds();
	Shrouds.RemoveAll([Now](const FCosmereCopperShroud& Entry)
	{
		return Entry.ExpiresAt <= Now;
	});
}

int32 UCosmereInvestitureComponent::GetCopperShroudLevel()
{
	PruneExpiredShrouds();

	int32 Strongest = 0;
	for (const FCosmereCopperShroud& Entry : Shrouds)
	{
		Strongest = FMath::Max(Strongest, Entry.Level);
	}

	return Strongest;
}

bool UCosmereInvestitureComponent::IsHiddenFromBronze(int32 BronzeLevel)
{
	const int32 ShroudLevel = GetCopperShroudLevel();

	// A cloud holds against its own flare and anything weaker. Only a harder-burning bronze burns through.
	return ShroudLevel > 0 && BronzeLevel <= ShroudLevel;
}
