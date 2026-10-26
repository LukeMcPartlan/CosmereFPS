#include "Maps/KredikShaw/CosmereKredikShawNavOnlyComponent.h"

#include "AI/NavigationSystemBase.h"
#include "AI/NavigationSystemHelpers.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogKredikShawNav, Log, All);

bool IsKredikShawOwnedWorld(const UWorld* World)
{
	if (!World)
	{
		return false;
	}
	const FString Package = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
	return Package == TEXT("/Game/Maps/Lvl_KendrikShaw") || Package.StartsWith(TEXT("/Game/Maps/KendrikShaw/"));
}

UCosmereKredikShawNavOnlyComponent::UCosmereKredikShawNavOnlyComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitBoxExtent(FVector(200.f, 200.f, 10.f));
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	SetGenerateOverlapEvents(false);
	SetHiddenInGame(true);
	bHasCustomNavigableGeometry = EHasCustomNavigableGeometry::EvenIfNotCollidable;
	SetCanEverAffectNavigation(true);
}

void UCosmereKredikShawNavOnlyComponent::OnRegister()
{
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Super::SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	SetGenerateOverlapEvents(false);
	bHasCustomNavigableGeometry = EHasCustomNavigableGeometry::EvenIfNotCollidable;
	Super::OnRegister();
	if (!IsKredikShawOwnedWorld(GetWorld()))
	{
		UE_LOG(LogKredikShawNav, Warning, TEXT("NAV proxy %s outside owned palace world; no navigation exported."), *GetPathName());
	}
}

void UCosmereKredikShawNavOnlyComponent::SetCollisionEnabled(ECollisionEnabled::Type NewType)
{
	// No query-only/physics profile is an acceptable replacement for custom navigation export.
	Super::SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

bool UCosmereKredikShawNavOnlyComponent::IsNavigationRelevant() const
{
	return IsKredikShawOwnedWorld(GetWorld()) && Super::IsNavigationRelevant();
}

bool UCosmereKredikShawNavOnlyComponent::IsGameplayQueryIsolated() const
{
	return GetCollisionEnabled() == ECollisionEnabled::NoCollision && !IsPhysicsStateCreated() && !GetGenerateOverlapEvents();
}

bool UCosmereKredikShawNavOnlyComponent::SetNavigationHalfExtents(FVector HalfExtentsCM)
{
	if (HalfExtentsCM.ContainsNaN() || HalfExtentsCM.GetMin() <= 0.)
	{
		UE_LOG(LogKredikShawNav, Warning, TEXT("Rejected invalid NAV half extents on %s."), *GetPathName());
		return false;
	}
	SetBoxExtent(HalfExtentsCM, false);
	if (IsRegistered())
	{
		FNavigationSystem::UpdateComponentData(*this);
	}
	return true;
}

bool UCosmereKredikShawNavOnlyComponent::DoCustomNavigableGeometryExport(FNavigableGeometryExport& GeomExport) const
{
	const FVector E = GetUnscaledBoxExtent();
	const FTransform Transform = GetComponentTransform();
	if (!IsKredikShawOwnedWorld(GetWorld()) || E.ContainsNaN() || E.GetMin() <= 0.
		|| Transform.ContainsNaN() || Transform.GetScale3D().GetMin() <= 0. || !FMath::IsFinite(GroundDatumZCM))
	{
		return false;
	}
	// Use the locally inspected Recast box topology/winding. No physics BodySetup export is needed.
	const FVector V[] = {
		FVector(-E.X, -E.Y, E.Z), FVector(E.X, -E.Y, E.Z), FVector(-E.X, -E.Y, -E.Z), FVector(E.X, -E.Y, -E.Z),
		FVector(-E.X, E.Y, E.Z), FVector(E.X, E.Y, E.Z), FVector(-E.X, E.Y, -E.Z), FVector(E.X, E.Y, -E.Z)
	};
	const int32 I[] = { 3,2,0, 3,0,1, 7,3,1, 7,1,5, 6,7,5, 6,5,4, 2,6,4, 2,4,0, 1,0,4, 1,4,5, 7,6,2, 7,2,3 };
	for (const FVector& Vertex : V)
	{
		if (Transform.TransformPosition(Vertex).Z > GroundDatumZCM + 3200.1f)
		{
			UE_LOG(LogKredikShawNav, Warning, TEXT("NAV proxy %s rejected: authored surface above ground+3200 cm."), *GetPathName());
			return false;
		}
	}
	GeomExport.ExportCustomMesh(V, UE_ARRAY_COUNT(V), I, UE_ARRAY_COUNT(I), Transform);
	return false; // Suppress default collision export, including Vehicle-blocking proxy workarounds.
}
