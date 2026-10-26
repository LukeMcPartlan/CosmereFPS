// Copyright Epic Games, Inc. All Rights Reserved.

#include "Power/CosmerePowerArchetypes.h"

#include "Ability/CosmereAbilitySystemComponent.h"
#include "Ability/CosmereAttributeSet.h"
#include "Character/CosmereCharacter.h"
#include "Character/CosmereInvestitureComponent.h"
#include "Character/CosmereMovementModifierComponent.h"
#include "Character/CosmereRealmComponent.h"
#include "Components/CapsuleComponent.h"
#include "CosmereRivals.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "GameplayEffectTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Net/CosmerePlayerState.h"
#include "Power/CosmerePowerResourceComponent.h"
#include "TimerManager.h"

namespace
{
	/**
	 * Health goes through the ASC rather than SetNumericAttributeBase so UCosmereAttributeSet's own
	 * PreAttributeChange clamp still runs, and so a heal is the mirror image of TakeDamage's subtract.
	 * Clamped again here because a heal of 30 on a pawn missing 4 should restore 4, not 30.
	 */
	void AddHealth(ACosmereCharacter* Target, float Amount)
	{
		if (!Target || Amount <= 0.f)
		{
			return;
		}

		UCosmereAbilitySystemComponent* ASC =
			Cast<UCosmereAbilitySystemComponent>(Target->GetAbilitySystemComponent());
		if (!ASC)
		{
			return;
		}

		const UCosmereAttributeSet* Set = ASC->GetSet<UCosmereAttributeSet>();
		if (!Set)
		{
			return;
		}

		const float Missing = FMath::Max(0.f, Set->GetMaxHealth() - Set->GetHealth());
		const float Delta = FMath::Min(Amount, Missing);
		if (Delta > 0.f)
		{
			ASC->ApplyModToAttribute(UCosmereAttributeSet::GetHealthAttribute(), EGameplayModOp::Additive, Delta);
		}
	}

	/** The pool lives on the PlayerState, so this survives the target's next respawn. */
	void AddResource(ACosmereCharacter* Target, float Amount)
	{
		if (!Target || Amount <= 0.f)
		{
			return;
		}

		if (ACosmerePlayerState* PlayerState = Cast<ACosmerePlayerState>(Target->GetPlayerState()))
		{
			if (UCosmerePowerResourceComponent* Pool = PlayerState->GetPowerResourceComponent())
			{
				// Every metal the target has actually burned gets some back. Which metal a support power is
				// topping up is not something this layer knows, and a reserve that is still full has no entry
				// to touch, so this cannot overfill anything.
				Pool->RefillAll(Amount);
			}
		}
	}

	/** The controller's view point when there is one, so aim lands where the crosshair is. */
	void GetAimPoint(const FGameplayAbilityActorInfo* ActorInfo, FVector& OutLocation, FVector& OutDirection, APawn*& OutPawn)
	{
		OutPawn = ActorInfo ? Cast<APawn>(ActorInfo->AvatarActor.Get()) : nullptr;
		OutLocation = FVector::ZeroVector;
		OutDirection = FVector::ForwardVector;

		if (!OutPawn)
		{
			return;
		}

		FVector Location;
		FRotator Rotation;
		if (const APlayerController* PC = Cast<APlayerController>(OutPawn->GetController()))
		{
			PC->GetPlayerViewPoint(Location, Rotation);
		}
		else
		{
			OutPawn->GetActorEyesViewPoint(Location, Rotation);
		}

		OutLocation = Location;
		OutDirection = Rotation.Vector();
	}

	// Deliberately no damage-resistance ceiling. Sources compose multiplicatively on the pawn and a fully
	// flared metal is allowed to reach complete immunity, so the only limit left is the 0..1 clamp on each
	// individual contribution inside ACosmereCharacter::GetDamageResistance.
}

//~ Begin UCosmereSelfBuffAbility

void UCosmereSelfBuffAbility::ApplyMetalmindTick()
{
	ACharacter* Character = OwnerCharacter.Get();
	if (!Character)
	{
		// A metalmind press deliberately skips OnPowerActivated, which is where the timed buff assigns its
		// owner. Resolving it from the actor info here is what lets a store, a tap and a flare move stats
		// without that call ever running.
		const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
		Character = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
		OwnerCharacter = Character;

		if (!Character)
		{
			return;
		}
	}



	// One set of numbers per metal, read in both directions: the tap multiplies and the matching store divides,
	// so steel's x2 tap is a x0.5 store. A level above 1 raises the multiplier to its own power rather than
	// growing the delta, which is what turns x2 into x4, x8, x16 and x32 - the whole multiplier doubles, so the
	// speed a player actually feels doubles with it.
	const int32 Level = FMath::Max(1, GetMetalmindLevel());
	const float LevelScale = FMath::Pow(2.f, static_cast<float>(Level - 1));

	float Speed = 1.f;
	float Jump = 1.f;
	float Gravity = 1.f;
	float Air = 0.f;
	float Resistance = 0.f;
	float Melee = 1.f;
	float Mass = 1.f;

	switch (GetMetalmindMode())
	{
	case EMetalmindMode::Storing:
		Speed = 1.f / FMath::Max(SpeedMultiplier, KINDA_SMALL_NUMBER);
		Jump = 1.f / FMath::Max(JumpMultiplier, KINDA_SMALL_NUMBER);
		Gravity = 1.f / FMath::Max(GravityScaleMultiplier, KINDA_SMALL_NUMBER);
		Air = -AirControlBonus;
		Melee = 1.f / FMath::Max(MeleeDamageMultiplier, KINDA_SMALL_NUMBER);

		// A metal whose store is its own direction, rather than the tap read backwards, says so explicitly.
		Mass = (StoreMassMultiplier > 0.f)
			? StoreMassMultiplier
			: (1.f / FMath::Max(MassMultiplier, KINDA_SMALL_NUMBER));
		break;

	case EMetalmindMode::Tapping:
	case EMetalmindMode::Flaring:
		Speed = FMath::Pow(SpeedMultiplier, static_cast<float>(Level));
		Jump = FMath::Pow(JumpMultiplier, static_cast<float>(Level));
		Gravity = FMath::Pow(GravityScaleMultiplier, static_cast<float>(Level));
		Air = AirControlBonus * LevelScale;
		// Iron and Pewter read resistance as one step per tier rather than a doubling, so a tier is worth the same
		// at the top of the ladder as at the bottom. Every other metal keeps the shared doubling ladder, which
		// with no ceiling left reaches total immunity comparatively early.
		Resistance = DamageResistance * (bLinearResistanceLadder ? static_cast<float>(Level) : LevelScale);

		// The part above 1 is what scales, so the ladder compounds the edge a strength metal has rather than
		// multiplying a 1 that does nothing: 1.35 becomes 1.35, then 1.7, then 2.4.
		Melee = 1.f + (MeleeDamageMultiplier - 1.f) * LevelScale;

		// Compounded like speed rather than added like resistance: a heavy metal is heavy, and the ladder is
		// what turns 2.2 into 51.5 by level five.
		Mass = FMath::Pow(MassMultiplier, static_cast<float>(Level));
		break;

	default:
		break;
	}

	ApplyMovementModifiers(Speed, Jump, Gravity, Air);

	// Mass lands on both machines, unlike the resistance below it. It is what this pawn's own fall speed is
	// derived from, and fall speed is simulated locally, so a client that skipped this would fall at the wrong
	// rate and then be corrected by the server every frame.
	if (ACosmereCharacter* MassHolder = GetOwnerCosmereCharacter())
	{
		MassHolder->SetMassMultiplierForSource(GetModifierSource(), Mass);
	}

	if (const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo())
	{
		if (ActorInfo->IsNetAuthority())
		{
			if (ACosmereCharacter* Cosmere = Cast<ACosmereCharacter>(Character))
			{
				Cosmere->SetDamageResistanceForSource(GetModifierSource(), Resistance);
				Cosmere->SetMeleeDamageMultiplier(Melee);
			}
		}
	}
}

void UCosmereSelfBuffAbility::ClearMetalmindStats()
{
	// Idempotent on purpose: OnPowerEnded clears too, and the base runs both on every end path.
	ClearMovementModifiers();

	// Both machines, for the same reason the mass was set on both.
	if (ACosmereCharacter* MassHolder = GetOwnerCosmereCharacter())
	{
		MassHolder->ClearMassMultiplierSource(GetModifierSource());
	}

	if (const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo())
	{
		if (ActorInfo->IsNetAuthority())
		{
			if (ACosmereCharacter* Cosmere = Cast<ACosmereCharacter>(OwnerCharacter.Get()))
			{
				Cosmere->ClearDamageResistanceSource(GetModifierSource());
				Cosmere->SetMeleeDamageMultiplier(1.f);
			}
		}
	}
}


UCosmereSelfBuffAbility::UCosmereSelfBuffAbility()
{
	// Overridden per power in OnPowerActivated, because bChannelWhileHeld is a data property and the base
	// reads bEndAfterActivation only after OnPowerActivated has returned.
	bEndAfterActivation = true;
}

UCosmereMovementModifierComponent* UCosmereSelfBuffAbility::GetMovementModifiers() const
{
	// Resolved from the avatar rather than the cached owner: a metalmind press never runs OnPowerActivated, and
	// that is the only place OwnerCharacter is assigned.
	const ACosmereCharacter* Cosmere = GetOwnerCosmereCharacter();
	if (!Cosmere)
	{
		Cosmere = Cast<ACosmereCharacter>(OwnerCharacter.Get());
	}

	return Cosmere ? Cosmere->GetMovementModifiers() : nullptr;
}

void UCosmereSelfBuffAbility::ApplyMovementModifiers(float Speed, float Jump, float Gravity, float Air)
{
	UCosmereMovementModifierComponent* Modifiers = GetMovementModifiers();
	if (!Modifiers)
	{
		return;
	}

	// One source for all four channels, so the whole contribution leaves together when the power ends. Health and
	// damage resistance are deliberately not here: they live on the attribute set, not on the movement component.
	const FName Source = GetModifierSource();

	Modifiers->SetModifier(Source, ECosmereMovementChannel::WalkSpeed, ECosmereMovementOp::Multiply, Speed);
	Modifiers->SetModifier(Source, ECosmereMovementChannel::JumpVelocity, ECosmereMovementOp::Multiply, Jump);
	Modifiers->SetModifier(Source, ECosmereMovementChannel::GravityScale, ECosmereMovementOp::Multiply, Gravity);
	Modifiers->SetModifier(Source, ECosmereMovementChannel::AirControl, ECosmereMovementOp::Add, Air);
}

void UCosmereSelfBuffAbility::ClearMovementModifiers()
{
	if (UCosmereMovementModifierComponent* Modifiers = GetMovementModifiers())
	{
		Modifiers->ClearSource(GetModifierSource());
	}
}

void UCosmereSelfBuffAbility::ApplyBuffTick()
{
	ACharacter* Character = OwnerCharacter.Get();
	if (!Character)
	{
		return;
	}

	// Registered rather than written: the stack resolves these against the pawn's own numbers and every other
	// source, so a buff landing on a slowed pawn multiplies the slow instead of overwriting it. Re-registering
	// every tick is free because a source replaces its own entry rather than adding another.
	ApplyMovementModifiers(SpeedMultiplier, JumpMultiplier, GravityScaleMultiplier, AirControlBonus);

	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	if (!ActorInfo || !ActorInfo->IsNetAuthority())
	{
		return;
	}

	if (HealPerSecond > 0.f)
	{
		AddHealth(Cast<ACosmereCharacter>(Character), HealPerSecond * TickInterval);
	}
}



void UCosmereSelfBuffAbility::OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	UWorld* World = GetPowerWorld(ActorInfo);
	if (!World)
	{
		return;
	}

	OwnerCharacter = Cast<ACharacter>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
	if (!OwnerCharacter.IsValid())
	{
		return;
	}

	const bool bAuthority = ActorInfo->IsNetAuthority();

	// A cleanse throws away whatever was holding the pawn back before this buff adds its own effect. It drops
	// the penalties off the movement stack rather than snapping to a number, because a snap has to choose
	// between the build and a snapshot taken after the slow it is meant to remove.
	if (bCleanse)
	{
		if (ACosmereCharacter* Cosmere = Cast<ACosmereCharacter>(OwnerCharacter.Get()))
		{
			if (UCosmereMovementModifierComponent* Modifiers = Cosmere->GetMovementModifiers())
			{
				Modifiers->CleansePenalties();
			}

			// A purge throws away every open window, not just its own - the movement half above does the same.
			Cosmere->ClearAllDamageResistance();
		}
	}

	if (bAuthority)
	{
		if (ACosmereCharacter* Cosmere = Cast<ACosmereCharacter>(OwnerCharacter.Get()))
		{
			Cosmere->SetDamageResistanceForSource(GetModifierSource(), DamageResistance);
			Cosmere->SetMeleeDamageMultiplier(MeleeDamageMultiplier);
		}

		if (bHiddenWhileActive)
		{
			OwnerCharacter->SetActorHiddenInGame(true);
		}
	}

	ApplyBuffTick();
	
	// A buff has to outlive ActivateAbility to have any effect at all: the base ends the ability the moment
	// OnPowerActivated returns, and EndAbility is what runs OnPowerEnded - which is what clears the timers
	// and puts the movement back. So this never lets the base close the ability. The buff closes itself
	// instead: from the duration timer when it is timed, from EndPower when the input layer releases it, or
	// when the row's ongoing charge runs the pool dry.
	bEndAfterActivation = false;
	
	// A metalmind does not run the timed buff: its numbers come from ApplyMetalmindTick once per metalmind
	// slice, and it stays up until the reserve empties or the player taps again. Starting the duration timer
	// here would end a tap after Duration seconds with metal still in the metalmind.
	if (IsMetalmind())
	{
		return;
	}

	World->GetTimerManager().SetTimer(TickTimer, this, &UCosmereSelfBuffAbility::ApplyBuffTick, TickInterval, /*bLoop=*/true);
	
	// A row with neither a duration nor a hold could never be ended, so it gets the shortest workable
	// window rather than leaving the pawn permanently altered.
	const float Window = bChannelWhileHeld ? 0.f : ((Duration > 0.f) ? Duration : 0.25f);
	if (Window > 0.f)
	{
		World->GetTimerManager().SetTimer(DurationTimer, this, &UCosmereSelfBuffAbility::HandleDurationExpired, Window, /*bLoop=*/false);
	}

	UE_LOG(LogCosmere, Log, TEXT("CosmereSelfBuffAbility: '%s' is running on %s for %.1fs (speed x%.2f)."),
		*GetPowerId().ToString(), *OwnerCharacter->GetName(), bChannelWhileHeld ? -1.f : Duration, SpeedMultiplier);
}

void UCosmereSelfBuffAbility::HandleDurationExpired()
{
	// The timer is the end for a timed buff; EndPower is the end for a held one. Both land in OnPowerEnded.
	EndPower();
}

void UCosmereSelfBuffAbility::OnPowerEnded(bool bWasCancelled)
{
	if (UWorld* World = GetPowerWorld(GetCurrentActorInfo()))
	{
		World->GetTimerManager().ClearTimer(TickTimer);
		World->GetTimerManager().ClearTimer(DurationTimer);
	}

	ClearMovementModifiers();

	if (ACharacter* Character = OwnerCharacter.Get())
	{
		Character->SetActorHiddenInGame(false);

		// Mass comes off here as well as in ClearMetalmindStats, because a timed buff never runs that path.
		if (ACosmereCharacter* MassHolder = Cast<ACosmereCharacter>(Character))
		{
			MassHolder->ClearMassMultiplierSource(GetModifierSource());
		}

		if (ACosmereCharacter* Cosmere = Cast<ACosmereCharacter>(Character))
		{
			Cosmere->ClearDamageResistanceSource(GetModifierSource());
			Cosmere->SetMeleeDamageMultiplier(1.f);
		}
	}

	OwnerCharacter.Reset();
}

//~ End UCosmereSelfBuffAbility

//~ Begin UCosmereStrikeAbility

UCosmereStrikeAbility::UCosmereStrikeAbility()
{
	bEndAfterActivation = true;
}

bool UCosmereStrikeAbility::ResolveStrike(FVector& OutHitPoint, AActor*& OutVictim, FVector& OutDirection) const
{
	OutVictim = nullptr;

	FVector ViewLocation;
	APawn* Pawn = nullptr;
	GetAimPoint(GetCurrentActorInfo(), ViewLocation, OutDirection, Pawn);

	UWorld* World = GetPowerWorld(GetCurrentActorInfo());
	if (!World || !Pawn)
	{
		return false;
	}

	const FVector TraceEnd = ViewLocation + OutDirection * Range;

	FCollisionQueryParams Params;
	Params.bTraceComplex = true;
	Params.AddIgnoredActor(Pawn);

	// A sealed body is not a target: the trace carries on through it, so a soul warded by copper is a wall you
	// cannot push that also does not stop the power dead on their chest. Anything with no investiture answers
	// false, which is why this costs nothing on a prop or a piece of architecture.
	FHitResult Hit;
	while (World->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, ECC_Visibility, Params))
	{
		AActor* HitActor = Hit.GetActor();

		// Two ways of not being a target, and they are the same shape: a soul sealed by copper, and a body
		// standing in the other realm. Both are stepped over so the power carries on to whatever is really
		// behind them, which is why the realm test sits inside the loop rather than guarding it from above.
		if (UCosmereRealmComponent::ShareRealm(Pawn, HitActor)
			&& !UCosmereInvestitureComponent::IsTargetInvestitureImmune(HitActor))
		{
			OutHitPoint = Hit.ImpactPoint;
			OutVictim = HitActor;
			return true;
		}

		Params.AddIgnoredActor(HitActor);
	}

	return false;
}

void UCosmereStrikeAbility::ApplyStrikeTick()
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	if (!ActorInfo)
	{
		return;
	}

	FVector HitPoint = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	AActor* Victim = nullptr;

	if (!ResolveStrike(HitPoint, Victim, Direction))
	{
		return;
	}

	APawn* Pawn = Cast<APawn>(ActorInfo->AvatarActor.Get());
	if (!Pawn || Victim == Pawn)
	{
		return;
	}

	// Everything below changes state that replicates, so it is the authority's to do - the same split
	// Division makes between its field (both machines) and its damage (authority only).
	if (!ActorInfo->IsNetAuthority())
	{
		return;
	}

	AController* InstigatorController = Pawn->GetInstigatorController();

	// What a body refuses is asked of that body rather than of whoever is working on it: a copper cloud covers
	// everyone standing in it, so one zinc finds two players and only gets paid for the one outside the cloud.
	const float DamageYield = bDamageIsSpiritual ? GetSpiritualYield(Victim) : GetPhysicalYield(Victim);

	if (Damage > 0.f)
	{
		const float AppliedDamage = Damage * DamageYield;
		if (AppliedDamage > 0.f)
		{
			// Through the engine's damage pipeline, so a power kill is credited exactly as a shot is.
			UGameplayStatics::ApplyDamage(Victim, AppliedDamage, InstigatorController, Pawn, UDamageType::StaticClass());
		}
	}

	// One force, both ends. Every power that shoves asks the same question - how heavy is each body, and can
	// each body actually go anywhere - so the answer lives here rather than in Iron or Steel. Nothing below
	// knows which metal is burning; it only knows the numbers on this ability.
	ApplyShove(Pawn, Victim, Direction);

	if (SlowMultiplier < 1.f && SlowDuration > 0.f && !bSlowPending)
	{
		if (ACharacter* VictimCharacter = Cast<ACharacter>(Victim))
		{
			const ACosmereCharacter* CosmereVictim = Cast<ACosmereCharacter>(VictimCharacter);
			UCosmereMovementModifierComponent* VictimModifiers =
				CosmereVictim ? CosmereVictim->GetMovementModifiers() : nullptr;

			if (VictimModifiers)
			{
				SlowedVictim = VictimCharacter;
				bSlowPending = true;

				// Registered on the victim's own stack under this power's source, so the slow multiplies with whatever
				// else is already on them and comes off again without disturbing any of it.
				VictimModifiers->SetModifier(GetModifierSource(), ECosmereMovementChannel::WalkSpeed, ECosmereMovementOp::Multiply, SlowMultiplier);

				if (UWorld* World = GetPowerWorld(ActorInfo))
				{
					World->GetTimerManager().SetTimer(SlowTimer, this, &UCosmereStrikeAbility::RestoreSlowedVictim, SlowDuration, /*bLoop=*/false);
				}
			}
		}
	}

	// The pool lives on the PlayerState, and GetPlayerState is on APawn rather than AActor - a struck wall
	// has no reserve to leech, so the victim is narrowed to a pawn before the pool is asked for.
	APawn* VictimPawn = Cast<APawn>(Victim);
	if (ResourceDrain > 0.f && VictimPawn)
	{
		if (ACosmerePlayerState* VictimState = Cast<ACosmerePlayerState>(VictimPawn->GetPlayerState()))
		{
			if (UCosmerePowerResourceComponent* Pool = VictimState->GetPowerResourceComponent())
			{
				// A leech is invested work rather than a blow, so a warded soul keeps its reserve. Chromium is
				// the only drain in the set, and copper is the answer to it. It takes metal, not one named metal,
				// so it drains every pool the victim has spent into.
				Pool->DrainAll(ResourceDrain * GetSpiritualYield(Victim));
			}
		}
	}

	UE_LOG(LogCosmere, Log, TEXT("CosmereStrikeAbility: '%s' struck %s for %.1f at %s."),
		*GetPowerId().ToString(), *GetNameSafe(Victim), Damage, *HitPoint.ToCompactString());
}

void UCosmereStrikeAbility::RestoreSlowedVictim()
{
	bSlowPending = false;

	if (const ACosmereCharacter* CosmereVictim = Cast<ACosmereCharacter>(SlowedVictim.Get()))
	{
		if (UCosmereMovementModifierComponent* VictimModifiers = CosmereVictim->GetMovementModifiers())
		{
			VictimModifiers->ClearSource(GetModifierSource());
		}
	}

	SlowedVictim.Reset();
}

float UCosmereStrikeAbility::GetPhysicalYield(AActor* Victim)
{
	const ACosmereCharacter* CosmereVictim = Cast<ACosmereCharacter>(Victim);
	UCosmereInvestitureComponent* Ward = CosmereVictim ? CosmereVictim->GetInvestitureComponent() : nullptr;

	// 1 for a body carrying nothing, so every power that predates resistances behaves exactly as it did.
	return Ward ? Ward->ScaleForPhysical(1.f) : 1.f;
}

float UCosmereStrikeAbility::GetSpiritualYield(AActor* Victim)
{
	const ACosmereCharacter* CosmereVictim = Cast<ACosmereCharacter>(Victim);
	UCosmereInvestitureComponent* Ward = CosmereVictim ? CosmereVictim->GetInvestitureComponent() : nullptr;

	return Ward ? Ward->ScaleForSpiritual(1.f) : 1.f;
}

void UCosmereStrikeAbility::ApplyShove(APawn* Caster, AActor* Victim, const FVector& Direction)
{
	if (!Caster || !Victim || FMath::IsNearlyZero(Impulse))
	{
		return;
	}

	// One force, two bodies. The caster aimed +Direction at the target, so the target takes the force along
	// +Direction and the caster takes the equal-and-opposite along -Direction: pushing steel throws you back
	// off it, pulling iron drags you toward it. The force is the same number for both ends; only how far it
	// carries each body differs, and that is the body's own weight rather than the power's business.
	//
	// What a body shrugs off is subtracted before the exchange rather than after it: the force a warded body
	// refuses never entered, so there is nothing of it to push back through the other end either.
	const FVector VictimForce = Direction * Impulse * GetPhysicalYield(Victim);
	const FVector CasterForce = -VictimForce;

	// Quoted against the caster's own build weight, so a prop and a pawn of the same mass move the same
	// distance under the same force. A heavier race therefore shifts every number below with it.
	const ACosmereCharacter* CasterCosmere = Cast<ACosmereCharacter>(Caster);
	const float ReferenceMass = CasterCosmere ? FMath::Max(CasterCosmere->GetBaseMass(), KINDA_SMALL_NUMBER) : 80.f;

	const bool bVictimCanMove = CanBeShoved(Victim, Caster, VictimForce);
	const bool bCasterCanMove = CanBeShoved(Caster, Victim, CasterForce);

	if (!bVictimCanMove && !bCasterCanMove)
	{
		// Both ends are held. The force has nowhere to go, so it goes into the bodies instead: a character
		// pinned to a wall by something bolted down takes the crush, and so does the one who pushed - they
		// are the other half of the same squeeze. Only characters have anything to damage; two girders ring.
		const float CrushDamage = FMath::Abs(Impulse) * CrushDamagePerImpulse;
		AController* InstigatorController = Caster->GetInstigatorController();

		if (CrushDamage > 0.f)
		{
			if (ACharacter* VictimCharacter = Cast<ACharacter>(Victim))
			{
				UGameplayStatics::ApplyDamage(VictimCharacter, CrushDamage, InstigatorController, Caster, UDamageType::StaticClass());
			}

			if (ACharacter* CasterCharacter = Cast<ACharacter>(Caster))
			{
				UGameplayStatics::ApplyDamage(CasterCharacter, CrushDamage, InstigatorController, Caster, UDamageType::StaticClass());
			}
		}

		UE_LOG(LogCosmere, Log, TEXT("CosmereShove: '%s' crushed both ends for %.1f - neither %s nor %s could move."),
			*GetPowerId().ToString(), CrushDamage, *GetNameSafe(Caster), *GetNameSafe(Victim));
		return;
	}

	// Whatever cannot move hands its half of the force to the end that can, doubled. The girder is bolted to
	// the world, so the world pushes back through the only body that is free - and the doubling is what makes
	// bracing against architecture feel like a launch rather than the shrug it was before.
	const float TransferScale = (bVictimCanMove && bCasterCanMove) ? 1.f : 2.f;

	if (bVictimCanMove)
	{
		ShoveBody(Victim, VictimForce, GetShoveScale(Victim, ReferenceMass) * TransferScale);
	}

	if (bCasterCanMove)
	{
		ShoveBody(Caster, CasterForce, GetShoveScale(Caster, ReferenceMass) * TransferScale);
	}
}

bool UCosmereStrikeAbility::CanBeShoved(const AActor* Actor, const AActor* OtherEnd, const FVector& ShoveDirection) const
{
	if (!Actor)
	{
		return false;
	}

	const UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
	if (!Root)
	{
		return false;
	}

	const ACharacter* Character = Cast<ACharacter>(Actor);

	// A walking pawn is moved by its movement component and needs no simulating body. Anything else has to
	// be simulating to take an impulse at all: a Static-mobility mesh is architecture, which is exactly what
	// anchored metal is, and no impulse can be spent on one.
	if (!Character && !Root->IsSimulatingPhysics())
	{
		return false;
	}

	UWorld* World = GetPowerWorld(GetCurrentActorInfo());
	if (!World)
	{
		return true;
	}

	// Nothing tracks "pressed against a wall" as a state, so ask the world directly: can a body-sized probe
	// escape along the shove? If it cannot, this end of the force is going into the wall rather than the body
	// and belongs at the other end instead. The far end is exempt, or a pull would block on the puller.
	FCollisionQueryParams Params(FName(TEXT("CosmereShoveBlocked")), /*bTraceComplex=*/false, Actor);
	if (OtherEnd)
	{
		Params.AddIgnoredActor(OtherEnd);
	}

	const UPrimitiveComponent* ProbeBody = Root;
	if (Character && Character->GetCapsuleComponent())
	{
		ProbeBody = Character->GetCapsuleComponent();
	}

	const float ProbeRadius = FMath::Max(static_cast<float>(ProbeBody->Bounds.SphereRadius) * 0.25f, 12.f);
	const FVector ProbeStart = Actor->GetActorLocation();

	FHitResult Blocked;
	const bool bBlocked = World->SweepSingleByChannel(
		Blocked,
		ProbeStart,
		ProbeStart + ShoveDirection.GetSafeNormal() * 40.f,
		FQuat::Identity,
		ECC_Visibility,
		FCollisionShape::MakeSphere(ProbeRadius),
		Params);

	return !bBlocked;
}

float UCosmereStrikeAbility::GetShoveScale(const AActor* Actor, float ReferenceMass) const
{
	if (const ACharacter* Character = Cast<ACharacter>(Actor))
	{
		// A pawn already knows its own weight ratio from whatever metalmind is open; the base-mass term
		// re-quotes that ratio against the shared yardstick so a heavier race does not borrow the caster's.
		if (const ACosmereCharacter* Cosmere = Cast<ACosmereCharacter>(Character))
		{
			return Cosmere->GetLaunchScale() * (ReferenceMass / FMath::Max(Cosmere->GetBaseMass(), KINDA_SMALL_NUMBER));
		}

		return 1.f;
	}

	const UPrimitiveComponent* Root = Actor ? Cast<UPrimitiveComponent>(Actor->GetRootComponent()) : nullptr;
	const float BodyMass = Root ? Root->GetMass() : 0.f;

	// A prop has no metalmind, so its own physics mass is its whole answer: the same force carries a
	// ten-kilogram crate half as far as it carries a five-kilogram one, with nothing to tune per asset.
	return BodyMass > KINDA_SMALL_NUMBER ? ReferenceMass / BodyMass : 1.f;
}

void UCosmereStrikeAbility::ShoveBody(AActor* Actor, const FVector& Force, float Scale)
{
	if (!Actor || FMath::IsNearlyZero(Scale))
	{
		return;
	}

	// The lift rides the same force and the same scale, so a heavy body does not get a free vertical pop
	// that the horizontal push just denied it.
	const FVector Shove = (Force + FVector(0.f, 0.f, FMath::Abs(Impulse) * ShoveLiftFraction)) * Scale;

	if (ACharacter* Character = Cast<ACharacter>(Actor))
	{
		// A walking pawn has no simulating body to impulse, so the shove becomes a launch. bXYOverride makes
		// the number read as a velocity change, which is what the mass scale just computed for it.
		Character->LaunchCharacter(Shove, true, false);
		return;
	}

	if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
	{
		if (Root->IsSimulatingPhysics())
		{
			// VelChange makes the impulse independent of the body's mass on its own, so the scale has already
			// divided that mass back out - the same divide a pawn does through GetLaunchScale().
			Root->AddImpulse(Shove, NAME_None, /*bVelChange=*/true);
		}
	}
}

void UCosmereStrikeAbility::OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	UWorld* World = GetPowerWorld(ActorInfo);
	if (!World)
	{
		return;
	}

	bEndAfterActivation = !bChannelWhileHeld;

	ApplyStrikeTick();

	if (bChannelWhileHeld)
	{
		World->GetTimerManager().SetTimer(ChannelTimer, this, &UCosmereStrikeAbility::ApplyStrikeTick, ChannelTickInterval, /*bLoop=*/true);
	}
}

void UCosmereStrikeAbility::OnPowerEnded(bool bWasCancelled)
{
	if (UWorld* World = GetPowerWorld(GetCurrentActorInfo()))
	{
		World->GetTimerManager().ClearTimer(ChannelTimer);
	}
	
	// SlowTimer is deliberately left running and the victim deliberately not restored here. A one-shot
	// strike ends as soon as it has landed, so tying the victim's slow to this ability's lifetime would
	// undo it on the very frame it was applied. The slow expires on its own timer instead, which is also
	// what lets it outlive the caster - a slow sitting on a target is not the caster's to hold on to.
}

//~ End UCosmereStrikeAbility

//~ Begin UCosmereFieldAbility

UCosmereFieldAbility::UCosmereFieldAbility()
{
	bEndAfterActivation = false;
}

void UCosmereFieldAbility::ApplyFieldTick()
{
	UWorld* World = GetPowerWorld(GetCurrentActorInfo());
	if (!World)
	{
		return;
	}

	APawn* Pawn = Cast<APawn>(GetCurrentActorInfo() ? GetCurrentActorInfo()->AvatarActor.Get() : nullptr);

	const float RadiusSquared = Radius * Radius;

	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		ACharacter* Other = *It;
		if (!IsValid(Other) || Other == Pawn)
		{
			continue;
		}

		if (!bAffectOthers)
		{
			break;
		}

		if (FVector::DistSquared(Other->GetActorLocation(), FieldCentre) > RadiusSquared)
		{
			continue;
		}

		// The bubble is opened in one realm and reaches nobody in the other. It is the caster's realm that
		// decides, since the field is a thing the caster did rather than a place in the map.
		if (!UCosmereRealmComponent::ShareRealm(Pawn, Other))
		{
			continue;
		}

		// A sealed body is not slowed, sped or held. This sits in the others loop rather than above it, because
		// a burner's own seal does not exempt them from their own bubble.
		if (UCosmereInvestitureComponent::IsTargetInvestitureImmune(Other))
		{
			continue;
		}

		ApplyFieldTo(Other);
	}

	if (bAffectSelf && Pawn)
	{
		// The bubble treats the caster exactly as it treats everyone else, so there is no second code path to keep
		// in step with the first.
		ApplyFieldTo(Cast<ACharacter>(Pawn));
	}

	if (ActiveField)
	{
		ActiveField->SetActorLocation(FieldCentre);
	}
}

void UCosmereFieldAbility::ApplyFieldTo(ACharacter* Target)
{
	if (!Target)
	{
		return;
	}

	const ACosmereCharacter* Cosmere = Cast<ACosmereCharacter>(Target);
	UCosmereMovementModifierComponent* Modifiers = Cosmere ? Cosmere->GetMovementModifiers() : nullptr;
	if (!Modifiers)
	{
		return;
	}

	// Registered against the target's own base rather than against what they were holding when they walked in, so
	// the bubble multiplies a metalmind or another slow instead of replacing it. Re-registering every tick replaces
	// this source's own entry, which is why standing in the bubble cannot grind a pawn to a halt one tick at a time.
	Modifiers->SetModifier(GetModifierSource(), ECosmereMovementChannel::WalkSpeed, ECosmereMovementOp::Multiply, SpeedMultiplier);

	const bool bAlreadyTracked = Affected.ContainsByPredicate([Target](const TWeakObjectPtr<ACharacter>& Entry)
	{
		return Entry.Get() == Target;
	});

	if (!bAlreadyTracked)
	{
		Affected.Add(Target);
	}
}

void UCosmereFieldAbility::RestoreAffected()
{
	for (const TWeakObjectPtr<ACharacter>& Entry : Affected)
	{
		if (const ACosmereCharacter* Cosmere = Cast<ACosmereCharacter>(Entry.Get()))
		{
			if (UCosmereMovementModifierComponent* Modifiers = Cosmere->GetMovementModifiers())
			{
				Modifiers->ClearSource(GetModifierSource());
			}
		}
	}

	Affected.Reset();
}

void UCosmereFieldAbility::OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	UWorld* World = GetPowerWorld(ActorInfo);
	if (!World)
	{
		return;
	}

	FVector ViewLocation;
	FVector Direction;
	APawn* Pawn = nullptr;
	GetAimPoint(ActorInfo, ViewLocation, Direction, Pawn);

	if (!Pawn)
	{
		return;
	}

	FieldCentre = Pawn->GetActorLocation();

	if (Range > 0.f)
	{
		const FVector TraceEnd = ViewLocation + Direction * Range;

		FCollisionQueryParams Params;
		Params.AddIgnoredActor(Pawn);

		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, ECC_Visibility, Params))
		{
			FieldCentre = Hit.ImpactPoint;
		}
		else
		{
			FieldCentre = TraceEnd;
		}
	}

	if (FieldClass)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = Pawn;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ActiveField = World->SpawnActor<AActor>(FieldClass, FieldCentre, FRotator::ZeroRotator, SpawnParams);

		// Stamped so the bubble is a thing of the realm that made it: a material player cannot interact with it,
		// rather than only being spared its effect.
		UCosmereRealmComponent::StampRealm(ActiveField, Pawn);
	}

	ApplyFieldTick();

	World->GetTimerManager().SetTimer(FieldTickTimer, this, &UCosmereFieldAbility::ApplyFieldTick, TickInterval, /*bLoop=*/true);

	if (Duration > 0.f)
	{
		World->GetTimerManager().SetTimer(DurationTimer, this, &UCosmereFieldAbility::HandleDurationExpired, Duration, /*bLoop=*/false);
	}

	UE_LOG(LogCosmere, Log, TEXT("CosmereFieldAbility: '%s' opened a %.0fuu bubble at %s for %.1fs (speed x%.2f)."),
		*GetPowerId().ToString(), Radius, *FieldCentre.ToCompactString(), Duration, SpeedMultiplier);
}

void UCosmereFieldAbility::HandleDurationExpired()
{
	EndPower();
}

void UCosmereFieldAbility::OnPowerEnded(bool bWasCancelled)
{
	if (UWorld* World = GetPowerWorld(GetCurrentActorInfo()))
	{
		World->GetTimerManager().ClearTimer(FieldTickTimer);
		World->GetTimerManager().ClearTimer(DurationTimer);
	}

	RestoreAffected();

	if (ActiveField)
	{
		ActiveField->Destroy();
		ActiveField = nullptr;
	}
}

//~ End UCosmereFieldAbility

//~ Begin UCosmereBlinkAbility

UCosmereBlinkAbility::UCosmereBlinkAbility()
{
	bEndAfterActivation = true;
}

void UCosmereBlinkAbility::OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	UWorld* World = GetPowerWorld(ActorInfo);
	if (!World)
	{
		return;
	}

	FVector ViewLocation;
	FVector Direction;
	APawn* Pawn = nullptr;
	GetAimPoint(ActorInfo, ViewLocation, Direction, Pawn);

	if (!Pawn)
	{
		return;
	}

	const FVector TraceEnd = ViewLocation + Direction * Range;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Pawn);

	FHitResult Hit;
	FVector Destination = TraceEnd;
	if (World->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, ECC_Visibility, Params))
	{
		// Short of the surface rather than on it, so the capsule does not arrive overlapping the thing the
		// crosshair was pointing at.
		Destination = Hit.ImpactPoint - Direction * StandOff;
	}

	Destination += FVector(0.f, 0.f, DestinationLift);

	// Swept, so a blink into geometry stops at the wall instead of tunnelling through it.
	FHitResult SweepHit;
	const bool bMoved = Pawn->SetActorLocation(Destination, /*bSweep=*/true, &SweepHit, ETeleportType::TeleportPhysics);

	UE_LOG(LogCosmere, Log, TEXT("CosmereBlinkAbility: '%s' blinked %s to %s (%s)."),
		*GetPowerId().ToString(), *Pawn->GetName(), *Pawn->GetActorLocation().ToCompactString(),
		bMoved ? TEXT("clear") : TEXT("blocked"));
}

//~ End UCosmereBlinkAbility

//~ Begin UCosmereGroupBlinkAbility

UCosmereGroupBlinkAbility::UCosmereGroupBlinkAbility()
{
	// Instantaneous: OnPowerActivated carries everyone, and the base is then allowed to close the ability
	// the moment it returns.
	bEndAfterActivation = true;
}

bool UCosmereGroupBlinkAbility::BlinkPawn(ACharacter* Target) const
{
	UWorld* World = GetPowerWorld(GetCurrentActorInfo());
	if (!World || !Target)
	{
		return false;
	}

	// Being moved is being affected, whether or not it hurts. A sealed body stays where it is and the rest of
	// the group goes on without them.
	if (UCosmereInvestitureComponent::IsTargetInvestitureImmune(Target))
	{
		return false;
	}

	// Being carried is being affected too, and a body in the other realm is not there to be carried. The
	// caster's own realm is the only one this power knows how to move anyone within.
	const AActor* Caster = GetCurrentActorInfo() ? GetCurrentActorInfo()->AvatarActor.Get() : nullptr;
	if (!UCosmereRealmComponent::ShareRealm(Caster, Target))
	{
		return false;
	}

	FCollisionQueryParams Params;
	Params.bTraceComplex = true;
	Params.AddIgnoredActor(Target);

	const FVector Origin = Target->GetActorLocation();

	FHitResult Hit;

	if (Target->GetCharacterMovement() && Target->GetCharacterMovement()->IsMovingOnGround())
	{
		// Grounded: carried along the way they are already going. Velocity is the literal answer to "the
		// direction they are currently moving"; a player standing still has no such direction, so the way
		// they are facing is used instead - a forward blink is the only useful reading of the power for
		// someone who is not moving.
		FVector Travel = Target->GetVelocity();
		Travel.Z = 0.f;

		if (Travel.SizeSquared() < KINDA_SMALL_NUMBER)
		{
			Travel = Target->GetActorForwardVector();
			Travel.Z = 0.f;
		}

		Travel = Travel.GetSafeNormal();

		const FVector TraceEnd = Origin + Travel * Radius;

		// Found short of the surface rather than on it, so the destination is not inside the thing the
		// trace stopped at - the same treatment UCosmereBlinkAbility gives its own landing point.
		FVector Destination = TraceEnd;
		if (World->LineTraceSingleByChannel(Hit, Origin, TraceEnd, ECC_Visibility, Params))
		{
			Destination = Hit.ImpactPoint - Travel * StandOff;
		}

		// Swept, so anything off the ray still stops the pawn rather than being tunnelled through.
		FHitResult SweepHit;
		const bool bMoved = Target->SetActorLocation(Destination, /*bSweep=*/true, &SweepHit, ETeleportType::TeleportPhysics);

		UE_LOG(LogCosmere, Log, TEXT("CosmereGroupBlinkAbility: '%s' carried %s %.0fuu along %s from %s (%s)."),
			*GetPowerId().ToString(), *GetNameSafe(Target), Radius, *Travel.ToCompactString(),
			*Origin.ToCompactString(), bMoved ? TEXT("clear") : TEXT("blocked"));

		return bMoved;
	}

	// Airborne: there is no floor underneath them, so the blink becomes the drop. Radius is the cap, and
	// stopping there is what keeps a caster who stepped off a ledge from being pulled through the level.
	const FVector TraceEnd = Origin - FVector(0.f, 0.f, Radius);

	FVector Destination = TraceEnd;
	if (World->LineTraceSingleByChannel(Hit, Origin, TraceEnd, ECC_Visibility, Params))
	{
		// The actor sits at the centre of its capsule, so the landing point is the floor plus half a
		// capsule - arriving on the surface itself would bury the pawn in it.
		const float HalfHeight = Target->GetCapsuleComponent()
			? Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
			: 0.f;

		Destination = Hit.ImpactPoint + FVector(0.f, 0.f, HalfHeight);
	}

	FHitResult SweepHit;
	const bool bMoved = Target->SetActorLocation(Destination, /*bSweep=*/true, &SweepHit, ETeleportType::TeleportPhysics);

	UE_LOG(LogCosmere, Log, TEXT("CosmereGroupBlinkAbility: '%s' dropped %s from %s to %s (%s)."),
		*GetPowerId().ToString(), *GetNameSafe(Target), *Origin.ToCompactString(),
		*Target->GetActorLocation().ToCompactString(), bMoved ? TEXT("clear") : TEXT("blocked"));

	return bMoved;
}

void UCosmereGroupBlinkAbility::OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	UWorld* World = GetPowerWorld(ActorInfo);
	ACharacter* Caster = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (!World || !ActorInfo || !Caster)
	{
		return;
	}

	// Self moves on both machines, because the owning client is what simulates its own pawn; everyone
	// else moves on the authority only. The same split UCosmereFieldAbility makes for movement, with the
	// remote half narrowed to the authority because there is no prediction to be had for someone else.
	const bool bAuthority = ActorInfo->IsNetAuthority();
	const bool bCanMoveSelf = bAffectSelf && (bAuthority || ActorInfo->IsLocallyControlled());

	// Taken before anything moves, so the neighbours are resolved against where the caster was standing
	// when the power went off rather than against wherever the caster just blinked to.
	const FVector Origin = Caster->GetActorLocation();
	const float RadiusSquared = Radius * Radius;

	int32 Carried = 0;

	if (bCanMoveSelf && BlinkPawn(Caster))
	{
		++Carried;
	}

	if (bAffectOthers && bAuthority)
	{
		for (TActorIterator<ACosmereCharacter> It(World); It; ++It)
		{
			ACosmereCharacter* Other = *It;
			if (!IsValid(Other) || Other == Caster || !Other->IsAlive())
			{
				continue;
			}

			// Measured from the caster, not from the trace: this is who the power reached, and it is the
			// same measurement the radius that sets the travel distance describes.
			if (FVector::DistSquared(Other->GetActorLocation(), Origin) > RadiusSquared)
			{
				continue;
			}

			if (BlinkPawn(Other))
			{
				++Carried;
			}
		}
	}

	UE_LOG(LogCosmere, Log, TEXT("CosmereGroupBlinkAbility: '%s' carried %d pawn(s) within %.0fuu of %s."),
		*GetPowerId().ToString(), Carried, Radius, *Origin.ToCompactString());
}

//~ End UCosmereGroupBlinkAbility

//~ Begin UCosmereRevealAbility

UCosmereRevealAbility::UCosmereRevealAbility()
{
	bEndAfterActivation = true;
}

void UCosmereRevealAbility::OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	UWorld* World = GetPowerWorld(ActorInfo);
	if (!World)
	{
		return;
	}

	ACharacter* Caster = Cast<ACharacter>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
	ACosmereCharacter* Viewer = Cast<ACosmereCharacter>(Caster);
	if (!Caster || !Viewer)
	{
		return;
	}

	const FVector Origin = Caster->GetActorLocation();
	const float RadiusSquared = Radius * Radius;

	TArray<AActor*> Found;
	for (TActorIterator<ACosmereCharacter> It(World); It; ++It)
	{
		ACosmereCharacter* Other = *It;
		if (!IsValid(Other) || !Other->IsAlive())
		{
			continue;
		}

		// A reveal is the caster's own picture, and the picture only contains one realm.
		if (!UCosmereRealmComponent::ShareRealm(Viewer, Other))
		{
			continue;
		}

		if (Other == Viewer && !bIncludeSelf)
		{
			continue;
		}

		if (FVector::DistSquared(Other->GetActorLocation(), Origin) > RadiusSquared)
		{
			continue;
		}

		Found.Add(Other);
	}

	// Registered on the caster, because the HUD that draws them belongs to that pawn's owner. The reveal
	// travels with the player rather than with the world, which is what makes it theirs.
	Viewer->RevealActors(Found, Duration);

	UE_LOG(LogCosmere, Log, TEXT("CosmereRevealAbility: '%s' revealed %d pawn(s) within %.0fuu of %s for %.1fs."),
		*GetPowerId().ToString(), Found.Num(), Radius, *Viewer->GetName(), Duration);
}

//~ End UCosmereRevealAbility

//~ Begin UCosmereSupportAbility

UCosmereSupportAbility::UCosmereSupportAbility()
{
	bEndAfterActivation = true;
}

void UCosmereSupportAbility::OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	if (!Info || !Info->IsNetAuthority())
	{
		// Health and the pool are both server-authoritative, so a predicting client runs nothing here.
		return;
	}

	ACharacter* Caster = Cast<ACharacter>(Info->AvatarActor.Get());
	if (!Caster)
	{
		return;
	}

	ACosmereCharacter* Target = nullptr;

	if (UWorld* World = GetPowerWorld(Info))
	{
		FVector ViewLocation;
		FVector Direction;
		APawn* Pawn = nullptr;
		GetAimPoint(Info, ViewLocation, Direction, Pawn);

		if (Pawn)
		{
			const FVector TraceEnd = ViewLocation + Direction * Range;

			FCollisionQueryParams Params;
			Params.AddIgnoredActor(Pawn);

			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, ECC_Visibility, Params))
			{
				Target = Cast<ACosmereCharacter>(Hit.GetActor());
			}
		}
	}

	if (!Target && bFallBackToSelf)
	{
		Target = Cast<ACosmereCharacter>(Caster);
	}

	if (!Target)
	{
		UE_LOG(LogCosmere, Log, TEXT("CosmereSupportAbility: '%s' found nothing to support."), *GetPowerId().ToString());
		return;
	}

	// The veil before the seal, because they are different refusals. A body in the other realm was never under
	// the crosshair at all, so falling back to healing the caster would be healing the wrong target quietly.
	if (!UCosmereRealmComponent::ShareRealm(Caster, Target))
	{
		UE_LOG(LogCosmere, Log, TEXT("CosmereSupportAbility: '%s' reached for %s, who is in the other realm."),
			*GetPowerId().ToString(), *Target->GetName());
		return;
	}

	// One rule and no exceptions: a sealed soul takes nothing in, healing included. An exception here would
	// make every power after this one something to argue about instead of something that calls the same check.
	if (UCosmereInvestitureComponent::IsTargetInvestitureImmune(Target))
	{
		UE_LOG(LogCosmere, Log, TEXT("CosmereSupportAbility: '%s' was turned aside by %s's investiture immunity."),
			*GetPowerId().ToString(), *Target->GetName());
		return;
	}

	AddHealth(Target, HealAmount);
	AddResource(Target, ResourceRefill);

	UE_LOG(LogCosmere, Log, TEXT("CosmereSupportAbility: '%s' gave %s %.1f health and %.1f resource."),
		*GetPowerId().ToString(), *Target->GetName(), HealAmount, ResourceRefill);
}

//~ End UCosmereSupportAbility
