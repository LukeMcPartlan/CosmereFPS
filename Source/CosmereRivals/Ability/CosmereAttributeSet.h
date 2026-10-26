// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "CosmereAttributeSet.generated.h"

/**
 * The in-match health model: exactly two attributes, Health and MaxHealth.
 *
 * Two only, deliberately. The locked design decision is health-only - no shields, no armor - and
 * power resources are NOT attributes here. A GameplayEffect can only modify AttributeSet
 * attributes, and attributes must be declared at compile time, so a shared Stormlight / metal
 * reserve / shield attribute would contradict the per-ability gating design.
 *
 * Note the accessor macro used below is ATTRIBUTE_ACCESSORS_BASIC. A macro literally named
 * ATTRIBUTE_ACCESSORS does appear in the engine header, but only inside a documentation comment - it
 * is not defined, and using it will not compile.
 */
UCLASS()
class COSMERERIVALS_API UCosmereAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UCosmereAttributeSet();

	//~ Begin UObject interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UObject interface

	//~ Begin UAttributeSet interface
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	//~ End UAttributeSet interface

	/** Current health. Set from the build's race row at spawn, then driven by damage. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Health, Category = "Cosmere|Attributes")
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS_BASIC(UCosmereAttributeSet, Health);

	/** Maximum health. Also the value Health resets to on respawn. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxHealth, Category = "Cosmere|Attributes")
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS_BASIC(UCosmereAttributeSet, MaxHealth);

protected:
	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldHealth);

	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth);
};
