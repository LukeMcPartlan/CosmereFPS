#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "CosmereKredikShawNavOnlyComponent.generated.h"

/** Checks actual world package, stripping PIE prefix; never opts another map into this policy. */
COSMERERIVALS_API bool IsKredikShawOwnedWorld(const UWorld* World);

/** Navigation deck proxy with no physics/query state, independent of VIS/COL/capture geometry. */
UCLASS(ClassGroup=(KredikShaw), meta=(BlueprintSpawnableComponent), hidecategories=(Collision))
class COSMERERIVALS_API UCosmereKredikShawNavOnlyComponent : public UBoxComponent
{
	GENERATED_BODY()

public:
	UCosmereKredikShawNavOnlyComponent(const FObjectInitializer& ObjectInitializer);
	virtual bool IsNavigationRelevant() const override;
	virtual bool DoCustomNavigableGeometryExport(FNavigableGeometryExport& GeomExport) const override;
	virtual void SetCollisionEnabled(ECollisionEnabled::Type NewType) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kredik Shaw|Navigation")
	float GroundDatumZCM = 0.f;

	/** Use this instead of inherited SetBoxExtent when changing extents at runtime. */
	UFUNCTION(BlueprintCallable, Category="Kredik Shaw|Navigation")
	bool SetNavigationHalfExtents(FVector HalfExtentsCM);

	UFUNCTION(BlueprintPure, Category="Kredik Shaw|Diagnostics")
	bool IsGameplayQueryIsolated() const;

protected:
	virtual void OnRegister() override;
	/** Even direct BodyInstance profile edits cannot create a gameplay scene-query body. */
	virtual bool ShouldCreatePhysicsState() const override { return false; }
};
