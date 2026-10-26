// Copyright Epic Games, Inc. All Rights Reserved.

#include "Power/CosmereDivisionAbility.h"

#include "CosmereRivals.h"
#include "Engine/EngineTypes.h"
#include "Character/CosmereRealmComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

UCosmereDivisionAbility::UCosmereDivisionAbility()
{
	// Nothing here ends the channel on its own: it runs until the input layer calls EndPower, or until
	// the row's own ongoing charge can no longer be paid. The base class would otherwise end the ability
	// as soon as OnPowerActivated returned, which would make this a one-frame touch.
	bEndAfterActivation = false;
}

bool UCosmereDivisionAbility::ResolveTouch(FVector& OutTouchPoint, AActor*& OutVictim) const
{
	OutVictim = nullptr;

	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	APawn* Pawn = ActorInfo ? Cast<APawn>(ActorInfo->AvatarActor.Get()) : nullptr;
	UWorld* World = GetPowerWorld(ActorInfo);
	if (!Pawn || !World)
	{
		return false;
	}

	// The controller's view point rather than the actor transform, so the decay lands where the
	// crosshair is actually pointing. Same choice the weapon's hitscan makes, for the same reason.
	FVector ViewLocation;
	FRotator ViewRotation;
	if (const APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
	{
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	else
	{
		Pawn->GetActorEyesViewPoint(ViewLocation, ViewRotation);
	}

	const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * TouchDistance;

	FCollisionQueryParams Params;
	Params.bTraceComplex = true;
	Params.AddIgnoredActor(Pawn);

	// The field itself must never be what the next tick burns, or the power would eat its own effect.
	if (ActiveField)
	{
		Params.AddIgnoredActor(ActiveField);
	}

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, ECC_Visibility, Params))
	{
		return false;
	}

	OutTouchPoint = Hit.ImpactPoint;
	OutVictim = Hit.GetActor();
	return true;
}

void UCosmereDivisionAbility::OnPowerActivated(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	UWorld* World = GetPowerWorld(ActorInfo);
	if (!World)
	{
		return;
	}

	// Authority only. The predicting client deliberately does not get a tick of its own; it sees the
	// decay through the replicated state of whatever the server is burning.
	if (!ActorInfo || !ActorInfo->IsNetAuthority())
	{
		return;
	}

	if (FieldClass)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = ActorInfo->AvatarActor.Get();
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector SpawnLocation = ActorInfo->AvatarActor.IsValid()
				? ActorInfo->AvatarActor->GetActorLocation()
				: FVector::ZeroVector;
		ActiveField = World->SpawnActor<AActor>(FieldClass, SpawnLocation, FRotator::ZeroRotator, SpawnParams);

		// Stamped so the field is a thing of the realm that made it, the same way the archetype fields are.
		UCosmereRealmComponent::StampRealm(ActiveField, ActorInfo->AvatarActor.Get());
	}

	// Ticked once right here and then every interval, so the channel bites on the frame it is pressed
	// rather than a quarter of a second later - and so the field above is placed by that same tick.
	ApplyDecayTick();

	World->GetTimerManager().SetTimer(
		DecayTimer, this, &UCosmereDivisionAbility::ApplyDecayTick, TickInterval, /*bLoop=*/true);
}

void UCosmereDivisionAbility::ApplyDecayTick()
{
	FVector TouchPoint = FVector::ZeroVector;
	AActor* Victim = nullptr;
	const bool bTouching = ResolveTouch(TouchPoint, Victim);

	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	APawn* Pawn = ActorInfo ? Cast<APawn>(ActorInfo->AvatarActor.Get()) : nullptr;

	if (ActiveField)
	{
		// Moved only while something is actually under it. Setting the location unconditionally
		// would park the field at the world origin, which is what a zeroed touch point resolves to.
		if (bTouching)
		{
			ActiveField->SetActorLocation(TouchPoint);
		}
		ActiveField->SetActorHiddenInGame(!bTouching);
	}

	if (!bTouching || !Victim || Victim == Pawn)
	{
		// Edge-triggered, so a run that lands nothing still says which side failed: a decay that never
		// reports a touch at all is a tick that never ran, while a touch with no decay behind it is a
		// reach problem.
		if (bTouchingLastTick)
		{
			UE_LOG(LogCosmere, Log, TEXT("CosmereDivisionAbility: decay lost its touch."));
			bTouchingLastTick = false;
		}

		return;
	}

	if (!bTouchingLastTick)
	{
		UE_LOG(LogCosmere, Log, TEXT("CosmereDivisionAbility: decay touched %s at %s."),
			*GetNameSafe(Victim), *TouchPoint.ToCompactString());
		bTouchingLastTick = true;
	}

	// Through the engine's damage pipeline rather than ApplyModToAttribute, so a decay kill reaches
	// ACosmereCharacter::TakeDamage with this player's controller attached and is credited the same way
	// a weapon kill is. AController and not APlayerController: a bot is driven by an AAIController.
	AController* InstigatorController = Pawn ? Pawn->GetInstigatorController() : nullptr;
	UGameplayStatics::ApplyDamage(Victim, DamagePerTick, InstigatorController, Pawn, nullptr);

	UE_LOG(LogCosmere, Log,
		TEXT("CosmereDivisionAbility: decayed %s for %.1f at %s."),
		*GetNameSafe(Victim), DamagePerTick, *TouchPoint.ToCompactString());
}

void UCosmereDivisionAbility::OnPowerEnded(bool bWasCancelled)
{
	// Both the timer and the field are cleared here rather than in EndAbility, because this is the one
	// hook every end path runs - release, running dry, a cancel, and the replicated end on a client.
	if (UWorld* World = GetPowerWorld(GetCurrentActorInfo()))
	{
		World->GetTimerManager().ClearTimer(DecayTimer);
	}

	if (ActiveField)
	{
		ActiveField->Destroy();
		ActiveField = nullptr;
		bTouchingLastTick = false;
	}
}
