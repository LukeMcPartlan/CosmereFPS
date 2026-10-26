// Copyright Epic Games, Inc. All Rights Reserved.

#include "Character/CosmereTinSensesComponent.h"

#include "AudioDevice.h"
#include "Camera/CameraComponent.h"
#include "Character/CosmereCharacter.h"
#include "Character/CosmereRealmComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"

UCosmereTinSensesComponent::UCosmereTinSensesComponent()
{
	// No tick. The burn level is pushed by the power that is spending the metal, and the only per-frame
	// consumer is the HUD asking queries - so there is nothing here to advance on a frame of its own.
	PrimaryComponentTick.bCanEverTick = false;
}

void UCosmereTinSensesComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// A burn still up when the pawn goes away would otherwise leave the machine's master volume raised,
	// because the ability that owns the level has no reason to end before the pawn does.
	RestoreLocalSenses();

	Super::EndPlay(EndPlayReason);
}

ACosmereCharacter* UCosmereTinSensesComponent::GetOwningCosmereCharacter() const
{
	return Cast<ACosmereCharacter>(GetOwner());
}

float UCosmereTinSensesComponent::GetSenseScale() const
{
	return (TinLevel <= 0) ? 1.f : FMath::Pow(2.f, static_cast<float>(TinLevel));
}

float UCosmereTinSensesComponent::GetHearingRadius() const
{
	return BaseHearingRadius * GetSenseScale();
}

float UCosmereTinSensesComponent::GetVisionRadius() const
{
	return VisionRadius * GetSenseScale();
}

void UCosmereTinSensesComponent::SetTinLevel(int32 NewLevel)
{
	const int32 ClampedLevel = FMath::Clamp(NewLevel, 0, MaxTinLevel);
	if (ClampedLevel == TinLevel)
	{
		return;
	}

	TinLevel = ClampedLevel;

	// The picture is per machine, so a level pushed on the server changes nothing here and the owning
	// client's own copy is the one that moves the exposure.
	ApplyLocalSenses();

	OnTinSensesChanged.Broadcast(TinLevel, GetSenseScale());
}

void UCosmereTinSensesComponent::ApplyLocalSenses()
{
	ACosmereCharacter* Character = GetOwningCosmereCharacter();
	UWorld* World = GetWorld();
	if (!Character || !World)
	{
		return;
	}

	// Only the machine that owns this pawn draws the picture. A remote pawn burning tin must not brighten
	// somebody else's screen, so every branch below is behind this gate.
	if (!Character->IsLocallyControlled())
	{
		return;
	}

	if (UCameraComponent* Camera = Character->FindComponentByClass<UCameraComponent>())
	{
		FPostProcessSettings& Settings = Camera->PostProcessSettings;

		// Captured once, before the first boost is written, so clearing tin restores what the pawn
		// actually shipped with rather than an assumed zero.
		if (!bLocalSensesApplied)
		{
			CachedExposureBias = Settings.AutoExposureBias;
			bCachedExposureOverride = Settings.bOverride_AutoExposureBias;
			bLocalSensesApplied = true;
		}

		if (TinLevel > 0)
		{
			// Scale / 2 is 1 at level 1 and doubles with each flare, which is the same ladder the radii use.
			const float ExposureSteps = DarkVisionExposureBias * (GetSenseScale() * 0.5f);
			Settings.bOverride_AutoExposureBias = true;
			Settings.AutoExposureBias = CachedExposureBias + ExposureSteps;
		}
		else
		{
			Settings.bOverride_AutoExposureBias = bCachedExposureOverride;
			Settings.AutoExposureBias = CachedExposureBias;
		}
	}

	// The whole mix rather than one sound: tin is a sense, and what it sharpens is everything the player
	// can hear. The audio device is per machine, so this only ever touches the burner's own output.
	if (FAudioDevice* Audio = World->GetAudioDeviceRaw())
	{
		const float Volume = (TinLevel > 0) ? 1.f + (SenseVolumePerLevel * static_cast<float>(TinLevel)) : 1.f;
		Audio->SetTransientPrimaryVolume(Volume);
	}
}

void UCosmereTinSensesComponent::RestoreLocalSenses()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (FAudioDevice* Audio = World->GetAudioDeviceRaw())
	{
		Audio->SetTransientPrimaryVolume(1.f);
	}
}

bool UCosmereTinSensesComponent::IsVisibleWithTin(const AActor* Target) const
{
	if (TinLevel <= 0 || !Target)
	{
		return false;
	}

	const ACosmereCharacter* Character = GetOwningCosmereCharacter();
	const UWorld* World = GetWorld();
	if (!Character || !World || Target == Character)
	{
		return false;
	}

	// Sight is the listener's own picture, so a remote pawn must never claim to be seeing anything.
	if (!Character->IsLocallyControlled())
	{
		return false;
	}

	// And the picture only holds one realm. A body in Shadesmar is not a shape behind a wall - it is not there
	// at all, so keen senses have nothing to find through it.
	if (!UCosmereRealmComponent::ShareRealm(Character, Target))
	{
		return false;
	}

	const FVector EyeLocation = Character->GetPawnViewLocation();
	const FVector ViewDirection = Character->GetViewRotation().Vector();
	const FVector TargetFocus = Target->GetActorLocation();
	const FVector ToTarget = TargetFocus - EyeLocation;

	if (ToTarget.SizeSquared() > FMath::Square(GetVisionRadius()))
	{
		return false;
	}

	// A cone test as well as a distance one: a pawn standing behind the player is not something tin
	// picks out, it is just close.
	if (ToTarget.GetSafeNormal().Dot(ViewDirection) < 0.35f)
	{
		return false;
	}

	FCollisionQueryParams QueryParams(TEXT("CosmereTinSight"), false, Character);
	QueryParams.AddIgnoredActor(Target);

	FHitResult Hit;
	const bool bBlocked = World->LineTraceSingleByChannel(Hit, EyeLocation, TargetFocus, ECC_Visibility, QueryParams);

	return !bBlocked;
}

void UCosmereTinSensesComponent::NotifyHeardNoise(AActor* Source)
{
	if (TinLevel <= 0 || !Source)
	{
		return;
	}

	ACosmereCharacter* Character = GetOwningCosmereCharacter();
	if (!Character || !Character->IsLocallyControlled() || Source == Character)
	{
		return;
	}

	// Measured from the pawn rather than from the camera: what carries is the body, and a flared burner's
	// reach has to be the same number whichever way they happen to be looking.
	if (FVector::DistSquared(Character->GetActorLocation(), Source->GetActorLocation()) > FMath::Square(GetHearingRadius()))
	{
		return;
	}

	// The same seam Bronze reveals through, so a heard enemy is bracketed by the existing HUD pass and
	// needs no second drawing path of its own.
	Character->RevealActors({ Source }, RevealDuration);
}
