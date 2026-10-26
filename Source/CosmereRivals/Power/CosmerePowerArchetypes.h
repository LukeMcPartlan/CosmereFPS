// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Power/CosmerePowerAbility.h"
#include "CosmerePowerArchetypes.generated.h"

class ACosmereCharacter;

/**
 * The seven shapes every non-Gravitation, non-Division DT_Powers row is built from.
 *
 * Why archetypes rather than forty hand-written abilities: UCosmerePowerAbility::ResolveRow resolves a row
 * by matching FPowerDefinition::AbilityClass against GetClass(), so one row needs one class - but the
 * BEHAVIOUR behind that class is shared. Each archetype below owns one shape and exposes the numbers that
 * make a power that shape as EditDefaultsOnly properties, so a power is a small class derived from an
 * archetype whose constructor fills those in. A Blueprint child of that class can override any of them
 * without a recompile, which is the seam to tune a power without rebuilding the project.
 *
 * Movement is applied on both machines, because the local machine's CharacterMovementComponent is what the
 * player actually feels. Anything that changes replicated state - damage, a victim's speed on someone
 * else's machine, health, the power pool - is authority-only, the same split UCosmereDivisionAbility makes
 * for its decay tick.
 *
 * These are Abstract: naming one of them directly on a DT_Powers row is a data error, which is exactly the
 * contract UCosmerePowerAbility itself documents for itself.
 */

/** A timed change to the caster's own body: faster, lighter, tougher, healing, or unseen. */
UCLASS(Abstract)
class COSMERERIVALS_API UCosmereSelfBuffAbility : public UCosmerePowerAbility
{
	GENERATED_BODY()

public:
	UCosmereSelfBuffAbility();

protected:
	virtual void OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void OnPowerEnded(bool bWasCancelled) override;

	/** Re-asserts the movement change and pays one slice of healing. Safe to call repeatedly. */
	void ApplyBuffTick();

	/**
	 * Registers this power's contribution to the four movement channels. Re-registering replaces the previous
	 * numbers, so an escalation or a per-tick re-assert costs nothing and never stacks on itself.
	 */
	void ApplyMovementModifiers(float Speed, float Jump, float Gravity, float Air);

	/** Takes this power's contribution back out, leaving every other source on the pawn alone. */
	void ClearMovementModifiers();

	//~ Begin metalmind. The buff numbers are the tap direction; storing is their exact inverse, which is why
	//~ one set of rows describes both halves of the metal.
	virtual void ApplyMetalmindTick() override;
	virtual void ClearMetalmindStats() override;
	//~ End metalmind

	/** True while the power runs only as long as the input is held, rather than for Duration. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Self")
	bool bChannelWhileHeld = false;

	/** Seconds the buff lasts. Ignored when bChannelWhileHeld is true. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Self", meta = (ClampMin = "0.0"))
	float Duration = 5.f;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Self", meta = (ClampMin = "0.1"))
	float SpeedMultiplier = 1.f;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Self", meta = (ClampMin = "0.1"))
	float JumpMultiplier = 1.f;

	/** Below 1 is lighter than normal, above 1 is heavier. Multiplies the build's own gravity scale. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Self", meta = (ClampMin = "0.05"))
	float GravityScaleMultiplier = 1.f;

	/**
	 * Mass multiplier while tapping, compounded per flare level: 2.2 becomes 2.2, 4.8, 10.6, 23.4, 51.5.
	 * 1 leaves the pawn's weight alone.
	 *
	 * Mass is the only physical quantity a metal should move. Fall speed, knockback and pull distance are all
	 * read back out of it by the pawn, so a power that wants one of those changes this and nothing else - which
	 * is also what keeps it from competing with an ability that owns gravity outright.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Self", meta = (ClampMin = "0.01"))
	float MassMultiplier = 1.f;

	/**
	 * Mass multiplier while storing. 0 falls back to the reciprocal of MassMultiplier, which is the usual shape;
	 * a metal whose stored state is meant to be far lighter than its tap is heavier sets this explicitly.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Self", meta = (ClampMin = "0.0"))
	float StoreMassMultiplier = 0.f;

	/** Added to the build's air control rather than multiplying it, which is what makes a glide glide. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Self", meta = (ClampMin = "0.0"))
	float AirControlBonus = 0.f;

	/** Health restored per second while the buff is running. 0 leaves health alone. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Self", meta = (ClampMin = "0.0"))
	float HealPerSecond = 0.f;

	/** Damage taken is scaled by (1 - this). 0.5 halves every hit for the duration. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Self", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float DamageResistance = 0.f;

	/**
	 * When true, DamageResistance is read as one step per flare level (10%, 20%, 30%...) instead of doubling
	 * with it. Iron and Pewter want a rate the player can predict: doubling a base of 0.5 reaches immunity at
	 * tier two, which leaves the rest of the ladder with nothing left to give.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Self")
	bool bLinearResistanceLadder = false;

	/**
	 * Multiplier on the damage the caster deals while the buff runs. 1 leaves it alone; Pewter is the row
	 * that sets it. The metalmind ladder raises the edge above 1 rather than the multiplier itself, so a
	 * level-1 tap is exactly this number and a level-3 tap is 1 + (this - 1) x 4.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Self", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float MeleeDamageMultiplier = 1.f;

	/** Hides the pawn for the duration. Stands in for the cloak-shaped powers until stealth exists. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Self")
	bool bHiddenWhileActive = false;

	/**
	 * Shakes off anything currently holding the pawn back: every movement modifier that makes the pawn slower or
	 * weaker is dropped, whoever registered it, before the buff applies its own contribution.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Self")
	bool bCleanse = false;

	/** How often ApplyBuffTick runs once the buff is live. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Self", meta = (ClampMin = "0.05"))
	float TickInterval = 0.25f;

private:
	void HandleDurationExpired();

	FTimerHandle TickTimer;
	FTimerHandle DurationTimer;

	TWeakObjectPtr<ACharacter> OwnerCharacter;

	/**
	 * The stack this power writes into. Resolved from the avatar every time rather than cached, because a metalmind
	 * press skips OnPowerActivated and those are the only two places the owner is known.
	 */
	class UCosmereMovementModifierComponent* GetMovementModifiers() const;
};

/** One hit at the crosshair: damage, a shove, a slow, or a drain of the victim's own power pool. */
UCLASS(Abstract)
class COSMERERIVALS_API UCosmereStrikeAbility : public UCosmerePowerAbility
{
	GENERATED_BODY()

public:
	UCosmereStrikeAbility();

protected:
	virtual void OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void OnPowerEnded(bool bWasCancelled) override;

	/** Finds what the crosshair is on. False when nothing is in reach. */
	bool ResolveStrike(FVector& OutHitPoint, AActor*& OutVictim, FVector& OutDirection) const;

	/** One application of the strike. Called once for a one-shot, every interval for a channel. */
	void ApplyStrikeTick();

	/**
	 * Spends the shove on both ends at once rather than on the victim alone.
	 *
	 * Direction is the caster's aim, so the victim is forced along +Direction and the caster along -Direction
	 * by the same amount: pushing steel throws you back off it, pulling iron drags you toward it. The force is
	 * one number for both ends - only how far it carries each body differs, and that is the body's own weight.
	 * When one end has nowhere to go (anchored metal, a back against a wall) its half transfers to the other
	 * end, doubled; when neither can move the force goes into both of them as damage.
	 */
	void ApplyShove(APawn* Caster, AActor* Victim, const FVector& Direction);

	/** True when a shove along ShoveDirection has somewhere to put this body. OtherEnd is exempt from the probe. */
	bool CanBeShoved(const AActor* Actor, const AActor* OtherEnd, const FVector& ShoveDirection) const;

	/** The velocity change this body takes from the shared force, quoted against a ReferenceMass body. */
	float GetShoveScale(const AActor* Actor, float ReferenceMass) const;

	/** Moves one body: a launch for a character, an impulse for a simulating prop. */
	void ShoveBody(AActor* Actor, const FVector& Force, float Scale);

	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Strike", meta = (ClampMin = "0.0"))
	float Range = 3000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Strike", meta = (ClampMin = "0.0"))
	float Damage = 20.f;

	/** Positive pushes the victim away, negative pulls it in. 0 leaves it where it stands. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Strike")
	float Impulse = 0.f;

	/** Fraction of the force added straight up, so a shove lofts instead of skidding. 0 keeps it flat. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Strike", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ShoveLiftFraction = 0.25f;

	/** Damage per 1000 of trapped impulse, to both ends, when neither of them can move. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Strike", meta = (ClampMin = "0.0"))
	float CrushDamagePerImpulse = 0.02f;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Strike", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float SlowMultiplier = 1.f;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Strike", meta = (ClampMin = "0.0"))
	float SlowDuration = 0.f;

	/** Removed from the victim's own power pool. 0 leaves it alone. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Strike", meta = (ClampMin = "0.0"))
	float ResourceDrain = 0.f;

	/**
	 * True when this strike works on the mind and soul rather than on the body: zinc's riot, brass's soothe,
	 * chromium's leech. Copper wards those off. Iron's pull, steel's push and a pewter blow are met by the
	 * body instead, which is what iron and pewter store against.
	 *
	 * One flag rather than a category per number, because a power is one thing or the other - a strike that is
	 * half physical is a strike that wants splitting into two rows.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Strike")
	bool bDamageIsSpiritual = false;

	/** The fraction of a physical effect Victim still takes, after whatever their own body shrugs off. */
	float GetPhysicalYield(AActor* Victim);

	/** The fraction of an invested effect Victim still takes, after whatever wards their soul. */
	float GetSpiritualYield(AActor* Victim);

	/** Held rather than one-shot: the strike stays live until the input is released. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Strike")
	bool bChannelWhileHeld = false;

	/** Seconds between strikes while channelling. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Strike", meta = (ClampMin = "0.05"))
	float ChannelTickInterval = 0.5f;

private:
	void RestoreSlowedVictim();

	FTimerHandle ChannelTimer;
	FTimerHandle SlowTimer;

	TWeakObjectPtr<ACharacter> SlowedVictim;
	bool bSlowPending = false;
};

/** A bubble centred on the aim point that hurries or stalls whatever stands inside it. */
UCLASS(Abstract)
class COSMERERIVALS_API UCosmereFieldAbility : public UCosmerePowerAbility
{
	GENERATED_BODY()

public:
	UCosmereFieldAbility();

protected:
	virtual void OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void OnPowerEnded(bool bWasCancelled) override;

	void ApplyFieldTick();

	/** Registers or refreshes the bubble's contribution on one character. Safe to call on every tick. */
	void ApplyFieldTo(ACharacter* Target);

	void RestoreAffected();
	void HandleDurationExpired();

	/** Optional visual. The bubble is fully functional without it, exactly as Division's field is. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Field")
	TSubclassOf<AActor> FieldClass;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Field", meta = (ClampMin = "100.0"))
	float Radius = 900.f;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Field", meta = (ClampMin = "0.1"))
	float Duration = 6.f;

	/** Applied to every character standing in the bubble. Below 1 stalls, above 1 hurries. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Field", meta = (ClampMin = "0.05"))
	float SpeedMultiplier = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Field")
	bool bAffectSelf = false;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Field")
	bool bAffectOthers = true;

	/** 0 places the bubble on the caster; greater than 0 places it where the crosshair lands. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Field", meta = (ClampMin = "0.0"))
	float Range = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Field", meta = (ClampMin = "0.05"))
	float TickInterval = 0.25f;

private:
	FTimerHandle FieldTickTimer;
	FTimerHandle DurationTimer;

	TObjectPtr<AActor> ActiveField;

	FVector FieldCentre = FVector::ZeroVector;

	/**
	 * Everyone the bubble has touched. The slow itself lives on their own stacks under this power's source, so
	 * clearing it takes exactly the bubble's contribution and leaves the rest of their modifiers alone.
	 */
	TArray<TWeakObjectPtr<ACharacter>> Affected;
};

/** Instant repositioning to wherever the crosshair lands. */
UCLASS(Abstract)
class COSMERERIVALS_API UCosmereBlinkAbility : public UCosmerePowerAbility
{
	GENERATED_BODY()

public:
	UCosmereBlinkAbility();

protected:
	virtual void OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Blink", meta = (ClampMin = "100.0"))
	float Range = 1800.f;

	/** Distance kept short of the surface that was hit, so the destination is not inside it. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Blink", meta = (ClampMin = "0.0"))
	float StandOff = 60.f;

	/** Lift applied on arrival, so a blink into a floor lands on it rather than in it. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Blink")
	float DestinationLift = 90.f;
};

/**
 * A blink that carries the caster and everyone standing near them, instead of the caster alone.
 *
 * Grounded: each affected pawn is carried along the direction it is already travelling, stopping short
 * of whatever the trace finds rather than tunnelling through it.
 *
 * Airborne: there is no travel direction to inherit, so the blink becomes a drop instead - straight
 * down to the first floor within reach, or the full distance when there is none. That is what makes the
 * power a recovery as well as an engage: it re-grounds a falling player rather than moving them
 * sideways while they fall.
 *
 * The distance travelled is Radius. Deliberately one number and not two, because the reach that decides
 * who is carried and the distance each of them is carried are the same measurement - a second property
 * could be tuned away from the first and leave the power describing two different ranges.
 *
 * Self is moved on both machines and everyone else on the authority only. The caster's own pawn is
 * simulated by its owning client, so a blink only the server performed would be argued with by the next
 * move that client sent; a remote pawn has no such claim, and moving one on a client would only be
 * snapped back when the next update arrived.
 */
UCLASS(Abstract)
class COSMERERIVALS_API UCosmereGroupBlinkAbility : public UCosmerePowerAbility
{
	GENERATED_BODY()

public:
	UCosmereGroupBlinkAbility();

protected:
	virtual void OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	/**
	 * Moves one pawn. Grounded pawns go sideways along their own velocity, airborne pawns go down.
	 *
	 * Returns whether the pawn actually ended up somewhere new: a blink into a wall the pawn is already
	 * standing against is a legitimate no-op rather than a failure.
	 */
	bool BlinkPawn(ACharacter* Target) const;

	/** The caster plus everyone within this of them, and how far each of them is carried. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|GroupBlink", meta = (ClampMin = "100.0"))
	float Radius = 1100.f;

	/** True carries the caster as well as the bystanders. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|GroupBlink")
	bool bAffectSelf = true;

	/** True carries everyone else standing in the radius. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|GroupBlink")
	bool bAffectOthers = true;

	/** Distance kept short of a surface the sideways trace found, so the capsule does not arrive inside it. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|GroupBlink", meta = (ClampMin = "0.0"))
	float StandOff = 60.f;
};

/** Marks nearby pawns so the local HUD draws them through geometry for a while. */
UCLASS(Abstract)
class COSMERERIVALS_API UCosmereRevealAbility : public UCosmerePowerAbility
{
	GENERATED_BODY()

public:
	UCosmereRevealAbility();

protected:
	virtual void OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Reveal", meta = (ClampMin = "100.0"))
	float Radius = 6000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Reveal", meta = (ClampMin = "0.1"))
	float Duration = 6.f;

	/** Normally the caster is left unmarked - they already know where they are. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Reveal")
	bool bIncludeSelf = false;
};

/** Hands health and power back to whoever is under the crosshair, falling back to the caster. */
UCLASS(Abstract)
class COSMERERIVALS_API UCosmereSupportAbility : public UCosmerePowerAbility
{
	GENERATED_BODY()

public:
	UCosmereSupportAbility();

protected:
	virtual void OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Support", meta = (ClampMin = "0.0"))
	float Range = 3000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Support", meta = (ClampMin = "0.0"))
	float HealAmount = 30.f;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Support", meta = (ClampMin = "0.0"))
	float ResourceRefill = 0.f;

	/** When nothing is under the crosshair, the caster receives the effect instead. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power|Support")
	bool bFallBackToSelf = true;
};
