// Copyright Epic Games, Inc. All Rights Reserved.

#include "Character/CosmereRealmComponent.h"

#include "Camera/CameraComponent.h"
#include "Character/CosmereCharacter.h"
#include "Character/CosmereMovementModifierComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Scene.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

namespace
{
	/** The single source key this component registers its movement contribution under. */
	const FName ShadesmarSpeedSource(TEXT("Realm.Shadesmar"));
}

UCosmereRealmComponent::UCosmereRealmComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// The realm is match state, so every machine has to be told about it rather than each machine keeping its
	// own answer. A component that does not replicate would leave a client believing its opponent is still
	// real, which is the one thing this whole component exists to prevent.
	SetIsReplicatedByDefault(true);
}

void UCosmereRealmComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCosmereRealmComponent, Realm);
}

void UCosmereRealmComponent::BeginPlay()
{
	Super::BeginPlay();

	// Both are idempotent, so a pawn that spawned into a world where a realm was already running simply
	// re-asserts the answer it already had.
	ApplyRealm();
}

FName UCosmereRealmComponent::ShadesmarTagName()
{
	// A function rather than a file-scope constant: a static FName in a load-time-initialised namespace is the
	// kind of thing that is fine until it is not, and this is called a handful of times per frame at most.
	return FName(TEXT("Cosmere.Realm.Shadesmar"));
}

UCosmereRealmComponent* UCosmereRealmComponent::FindRealm(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UCosmereRealmComponent>() : nullptr;
}

ECosmereRealm UCosmereRealmComponent::GetActorRealm(const AActor* Actor)
{
	if (!Actor)
	{
		return ECosmereRealm::Material;
	}

	if (const UCosmereRealmComponent* Component = FindRealm(Actor))
	{
		return Component->GetRealm();
	}

	// A stamped spawn is the one other kind of thing that lives on a side. Everything else - the floor, a
	// wall, a prop - reads as material, and ShareRealm below is what keeps that from mattering.
	if (Actor->ActorHasTag(ShadesmarTagName()))
	{
		return ECosmereRealm::Shadesmar;
	}

	return ECosmereRealm::Material;
}

bool UCosmereRealmComponent::ShareRealm(const AActor* A, const AActor* B)
{
	if (!A || !B)
	{
		return false;
	}

	const bool bAAnswers = (FindRealm(A) != nullptr) || A->ActorHasTag(ShadesmarTagName());
	const bool bBAnswers = (FindRealm(B) != nullptr) || B->ActorHasTag(ShadesmarTagName());

	// The world is in both realms at once. A wall that stopped only the material players would be a wall you
	// could walk through by phasing, and a bullet that carried on through the floor would be worse than that.
	if (!bAAnswers || !bBAnswers)
	{
		return true;
	}

	return GetActorRealm(A) == GetActorRealm(B);
}

void UCosmereRealmComponent::StampRealm(AActor* Spawned, const AActor* Source)
{
	if (!Spawned || GetActorRealm(Source) != ECosmereRealm::Shadesmar)
	{
		return;
	}

	Spawned->Tags.AddUnique(ShadesmarTagName());
}

bool UCosmereRealmComponent::SetRealm(ECosmereRealm NewRealm)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return false;
	}

	if (NewRealm == Realm)
	{
		return false;
	}

	Realm = NewRealm;

	// OnRep does not fire on the authority, so the authority applies its own change directly. Clients reach
	// the same place through OnRep_Realm.
	ApplyRealm();
	OnRealmChanged.Broadcast(Realm);

	return true;
}

bool UCosmereRealmComponent::ToggleRealm()
{
	return SetRealm(IsInShadesmar() ? ECosmereRealm::Material : ECosmereRealm::Shadesmar);
}

void UCosmereRealmComponent::OnRep_Realm()
{
	ApplyRealm();
	OnRealmChanged.Broadcast(Realm);
}

void UCosmereRealmComponent::ApplyRealm()
{
	ApplySpeedModifier();
	ApplyLocalFilter();
	RefreshPairing();
	RefreshLocalView(GetWorld());
}

void UCosmereRealmComponent::ApplySpeedModifier()
{
	ACosmereCharacter* Character = Cast<ACosmereCharacter>(GetOwner());
	UCosmereMovementModifierComponent* Modifiers = Character ? Character->GetMovementModifiers() : nullptr;
	if (!Modifiers)
	{
		return;
	}

	if (Realm == ECosmereRealm::Shadesmar)
	{
		// A Multiply on WalkSpeed rather than a write to MaxWalkSpeed, so phasing multiplies a metalmind or a
		// slow instead of erasing it, and so leaving Shadesmar takes exactly its own contribution back out.
		Modifiers->SetModifier(ShadesmarSpeedSource, ECosmereMovementChannel::WalkSpeed,
			ECosmereMovementOp::Multiply, ShadesmarSpeedMultiplier);
		return;
	}

	Modifiers->ClearSource(ShadesmarSpeedSource);
}

void UCosmereRealmComponent::ApplyLocalFilter()
{
	const ACosmereCharacter* Character = Cast<ACosmereCharacter>(GetOwner());
	if (!Character || !Character->IsLocallyControlled())
	{
		return;
	}

	UCameraComponent* Camera = Character->FindComponentByClass<UCameraComponent>();
	if (!Camera)
	{
		return;
	}

	FPostProcessSettings& Settings = Camera->PostProcessSettings;

	if (!bFilterCached)
	{
		CachedSaturation = Settings.ColorSaturation;
		bCachedSaturationOverride = Settings.bOverride_ColorSaturation;
		bFilterCached = true;
	}

	if (Realm == ECosmereRealm::Shadesmar)
	{
		// Colour grading only, and only the saturation term of it: tin owns the exposure and the master volume
		// on this same camera, and neither of them should be disturbed by stepping sideways.
		const double Grey = static_cast<double>(ShadesmarSaturation);
		Settings.bOverride_ColorSaturation = true;
		Settings.ColorSaturation = FVector4(Grey, Grey, Grey, 1.0);
		return;
	}

	Settings.bOverride_ColorSaturation = bCachedSaturationOverride;
	Settings.ColorSaturation = CachedSaturation;
}

void UCosmereRealmComponent::RefreshPairing()
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}

	UPrimitiveComponent* MyBody = Cast<UPrimitiveComponent>(Owner->GetRootComponent());
	if (!MyBody)
	{
		return;
	}

	for (TActorIterator<ACosmereCharacter> It(World); It; ++It)
	{
		ACosmereCharacter* Other = *It;
		if (!IsValid(Other) || Other == Owner)
		{
			continue;
		}

		UPrimitiveComponent* TheirBody = Cast<UPrimitiveComponent>(Other->GetRootComponent());
		if (!TheirBody)
		{
			continue;
		}

		const bool bIgnore = !ShareRealm(Owner, Other);

		// Both directions. IgnoreActorWhenMoving only ever covers the body that is doing the moving, so a pair
		// written from one end would still be solid when the other player walked into it.
		MyBody->IgnoreActorWhenMoving(Other, bIgnore);
		TheirBody->IgnoreActorWhenMoving(Owner, bIgnore);
	}
}

void UCosmereRealmComponent::SetPhasedOutLocally(bool bNewPhasedOut)
{
	if (bNewPhasedOut == bPhasedOut)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	bPhasedOut = bNewPhasedOut;

	if (bPhasedOut)
	{
		TArray<UPrimitiveComponent*> Primitives;
		Owner->GetComponents<UPrimitiveComponent>(Primitives);

		HiddenPrimitives.Reset();
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			if (!Primitive || !Primitive->IsVisible())
			{
				// Only what was actually on. Restoring a component the pawn itself had hidden would be a second
				// bug (a muzzle flash left lit, a weapon placeholder popping back) hidden inside the first.
				continue;
			}

			Primitive->SetVisibility(false, /*bPropagateToChildren=*/false);
			HiddenPrimitives.Add(Primitive);
		}

		return;
	}

	for (const TWeakObjectPtr<UPrimitiveComponent>& Entry : HiddenPrimitives)
	{
		if (UPrimitiveComponent* Primitive = Entry.Get())
		{
			Primitive->SetVisibility(true, /*bPropagateToChildren=*/false);
		}
	}

	HiddenPrimitives.Reset();
}

void UCosmereRealmComponent::RefreshLocalView(UWorld* World)
{
	if (!World)
	{
		return;
	}

	// GetPlayerPawn(0) rather than the world's local players: this is a single-viewer game, and every machine
	// answers for exactly one pair of eyes - the one sitting in front of it. No viewer means no picture to
	// correct, which is the dedicated server's answer.
	const APawn* Viewer = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!Viewer)
	{
		return;
	}

	const ECosmereRealm ViewRealm = GetActorRealm(Viewer);
	const bool bViewerIsPhased = (ViewRealm == ECosmereRealm::Shadesmar);

	for (TActorIterator<ACosmereCharacter> It(World); It; ++It)
	{
		ACosmereCharacter* Other = *It;
		if (!IsValid(Other) || Other == Viewer)
		{
			continue;
		}

		if (UCosmereRealmComponent* Theirs = Other->GetRealmComponent())
		{
			Theirs->SetPhasedOutLocally(GetActorRealm(Other) != ViewRealm);
		}
	}

	// Stamped spawns carry no component to hold their own answer, so they are decided here in one pass: a
	// bubble opened in Shadesmar is visible to the Shadesmar and to nobody else.
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Stamped = *It;
		if (!IsValid(Stamped) || !Stamped->ActorHasTag(ShadesmarTagName()))
		{
			continue;
		}

		TArray<UPrimitiveComponent*> Primitives;
		Stamped->GetComponents<UPrimitiveComponent>(Primitives);
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			if (Primitive)
			{
				Primitive->SetVisibility(bViewerIsPhased, /*bPropagateToChildren=*/false);
			}
		}
	}
}
