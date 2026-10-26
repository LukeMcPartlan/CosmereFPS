// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Power/CosmerePowerArchetypes.h"
#include "CosmerePowersSurge.generated.h"

class UCosmereRealmComponent;

/**
 * One thin class per Surge row in DT_Powers.
 *
 * UCosmerePowerAbility::ResolveRow identifies a row by matching its AbilityClass against the ability's own
 * class, so every row needs a class of its own even when two powers are the same shape. That is all these
 * are: the shape lives once in Power/CosmerePowerArchetypes.h and the numbers live here, one constructor
 * per power. Nothing behaviour-bearing is overridden, so a power is retuned by changing a number rather
 * than by editing eight copies of the same code.
 *
 * Native rather than Blueprint deliberately. A row can name these directly as
 * /Script/CosmereRivals.CosmereAdhesionAbility, which is how Surge_Gravitation already names its ability,
 * so no asset is needed to make a row work. A Blueprint child of any of these is still possible later if
 * an individual power needs to be tuned without a rebuild.
 *
 * Gravitation and Division are deliberately absent: both already have a real implementation and neither is
 * an archetype.
 */

/** Adhesion: pins the target to whatever it is standing on and drags it in. A hold rather than a hit. */
UCLASS()
class COSMERERIVALS_API UCosmereAdhesionAbility : public UCosmereStrikeAbility
{
	GENERATED_BODY()

public:
	UCosmereAdhesionAbility();
};

/** Abrasion: the ground stops holding on. Faster and lighter for a while. */
UCLASS()
class COSMERERIVALS_API UCosmereAbrasionAbility : public UCosmereSelfBuffAbility
{
	GENERATED_BODY()

public:
	UCosmereAbrasionAbility();
};

/** Progression: growth, and the only purely restorative Surge. */
UCLASS()
class COSMERERIVALS_API UCosmereProgressionAbility : public UCosmereSupportAbility
{
	GENERATED_BODY()

public:
	UCosmereProgressionAbility();
};

/** Illumination: light everywhere. Stands in for the illusion Surge by marking what is around you. */
UCLASS()
class COSMERERIVALS_API UCosmereIlluminationAbility : public UCosmereRevealAbility
{
	GENERATED_BODY()

public:
	UCosmereIlluminationAbility();
};

/** Transformation: Soulcasting. Short range, and it hurts. */
UCLASS()
class COSMERERIVALS_API UCosmereTransformationAbility : public UCosmereStrikeAbility
{
	GENERATED_BODY()

public:
	UCosmereTransformationAbility();
};

/** Cohesion: the ground splits under them. A short-range shove that lands hard. */
UCLASS()
class COSMERERIVALS_API UCosmereCohesionAbility : public UCosmereStrikeAbility
{
	GENERATED_BODY()

public:
	UCosmereCohesionAbility();
};

/** Tension: stiffens what it touches, so the target fights its own body to move. */
UCLASS()
class COSMERERIVALS_API UCosmereTensionAbility : public UCosmereStrikeAbility
{
	GENERATED_BODY()

public:
	UCosmereTensionAbility();
};

/**
 * Transportation: Elsecalling. Steps the caster out of the Physical Realm and into Shadesmar.
 *
 * The blink it used to be was the shape without the meaning. Both realms are the same map, so nothing needs
 * to be jumped over: the distance is covered on foot, at the speed Shadesmar's shortened distances imply, and
 * stepping back out puts the caster wherever they have walked to. What the trip buys is nobody else being
 * there - and what it costs is that nobody else is there, so a phased player has no targets either.
 *
 * The row's cooldown gates the step in only. Leaving is always available, because a power that cannot be
 * switched off until an unrelated timer expires is a trap rather than a commitment; what keeps the flip from
 * turning into a scramble is MinimumExitCooldownSeconds, charged on the way out.
 */
UCLASS()
class COSMERERIVALS_API UCosmereTransportationAbility : public UCosmerePowerAbility
{
	GENERATED_BODY()

public:
	UCosmereTransportationAbility();

	//~ Begin UGameplayAbility interface
	virtual bool CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	//~ End UGameplayAbility interface

protected:
	virtual void OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	/**
	 * The shortest wait that leaving Shadesmar may leave on the row's clock, in seconds.
	 *
	 * An exit with less than this left pushes the remainder back up to it, which is what stops the flip being
	 * pressed out twice in one window. An exit taken deep into the cooldown keeps whatever it already has.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Transportation", meta = (ClampMin = "0.0"))
	float MinimumExitCooldownSeconds = 1.f;

	/** The realm component of the caster, or null when the caster is not a character. */
	UCosmereRealmComponent* GetCasterRealm() const;

private:
	/**
	 * How much of the row clock was left before this press stamped it.
	 *
	 * Captured in ActivateAbility because that is the last moment it exists: the base stamps LastActivatedTime
	 * with "now" before OnPowerActivated runs, so by then the remainder is the full row again.
	 */
	float CooldownRemainingBeforePress = 0.f;
};
