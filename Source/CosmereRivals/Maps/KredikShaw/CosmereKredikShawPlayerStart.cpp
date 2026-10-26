#include "Maps/KredikShaw/CosmereKredikShawPlayerStart.h"

ACosmereKredikShawPlayerStart::ACosmereKredikShawPlayerStart(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool ACosmereKredikShawPlayerStart::IsTier(int32 HeightCM)
{
	return HeightCM == -1200 || HeightCM == 0 || HeightCM == 800 || HeightCM == 1600
		|| HeightCM == 2400 || HeightCM == 3200;
}

bool ACosmereKredikShawPlayerStart::Describe(const APlayerStart* Start, FKredikShawStartDescriptor& Out)
{
	Out = FKredikShawStartDescriptor();
	if (!IsValid(Start))
	{
		return false;
	}
	if (const ACosmereKredikShawPlayerStart* Native = Cast<ACosmereKredikShawPlayerStart>(Start))
	{
		if (!Native->bSpawnEnabled)
		{
			return false;
		}
		Out.StableId = Native->StableId;
		Out.Team = Native->Team;
		Out.TierCM = Native->TierHeightCM;
		Out.bFreeForAll = Native->bFreeForAllOnly;
	}
	else
	{
		// Explicit opt-in only. Never treat arbitrary/PIE PlayerStarts as map starts.
		const bool bFFA = Start->ActorHasTag(TEXT("KS.FFA"));
		const bool bControl = Start->ActorHasTag(TEXT("KS.Control"));
		const bool bA = Start->ActorHasTag(TEXT("KS.Team.A"));
		const bool bB = Start->ActorHasTag(TEXT("KS.Team.B"));
		if (bFFA == bControl || (bControl && bA == bB) || (bFFA && (bA || bB)))
		{
			return false;
		}
		Out.StableId = Start->PlayerStartTag;
		Out.bFreeForAll = bFFA;
		Out.Team = bA ? ECosmereTeam::TeamA : bB ? ECosmereTeam::TeamB : ECosmereTeam::None;
		const TCHAR* TierTags[] = { TEXT("KS.Tier.Well"), TEXT("KS.Tier.Ground"), TEXT("KS.Tier.8"),
			TEXT("KS.Tier.16"), TEXT("KS.Tier.24"), TEXT("KS.Tier.32") };
		const int32 TierValues[] = { -1200, 0, 800, 1600, 2400, 3200 };
		int32 Found = 0;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(TierValues); ++Index)
		{
			if (Start->ActorHasTag(FName(TierTags[Index])))
			{
				Out.TierCM = TierValues[Index];
				++Found;
			}
		}
		if (Found != 1)
		{
			return false;
		}
	}
	return !Out.StableId.IsNone() && IsTier(Out.TierCM)
		&& (Out.bFreeForAll ? Out.Team == ECosmereTeam::None : Out.Team != ECosmereTeam::None);
}
