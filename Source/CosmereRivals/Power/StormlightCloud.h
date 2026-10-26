// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StormlightCloud.generated.h"

class UPointLightComponent;
class USphereComponent;
class UStaticMeshComponent;

/**
 * A gem lamp: a standing sphere of stormlight that refills the surge reserves of anyone near it.
 *
 * A world object rather than a power, because most of what it does is spacing. It is the only refill a
 * surge gets - surge pools do not regenerate on their own - so where these sit is the entire cost model
 * for Gravitation, and a level designer has to be able to place one without touching code.
 *
 * It refills continuously rather than instantly: standing in the light buys reserve at a rate, so a fight
 * beside a lamp is a fight with a supply line rather than a key to free lashes.
 */
UCLASS()
class COSMERERIVALS_API AStormlightCloud : public AActor
{
	GENERATED_BODY()

public:
	AStormlightCloud();

	//~ Begin AActor interface
	virtual void OnConstruction(const FTransform& Transform) override;
	//~ End AActor interface

protected:
	//~ Begin AActor interface
	virtual void BeginPlay() override;
	//~ End AActor interface

	/** Hands every overlapping player a slice of surge. Runs on a timer, not on tick. */
	void RefreshOverlappingPawns();

	/** The volume a pawn has to be inside to be refilled. Also the actor's root. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cosmere|Stormlight")
	TObjectPtr<USphereComponent> InfluenceSphere;

	/** The gem itself. Purely visual: nothing reads its transform. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cosmere|Stormlight")
	TObjectPtr<UStaticMeshComponent> LampMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cosmere|Stormlight")
	TObjectPtr<UPointLightComponent> LampGlow;

	/** How close a pawn has to be, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmere|Stormlight", meta = (ClampMin = "50.0"))
	float InfluenceRadius = 400.f;

	/** Surge units restored per second while inside the light. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmere|Stormlight", meta = (ClampMin = "0.0"))
	float RefreshPerSecond = 40.f;

	/** Seconds between refresh slices. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmere|Stormlight", meta = (ClampMin = "0.05"))
	float RefreshInterval = 0.25f;

private:
	FTimerHandle RefreshTimer;
};
