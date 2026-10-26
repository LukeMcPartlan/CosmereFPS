#include "Input/CosmereLookPreferencesModifier.h"
#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "UI/CosmereSettingsSubsystem.h"
FInputActionValue UCosmereLookPreferencesModifier::ModifyRaw_Implementation(const UEnhancedPlayerInput* Input,FInputActionValue Value,float)
{
 if(Value.GetValueType()!=EInputActionValueType::Axis2D || !Input) return Value;
 const auto* PC=Cast<APlayerController>(Input->GetOuter());
 auto* LP=PC?PC->GetLocalPlayer():nullptr;
 if(auto* S=LP?LP->GetSubsystem<UCosmereSettingsSubsystem>():nullptr) return FInputActionValue(S->AdjustLook(Value.Get<FVector2D>(),bGamepad));
 return Value;
}
