// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "Abilities/GameplayAbility.h"
#include "Data/CosmereTypes.h"
#include "GameFramework/Character.h"
#include "CosmereCharacter.generated.h"

class FLifetimeProperty;

class ACosmerePlayerState;
class UCosmereAbilitySystemComponent;
class UCosmereAttributeSet;
class UCosmereWeaponComponent;
class UCosmerePowerAbility;
class UCosmereMovementModifierComponent;
class UCosmereGravityComponent;
class UCosmereTinSensesComponent;
class UCosmereInvestitureComponent;
class UCosmereRealmComponent;
class USoundBase;
class UInputMappingContext;
class ULineBatchComponent;

/**
 * Fired once per successful ApplyBuildToPawn, on the authority and on the client alike, after the
 * stats have already been written. This is the seam downstream systems hook.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCosmereBuildApplied, const FSpiritwebCharacterSave&, Build);

/**
 * Fired once when this pawn is killed.
 *
 * Killer is the controller credited with the hit, or null when the damage could not be attributed -
 * a fall, world damage, or a shot whose instigator has since disconnected.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCosmerePawnDied, AController*, Killer);

/**
 * The match pawn. Two jobs: hand the player's accepted build down onto this actor, and carry the
 * ability system's avatar link.
 *
 * Why the movement stats are written here rather than by a GameplayEffect: GAS cannot modify a
 * CharacterMovementComponent. Run speed, jump velocity, gravity scale and air control are CMC
 * properties, so the race row has to be pushed onto the component explicitly. Only Health and
 * MaxHealth are attributes.
 *
 * Naming note: BP_FirstPersonCharacter already declares its own Move and Aim functions, so do not
 * add members with those names here - they would collide on reparent.
 */
UCLASS()
class COSMERERIVALS_API ACosmereCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ACosmereCharacter();

	//~ Begin AActor interface
	/**
	 * Clamps falling speed along the pawn's own gravity axis.
	 *
	 * UCharacterMovementComponent has no terminal-velocity property to write in 5.8 - falling is integrated
	 * from GravityScale with no ceiling - so the pawn is the only place a mass-driven fall speed can live.
	 * It is also the only place that can do it correctly once a Gravitation lashing has turned a wall into the
	 * floor: the ceiling has to be measured against GetGravityDirection(), not against world Z.
	 */
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Rolls the view onto the capsule's own up vector whenever the capsule is off vertical.
	 *
	 * A lashing has to make the surface underfoot the floor, and the body is the thing that lies onto it. A
	 * view left world-upright would instead show the pawn sliding sideways down a wall, so the picture has to
	 * turn with the body. The roll is read back from the capsule every frame rather than animated on a rate
	 * of its own, which makes it exactly as fast as the body with nothing to tune and nothing to drift.
	 *
	 * Only the roll about the view's forward axis changes, so the forward vector survives untouched: the world
	 * turns and the crosshair does not. While the capsule is upright this returns Super's rotation unchanged.
	 */
	virtual FRotator GetViewRotation() const override;
	/**
	 * Takes look input about the axes the picture is actually showing.
	 *
	 * AddControllerYawInput and AddControllerPitchInput always rotate about the WORLD axes, which is the
	 * same thing as the screen only while the view is upright. Once GetViewRotation has rolled the camera
	 * onto a wall or a ceiling, world right is no longer screen right and the mouse reads inverted or
	 * diagonal. Both overrides hand the input to Super first, so the amount, the scaling and the
	 * ignore-look gate stay the engine's, then spend it about the screen axes instead.
	 */
	virtual void AddControllerYawInput(float Val) override;
	virtual void AddControllerPitchInput(float Val) override;
	//~ End AActor interface
	
	/**
	 * True while a free 3-axis orientation is driving the picture instead of the body's surface frame.
	 *
	 * This is the Gravitation feel. Once a lashing has the pawn off the floor the view stops following the
	 * capsule, so a gravity flip turns the world around a crosshair that does not move, and the aim can be
	 * carried straight past straight up. Landing puts the aim back into the landed surface's frame, which is
	 * the lock. A query rather than a setter: both ends of that transition are decided here.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Gravity")
	bool HasFreeAim() const { return bFreeAimActive; }
	
	/**
	 * Records a gravity component this player's gravitation reserve is now paying for.
	 *
	 * Called by whatever applied the lash, so running dry can take down everything the reserve is holding up
	 * rather than only the caller's own pawn. The player's own component is the only entry today, and it is
	 * registered through this same call - being the caster is not a special case, it is just the usual target.
	 */
	void RegisterLashTarget(UCosmereGravityComponent* Target);
	
	//~ Begin IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface

	/**
	 * Pushes Build's race stats onto this pawn: run speed / jump / gravity / air control onto the
	 * movement component, and health onto the attribute set.
	 *
	 * A hero build resolves its race through the hero row rather than Build.RaceId. A prebuilt hero
	 * keeps the manually built kit underneath it so that clearing the hero restores that kit, which
	 * means RaceId can legitimately be stale on a hero build.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Build")
	void ApplyBuildToPawn(const FSpiritwebCharacterSave& Build);

	/** True once a build has been applied to this pawn. False means the pawn is still on defaults. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Build")
	bool HasAppliedBuild() const { return bAppliedBuild; }

	/**
	 * The build this pawn is currently running, cached at apply time.
	 *
	 * Deliberately a pawn-local copy rather than a read-back from the PlayerState, so a system can
	 * be driven from the pawn alone and does not inherit the PlayerState's replication semantics.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Build")
	FSpiritwebCharacterSave GetAppliedBuild() const { return AppliedBuild; }

	/**
	 * The seam every downstream system should hook - weapons, powers, HUD.
	 *
	 * Fires exactly once per successful ApplyBuildToPawn, after the stats are written, and fires
	 * identically whether the build arrived on the authority or was replicated in. Binding here
	 * rather than to the PlayerState's replication callback is what keeps downstream systems
	 * independent of how the build travels.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Cosmere|Build")
	FCosmereBuildApplied OnBuildApplied;
	
	/**
	 * Presses or releases the ability granted for one of this build's two power slots.
	 *
	 * The abilities themselves already know whether they are a held channel or a latch - a held power is
	 * switched off with EndPower on release, and a latch is switched off with EndPower on the second press
	 * and cannot be re-activated while it is running. Rather than have the input layer re-derive that, this
	 * always activates on a press and always calls EndPower on a release, and the ability decides what that
	 * means. A latch therefore needs its input wired to the same seam both ways, which is what a
	 * Started/Completed pair on one action gives it.
	 *
	 * The slot is resolved through the PlayerState's submitted build rather than through a cached spec
	 * handle, so this still works on the frame a build lands. Returns false when no ability is granted for
	 * that slot yet, which is the normal answer before the build has been applied.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Power")
	bool SetPowerSlotHeld(EPowerSlot Slot, bool bHeld);

	//~ Begin life state. Health itself lives on the PlayerState's attribute set, so that it outlives
	//~ a pawn; these are the pawn-side reads and the death seam.
	/** True while this pawn can still act. False from death until the respawn hands it a new pawn. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Health")
	bool IsAlive() const { return !bIsDead; }

	UFUNCTION(BlueprintPure, Category = "Cosmere|Health")
	float GetHealth() const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Health")
	float GetMaxHealth() const;
	//~ End life state

	//~ Begin power seams. Read by the archetype powers in Power/CosmerePowerArchetypes.h, so a power can
	//~ lean on the pawn for the two things it cannot own itself: the build's run speed, and damage.
	/**
	 * The run speed this pawn's build asked for, before any power touched it.
	 *
	 * A power that puts movement back restores it to here rather than to a snapshot taken on the way in,
	 * because a snapshot taken while the pawn was already slowed would freeze that slow in place.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	float GetBaseWalkSpeed() const { return BaseWalkSpeed; }

	/** Jump launch velocity the race row supplied. See GetBaseWalkSpeed(). */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Movement")
	float GetBaseJumpVelocity() const { return BaseJumpVelocity; }

	/** Gravity scale the race row supplied. See GetBaseWalkSpeed(). */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Movement")
	float GetBaseGravityScale() const { return BaseGravityScale; }

	/** Air control the race row supplied. See GetBaseWalkSpeed(). */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Movement")
	float GetBaseAirControl() const { return BaseAirControl; }

	/**
	 * The stack every movement-changing system writes into. Nothing assigns MaxWalkSpeed, JumpZVelocity,
	 * GravityScale or AirControl directly: a system registers a named modifier here and this resolves all of
	 * them against the race's own numbers, so any number of sources compose instead of overwriting each other.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Movement")
	UCosmereMovementModifierComponent* GetMovementModifiers() const;

	/**
	 * This pawn's own gravity: the latched lashing direction, its flare level, and the re-orientation that
	 * turns a wall or a ceiling into a floor.
	 *
	 * Separate from the movement modifier stack because it owns a direction rather than a number, and because
	 * it has to keep re-examining that direction after the ability that set it is long over - landing on a
	 * surface is what refines it. It reaches GravityScale only through the modifier stack, like everything else.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Gravity")
	UCosmereGravityComponent* GetGravityComponent() const;

	/**
	 * Arms the flight aim at the instant a gravitation press latches, before the capsule begins its slew.
	 *
	 * The seed has to be taken here rather than on the first frame the pawn is airborne: LashRotationRateTilt
	 * starts turning the body the moment the gravity direction is written, so a seed taken a frame later is a
	 * seed taken from a picture that has already rolled - which is the camera moving as gravitation is pressed.
	 * Read between that write and the first PhysicsRotation, the seed is the exact orientation on screen, and
	 * engaging the flight aim moves nothing at all.
	 *
	 * Called by UCosmereGravityComponent::Lash. Does nothing while a flight is already running, so a repeat press
	 * that levels the lash cannot re-seed and cannot move the view.
	 */
	void BeginLashFreeAim();

	/**
	 * Burning tin: the doubled senses, the dark vision, and the hearing that reaches through walls.
	 *
	 * The component is authored here so every pawn has one and no Blueprint has to remember to add it. It is
	 * inert until a tin power pushes a level into it, and every effect it drives is local - the burn is the
	 * only thing both machines agree on.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Tin")
	UCosmereTinSensesComponent* GetTinSensesComponent() const;

	/**
	 * What other people's investiture runs into on this pawn: the two resistances, and the copper cloud over it.
	 *
	 * Authored here for the same reason tin is - every pawn carries one and no Blueprint has to remember to add
	 * it. It stays empty until a burn writes into it, and a copper that stops re-asserting its shield leaves
	 * nothing behind, so a pawn that never burns copper costs nothing.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Investiture")
	UCosmereInvestitureComponent* GetInvestitureComponent() const;

	/**
	 * Which side of the veil this pawn is standing on, and everything that follows from it: the travel speed,
	 * the colour grade, the body that steps through the other realm's bodies.
	 *
	 * Authored here for the same reason tin is - every pawn carries one, and a pawn that never phases costs
	 * nothing. Every cross-veil question in the project is asked through ShareRealm rather than through this
	 * getter, so a system that has to work on a wall or a prop as well as on a body never needs the cast.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Realm")
	UCosmereRealmComponent* GetRealmComponent() const;

	/**
	 * True while this pawn is burning any metal at any level, which is the one fact bronze listens for.
	 *
	 * The level lives on the ability rather than on the pawn, so this asks every granted power instead of
	 * reading a cached flag: a burn that ran out of metal, or one the input layer put out, is already at level
	 * 0 by the time this looks, and nothing has to remember to clear anything for it to be correct.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Investiture")
	bool IsBurningPower() const;

	/**
	 * Reports a noise this pawn just made, so every machine can place it by ear.
	 *
	 * A server call because the pawns that make the interesting noises - a footstep, a landed jump - are
	 * simulated on their own client and nothing about them reaches the other machines on its own. The
	 * authority is the only side that can fan one noise out to everybody, and it is the side that already
	 * owns every other shared fact in a match.
	 */
	UFUNCTION(Server, Unreliable)
	void ServerReportNoise(USoundBase* Sound, FVector Location, float Loudness);

	/**
	 * The fraction of incoming damage this pawn currently ignores, composed from every active source.
	 *
	 * Sources multiply rather than add: two 50% sources leave a quarter of the hit, not nothing. The
	 * composed result is not capped, so a fully flared metal can reach complete immunity - which is the
	 * whole point of storing that much of it.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	float GetDamageResistance() const;

	/**
	 * Opens a resistance window under Source. Re-writing the same Source replaces its own number rather than
	 * stacking with it, so a metalmind re-asserting itself ten times a second costs nothing.
	 *
	 * A stack on the pawn rather than a GameplayEffect: UCosmereAttributeSet is deliberately health-only, so
	 * there is no attribute to hang a resistance on, and this is what TakeDamage already reads. Fractions are
	 * clamped to 0..1 per source; the composition of them is not clamped at all.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Power")
	void SetDamageResistanceForSource(FName Source, float Fraction);

	/** Takes Source's contribution back out, leaving every other source on the pawn alone. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Power")
	void ClearDamageResistanceSource(FName Source);

	/**
	 * Drops every open window at once. A purge power wants this; a power ending wants
	 * ClearDamageResistanceSource, which cannot silence the metal somebody else is still burning.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Power")
	void ClearAllDamageResistance();

	/** Single-number write for a caller that has no name of its own. Lands under a fixed legacy source. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Power")
	void SetDamageResistance(float NewResistance);

	//~ Begin mass. Iron is the only metal that moves this, and it is the only physical quantity a metal should
	//~ move: fall speed, knockback and pull distance are all read back out of it rather than set alongside it.
	//~ One number to tune, and no way for two systems to disagree about how heavy the pawn is.

	/** Mass in kg with no metalmind open. See GetMass(). */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Mass")
	float GetBaseMass() const { return BaseMass; }

	/** Mass in kg right now: the base times every open multiplier. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Mass")
	float GetMass() const;

	/** GetMass() over GetBaseMass(): 1 is the pawn's own weight, 0.1 is a tenth of it. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Mass")
	float GetMassRatio() const;

	/**
	 * How much faster or slower this pawn should fall, derived from its mass rather than set by any power.
	 *
	 * The ratio raised to FallSpeedMassExponent, which defaults to 1 so that storing weight reads as a clean 90%
	 * slow. Halve the exponent for the square root quadratic drag actually gives, at the cost of the metals no
	 * longer landing on round percentages.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Mass")
	float GetFallSpeedScale() const;

	/**
	 * Multiplier on a fixed impulse applied to this pawn, derived from its mass.
	 *
	 * Delta-v is impulse over mass, so a tenth of the weight means ten times the shove. This is what makes a
	 * stored metalmind get thrown across a room and a flared one shrug the same hit off.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Mass")
	float GetLaunchScale() const;

	/**
	 * The fastest this pawn may fall, in cm/s, along its own gravity axis.
	 *
	 * Derived from the mass ratio for the same reason the launch scale is: a stored metalmind should drift down
	 * and a flared one should drop like a stone. Derived rather than registered on the movement stack, so a
	 * CleansePenalties cannot strip a stored metal's slow and leave the pawn falling at normal speed.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Mass")
	float GetMaxFallSpeed() const;

	/** Opens or replaces Source's mass multiplier. A neutral multiplier is a removal, not an entry. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Mass")
	void SetMassMultiplierForSource(FName Source, float Multiplier);

	/** Takes Source's contribution back out, leaving every other source alone. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Mass")
	void ClearMassMultiplierSource(FName Source);

	/** Multiplier on the damage this pawn deals, 1 being unaided. See SetMeleeDamageMultiplier. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	float GetMeleeDamageMultiplier() const { return MeleeDamageMultiplier; }

	/**
	 * Scales the damage this pawn deals. Clamped to 0.1..5, because a strength power should make a hit
	 * hurt, not delete the other player or make the pawn unable to fight at all.
	 *
	 * A scalar on the pawn for the same reason DamageResistance is one: UCosmereAttributeSet is health-only,
	 * so there is no attribute to hang an outgoing multiplier on. The weapon component reads it at the one
	 * funnel every hit this pawn lands passes through.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Power")
	void SetMeleeDamageMultiplier(float NewMultiplier);

	/**
	 * Marks Actors as visible through geometry for this player until Duration seconds have passed.
	 *
	 * Deliberately local and unreplicated: a reveal is the caster's own information, so it lives on their
	 * pawn and their HUD reads it. Nothing about the marked actor changes, which is why this needs no state
	 * on the victim and nothing sent to it.
	 */
	void RevealActors(const TArray<AActor*>& Actors, float Duration);

	/** True while Actor is still inside a reveal window this pawn opened. Drives the HUD's second pass. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	bool IsRevealed(const AActor* Actor) const;

	/** The DT_Powers row the build put in Slot, or NAME_None before a build lands. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	FName GetPowerSlotId(EPowerSlot Slot) const;

	/**
	 * How far through Slot's cooldown is, 0..1, where 1 means the next press would be accepted.
	 *
	 * A slot with no granted ability, or a row whose cooldown is 0, reads 1: a power that is always
	 * available must not draw itself as charging.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	float GetPowerSlotCooldownFraction(EPowerSlot Slot) const;

	/**
	 * Seconds since the last press on Slot was refused because the power was unaffordable or still on
	 * its cooldown. A large number means nothing has been refused yet.
	 *
	 * The refusal is recorded here rather than read back off the ability, because "I pressed it and
	 * nothing happened" is input state. The pool is queryable from the PlayerState and the cooldown lives
	 * on the row, but no other object sees the press - and the press is the whole event the HUD flashes on.
	 *
	 * Held locally and deliberately not replicated: a press belongs to the player who made it, and only
	 * their own HUD draws the result.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	float GetPowerSlotRefusedAge(EPowerSlot Slot) const;

	/**
	 * True when Slot's DT_Powers row is Feruchemical, so the slot runs a metalmind rather than a cooldown.
	 * Reads the same build and catalog the press path does, so a slot that cannot resolve reads as false.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	bool IsMetalmindSlot(EPowerSlot Slot) const;

	/**
	 * Which of the three economies Slot's DT_Powers row spends.
	 *
	 * The HUD needs it because the three are not the same bar. A Feruchemical slot draws its own metalmind, a
	 * Surge slot draws that surge's own reserve, and everything else draws the shared power pool - and a slot
	 * drawn off the wrong one is a bar that never moves when the power is used.
	 *
	 * Reads the build and the catalog the same way IsMetalmindSlot does, so the two agree about a slot. A row
	 * that cannot be resolved reads as Allomantic, which is the shared-pool answer and the harmless one: an
	 * unknown row then draws exactly what it drew before surges had reserves at all.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	EPowerSource GetPowerSlotSource(EPowerSlot Slot) const;

	/** Slot's metalmind reserve as 0..1. 0 for a slot that is not a metalmind, or has no pawn state yet. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	float GetMetalmindReserveFraction(EPowerSlot Slot) const;

	/** Slot's metalmind reserve in units, which is the scale a power's per-second rate is quoted in. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Power")
	float GetMetalmindReserveUnits(EPowerSlot Slot) const;

	/**
	 * Moves Slot's metalmind reserve by DeltaUnits, clamped to 0..MetalmindCapacity, and returns how much
	 * actually moved, signed. Authority only, and the negative result of an empty metalmind is what tells
	 * the power to end itself.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Power")
	float AddMetalmindReserve(EPowerSlot Slot, float DeltaUnits);
	//~ End power seams

	/** Fired once when this pawn dies, before the respawn timer is started. */
	UPROPERTY(BlueprintAssignable, Category = "Cosmere|Health")
	FCosmerePawnDied OnCosmereDied;

protected:
	//~ Begin AActor interface
	virtual void BeginPlay() override;
	virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void PawnClientRestart() override;
	virtual void OnRep_PlayerState() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor interface

	/** Shared body of the two look overrides; see AddControllerYawInput. */
	void ApplyFramedLookInput();
	
	/** Re-decides which frame the aim belongs in, seeding the free aim as it engages. */
	void RefreshFreeAim();
	
	/** Slews the surface frame's horizon toward the floor the pawn is standing on. */
	void UpdateSurfaceHorizon(float DeltaSeconds);
	
	/** Whether the aim belongs in the free frame rather than in the body's surface frame. */
	bool ShouldUseFreeAim() const;
	
	/** Redraws the owner-only gravity-axis reference line. */
	void UpdateGravityReferenceLine();
	
	/** Drops every lash this player's gravitation reserve has been paying for, once that reserve is empty. */
	void EnforceLashReserve();
	
	/** This player's remaining gravitation reserve, 0..1. Reads 1 when there is no reserve to read. */
	float GetGravitationSurgeFraction() const;
	
	/**
	 * The free aim: its own orientation, accumulated about its own axes.
	 *
	 * Its own orientation rather than the body's with a correction is the whole difference - that is what makes
	 * it six degrees of freedom instead of four. Only read while bFreeAimActive, and always seeded from the
	 * picture already on screen, so engaging it cannot move the view by a single degree.
	 */
	FQuat FreeAimRotation = FQuat::Identity;
	
	/**
	 * The up axis the free aim is rolled against, captured as the lash engages and held for the whole flight.
	 *
	 * Re-deriving the roll from the body every frame is what used to turn the picture as the capsule slewed into
	 * the new gravity frame. With this held, the aim can turn and pitch all it likes and its roll does not move -
	 * the view the player took off with is the view they fly with, and the only time it changes is the snap, when
	 * the aim hands back to the surface it has landed on.
	 */
	FVector FreeAimRollReference = FVector::UpVector;

	/**
	 * How far the aim may pitch off the horizon, in degrees, in any frame that is not the world's own.
	 *
	 * 87 rather than 90 because the pitch maths degenerates exactly at the pole: the forward vector becomes
	 * parallel to the axis it is being turned about, yaw stops meaning anything, and the picture rolls over.
	 * Three degrees of margin is enough to look straight at a ceiling or a floor without ever arriving on the
	 * singularity, and it is measured against whichever up the view is currently in - the held horizon while
	 * free-aiming, the pawn's own gravity frame once it has landed.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Gravity", meta = (ClampMin = "45.0", ClampMax = "89.0"))
	float CameraPitchLimitDegrees = 87.f;
	
	/**
	 * The up axis the surface frame is rolled against, and whether it is currently seeded.
	 *
	 * Read by GetViewRotation in place of the capsule's own up while a lash has the pawn standing on a floor.
	 * The capsule is only re-pointed by PhysicsRotation where a floor check tells it to be, which on anything
	 * curved arrives as one discrete step per facet; slewing this instead turns those steps into a horizon
	 * that follows the surface. Everywhere else the body's own up is used, exactly as before.
	 *
	 * Invalidated whenever that frame is not the one on screen, so it never carries across a flight or a jump
	 * and always comes back seeded from the body that is actually being drawn.
	 */
	FVector SurfaceHorizonUp = FVector::UpVector;
	bool bSurfaceHorizonValid = false;
	
	/**
	 * How fast the surface frame's horizon follows a changing floor, in degrees per second.
	 *
	 * The only rate this file adds. It is deliberately slower than LashRotationRateTilt, because its whole job
	 * is to be the part of the turn the body cannot supply: the capsule snaps a facet at a time, this fills in
	 * between.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Gravity", meta = (ClampMin = "0.0"))
	float SurfaceHorizonDegreesPerSecond = 90.f;
	
	/** True while FreeAimRotation is driving the picture. Maintained by RefreshFreeAim. */
	bool bFreeAimActive = false;

	/**
	 * True once a gravitation press has asked for the flight aim, and false again as soon as that flight ends.
	 *
	 * This is what keeps the flight aim a press rather than a state. Nothing else may arm it: without this, any
	 * frame spent off the ground while a lash is still latched - a jump off the wall the pawn has already settled
	 * on - reads as flight and puts the player into an aim they never asked for.
	 */
	bool bFreeAimArmed = false;
	
	/** How far past the capsule the gravity reference line runs, in cm. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Gravity", meta = (ClampMin = "0.0"))
	float GravityReferenceOverhang = 60.f;
	
	/**
	 * Colour of the gravity reference line. Blue, so it reads as the gravity axis and not as a surface.
	 *
	 * Alpha 0.1, i.e. ninety percent transparent. LineBatchComponent draws through DrawTranslucentLine, so the
	 * alpha is a real blend rather than a mask: the line lays a hint over the world instead of painting on it.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Gravity")
	FLinearColor GravityReferenceColor = FLinearColor(0.05f, 0.35f, 1.f, 0.1f);
	
	
	/**
	 * Every gravitation target this player's reserve is currently paying for.
	 *
	 * Weak, because a target can be destroyed or can clear its own lashing without telling anyone, and a list
	 * that could keep an actor alive would be a worse bug than the one it is tracking. The player's own gravity
	 * component is the only entry today; the list is what makes "the reserve pays for the lash" one rule rather
	 * than a special case for self.
	 */
	TArray<TWeakObjectPtr<UCosmereGravityComponent>> LashTargets;

	/** Draws the gravity reference line. Owned here so no Blueprint has to add it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cosmere|Gravity")
	TObjectPtr<ULineBatchComponent> GravityReferenceLines;

	/** Bound to the PlayerState's OnBuildReplicated so a late-arriving build still gets applied. */
	UFUNCTION()
	void HandleBuildReplicated(bool bHasBuild);

	/** The owning PlayerState, or null if this pawn does not have one yet. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Build")
	ACosmerePlayerState* GetCosmerePlayerState() const;

	//~ Begin metalmind input. A Feruchemical press has three readings - hold, tap, double tap - and they are
	//~ all decided here rather than in the power, because they are properties of the press and not of the row.

	/** Fires when a press has been held long enough to read as storing rather than as a tap. */
	void StartMetalmindStore(EPowerSlot Slot, FGameplayAbilitySpecHandle Handle);

	/** Full metalmind, in reserve units. How fast it fills and empties is per-power, on the ability. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power", meta = (ClampMin = "1.0"))
	float MetalmindCapacity = 200.f;

	/** Index 0 is EPowerSlot::Alpha and 1 is Beta, the same pairing PowerIds uses. */
	UPROPERTY(Replicated)
	float MetalmindReserve[2] = { 0.f, 0.f };

	/** Pending until it elapses (a store) or the release cancels it (a tap). */
	FTimerHandle PowerHoldTimers[2];
	bool bPowerHoldClassified[2] = { false, false };
	double LastPowerTapTime[2] = { -1000.0, -1000.0 };

	/**
	 * Moves metal into every slot whose row names a passive stand-still rate. Ticked rather than event-driven
	 * because standing still is a state and not an input, and authority only because the reserve is.
	 */
	void AccrueMetalmindPassive();

	FTimerHandle MetalmindPassiveTimer;

	/** How often the passive accrual runs. Coarse on purpose: nothing reacts to it frame to frame. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power", meta = (ClampMin = "0.02"))
	float MetalmindPassiveInterval = 0.25f;

	/** Horizontal speed in cm/s at or below which the pawn counts as standing still. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Power", meta = (ClampMin = "0.0"))
	float MetalmindPassiveStillSpeed = 10.f;

	//~ End metalmind input

	/**
	 * Points the ASC's actor info at this pawn and re-subscribes to the build delegate. Idempotent,
	 * so it is safe to run from both the authority and the client path.
	 */
	void BindToPlayerState();

	/**
	 * Registers MatchMappingContext for the duration of the match, at a priority above the hub
	 * contexts. Deliberately a separate context registered from the pawn rather than an edit to the
	 * player controller's existing registrations.
	 */
	void RegisterMatchMappingContext();

	/**
	 * Match-only input context. Left unset by default, in which case registration logs and does
	 * nothing - the hub contexts still apply and movement keeps working.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Input")
	TObjectPtr<UInputMappingContext> MatchMappingContext;

	/** Priority for MatchMappingContext, above the hub contexts so match input wins any tie. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Input")
	int32 MatchMappingContextPriority = 1;

	/** Set by ApplyBuildToPawn. Guards against applying the same build twice on one pawn. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Cosmere|Build")
	bool bAppliedBuild = false;

	/** The pawn-local copy of the applied build. See GetAppliedBuild(). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Cosmere|Build")
	FSpiritwebCharacterSave AppliedBuild;

	/** Written by ApplyBuildToPawn from the resolved race row. See GetBaseWalkSpeed(). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Cosmere|Power")
	float BaseWalkSpeed = 600.f;

	/** Written by ApplyBuildToPawn from the resolved race row. See GetBaseJumpVelocity(). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Cosmere|Movement")
	float BaseJumpVelocity = 420.f;

	/** Written by ApplyBuildToPawn from the resolved race row. See GetBaseGravityScale(). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Cosmere|Movement")
	float BaseGravityScale = 1.f;

	/** Written by ApplyBuildToPawn from the resolved race row. See GetBaseAirControl(). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Cosmere|Movement")
	float BaseAirControl = 0.6f;

	/**
	 * Terminal velocity at mass ratio 1, in cm/s. Scaled by GetFallSpeedScale(), which is where mass enters.
	 *
	 * Set here rather than read from the movement component because there is nothing on it to read in 5.8.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cosmere|Movement", meta = (ClampMin = "100.0"))
	float BaseMaxFallSpeed = 1200.f;

	/** The single writer of this pawn's movement values. Every speed source on the pawn goes through it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cosmere|Movement")
	TObjectPtr<UCosmereMovementModifierComponent> MovementModifiers;

	/** Owns this pawn's gravity direction and the re-orientation a lashing needs. See GetGravityComponent(). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cosmere|Gravity")
	TObjectPtr<UCosmereGravityComponent> GravityComponent;

	/** Owns the burn level and the local picture tin draws. See GetTinSensesComponent(). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cosmere|Tin")
	TObjectPtr<UCosmereTinSensesComponent> TinSenses;

	/** Owns this pawn's resistances and the copper cloud over it. See GetInvestitureComponent(). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cosmere|Investiture")
	TObjectPtr<UCosmereInvestitureComponent> Investiture;

	/** Owns this pawn's realm and the local picture phasing draws. See GetRealmComponent(). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cosmere|Realm")
	TObjectPtr<UCosmereRealmComponent> RealmComponent;

	/**
	 * One fraction per contributing source, keyed by the source's own name. Composed in GetDamageResistance.
	 *
	 * Keyed rather than a single value because two metals burning at once are two windows, not a contest:
	 * Iron and Pewter each hold their own entry instead of overwriting each other ten times a second.
	 */
	UPROPERTY(Transient)
	TMap<FName, float> DamageResistanceBySource;

	/** Mass in kg the build carries with nothing burning. Per pawn for now; a race row can override it later. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Mass", meta = (ClampMin = "1.0"))
	float BaseMass = 80.f;

	/**
	 * Exponent tying fall speed to mass. 1 makes fall speed track the ratio exactly, so a tenth of the weight
	 * falls at a tenth of the speed; 0.5 is the square root quadratic drag actually gives, which would land a
	 * stored metal at 32% instead of 10% and soften every number the metals were tuned against.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Mass", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float FallSpeedMassExponent = 1.f;

	/** One multiplier per contributing source. See SetMassMultiplierForSource. */
	UPROPERTY(Transient)
	TMap<FName, float> MassMultiplierBySource;

	/** Outgoing damage multiplier while a strength power runs. See SetMeleeDamageMultiplier. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Cosmere|Power")
	float MeleeDamageMultiplier = 1.f;

	/**
	 * Each revealed actor with the world time its mark expires at.
	 *
	 * A flat array rather than a TMap so that marking the same actor again extends one entry instead of
	 * replacing a key, and so the whole list can be walked from a const read.
	 *
	 * Weak, and not a UPROPERTY: a reveal must never be the reason a dead pawn stays in memory.
	 */
	TArray<TPair<TWeakObjectPtr<AActor>, double>> RevealedUntil;

	/**
	 * World seconds at the last refused press, per slot. Index 0 is EPowerSlot::Alpha and index 1 is
	 * Beta, the same pairing FSpiritwebCharacterSave::PowerIds uses.
	 *
	 * Plain doubles rather than UPROPERTYs, for the same reason RevealedUntil is: only the local HUD ever
	 * reads this, so there is nothing here to replicate. The seed of -1000 is what makes an untouched slot
	 * read as refused a very long time ago rather than as refused at world time zero.
	 */
	double LastRefusedPowerTime[2] = { -1000.0, -1000.0 };

	/**
	 * The ability instance granted for Slot, or null before a build lands.
	 *
	 * Mirrors SetPowerSlotHeld's lookup - both resolve through the row's AbilityClass rather than through a
	 * remembered handle, because a re-grant invalidates handles and the build is the only thing that knows
	 * which row landed in which slot.
	 */
	UCosmerePowerAbility* FindPowerSlotAbility(EPowerSlot Slot) const;
	
	//~ Begin death and respawn
	/**
	 * Marks the pawn dead, credits the kill with the game mode, and schedules the respawn.
	 * Authority only: a client's copy of this pawn is hidden by replication instead.
	 */
	void Die(AController* Killer);

	/** Stops the pawn acting - movement, collision and weapon all off - then hides it. */
	void EnterDeadState();

	/**
	 * Authority: asks the game mode to hand this controller a brand new pawn.
	 *
	 * A new pawn is also the health reset. ApplyBuildToPawn writes both MaxHealth and Health and it
	 * runs again on the replacement pawn, so no part of the old pawn's health has to be restored.
	 */
	void Respawn();

	/** Seconds between dying and being respawned. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Health")
	float RespawnDelay = 6.f;

	/** Set by Die. A respawned pawn is a new actor, so this is false again by default. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Cosmere|Health")
	bool bIsDead = false;

	/** Cached in BeginPlay so the death path does not search the component list every time. */
	UPROPERTY(Transient)
	TObjectPtr<UCosmereWeaponComponent> CachedWeaponComponent;

	//~ Begin sound. Footsteps are the one noise every pawn makes, so they are emitted here rather than by a
	//~ system that would have to exist only to notice them.

	/** Played at the step and broadcast as a noise. Silent, but still heard, when left empty. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Sound")
	TObjectPtr<USoundBase> FootstepSound;

	/** Distance walked between steps, in cm. Shorter strides make a pawn easier to track by ear. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Sound", meta = (ClampMin = "20.0"))
	float FootstepStride = 180.f;

	/** How loud one step is. Carried to a listener as the same number their hearing radius is measured in. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Sound", meta = (ClampMin = "0.0"))
	float FootstepLoudness = 1.f;

	/** Below this speed a pawn is considered to be standing still, and makes no step. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Sound", meta = (ClampMin = "0.0"))
	float FootstepMinSpeed = 60.f;

	/**
	 * Distance walked since the last step. Local only, because only the machine simulating the walk can count
	 * it - a replicated position would make every client count the same step again.
	 */
	float FootstepAccumulator = 0.f;

	/** Emits a step once the pawn has covered FootstepStride while on the ground. Owning machine only. */
	void UpdateFootsteps(float DeltaSeconds);

	//~ End sound

	/** The pending respawn timer, held so a pawn torn down early cannot leave one behind. */
	FTimerHandle RespawnTimerHandle;
	//~ End death and respawn
};
