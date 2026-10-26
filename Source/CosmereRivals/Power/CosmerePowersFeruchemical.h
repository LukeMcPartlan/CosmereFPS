// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Power/CosmerePowerArchetypes.h"
#include "Power/CosmereTinSenseAbility.h"
#include "CosmerePowersFeruchemical.generated.h"

/**
 * One thin class per Feruchemical metal row in DT_Powers.
 *
 * The same pattern CosmerePowersSurge.h and CosmerePowersAllomantic.h use. Each class is a constructor that
 * fills in the numbers on one of the archetypes in Power/CosmerePowerArchetypes.h, and every row needs its
 * own class because UCosmerePowerAbility::ResolveRow matches a row to an ability by class identity.
 *
 * The 'Feruchemical' prefix is what keeps these distinct from their Allomantic counterparts: iron burned is a
 * pull on metal, iron stored is weight, and the two are different powers that happen to share a metal.
 */

/**
 * Iron: stores weight, so the caster is heavy - harder to move and harder to knock over.
 *
 * Weight is read as falling speed, and a tap that carries a Skimmer through at least a three metre fall
 * lands as a slam: everything near the impact point takes damage that doubles with the flare level.
 */
UCLASS()
class COSMERERIVALS_API UCosmereFeruchemicalIronAbility : public UCosmereSelfBuffAbility
{
	GENERATED_BODY()

public:
	UCosmereFeruchemicalIronAbility();

protected:
	/** Re-asserts the weight, then watches the fall that weight is for. */
	virtual void ApplyMetalmindTick() override;

	/**
	 * Shortest fall, measured from the top of the arc, that lands as a slam rather than as a step down.
	 * 300 cm is the three metres a Skimmer needs before there is anything behind the landing.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Iron", meta = (ClampMin = "0.0"))
	float ImpactMinFallHeight = 300.f;

	/** Damage the slam deals at level 1. Every level above it doubles this, the same ladder the stats use. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Iron", meta = (ClampMin = "0.0"))
	float ImpactDamage = 15.f;

	/** Radius in cm around the landing point that the slam reaches. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Iron", meta = (ClampMin = "0.0"))
	float ImpactRadius = 400.f;

private:
	/** Highest point of the fall in progress. Seeded on the first tick that finds the pawn airborne. */
	float PeakFallHeight = 0.f;

	/** True between the tick that found the pawn in the air and the tick that found it landed. */
	bool bWasAirborne = false;
};

/** Steel: stores speed, and spends it. */
UCLASS()
class COSMERERIVALS_API UCosmereFeruchemicalSteelAbility : public UCosmereSelfBuffAbility
{
	GENERATED_BODY()

public:
	UCosmereFeruchemicalSteelAbility();
};

/**
 * Tin: stores senses.
 *
 * Storing is taking the sense away, so only a tap reaches the pawn's own eyes and ears - a metalmind being
 * filled has nothing to see with. The level comes from the metalmind rather than from a burn clock, which is
 * why this shares the Allomantic ability's effects but not its ladder.
 */
UCLASS()
class COSMERERIVALS_API UCosmereFeruchemicalTinAbility : public UCosmereTinSenseAbility
{
	GENERATED_BODY()

public:
	UCosmereFeruchemicalTinAbility();

protected:
	/** Mirrors the metalmind's own level onto the pawn's senses on every tick of the loop. */
	virtual void ApplyMetalmindTick() override;

	/** Puts the senses back before the base clears the rest of the metalmind's stats. */
	virtual void ClearMetalmindStats() override;
};

/** Pewter: stores strength. */
UCLASS()
class COSMERERIVALS_API UCosmereFeruchemicalPewterAbility : public UCosmereSelfBuffAbility
{
	GENERATED_BODY()

public:
	UCosmereFeruchemicalPewterAbility();
};

/** Zinc: stores mental speed, which reads as quicker hands rather than quicker feet. */
UCLASS()
class COSMERERIVALS_API UCosmereFeruchemicalZincAbility : public UCosmereSelfBuffAbility
{
	GENERATED_BODY()

public:
	UCosmereFeruchemicalZincAbility();
};

/** Brass: stores warmth, so the caster can take a beating while it lasts. */
UCLASS()
class COSMERERIVALS_API UCosmereFeruchemicalBrassAbility : public UCosmereSelfBuffAbility
{
	GENERATED_BODY()

public:
	UCosmereFeruchemicalBrassAbility();
};

/** Copper: stores memory, so the caster notices what is around them. */
UCLASS()
class COSMERERIVALS_API UCosmereFeruchemicalCopperAbility : public UCosmereRevealAbility
{
	GENERATED_BODY()

public:
	UCosmereFeruchemicalCopperAbility();
};

/** Bronze: stores wakefulness. The widest of the Feruchemical reveals. */
UCLASS()
class COSMERERIVALS_API UCosmereFeruchemicalBronzeAbility : public UCosmereRevealAbility
{
	GENERATED_BODY()

public:
	UCosmereFeruchemicalBronzeAbility();
};

/** Cadmium: stores breath, spent here on the caster alone rather than on a bubble of enemies. */
UCLASS()
class COSMERERIVALS_API UCosmereFeruchemicalCadmiumAbility : public UCosmereFieldAbility
{
	GENERATED_BODY()

public:
	UCosmereFeruchemicalCadmiumAbility();
};

/** Bendalloy: stores nutrition, spent here as a bubble that holds others back. */
UCLASS()
class COSMERERIVALS_API UCosmereFeruchemicalBendalloyAbility : public UCosmereFieldAbility
{
	GENERATED_BODY()

public:
	UCosmereFeruchemicalBendalloyAbility();
};

/** Gold: stores health, and gives it back. */
UCLASS()
class COSMERERIVALS_API UCosmereFeruchemicalGoldAbility : public UCosmereSelfBuffAbility
{
	GENERATED_BODY()

public:
	UCosmereFeruchemicalGoldAbility();
};

/** Electrum: stores determination - the Feruchemical answer to being shot at. */
UCLASS()
class COSMERERIVALS_API UCosmereFeruchemicalElectrumAbility : public UCosmereSelfBuffAbility
{
	GENERATED_BODY()

public:
	UCosmereFeruchemicalElectrumAbility();
};

/** Chromium: stores fortune, spent here as a leech to match its Allomantic cousin. */
UCLASS()
class COSMERERIVALS_API UCosmereFeruchemicalChromiumAbility : public UCosmereStrikeAbility
{
	GENERATED_BODY()

public:
	UCosmereFeruchemicalChromiumAbility();
};

/** Nicrosil: stores Investiture, and hands it on. */
UCLASS()
class COSMERERIVALS_API UCosmereFeruchemicalNicrosilAbility : public UCosmereSupportAbility
{
	GENERATED_BODY()

public:
	UCosmereFeruchemicalNicrosilAbility();
};

/** Aluminum: stores identity - which is to say, sheds everything attached to it. */
UCLASS()
class COSMERERIVALS_API UCosmereFeruchemicalAluminumAbility : public UCosmereSelfBuffAbility
{
	GENERATED_BODY()

public:
	UCosmereFeruchemicalAluminumAbility();
};

/** Duralumin: stores connection, spent on making someone else whole again. */
UCLASS()
class COSMERERIVALS_API UCosmereFeruchemicalDuraluminAbility : public UCosmereSupportAbility
{
	GENERATED_BODY()

public:
	UCosmereFeruchemicalDuraluminAbility();
};
