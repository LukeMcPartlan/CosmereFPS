// Copyright Epic Games, Inc. All Rights Reserved.

#include "Power/CosmerePowersFeruchemical.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "CosmereRivals.h"

//~ Begin Iron

UCosmereFeruchemicalIronAbility::UCosmereFeruchemicalIronAbility()
{
	Duration = 6.f;

	// Iron is mass and damage resistance, and nothing else. Fall speed, knockback and pull distance are all read
	// back out of the mass this puts on, so there is no gravity term here - which is also what lets Gravitation
	// turn the world sideways without the two of them fighting over who owns gravity.
	MassMultiplier = 2.2f;

	// Storing is the direction that carries the metal: a tenth of the weight, so a tenth of the fall speed and
	// ten times the shove. The tap is the same ladder read the other way.
	StoreMassMultiplier = 0.1f;

	// Ten percent a tier, added rather than doubled, so the metal climbs to half damage off at level five.
	DamageResistance = 0.1f;
	bLinearResistanceLadder = true;

	// Filling this metalmind is the half worth having, since a light body is what keeps a pawn alive through a
	// fall. Three times the shared store rate so it is a decision rather than a chore.
	MetalmindStoreRateMultiplier = 3.f;
}

void UCosmereFeruchemicalIronAbility::ApplyMetalmindTick()
{
	// The stats first: this is what puts the weight on and, on a tap, the resistance that goes with it.
	Super::ApplyMetalmindTick();

	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	ACharacter* Character = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	const UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Move)
	{
		return;
	}

	const float NowZ = Character->GetActorLocation().Z;

	if (!Move->IsMovingOnGround())
	{
		// The first airborne slice seeds the peak, so a tap pressed in the air measures from where it was
		// pressed rather than from a height the ability never saw.
		if (!bWasAirborne)
		{
			PeakFallHeight = NowZ;
			bWasAirborne = true;
		}

		PeakFallHeight = FMath::Max(PeakFallHeight, NowZ);
		return;
	}

	if (!bWasAirborne)
	{
		return;
	}

	// Landed. The flag is cleared before every early-out below, so a fall that fails a check cannot leave the
	// pawn marked airborne and swallow the next one.
	bWasAirborne = false;

	const float FallDistance = PeakFallHeight - NowZ;
	PeakFallHeight = NowZ;

	const EMetalmindMode Mode = GetMetalmindMode();
	const bool bTapping = (Mode == EMetalmindMode::Tapping) || (Mode == EMetalmindMode::Flaring);

	// Storing weight is the light direction: a soft landing is the point of it, so only a tap can slam.
	if (!bTapping || FallDistance < ImpactMinFallHeight || ImpactDamage <= 0.f)
	{
		return;
	}

	// Damage replicates, so it is the authority's to deal - the same split every damaging power in this module
	// makes, and the reason a client cannot manufacture a slam by throwing itself off a ledge.
	if (!ActorInfo->IsNetAuthority())
	{
		return;
	}

	UWorld* World = GetPowerWorld(ActorInfo);
	if (!World)
	{
		return;
	}

	// Flaring is what makes the slam worth landing: the ladder that doubles the drain doubles the hit.
	const int32 Level = FMath::Max(1, GetMetalmindLevel());
	const float Damage = ImpactDamage * FMath::Pow(2.f, static_cast<float>(Level - 1));

	const FVector ImpactPoint = Character->GetActorLocation();
	const float RadiusSquared = ImpactRadius * ImpactRadius;
	AController* InstigatorController = Character->GetInstigatorController();

	int32 HitCount = 0;
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		ACharacter* Other = *It;
		if (!IsValid(Other) || Other == Character)
		{
			continue;
		}

		if (FVector::DistSquared(Other->GetActorLocation(), ImpactPoint) > RadiusSquared)
		{
			continue;
		}

		// Through the engine's damage pipeline, so a slam kill is credited exactly as a shot is.
		UGameplayStatics::ApplyDamage(Other, Damage, InstigatorController, Character, UDamageType::StaticClass());
		++HitCount;
	}

	UE_LOG(LogCosmere, Log,
		TEXT("CosmereIron: '%s' landed from %.0f cm at level %d and hit %d pawn(s) for %.1f."),
		*GetPowerId().ToString(), FallDistance, Level, HitCount, Damage);
}

//~ End Iron

//~ Begin Steel

UCosmereFeruchemicalSteelAbility::UCosmereFeruchemicalSteelAbility()
{
	Duration = 6.f;

	// Level 1 is double speed. The level does the rest - a second tap takes it to x4, a third to x8 - so the
	// row only has to name the first step.
	SpeedMultiplier = 2.f;

	// Holding pays 10 a second and costs half your speed, because Storing divides by SpeedMultiplier. Standing
	// still is the better earner on purpose: it costs 15 a second and charges while you do something else.
	MetalmindStoreRate = 10.f;

	// Steelrunner is the only metalmind that fills itself, and only while its owner is not moving.
	MetalmindPassiveStandRate = 15.f;
}

//~ End Steel

//~ Begin Tin

UCosmereFeruchemicalTinAbility::UCosmereFeruchemicalTinAbility()
{
	// The metalmind owns this one's lifetime and its level, so the ability still ends after each activation
	// exactly as every other metal does - the tap is the burn, and the metalmind loop is what keeps it up.
	bEndAfterActivation = true;
	bSenseLevelFromMetalmind = true;
	MaxTinLevel = 4;
}

void UCosmereFeruchemicalTinAbility::ApplyMetalmindTick()
{
	Super::ApplyMetalmindTick();

	// Only a tap has metal behind it. Storing is the sense being taken away, so it reads as no burn at all
	// rather than as a weaker one - there is nothing to see with while the metalmind is filling.
	const EMetalmindMode Mode = GetMetalmindMode();
	const bool bSpending = (Mode == EMetalmindMode::Tapping || Mode == EMetalmindMode::Flaring);
	ApplyTinLevel(bSpending ? FMath::Max(1, GetMetalmindLevel()) : 0);
}

void UCosmereFeruchemicalTinAbility::ClearMetalmindStats()
{
	Super::ClearMetalmindStats();

	ApplyTinLevel(0);
}

//~ End Tin

//~ Begin Pewter

UCosmereFeruchemicalPewterAbility::UCosmereFeruchemicalPewterAbility()
{
	Duration = 7.f;

	// Strength is the point of the metal; the speed is a nudge on top of it, because a pawn that both hits
	// hardest and sprints is a different power wearing this one's name.
	SpeedMultiplier = 1.12f;

	// Twenty percent a tier on the same additive ladder Iron uses: 20/40/60/80/100, so the last rung is
	// complete immunity and every rung before it is a fourth of that step.
	DamageResistance = 0.2f;
	bLinearResistanceLadder = true;
	MeleeDamageMultiplier = 1.35f;
}

//~ End Pewter

//~ Begin Zinc

UCosmereFeruchemicalZincAbility::UCosmereFeruchemicalZincAbility()
{
	Duration = 6.f;
	SpeedMultiplier = 1.25f;
	AirControlBonus = 0.4f;
}

//~ End Zinc

//~ Begin Brass

UCosmereFeruchemicalBrassAbility::UCosmereFeruchemicalBrassAbility()
{
	Duration = 6.f;

	// The most resistant of the Feruchemical buffs, and the only one that mends while it runs.
	DamageResistance = 0.5f;
	HealPerSecond = 2.f;
}

//~ End Brass

//~ Begin Copper

UCosmereFeruchemicalCopperAbility::UCosmereFeruchemicalCopperAbility()
{
	Radius = 5000.f;
	Duration = 12.f;
	bIncludeSelf = false;
}

//~ End Copper

//~ Begin Bronze

UCosmereFeruchemicalBronzeAbility::UCosmereFeruchemicalBronzeAbility()
{
	Radius = 8000.f;
	Duration = 10.f;
	bIncludeSelf = false;
}

//~ End Bronze

//~ Begin Cadmium

UCosmereFeruchemicalCadmiumAbility::UCosmereFeruchemicalCadmiumAbility()
{
	// Range 0 centres the bubble on the caster, and bAffectSelf with bAffectOthers off makes it a purely
	// personal effect - the Feruchemical version stores breath instead of altering anyone else's time.
	Radius = 1000.f;
	Duration = 6.f;
	SpeedMultiplier = 1.6f;
	Range = 0.f;
	bAffectSelf = true;
	bAffectOthers = false;
}

//~ End Cadmium

//~ Begin Bendalloy

UCosmereFeruchemicalBendalloyAbility::UCosmereFeruchemicalBendalloyAbility()
{
	Radius = 1200.f;
	Duration = 6.f;
	SpeedMultiplier = 0.4f;
	Range = 2000.f;
	bAffectSelf = false;
	bAffectOthers = true;
}

//~ End Bendalloy

//~ Begin Gold

UCosmereFeruchemicalGoldAbility::UCosmereFeruchemicalGoldAbility()
{
	Duration = 6.f;

	// The strongest sustained heal in the set. Six seconds of it is most of a full health bar back.
	HealPerSecond = 6.f;
	DamageResistance = 0.3f;
}

//~ End Gold

//~ Begin Electrum

UCosmereFeruchemicalElectrumAbility::UCosmereFeruchemicalElectrumAbility()
{
	Duration = 5.f;
	DamageResistance = 0.45f;
}

//~ End Electrum

//~ Begin Chromium

UCosmereFeruchemicalChromiumAbility::UCosmereFeruchemicalChromiumAbility()
{
	Range = 2500.f;
	Damage = 0.f;

	// A deeper leech than the Allomantic version, and no slower to land for it.
	ResourceDrain = 10.f;
}

//~ End Chromium

//~ Begin Nicrosil

UCosmereFeruchemicalNicrosilAbility::UCosmereFeruchemicalNicrosilAbility()
{
	Range = 2500.f;
	HealAmount = 0.f;
	ResourceRefill = 15.f;
}

//~ End Nicrosil

//~ Begin Aluminum

UCosmereFeruchemicalAluminumAbility::UCosmereFeruchemicalAluminumAbility()
{
	// Like its Allomantic cousin: the purge is the power, so there is no window to speak of.
	Duration = 1.f;
	bCleanse = true;
}

//~ End Aluminum

//~ Begin Duralumin

UCosmereFeruchemicalDuraluminAbility::UCosmereFeruchemicalDuraluminAbility()
{
	// Connection, so it heals hardest of any of the support powers rather than refilling reserve.
	Range = 2000.f;
	HealAmount = 50.f;
	ResourceRefill = 10.f;
}

//~ End Duralumin
