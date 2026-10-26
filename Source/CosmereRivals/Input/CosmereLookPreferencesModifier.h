#pragma once
#include "CoreMinimal.h"
#include "InputModifiers.h"
#include "CosmereLookPreferencesModifier.generated.h"
/** Append after EXISTING look modifiers, or use SettingsSubsystem.AdjustLook in BP, never both. */
UCLASS(meta=(DisplayName="Cosmere Device Look Preferences"))
class COSMERERIVALS_API UCosmereLookPreferencesModifier:public UInputModifier
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cosmere|Input") bool bGamepad=false;
protected:
 virtual FInputActionValue ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput,FInputActionValue CurrentValue,float DeltaTime) override;
};
