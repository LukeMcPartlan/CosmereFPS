// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CosmereWeaponComponent.h"

#include "Ability/CosmereAbilitySystemComponent.h"
#include "Ability/CosmereAttributeSet.h"
#include "Character/CosmereCharacter.h"
#include "Character/CosmereRealmComponent.h"
#include "CollisionQueryParams.h"
#include "Components/StaticMeshComponent.h"
#include "Data/CosmereCatalogSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/CosmereGameState.h"
#include "Net/UnrealNetwork.h"
#include "CosmereRivals.h"

static_assert(CosmereWeaponSlots::Count == static_cast<int32>(EWeaponSlot::Secondary) + 1,
	"CosmereWeaponSlots::Count is out of step with EWeaponSlot. Update both together.");

UCosmereWeaponComponent::UCosmereWeaponComponent()
{
	// Firing and swapping are edge-triggered from input, but the tick is what lands a reload and
	// advances the placeholder spin, so it stays on.
	PrimaryComponentTick.bCanEverTick = true;

	// The component must replicate for ServerRequestFire to have a connection to travel over, and so
	// the owning client can read slot state back.
	SetIsReplicatedByDefault(true);

	// Sized up front so indexing a slot never needs a bounds check at the call site.
	Slots.SetNum(CosmereWeaponSlots::Count);
}

void UCosmereWeaponComponent::BeginPlay()
{
	Super::BeginPlay();

	// Resolved before the build hook-up below, so the mesh is bound even when the owner is not an
	// ACosmereCharacter and that early return fires.
	ResolveWeaponMesh();

	ACosmereCharacter* Pawn = Cast<ACosmereCharacter>(GetOwner());
	if (!Pawn)
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereWeaponComponent: owner %s is not an ACosmereCharacter, so no build can be hooked."),
			*GetNameSafe(GetOwner()));
		return;
	}

	// The seam. Binding here rather than to the PlayerState's replication callback means this
	// component cannot be affected by how a build reaches the pawn.
	Pawn->OnBuildApplied.AddDynamic(this, &UCosmereWeaponComponent::HandleBuildApplied);

	// Possession can happen after component registration, so the build may already have been applied
	// before this component existed. Catch that case rather than waiting for an event already fired.
	if (Pawn->HasAppliedBuild())
	{
		EquipFromBuild(Pawn->GetAppliedBuild());
	}
}

void UCosmereWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Completion is checked here rather than on a timer so the same tick that advances the spin also
	// lands the magazine change, keeping the visual and the ammo in lockstep.
	if (bIsReloading && GetOwner() && GetOwner()->HasAuthority())
	{
		const UWorld* World = GetWorld();
		if (World && World->GetTimeSeconds() >= ReloadEndTime)
		{
			FinishReload();
		}
	}

	// Held-to-fire, for the rows that ask for it. bFireHeld is only ever set on the authority, and
	// TryFire re-guards, so this is safe to evaluate on every machine. CanFire enforces the row's
	// FireInterval, which paces the burst to the weapon rather than to the tick rate; a semi-auto row
	// fires once on the press and never reaches here at all.
	if (bFireHeld && IsActiveWeaponAutomatic())
	{
		TryFire();
	}

	UpdateReloadSpin(DeltaTime);
}

void UCosmereWeaponComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// A trigger still held when the pawn goes away would otherwise leave the next possession firing on
	// its first tick, before any input has arrived for it.
	bFireHeld = false;

	if (ACosmereCharacter* Pawn = Cast<ACosmereCharacter>(GetOwner()))
	{
		Pawn->OnBuildApplied.RemoveDynamic(this, &UCosmereWeaponComponent::HandleBuildApplied);
	}

	Super::EndPlay(EndPlayReason);
}

void UCosmereWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCosmereWeaponComponent, Slots);
	DOREPLIFETIME(UCosmereWeaponComponent, ActiveSlotIndex);
	DOREPLIFETIME(UCosmereWeaponComponent, bIsReloading);
	DOREPLIFETIME(UCosmereWeaponComponent, bWeaponEnabled);
}

UCosmereCatalogSubsystem* UCosmereWeaponComponent::GetCatalog() const
{
	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UCosmereCatalogSubsystem>() : nullptr;
}

bool UCosmereWeaponComponent::GetWeaponRow(FName WeaponId, FWeaponDefinition& OutRow) const
{
	UCosmereCatalogSubsystem* Catalog = GetCatalog();
	return Catalog && Catalog->GetWeaponRow(WeaponId, OutRow);
}

bool UCosmereWeaponComponent::IsValidSlotIndex(int32 Index) const
{
	return Slots.IsValidIndex(Index);
}

bool UCosmereWeaponComponent::IsSlotEquipped(int32 Index) const
{
	return Slots.IsValidIndex(Index) && Slots[Index].bEquipped;
}

int32 UCosmereWeaponComponent::GetAmmoInSlotByIndex(int32 Index) const
{
	return Slots.IsValidIndex(Index) ? Slots[Index].AmmoInMagazine : 0;
}

FName UCosmereWeaponComponent::GetActiveWeaponId() const
{
	return Slots.IsValidIndex(ActiveSlotIndex) ? Slots[ActiveSlotIndex].WeaponId : NAME_None;
}

FName UCosmereWeaponComponent::GetWeaponIdInSlot(EWeaponSlot Slot) const
{
	const int32 Index = static_cast<int32>(Slot);
	return Slots.IsValidIndex(Index) ? Slots[Index].WeaponId : NAME_None;
}

int32 UCosmereWeaponComponent::GetAmmoInSlot(EWeaponSlot Slot) const
{
	return GetAmmoInSlotByIndex(static_cast<int32>(Slot));
}

void UCosmereWeaponComponent::HandleBuildApplied(const FSpiritwebCharacterSave& Build)
{
	EquipFromBuild(Build);
}

void UCosmereWeaponComponent::EquipFromBuild(const FSpiritwebCharacterSave& Build)
{
	UCosmereCatalogSubsystem* Catalog = GetCatalog();
	if (!Catalog || !Catalog->IsCatalogReady())
	{
		UE_LOG(LogCosmere, Warning,
			TEXT("CosmereWeaponComponent: the catalog is not loaded, so no weapons were equipped on %s."),
			*GetNameSafe(GetOwner()));
		return;
	}

	Slots.SetNum(CosmereWeaponSlots::Count);
	
	// Whatever was in flight belonged to the kit being replaced.
	CancelReload();

	int32 EquippedCount = 0;
	for (int32 Index = 0; Index < CosmereWeaponSlots::Count; ++Index)
	{
		FCosmereWeaponSlotState& Slot = Slots[Index];
		Slot.WeaponId = Build.WeaponIds.IsValidIndex(Index) ? Build.WeaponIds[Index] : NAME_None;
		Slot.bEquipped = false;
		Slot.AmmoInMagazine = 0;

		if (Slot.WeaponId.IsNone())
		{
			continue;
		}

		FWeaponDefinition Row;
		if (!Catalog->GetWeaponRow(Slot.WeaponId, Row))
		{
			UE_LOG(LogCosmere, Warning,
				TEXT("CosmereWeaponComponent: weapon id '%s' is not in DT_Weapons, so slot %d on %s stays empty."),
				*Slot.WeaponId.ToString(), Index, *GetNameSafe(GetOwner()));
			Slot.WeaponId = NAME_None;
			continue;
		}

		Slot.bEquipped = true;
		Slot.AmmoInMagazine = Row.MagazineSize;
		++EquippedCount;
	}

	// Land on a slot that actually holds something, so a build carrying one weapon never leaves the
	// pawn pointed at the empty slot.
	ActiveSlotIndex = Slots[0].bEquipped ? 0 : 1;
	RefreshActiveWeaponRow();

	UE_LOG(LogCosmere, Log,
		TEXT("CosmereWeaponComponent: equipped %d/%d weapon(s) on %s - slot 0 '%s' (%d rounds), ")
			 TEXT("slot 1 '%s' (%d rounds), active slot %d."),
		EquippedCount, CosmereWeaponSlots::Count, *GetNameSafe(GetOwner()),
		*Slots[0].WeaponId.ToString(), Slots[0].AmmoInMagazine,
		*Slots[1].WeaponId.ToString(), Slots[1].AmmoInMagazine,
		ActiveSlotIndex);
}

void UCosmereWeaponComponent::RefreshActiveWeaponRow()
{
	ActiveWeaponRow = FWeaponDefinition();

	if (!IsValidSlotIndex(ActiveSlotIndex))
	{
		return;
	}

	const FName WeaponId = Slots[ActiveSlotIndex].WeaponId;
	if (WeaponId.IsNone())
	{
		return;
	}

	// Quiet on failure: a missing row was already warned about at equip time.
	GetWeaponRow(WeaponId, ActiveWeaponRow);
}

bool UCosmereWeaponComponent::CanFire() const
{
	// A dead pawn's weapon is switched off, and this is checked first because it has to win: the
	// input leg keeps arriving from a client that has not yet learned it died, and the tick's
	// held-to-fire leg knows nothing about health at all.
	if (!bWeaponEnabled)
	{
		return false;
	}

	// A reload owns the weapon until it finishes.
	if (bIsReloading)
	{
		return false;
	}

	if (!IsSlotEquipped(ActiveSlotIndex) || GetAmmoInSlotByIndex(ActiveSlotIndex) <= 0)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// Read the interval from the row rather than caching it, so a tuning change is picked up on the
	// next shot without needing a re-equip.
	FWeaponDefinition Row;
	if (!GetWeaponRow(Slots[ActiveSlotIndex].WeaponId, Row))
	{
		return false;
	}

	return World->GetTimeSeconds() - LastFireTime >= Row.FireInterval;
}

void UCosmereWeaponComponent::SetWeaponEnabled(bool bEnabled)
{
	if (bWeaponEnabled == bEnabled)
	{
		return;
	}

	bWeaponEnabled = bEnabled;

	if (bEnabled)
	{
		return;
	}

	UE_LOG(LogCosmere, Log,
		TEXT("CosmereWeaponComponent: the weapon on %s was switched off."),
		*GetNameSafe(GetOwner()));

	// Clearing the held trigger is what makes this more than a flag. A tick that still had bFireHeld
	// set would keep calling TryFire every frame, and the next possession would inherit a trigger that
	// nobody is holding.
	bFireHeld = false;

	// An in-flight reload is abandoned rather than left to land on a corpse. CancelReload refills
	// nothing, so the magazine the dead pawn had is the magazine it keeps until it respawns.
	CancelReload();
}

void UCosmereWeaponComponent::RequestFire()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		TryFire();
		return;
	}

	ServerRequestFire();
}

void UCosmereWeaponComponent::SetTriggerHeld(bool bHeld)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ApplyTriggerHeld(bHeld);
		return;
	}

	ServerSetTriggerHeld(bHeld);
}

void UCosmereWeaponComponent::ApplyTriggerHeld(bool bHeld)
{
	bFireHeld = bHeld;

	// The press takes its shot here rather than waiting for the next tick, so a semi-auto round and the
	// opening round of a burst both land on the frame the input arrived. The row's FireInterval absorbs
	// the duplicate the tick would otherwise produce on that same frame.
	if (bHeld)
	{
		TryFire();
	}
}

bool UCosmereWeaponComponent::IsActiveWeaponAutomatic() const
{
	if (!IsSlotEquipped(ActiveSlotIndex))
	{
		return false;
	}

	FWeaponDefinition Row;
	return GetWeaponRow(Slots[ActiveSlotIndex].WeaponId, Row) && Row.bAutomatic;
}

void UCosmereWeaponComponent::RequestReload()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		TryReload();
		return;
	}

	ServerRequestReload();
}

void UCosmereWeaponComponent::RequestSwapToSlot(EWeaponSlot Slot)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		SwapToSlot(Slot);
		return;
	}

	ServerRequestSwapToSlot(Slot);
}

void UCosmereWeaponComponent::RequestSwapToOtherSlot()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		SwapToOtherSlot();
		return;
	}

	ServerRequestSwapToOtherSlot();
}

void UCosmereWeaponComponent::ServerRequestFire_Implementation()
{
	TryFire();
}

void UCosmereWeaponComponent::ServerSetTriggerHeld_Implementation(bool bHeld)
{
	ApplyTriggerHeld(bHeld);
}

void UCosmereWeaponComponent::ServerRequestReload_Implementation()
{
	TryReload();
}

void UCosmereWeaponComponent::ServerRequestSwapToSlot_Implementation(EWeaponSlot Slot)
{
	SwapToSlot(Slot);
}

void UCosmereWeaponComponent::ServerRequestSwapToOtherSlot_Implementation()
{
	SwapToOtherSlot();
}

bool UCosmereWeaponComponent::TryFire()
{
	// The trace and the damage both happen on the authority, so a client calling this is a no-op
	// rather than a divergent local shot.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	if (!IsSlotEquipped(ActiveSlotIndex))
	{
		return false;
	}

	if (GetAmmoInSlotByIndex(ActiveSlotIndex) <= 0)
	{
		UE_LOG(LogCosmere, Log,
			TEXT("CosmereWeaponComponent: %s tried to fire an empty slot %d."),
			*GetNameSafe(GetOwner()), ActiveSlotIndex);

		// Pulling the trigger on an empty magazine reloads it rather than doing nothing. This is the one
		// funnel every fire path reaches - the server RPC, the press, and the held-to-fire tick - so the
		// reload is started here and nowhere else, and no caller has to remember to ask for it first.
		//
		// Guarded on the weapon switch, because firing an empty gun is not a request to reload it: a dead
		// pawn's input leg keeps arriving for a while after it died, and SetWeaponEnabled(false) is what
		// cancels an in-flight reload. Without the guard those two would fight, and the corpse would be
		// reloading itself for as long as the trigger was held.
		//
		// TryReload declines on its own while one is already running, which is what stops a held trigger
		// from restarting the spin on every frame of the tick.
		if (bWeaponEnabled)
		{
			TryReload();
		}

		return false;
	}

	// Covers both the fire interval and a row that has gone missing.
	if (!CanFire())
	{
		return false;
	}

	const FName WeaponId = Slots[ActiveSlotIndex].WeaponId;
	FWeaponDefinition Row;
	if (!GetWeaponRow(WeaponId, Row))
	{
		return false;
	}

	LastFireTime = GetWorld()->GetTimeSeconds();
	Slots[ActiveSlotIndex].AmmoInMagazine = FMath::Max(0, Slots[ActiveSlotIndex].AmmoInMagazine - 1);

	PerformHitscan(Row);
	ApplyRecoil();

	UE_LOG(LogCosmere, Log,
		TEXT("CosmereWeaponComponent: %s fired '%s' from slot %d, %d round(s) left."),
		*GetNameSafe(GetOwner()), *WeaponId.ToString(), ActiveSlotIndex,
		Slots[ActiveSlotIndex].AmmoInMagazine);
	
	OnWeaponFired.Broadcast(WeaponId, Slots[ActiveSlotIndex].AmmoInMagazine);

	// Fired on the authority, so this is the one place a shot can be put on every machine at once. The
	// shooter's own body rather than the crosshair, because a shot is heard where the shooter stands and tin
	// is what turns that into a position.
	AActor* Shooter = GetOwner();
	ACosmereGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACosmereGameState>() : nullptr;
	if (Shooter && GameState)
	{
		GameState->ReportNoise(Shooter, Shooter->GetActorLocation(), FireSound, FireLoudness);
	}
	
	return true;
}

void UCosmereWeaponComponent::PerformHitscan(const FWeaponDefinition& Row)
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	UWorld* World = GetWorld();
	if (!Pawn || !World)
	{
		return;
	}

	// The controller's view point rather than the actor transform, so the shot follows the camera
	// the crosshair is actually drawn against. Falls back to the pawn's eyes when unpossessed.
	FVector ViewLocation;
	FRotator ViewRotation;
	if (const APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
	{
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	else
	{
		Pawn->GetActorEyesViewPoint(ViewLocation, ViewRotation);
	}

	// Scattering the trace rather than the view keeps the shot authoritative, and stops the crosshair
	// from having to lie about where the round went.
	FVector ShotDirection = ViewRotation.Vector();
	if (SpreadDegrees > 0.f)
	{
		ShotDirection = FMath::VRandCone(ShotDirection, FMath::DegreesToRadians(SpreadDegrees));
	}

	const FVector TraceEnd = ViewLocation + ShotDirection * MaxTraceDistance;

	FCollisionQueryParams Params;
	Params.bTraceComplex = true;
	Params.AddIgnoredActor(Pawn);

	// A round only finds what is on its own side of the veil. A body in the other realm is stepped over rather
	// than stopped on, exactly as a copper-sealed body is stepped over by an invested strike: it is not a target
	// and it is not cover, and whatever is real behind it is what the shot was always aimed at.
	FHitResult Hit;
	bool bHit = false;
	while (World->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, TraceChannel, Params))
	{
		AActor* HitActor = Hit.GetActor();
		if (UCosmereRealmComponent::ShareRealm(Pawn, HitActor))
		{
			bHit = true;
			break;
		}

		Params.AddIgnoredActor(HitActor);
	}

	if (!bHit || !Hit.GetActor())
	{
		return;
	}

	// Falloff begins at the row's EffectiveRange and runs down to MinDamageFraction at the trace's
	// far end, so the curve is driven by authored per-weapon data rather than a hardcoded shape.
	float Damage = Row.Damage;
	if (Hit.Distance > Row.EffectiveRange)
	{
		const float Span = FMath::Max(MaxTraceDistance - Row.EffectiveRange, 1.f);
		const float Alpha = FMath::Clamp((Hit.Distance - Row.EffectiveRange) / Span, 0.f, 1.f);
		Damage = Row.Damage * FMath::Lerp(1.f, MinDamageFraction, Alpha);
	}

	ApplyWeaponDamage(Hit.GetActor(), Damage);
}

void UCosmereWeaponComponent::ApplyWeaponDamage(AActor* Victim, float Damage)
{
	if (!Victim || Damage <= 0.f)
	{
		return;
	}

	// The one funnel every hit this pawn lands passes through, so the veil is checked here as well as in the
	// trace. A caller that reaches this with a body from the other side - a future projectile, a future melee
	// cone - gets the same answer without having to remember to ask.
	if (!UCosmereRealmComponent::ShareRealm(GetOwner(), Victim))
	{
		return;
	}

	// One route for every victim, through the engine's damage pipeline.
	//
	// Why this is no longer a direct ApplyModToAttribute: the pipeline carries the instigator's
	// controller alongside the damage, and that is the only thing that lets a death be credited to
	// whoever caused it. ACosmereCharacter::TakeDamage turns this into the same Health attribute mod
	// this function used to apply itself, so Cosmere victims lose an identical amount of health,
	// while any other actor simply takes engine damage exactly as it did on the fallback path.
	//
	// Deliberately AController and not APlayerController. The previous cast to APlayerController
	// produced null for an AI opponent, because a bot is driven by an AAIController, which would have
	// silently dropped every bot's kill credit.
	AController* InstigatorController =
		GetOwner() ? GetOwner()->GetInstigatorController() : nullptr;

	// A strength power raises the damage its pawn deals, and this is the one funnel every hit that pawn lands
	// passes through - so the shot is scaled here, once, rather than at each caller. Read off the owning pawn
	// rather than cached, so a power that has just ended cannot leave a stale multiplier behind on a component
	// that outlives it.
	if (const ACosmereCharacter* Cosmere = Cast<ACosmereCharacter>(GetOwner()))
	{
		Damage *= Cosmere->GetMeleeDamageMultiplier();
	}

	UGameplayStatics::ApplyDamage(Victim, Damage, InstigatorController, GetOwner(), nullptr);

	UE_LOG(LogCosmere, Log,
		TEXT("CosmereWeaponComponent: %s hit %s for %.1f damage."),
		*GetNameSafe(GetOwner()), *GetNameSafe(Victim), Damage);
}

bool UCosmereWeaponComponent::TryReload()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	
	// One reload at a time, so a second press cannot restart the spin.
	if (bIsReloading)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (!IsSlotEquipped(ActiveSlotIndex))
	{
		return false;
	}

	const FName WeaponId = Slots[ActiveSlotIndex].WeaponId;
	FWeaponDefinition Row;
	if (!GetWeaponRow(WeaponId, Row))
	{
		return false;
	}

	if (Slots[ActiveSlotIndex].AmmoInMagazine >= Row.MagazineSize)
	{
		return false;
	}
	
	// The magazine is refilled by FinishReload once the delay elapses, not here, so the delay is real
	// rather than cosmetic and the weapon genuinely cannot fire in the meantime.
	ActiveReloadDuration = ResolveReloadTime(WeaponId);
	ReloadEndTime = World->GetTimeSeconds() + ActiveReloadDuration;
	LocalReloadStartTime = World->GetTimeSeconds();
	SpinStartMeshRotation = WeaponMesh ? WeaponMesh->GetRelativeRotation() : FRotator::ZeroRotator;
	bIsReloading = true;

	UE_LOG(LogCosmere, Log,
		TEXT("CosmereWeaponComponent: %s started reloading '%s' (%.2fs)."),
		*GetNameSafe(GetOwner()), *WeaponId.ToString(), ActiveReloadDuration);

	OnReloadStarted.Broadcast(WeaponId, ActiveReloadDuration);

	return true;
}

bool UCosmereWeaponComponent::SwapToSlot(EWeaponSlot Slot)
{
	// ActiveSlotIndex is replicated, so a client writing it here would be overwritten on the next update;
	// a client goes through RequestSwapToSlot and lets the authority perform the switch.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	const int32 NewIndex = static_cast<int32>(Slot);

	if (!IsValidSlotIndex(NewIndex) || NewIndex == ActiveSlotIndex)
	{
		return false;
	}

	// Refuse to land on an empty slot, so a quickswap can never leave the player holding a weapon
	// that cannot fire.
	if (!IsSlotEquipped(NewIndex))
	{
		return false;
	}
	
	// Swapping out aborts a reload, leaving the magazine as part-filled as it was in hand.
	CancelReload();

	ApplySlotSwitch(NewIndex);
	return true;
}

bool UCosmereWeaponComponent::SwapToOtherSlot()
{
	return SwapToSlot(static_cast<EWeaponSlot>(ActiveSlotIndex == 0 ? 1 : 0));
}

void UCosmereWeaponComponent::ApplySlotSwitch(int32 NewSlotIndex)
{
	ActiveSlotIndex = FMath::Clamp(NewSlotIndex, 0, CosmereWeaponSlots::Count - 1);
	RefreshActiveWeaponRow();

	UE_LOG(LogCosmere, Log,
		TEXT("CosmereWeaponComponent: %s swapped to slot %d ('%s', %d round(s))."),
		*GetNameSafe(GetOwner()), ActiveSlotIndex,
		*GetWeaponIdInSlot(static_cast<EWeaponSlot>(ActiveSlotIndex)).ToString(),
		GetAmmoInSlotByIndex(ActiveSlotIndex));

	OnWeaponSwapped.Broadcast(
		static_cast<EWeaponSlot>(ActiveSlotIndex),
		GetWeaponIdInSlot(static_cast<EWeaponSlot>(ActiveSlotIndex)));
}

float UCosmereWeaponComponent::GetReloadProgress() const
{
	if (!bIsReloading || ActiveReloadDuration <= 0.f)
	{
		return 0.f;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.f;
	}

	return FMath::Clamp((World->GetTimeSeconds() - LocalReloadStartTime) / ActiveReloadDuration, 0.f, 1.f);
}

float UCosmereWeaponComponent::GetReloadDuration() const
{
	return ResolveReloadTime(GetActiveWeaponId());
}

float UCosmereWeaponComponent::ResolveReloadTime(FName WeaponId) const
{
	if (!WeaponId.IsNone())
	{
		if (const float* Override = ReloadTimeOverrides.Find(WeaponId))
		{
			return FMath::Max(*Override, 0.f);
		}
	}

	return FMath::Max(DefaultReloadTime, 0.f);
}

void UCosmereWeaponComponent::CancelReload()
{
	if (!bIsReloading)
	{
		return;
	}

	// The flag is replicated, so only the authority may clear it. A client's copy is corrected by
	// replication rather than written to directly.
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		return;
	}

	bIsReloading = false;
	ReloadEndTime = 0.f;

	if (WeaponMesh)
	{
		WeaponMesh->SetRelativeRotation(SpinStartMeshRotation);
	}
}

void UCosmereWeaponComponent::FinishReload()
{
	if (!bIsReloading || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	const int32 Index = ActiveSlotIndex;
	bIsReloading = false;
	ReloadEndTime = 0.f;

	FName WeaponId = NAME_None;
	if (IsValidSlotIndex(Index))
	{
		WeaponId = Slots[Index].WeaponId;

		FWeaponDefinition Row;
		if (!WeaponId.IsNone() && GetWeaponRow(WeaponId, Row))
		{
			Slots[Index].AmmoInMagazine = Row.MagazineSize;
		}
	}

	if (WeaponMesh)
	{
		WeaponMesh->SetRelativeRotation(SpinStartMeshRotation);
	}

	UE_LOG(LogCosmere, Log,
		TEXT("CosmereWeaponComponent: %s finished reloading '%s' (%d round(s))."),
		*GetNameSafe(GetOwner()), *WeaponId.ToString(), GetAmmoInSlotByIndex(Index));

	OnReloadCompleted.Broadcast(WeaponId);
}

void UCosmereWeaponComponent::OnRep_IsReloading()
{
	// The reload's length arrives through the flag; the clock is this machine's own, so progress
	// never depends on server world time lining up with ours.
	LocalReloadStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	if (bIsReloading)
	{
		SpinStartMeshRotation = WeaponMesh ? WeaponMesh->GetRelativeRotation() : FRotator::ZeroRotator;
	}
	else if (WeaponMesh)
	{
		WeaponMesh->SetRelativeRotation(SpinStartMeshRotation);
	}
}

void UCosmereWeaponComponent::ResolveWeaponMesh()
{
	// A raw object pointer authored on a component template does not follow the component onto the
	// pawn: it arrives null, or still pointing at the class's template, so the spin would drive an
	// object that is not in the world. The reference is resolved against the owning actor instead.
	WeaponMesh = Cast<UStaticMeshComponent>(WeaponMeshReference.GetComponent(GetOwner()));

	UE_LOG(LogCosmere, Log, TEXT("CosmereWeaponComponent: %s resolved weapon mesh '%s'."),
		*GetNameSafe(GetOwner()), *GetNameSafe(WeaponMesh));
}

void UCosmereWeaponComponent::UpdateReloadSpin(float DeltaTime)
{
	if (!bIsReloading || !WeaponMesh || ActiveReloadDuration <= 0.f || ReloadSpinTurns <= 0.f)
	{
		return;
	}

	// Chosen so the spin covers exactly ReloadSpinTurns whole turns in one reload, which lands the
	// mesh back on its authored pose and makes the restore in OnRep_IsReloading invisible.
	const float DegreesPerSecond = (360.f * ReloadSpinTurns) / ActiveReloadDuration;
	WeaponMesh->AddLocalRotation(FRotator(0.f, 0.f, DegreesPerSecond * DeltaTime));
}

void UCosmereWeaponComponent::ApplyRecoil()
{
	if (RecoilPitchDegrees <= 0.f && RecoilYawDegrees <= 0.f)
	{
		return;
	}

	// Recoil is a view effect, so it can only land on the machine that owns the controller. A remote
	// client's aim is that client's to kick, so only the local machine reacts here.
	const APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	if (RecoilPitchDegrees > 0.f)
	{
		PC->AddPitchInput(-RecoilPitchDegrees);
	}

	if (RecoilYawDegrees > 0.f)
	{
		PC->AddYawInput(FMath::FRandRange(-RecoilYawDegrees, RecoilYawDegrees));
	}
}
