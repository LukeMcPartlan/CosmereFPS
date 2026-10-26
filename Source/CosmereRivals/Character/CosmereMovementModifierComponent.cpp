// Copyright Epic Games, Inc. All Rights Reserved.

#include "Character/CosmereMovementModifierComponent.h"

#include "Character/CosmereCharacter.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UCosmereMovementModifierComponent::UCosmereMovementModifierComponent()
{
	// Nothing here is per-frame: every write is driven by a mutation or by a base moving.
	PrimaryComponentTick.bCanEverTick = false;
}

int32 UCosmereMovementModifierComponent::FindModifier(FName Source, ECosmereMovementChannel Channel) const
{
	for (int32 Index = 0; Index < Modifiers.Num(); ++Index)
	{
		if (Modifiers[Index].Source == Source && Modifiers[Index].Channel == Channel)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

void UCosmereMovementModifierComponent::SetModifier(FName Source, ECosmereMovementChannel Channel, ECosmereMovementOp Op, float Value)
{
	// Replace rather than accumulate. Source names the contribution, not the caller, so a power re-asserting its
	// number every tick lands on the same slot instead of growing the stack.
	const int32 Existing = FindModifier(Source, Channel);
	if (Existing != INDEX_NONE)
	{
		Modifiers.RemoveAt(Existing);
	}

	// A contribution that resolves to nothing is dropped entirely, which makes "cleared" and "set to 1" the same
	// state and keeps the array from filling with no-ops when a store and a tap trade places.
	const bool bNeutral = (Op == ECosmereMovementOp::Multiply)
		? FMath::IsNearlyEqual(Value, 1.f)
		: FMath::IsNearlyZero(Value, 0.001f);

	if (!bNeutral)
	{
		FCosmereMovementModifier& Modifier = Modifiers.AddDefaulted_GetRef();
		Modifier.Source = Source;
		Modifier.Channel = Channel;
		Modifier.Op = Op;
		Modifier.Value = Value;
	}

	Apply();
}

void UCosmereMovementModifierComponent::ClearModifier(FName Source, ECosmereMovementChannel Channel)
{
	const int32 Existing = FindModifier(Source, Channel);
	if (Existing == INDEX_NONE)
	{
		return;
	}

	Modifiers.RemoveAt(Existing);
	Apply();
}

void UCosmereMovementModifierComponent::ClearSource(FName Source)
{
	const int32 Removed = Modifiers.RemoveAll([Source](const FCosmereMovementModifier& Modifier)
	{
		return Modifier.Source == Source;
	});

	if (Removed > 0)
	{
		Apply();
	}
}

void UCosmereMovementModifierComponent::CleansePenalties()
{
	const int32 Removed = Modifiers.RemoveAll([](const FCosmereMovementModifier& Modifier)
	{
		return (Modifier.Op == ECosmereMovementOp::Multiply) ? (Modifier.Value < 1.f) : (Modifier.Value < 0.f);
	});

	if (Removed > 0)
	{
		Apply();
	}
}

float UCosmereMovementModifierComponent::GetBaseValue(ECosmereMovementChannel Channel) const
{
	return ResolveBase(Channel);
}

float UCosmereMovementModifierComponent::GetEffectiveValue(ECosmereMovementChannel Channel) const
{
	// Additive contributions land first and the multipliers take the whole thing, so the answer does not depend on
	// the order the sources arrived in. That fixed order is the whole reason two buffs can compose at all.
	float Added = 0.f;
	float Multiplied = 1.f;

	for (const FCosmereMovementModifier& Modifier : Modifiers)
	{
		if (Modifier.Channel != Channel)
		{
			continue;
		}

		if (Modifier.Op == ECosmereMovementOp::Multiply)
		{
			Multiplied *= Modifier.Value;
		}
		else
		{
			Added += Modifier.Value;
		}
	}

	const float Result = (ResolveBase(Channel) + Added) * Multiplied;

	// Per-channel sanity, because a stack of multipliers can otherwise leave a pawn at zero gravity or a negative
	// speed that CharacterMovement will not climb back out of on its own.
	switch (Channel)
	{
	case ECosmereMovementChannel::GravityScale:
		return FMath::Max(Result, 0.01f);

	case ECosmereMovementChannel::AirControl:
		return FMath::Clamp(Result, 0.f, 1.f);

	default:
		return FMath::Max(Result, 0.f);
	}
}

float UCosmereMovementModifierComponent::ResolveBase(ECosmereMovementChannel Channel) const
{
	if (const ACosmereCharacter* Cosmere = Cast<ACosmereCharacter>(GetOwner()))
	{
		switch (Channel)
		{
		case ECosmereMovementChannel::WalkSpeed:    return Cosmere->GetBaseWalkSpeed();
		case ECosmereMovementChannel::JumpVelocity: return Cosmere->GetBaseJumpVelocity();
		case ECosmereMovementChannel::GravityScale: return Cosmere->GetBaseGravityScale();
		case ECosmereMovementChannel::AirControl:   return Cosmere->GetBaseAirControl();
		default: break;
		}
	}

	// A pawn with no build behind it has no numbers to read, so the first request records what the movement
	// component held. Every later request reads that snapshot, which keeps re-applying just as idempotent.
	if (!bSnapshotTaken)
	{
		bSnapshotTaken = true;

		if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
		{
			if (const UCharacterMovementComponent* Move = Character->GetCharacterMovement())
			{
				SnapshotWalkSpeed = Move->MaxWalkSpeed;
				SnapshotJumpVelocity = Move->JumpZVelocity;
				SnapshotGravityScale = Move->GravityScale;
				SnapshotAirControl = Move->AirControl;
			}
		}
	}

	switch (Channel)
	{
	case ECosmereMovementChannel::JumpVelocity: return SnapshotJumpVelocity;
	case ECosmereMovementChannel::GravityScale: return SnapshotGravityScale;
	case ECosmereMovementChannel::AirControl:   return SnapshotAirControl;
	default:                                    return SnapshotWalkSpeed;
	}
}

void UCosmereMovementModifierComponent::Apply()
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Move)
	{
		return;
	}

	Move->MaxWalkSpeed  = GetEffectiveValue(ECosmereMovementChannel::WalkSpeed);
	Move->JumpZVelocity = GetEffectiveValue(ECosmereMovementChannel::JumpVelocity);
	Move->GravityScale  = GetEffectiveValue(ECosmereMovementChannel::GravityScale);
	Move->AirControl    = GetEffectiveValue(ECosmereMovementChannel::AirControl);

	// Three channels and no fourth. Terminal velocity is the one movement value mass would want to move and the
	// one this component cannot write: UCharacterMovementComponent exposes no fall-speed property in 5.8 - falling
	// is integrated from GravityScale with no ceiling to set - so the mass-derived clamp lives on the pawn, in
	// ACosmereCharacter::Tick, where it can clamp the velocity vector along the gravity axis directly.
}
