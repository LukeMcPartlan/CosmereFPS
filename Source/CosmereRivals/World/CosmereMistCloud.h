// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CosmereMistCloud.generated.h"

/**
 * A cloud of mist that makes it hard to see, and that a tin burner sees straight through.
 *
 * The obscuring itself is the effect's job - this actor only owns the one thing the effect cannot decide
 * on its own: who is looking. Visibility is toggled locally on the machine doing the looking, which is what
 * makes tin a personal sense rather than a server-side state change, and it is also why nothing here is
 * replicated.
 *
 * Every scene component under the root is treated as part of the cloud, so a mist Blueprint can stack a
 * Niagara system, a haze mesh and a sound component without this class knowing what any of them are.
 */
UCLASS()
class COSMERERIVALS_API ACosmereMistCloud : public AActor
{
	GENERATED_BODY()

public:
	ACosmereMistCloud();

	//~ Begin AActor interface
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor interface

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cosmere|Mist")
	TObjectPtr<USceneComponent> SceneRoot;

	/**
	 * How visible this cloud is to someone burning tin. 0 removes it from their picture entirely, which is
	 * what the metal is for; above 0 leaves it in place so a cloud can be made thinner rather than gone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmere|Mist", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TinVisibility = 0.f;

private:
	/** Shows or hides every visual under the root. Called only on a change, never per tick. */
	void ApplyMistVisibility(bool bVisible);

	bool bMistVisible = true;
};
