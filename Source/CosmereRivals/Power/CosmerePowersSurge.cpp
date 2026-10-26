// Copyright Epic Games, Inc. All Rights Reserved.

#include "Power/CosmerePowersSurge.h"

#include "Character/CosmereCharacter.h"
#include "Character/CosmereRealmComponent.h"
#include "CosmereRivals.h"

//~ Begin Adhesion

UCosmereAdhesionAbility::UCosmereAdhesionAbility()
{
	Range = 3500.f;
	Damage = 0.f;

	// Negative pulls the target toward the caster rather than away from it.
	Impulse = -900.f;

	SlowMultiplier = 0.5f;
	SlowDuration = 2.5f;
}

//~ End Adhesion

//~ Begin Abrasion

UCosmereAbrasionAbility::UCosmereAbrasionAbility()
{
	Duration = 6.f;
	SpeedMultiplier = 1.5f;
	GravityScaleMultiplier = 0.9f;
	AirControlBonus = 0.3f;
}

//~ End Abrasion

//~ Begin Progression

UCosmereProgressionAbility::UCosmereProgressionAbility()
{
	Range = 2500.f;
	HealAmount = 40.f;
	ResourceRefill = 5.f;
}

//~ End Progression

//~ Begin Illumination

UCosmereIlluminationAbility::UCosmereIlluminationAbility()
{
	// The broadest reveal in the set, because lighting a place up is the point of it.
	Radius = 8000.f;
	Duration = 8.f;
	bIncludeSelf = false;
}

//~ End Illumination

//~ Begin Transformation

UCosmereTransformationAbility::UCosmereTransformationAbility()
{
	// Short range and the heaviest single hit of the Surges - Soulcasting is not a subtle Surge.
	Range = 2000.f;
	Damage = 45.f;
}

//~ End Transformation

//~ Begin Cohesion

UCosmereCohesionAbility::UCosmereCohesionAbility()
{
	Range = 2500.f;
	Damage = 30.f;
	Impulse = 900.f;
}

//~ End Cohesion

//~ Begin Tension

UCosmereTensionAbility::UCosmereTensionAbility()
{
	Range = 3500.f;
	Damage = 25.f;
	SlowMultiplier = 0.4f;
	SlowDuration = 3.f;
}

//~ End Tension

//~ Begin Transportation

UCosmereTransportationAbility::UCosmereTransportationAbility()
{
	// A press is the flip, and the flip is finished by the time OnPowerActivated returns. The row's own cooldown
	// is the gate on the way in only - see CheckCooldown - so the trip stays a commitment without an unrelated
	// timer being able to hold a player out of their own realm.
	bEndAfterActivation = true;
}

UCosmereRealmComponent* UCosmereTransportationAbility::GetCasterRealm() const
{
	const ACosmereCharacter* Caster = GetOwnerCosmereCharacter();
	return Caster ? Caster->GetRealmComponent() : nullptr;
}

bool UCosmereTransportationAbility::CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
	// Leaving is not a second use of the power, so the row's clock has no business refusing it - and it has to
	// be refused here rather than in OnPowerActivated, because CommitAbility re-runs this gate before the flip
	// is ever reached. Asked of the replicated realm, which every machine agrees on, so a predicting client
	// cannot refuse locally a press the authority would have taken.
	const UCosmereRealmComponent* RealmComponent = GetCasterRealm();
	if (RealmComponent && RealmComponent->IsInShadesmar())
	{
		// The engine's own cooldown check stays: it is the only part of this gate that is not this row's clock.
		return UGameplayAbility::CheckCooldown(Handle, ActorInfo, OptionalRelevantTags);
	}

	return Super::CheckCooldown(Handle, ActorInfo, OptionalRelevantTags);
}

void UCosmereTransportationAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	// Read before Super, because Super stamps the row clock with "now" and that stamp is what erases the
	// remainder this exit has to preserve. Left at zero on an entry, where there is no remainder worth keeping.
	CooldownRemainingBeforePress = 0.f;
	if (const UCosmereRealmComponent* RealmComponent = GetCasterRealm())
	{
		if (RealmComponent->IsInShadesmar())
		{
			CooldownRemainingBeforePress = GetCooldownRemainingSeconds();
		}
	}

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UCosmereTransportationAbility::OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	ACosmereCharacter* Caster = GetOwnerCosmereCharacter();
	UCosmereRealmComponent* RealmComponent = GetCasterRealm();
	if (!Caster || !RealmComponent)
	{
		return;
	}

	const bool bLeaving = RealmComponent->IsInShadesmar();

	// The exit goes through CommitAbility like any other activation, so the base has already stamped the clock
	// with "now" and the row reads as freshly spent. Putting back what it actually had left is what stops
	// leaving from also buying a new cooldown - re-entry still has to wait out the row - and the floor on top of
	// it is what keeps the last sliver of a cooldown from being an instant step back out and in.
	//
	// Done on both machines and before the authority gate, so the local readout cannot advertise a way back in
	// that the authority is still refusing.
	if (bLeaving)
	{
		SetCooldownRemaining(FMath::Max(CooldownRemainingBeforePress, MinimumExitCooldownSeconds));
	}

	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	if (!Info || !Info->IsNetAuthority())
	{
		// Which realm a body is in is a fact every machine has to agree on, so only the authority decides it. A
		// predicting client that flipped its own copy would be a client that could declare itself untouchable;
		// it learns the answer the same way everyone else does, through the replicated realm.
		return;
	}

	const ECosmereRealm Next = bLeaving ? ECosmereRealm::Material : ECosmereRealm::Shadesmar;
	if (!RealmComponent->SetRealm(Next))
	{
		return;
	}

	UE_LOG(LogCosmere, Log, TEXT("CosmereTransportationAbility: '%s' moved %s into %s."),
		*GetPowerId().ToString(), *Caster->GetName(),
		(Next == ECosmereRealm::Shadesmar) ? TEXT("Shadesmar") : TEXT("the Physical Realm"));
}

//~ End Transportation
