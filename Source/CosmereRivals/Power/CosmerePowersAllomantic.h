// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Power/CosmerePowerArchetypes.h"
#include "Power/CosmereBurnAbility.h"
#include "Power/CosmereTinSenseAbility.h"
#include "CosmerePowersAllomantic.generated.h"

class ACosmereCharacter;

/**
 * One thin class per Allomantic metal row in DT_Powers.
 *
 * The same pattern CosmerePowersSurge.h uses, kept in its own pair because sixteen metals is enough to want
 * them apart from the eight Surges. The shape lives once in Power/CosmerePowerArchetypes.h; each class below
 * is a constructor that fills in that shape's numbers. Every row needs its own class because
 * UCosmerePowerAbility::ResolveRow matches a row to an ability by comparing AbilityClass against the
 * ability's class, and two rows naming one class is reported as an ambiguity rather than resolved.
 *
 * Prefixing every class with 'Allomantic' is what keeps UCosmereAllomanticIronAbility distinct from
 * UCosmereFeruchemicalIronAbility - the same metal, burned or stored, is two different powers.
 */

/** Iron: pulls on metal, and on anyone wearing it. Held, because a pull is a tug and not a blow. */
UCLASS()
class COSMERERIVALS_API UCosmereAllomanticIronAbility : public UCosmereStrikeAbility
{
	GENERATED_BODY()

public:
	UCosmereAllomanticIronAbility();
};

/** Steel: the same tug, the other way. Also held. */
UCLASS()
class COSMERERIVALS_API UCosmereAllomanticSteelAbility : public UCosmereStrikeAbility
{
	GENERATED_BODY()

public:
	UCosmereAllomanticSteelAbility();
};

  /**
   * Tin: heightened senses. Nothing in the world changes - only what the caster can perceive.
   *
   * A sustained burn rather than a reveal: the row pays a slow rate while it runs, and the burn climbs a
   * flare ladder on the clock. What it changes is on the caster's own machine - brighter darks, a louder
   * world, smoke that stops mattering, and every noise inside earshot marked through whatever is between.
   */
  UCLASS()
  class COSMERERIVALS_API UCosmereAllomanticTinAbility : public UCosmereTinSenseAbility
{
	GENERATED_BODY()

public:
	UCosmereAllomanticTinAbility();
};

/** Pewter: strength, speed and a hide that holds while it lasts. */
UCLASS()
class COSMERERIVALS_API UCosmereAllomanticPewterAbility : public UCosmereSelfBuffAbility
{
	GENERATED_BODY()

public:
	UCosmereAllomanticPewterAbility();
};

/** Zinc: riots emotion, which in a build with no charm system reads as drawing a target in. */
UCLASS()
class COSMERERIVALS_API UCosmereAllomanticZincAbility : public UCosmereStrikeAbility
{
	GENERATED_BODY()

public:
	UCosmereAllomanticZincAbility();
};

/** Brass: soothes emotion, which is the closest this build has to calming a target down. */
UCLASS()
class COSMERERIVALS_API UCosmereAllomanticBrassAbility : public UCosmereStrikeAbility
{
	GENERATED_BODY()

public:
	UCosmereAllomanticBrassAbility();
};

/**
 * Copper: a cloud that hides every burner inside it from bronze, and seals the burner themself off from
 * investiture.
 *
 * Copper does not hide a body from eyes - that would be a different metal - it hides the USE of power. Two
 * facts come out of one burn. The shroud is what a bronze has to out-burn, and it covers everyone standing in
 * the cloud. The immunity seal is on the burner alone, because a soul is warded by the metal its own body is
 * burning and by nothing else. Both are re-asserted on a short clock, because a cloud is not an object: stop
 * burning and it is gone, which is what makes killing the copper mean something.
 */
UCLASS()
class COSMERERIVALS_API UCosmereAllomanticCopperAbility : public UCosmereBurnAbility
{
	GENERATED_BODY()

public:
	UCosmereAllomanticCopperAbility();

protected:
	virtual void ApplyBurnLevel(int32 NewLevel) override;

	/** Re-covers everybody standing in the cloud. Faster than the flare step, because a shroud lapses. */
	void AssertCloud();

	/** Takes this burner's cloud back off every body it ever covered. */
	void DropCloud();

	/** Radius of a level 1 cloud. Every flare doubles it, like every other burn's reach. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Copper", meta = (ClampMin = "0.0"))
	float ShieldRadius = 1200.f;

	/** Seconds between assertions. Well inside ShroudSeconds, so a steady cloud never blinks. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Copper", meta = (ClampMin = "0.05"))
	float ShieldInterval = 0.25f;

	/** How long one assertion covers a body. A cloud that stops being re-asserted lapses on its own. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Copper", meta = (ClampMin = "0.05"))
	float ShroudSeconds = 1.f;

private:
	FTimerHandle CloudTimer;

	/** Every body this burner's cloud has covered, so a cloud that ends comes off at once rather than lapsing. */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<ACosmereCharacter>> Covered;
};

/**
 * Bronze: listens for other people's metal.
 *
 * The one sense that cannot be worked out locally. Whether a pawn is burning is a number on that pawn's own
 * ability instance, and an instance only exists on its owner's machine - so a client-side scan would find
 * nobody at all. The authority, which does hold every level, does the looking and hands the answer to the
 * seer's machine alone.
 */
UCLASS()
class COSMERERIVALS_API UCosmereAllomanticBronzeAbility : public UCosmereBurnAbility
{
	GENERATED_BODY()

public:
	UCosmereAllomanticBronzeAbility();

protected:
	virtual void ApplyBurnLevel(int32 NewLevel) override;

	/** One sweep of the arena, authority-side. */
	void ScanForBurners();

	/** Reach of a level 1 burn. Every flare doubles it. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Bronze", meta = (ClampMin = "0.0"))
	float SearchRadius = 4000.f;

	/** Seconds between sweeps. Short enough that a burn is found while it is still worth knowing about. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Bronze", meta = (ClampMin = "0.05"))
	float ScanInterval = 0.25f;

	/** How long one sweep keeps its mark. Longer than ScanInterval, or a steady burn would flicker. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Bronze", meta = (ClampMin = "0.05"))
	float MarkDuration = 0.6f;

private:
	FTimerHandle ScanTimer;
};

/** Cadmium: a bubble that time runs slowly inside. It does not slow the caster. */
UCLASS()
class COSMERERIVALS_API UCosmereAllomanticCadmiumAbility : public UCosmereFieldAbility
{
	GENERATED_BODY()

public:
	UCosmereAllomanticCadmiumAbility();
};

/**
 * Bendalloy: the caster and everyone standing near them are carried along the way they are already
 * travelling - sideways while they are on their feet, straight down while they are not.
 *
 * No longer the time bubble its Feruchemical cousin still is. A bubble can only change how fast someone
 * moves, and this power is about moving them somewhere, so it is built from the group-blink shape.
 */
UCLASS()
class COSMERERIVALS_API UCosmereAllomanticBendalloyAbility : public UCosmereGroupBlinkAbility
{
	GENERATED_BODY()

public:
	UCosmereAllomanticBendalloyAbility();
};

/** Gold: the caster's own past. A short radius, because it is about them rather than the field. */
UCLASS()
class COSMERERIVALS_API UCosmereAllomanticGoldAbility : public UCosmereRevealAbility
{
	GENERATED_BODY()

public:
	UCosmereAllomanticGoldAbility();
};

/** Electrum: the caster's own near future, so it reads as a defensive buff. */
UCLASS()
class COSMERERIVALS_API UCosmereAllomanticElectrumAbility : public UCosmereSelfBuffAbility
{
	GENERATED_BODY()

public:
	UCosmereAllomanticElectrumAbility();
};

/** Chromium: leeches the target's reserve instead of hurting it. */
UCLASS()
class COSMERERIVALS_API UCosmereAllomanticChromiumAbility : public UCosmereStrikeAbility
{
	GENERATED_BODY()

public:
	UCosmereAllomanticChromiumAbility();
};

/** Nicrosil: flares someone else's metals, so it hands reserve back rather than taking any. */
UCLASS()
class COSMERERIVALS_API UCosmereAllomanticNicrosilAbility : public UCosmereSupportAbility
{
	GENERATED_BODY()

public:
	UCosmereAllomanticNicrosilAbility();
};

/** Aluminum: burns away every metal the caster is holding - including whatever was slowing them. */
UCLASS()
class COSMERERIVALS_API UCosmereAllomanticAluminumAbility : public UCosmereSelfBuffAbility
{
	GENERATED_BODY()

public:
	UCosmereAllomanticAluminumAbility();
};

/** Duralumin: one enormous flare. The shortest window and the biggest numbers in the set. */
UCLASS()
class COSMERERIVALS_API UCosmereAllomanticDuraluminAbility : public UCosmereSelfBuffAbility
{
	GENERATED_BODY()

public:
	UCosmereAllomanticDuraluminAbility();
};
