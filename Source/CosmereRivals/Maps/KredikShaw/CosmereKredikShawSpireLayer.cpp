#include "Maps/KredikShaw/CosmereKredikShawSpireLayer.h"

#include "Maps/KredikShaw/CosmereKredikShawNavOnlyComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Misc/Crc.h"

DEFINE_LOG_CATEGORY_STATIC(LogKredikShawLayer, Log, All);

ACosmereKredikShawSpireLayer::ACosmereKredikShawSpireLayer()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("LayerRoot")));
	PrimaryActorTick.bCanEverTick = false;
}

bool ACosmereKredikShawSpireLayer::HasOnlyDecorativeGeometry(const AActor* Actor) const
{
	TInlineComponentArray<UPrimitiveComponent*> Components;
	Actor->GetComponents(Components);
	for (const UPrimitiveComponent* Component : Components)
	{
		if (Component->GetCollisionEnabled() != ECollisionEnabled::NoCollision || Component->CanEverAffectNavigation()
			|| Component->GetGenerateOverlapEvents())
		{
			return false;
		}
	}
	// Do not silently approve a LevelInstance/child-actor container whose content was not inspected.
	TArray<AActor*> AttachedChildren;
	Actor->GetAttachedActors(AttachedChildren);
	return !Components.IsEmpty() && AttachedChildren.IsEmpty();
}

bool ACosmereKredikShawSpireLayer::ValidateOwnership() const
{
	if (!IsKredikShawOwnedWorld(GetWorld()) || !FMath::IsFinite(GroundDatumZCM))
	{
		UE_LOG(LogKredikShawLayer, Error, TEXT("Layer %s rejected: unowned world or invalid ground datum."), *GetName());
		return false;
	}
	TSet<FName> Ids;
	TSet<const AActor*> Actors;
	for (const FKredikShawLayerEntry& Entry : Entries)
	{
		const AActor* Actor = Entry.Actor.Get();
		if (Entry.StableId.IsNone() || Ids.Contains(Entry.StableId) || !IsValid(Actor) || Actors.Contains(Actor)
			|| Actor == this || Actor->GetLevel() != GetLevel() || Actor->GetAttachParentActor()
			|| Entry.LocalAnchor.ContainsNaN() || Actor->GetActorTransform().ContainsNaN())
		{
			UE_LOG(LogKredikShawLayer, Error, TEXT("Layer %s invalid stable ID/root %s; no transforms changed."),
				*GetName(), *Entry.StableId.ToString());
			return false;
		}
		Ids.Add(Entry.StableId);
		Actors.Add(Actor);
		if ((OwnedLayer != EKredikShawLayer::L1Playable || Entry.AnchorRole == EKredikShawAnchorRole::StandardTip
			|| Entry.AnchorRole == EKredikShawAnchorRole::CentralTip) && !HasOnlyDecorativeGeometry(Actor))
		{
			UE_LOG(LogKredikShawLayer, Error, TEXT("Layer %s decorative root %s has collision/navigation/children or no inspected geometry."),
				*GetName(), *Entry.StableId.ToString());
			return false;
		}
		for (TActorIterator<ACosmereKredikShawSpireLayer> It(GetWorld()); It; ++It)
		{
			if (*It == this) { continue; }
			for (const FKredikShawLayerEntry& Other : It->Entries)
			{
				if (Other.Actor == Actor || Other.StableId == Entry.StableId)
				{
					UE_LOG(LogKredikShawLayer, Error, TEXT("Cross-layer duplicate actor/ID %s; ownership is ambiguous."), *Entry.StableId.ToString());
					return false;
				}
			}
		}
	}
	return true;
}

bool ACosmereKredikShawSpireLayer::IsAuthoredFloorHeightValid(float WorldFloorZCM) const
{
	return FMath::IsFinite(WorldFloorZCM) && FMath::IsFinite(GroundDatumZCM)
		&& WorldFloorZCM >= GroundDatumZCM - 1200.1f && WorldFloorZCM <= GroundDatumZCM + 3200.1f;
}

bool ACosmereKredikShawSpireLayer::TrySetL1AnchorHeight(FName StableId, float RelativeHeightCM)
{
	if (OwnedLayer != EKredikShawLayer::L1Playable || !FMath::IsFinite(RelativeHeightCM) || !ValidateOwnership())
	{
		return false;
	}
	for (const FKredikShawLayerEntry& Entry : Entries)
	{
		if (Entry.StableId != StableId) { continue; }
		if (Entry.bPinned)
		{
			UE_LOG(LogKredikShawLayer, Warning, TEXT("Pinned L1 root %s retained."), *StableId.ToString());
			return false;
		}
		const bool bAllowed = Entry.AnchorRole == EKredikShawAnchorRole::WalkableFloor
			? IsAuthoredFloorHeightValid(GroundDatumZCM + RelativeHeightCM)
			: Entry.AnchorRole == EKredikShawAnchorRole::StandardTip ? FMath::IsNearlyEqual(RelativeHeightCM, 6000.f, .1f)
			: Entry.AnchorRole == EKredikShawAnchorRole::CentralTip ? FMath::IsNearlyEqual(RelativeHeightCM, 15000.f, .1f)
			: false;
		if (!bAllowed)
		{
			UE_LOG(LogKredikShawLayer, Warning, TEXT("L1 root %s rejected height %.1f for role %d."),
				*StableId.ToString(), RelativeHeightCM, static_cast<int32>(Entry.AnchorRole));
			return false;
		}
		AActor* Actor = Entry.Actor.Get();
		const double CurrentAnchorZ = Actor->GetActorTransform().TransformPosition(Entry.LocalAnchor).Z;
		FVector Location = Actor->GetActorLocation();
		Location.Z += GroundDatumZCM + RelativeHeightCM - CurrentAnchorZ;
#if WITH_EDITOR
		Actor->Modify();
#endif
		Actor->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogKredikShawLayer, Log, TEXT("L1 anchor %s target=ground%+.1f applied once; pinned and other layers unchanged. Geometry audit still required."),
			*StableId.ToString(), RelativeHeightCM);
		return true;
	}
	return false;
}

bool ACosmereKredikShawSpireLayer::RegenerateLayer()
{
	if (OwnedLayer == EKredikShawLayer::L1Playable || !ValidateOwnership()
		|| !FMath::IsFinite(MinimumRadiusCM) || !FMath::IsFinite(MaximumRadiusCM)
		|| MinimumRadiusCM < 0.f || MaximumRadiusCM < MinimumRadiusCM || !FMath::IsFinite(SkylineBaseHeightCM))
	{
		UE_LOG(LogKredikShawLayer, Warning, TEXT("Layer %s regeneration refused (L1 is never procedurally regenerated)."), *GetName());
		return false;
	}
	int32 Changed = 0, Pinned = 0;
	for (const FKredikShawLayerEntry& Entry : Entries)
	{
		if (Entry.bPinned) { ++Pinned; continue; }
		// CRC of the ID text, not FName's process-local index or array order; stable after reload/reorder.
		const uint32 Seed = FCrc::StrCrc32(*Entry.StableId.ToString()) ^ static_cast<uint32>(LayerSeed)
			^ (static_cast<uint32>(OwnedLayer) * 0x9e3779b9u);
		FRandomStream Random(static_cast<int32>(Seed));
		const float Angle = Random.FRandRange(-PI, PI);
		const float Radius = Random.FRandRange(MinimumRadiusCM, MaximumRadiusCM);
		const float Yaw = Random.FRandRange(-180.f, 180.f);
		AActor* Actor = Entry.Actor.Get();
		FTransform Transform = Actor->GetActorTransform();
		Transform.SetRotation(FRotator(0.f, Yaw, 0.f).Quaternion());
		const FVector AnchorTarget(GetActorLocation().X + FMath::Cos(Angle) * Radius,
			GetActorLocation().Y + FMath::Sin(Angle) * Radius, GroundDatumZCM + SkylineBaseHeightCM);
		Transform.SetLocation(AnchorTarget - Transform.TransformVector(Entry.LocalAnchor));
#if WITH_EDITOR
		Actor->Modify();
#endif
		Actor->SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
		++Changed;
	}
	UE_LOG(LogKredikShawLayer, Log, TEXT("Layer %d seed=%d regenerated=%d pinned=%d; no actor recreation, cross-layer writes or mesh changes."),
		static_cast<int32>(OwnedLayer), LayerSeed, Changed, Pinned);
	return true;
}
