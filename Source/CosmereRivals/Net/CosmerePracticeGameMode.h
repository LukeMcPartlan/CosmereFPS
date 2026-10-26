// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/CosmereTypes.h"
#include "Net/CosmereGameMode.h"
#include "CosmerePracticeGameMode.generated.h"

/** The practice world doubles as the front end; gameplay starts only on an explicit request. */
UCLASS()
class COSMERERIVALS_API ACosmerePracticeGameMode : public ACosmereGameMode
{
	GENERATED_BODY()

public:
	ACosmerePracticeGameMode();

	UFUNCTION(BlueprintCallable, Category = "Cosmere|Practice")
	bool StartPractice(APlayerController* Controller, const FSpiritwebCharacterSave& Build, FText& OutError);
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Practice")
	void StopPractice(APlayerController* Controller);
	UFUNCTION(BlueprintPure, Category = "Cosmere|Practice")
	bool IsPracticeRunning() const { return bPracticeRunning; }

protected:
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual bool ShouldRunMatchRules() const override { return false; }

private:
	bool bPracticeRunning = false;
};
