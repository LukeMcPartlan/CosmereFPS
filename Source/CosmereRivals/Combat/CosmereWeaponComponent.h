// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Data/CosmereTypes.h"
#include "CosmereWeaponComponent.generated.h"

class ACosmereCharacter;
class UCosmereCatalogSubsystem;
class UStaticMeshComponent;
class USoundBase;

/**
 * How many weapon slots a build fills. Kept in step with EWeaponSlot by a static_assert in the .cpp,
 * so adding a third slot to the enum without touching this file is a compile error rather than a
 * silently ignored slot.
 */
namespace CosmereWeaponSlots
{
	inline constexpr int32 Count = 2;
}

/**
 * Runtime state for one carried weapon.
 *
 * Ammo lives per slot rather than as a single counter on the pawn, so a quickswap preserves each
 * weapon's magazine independently - swapping back to a half-empty weapon finds it still half-empty.
 */
USTRUCT(BlueprintType)
struct FCosmereWeaponSlotState
{
	GENERATED_BODY()

	/** Row name in DT_Weapons. NAME_None when this slot holds nothing. */
	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|Weapons")
	FName WeaponId = NAME_None;

	/** Rounds currently in the magazine, never the reserve. */
	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|Weapons")
	int32 AmmoInMagazine = 0;

	/** True only once this slot has been filled from a catalog-valid DT_Weapons row. */
	UPROPERTY(BlueprintReadOnly, Category = "Cosmere|Weapons")
	bool bEquipped = false;
};

/**
 * Fired when the active weapon takes a shot. Authority only, because only the authority fires.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCosmereWeaponFired, FName, WeaponId, int32, AmmoRemaining);

/**
 * Fired when a reload begins, carrying the seconds it will take so an animation can be timed to it
 * without re-deriving the duration.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCosmereReloadStarted, FName, WeaponId, float, Duration);

/** Fired when a reload finishes and the magazine has been refilled. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCosmereReloadCompleted, FName, WeaponId);

/** Fired when the active slot changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCosmereWeaponSwapped, EWeaponSlot, NewSlot, FName, WeaponId);

/**
 * The weapon half of a character build. Two slots, hitscan fire, per-slot ammo, reload, quickswap.
 *
 * This component is deliberately driven entirely from the pawn's OnBuildApplied seam, and
 * EquipFromBuild takes the build as an argument. It never reads the PlayerState and never inspects
 * replicated state, so the whole weapon layer can be exercised by calling EquipFromBuild directly
 * with a hand-made build - no session, no replication, and no dependency on how a build reaches a
 * pawn. That separation is the point: the weapon layer cannot be broken by a change to the build
 * replication path, because it does not observe that path at all.
 *
 * Damage is applied as a loose attribute mod rather than through a GameplayEffect asset. Loose mods
 * are authority-gated inside the ability system, which matches a server-authoritative hitscan, and
 * they need no authored content - so this ships without a GE asset to create or wire up.
 */
UCLASS(ClassGroup = (Cosmere), meta = (BlueprintSpawnableComponent))
class COSMERERIVALS_API UCosmereWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCosmereWeaponComponent();

	//~ Begin build seam

	/**
	 * Equips Build.WeaponIds, one per EWeaponSlot, refilling each magazine to its row's MagazineSize.
	 *
	 * Everything this needs arrives in Build, so it is safe to call with a hand-made build. Bound to
	 * the pawn's OnBuildApplied seam by default, but calling it directly is equally valid.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Weapons")
	void EquipFromBuild(const FSpiritwebCharacterSave& Build);

	//~ End build seam
	
	//~ Begin presentation hooks
	
	/**
	 * The presentation seam, following the shape ACosmereCharacter::OnBuildApplied established: the
	 * visual layer binds to these rather than polling state, so a real weapon animation can be
	 * authored against them without knowing how firing or reloading is implemented underneath.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Cosmere|Weapons|Hooks")
	FCosmereWeaponFired OnWeaponFired;
	
	/** Duration is the resolved seconds this reload will take, so an animation can be timed to it. */
	UPROPERTY(BlueprintAssignable, Category = "Cosmere|Weapons|Hooks")
	FCosmereReloadStarted OnReloadStarted;
	
	UPROPERTY(BlueprintAssignable, Category = "Cosmere|Weapons|Hooks")
	FCosmereReloadCompleted OnReloadCompleted;
	
	UPROPERTY(BlueprintAssignable, Category = "Cosmere|Weapons|Hooks")
	FCosmereWeaponSwapped OnWeaponSwapped;

	//~ End presentation hooks

	//~ Begin sound. A shot is the loudest thing a pawn does, so it is also the noise that carries furthest.

	/** Played at the muzzle on every machine. Silent, but still heard, when left empty. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Weapons|Sound")
	TObjectPtr<USoundBase> FireSound;

	/**
	 * How loud one shot is, in the same units a listener's hearing radius is measured in. Carried rather than
	 * used for the mix: a shot is audible to everybody, and tin is what decides who can place the shooter.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Weapons|Sound", meta = (ClampMin = "0.0"))
	float FireLoudness = 4.f;

	//~ End sound

	//~ Begin queries
	
	UFUNCTION(BlueprintPure, Category = "Cosmere|Weapons")
	EWeaponSlot GetActiveSlot() const { return static_cast<EWeaponSlot>(ActiveSlotIndex); }

	UFUNCTION(BlueprintPure, Category = "Cosmere|Weapons")
	FName GetActiveWeaponId() const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Weapons")
	FText GetActiveWeaponDisplayName() const { return ActiveWeaponRow.DisplayName; }

	UFUNCTION(BlueprintPure, Category = "Cosmere|Weapons")
	int32 GetActiveAmmo() const { return GetAmmoInSlotByIndex(ActiveSlotIndex); }

	UFUNCTION(BlueprintPure, Category = "Cosmere|Weapons")
	int32 GetActiveMagazineSize() const { return ActiveWeaponRow.MagazineSize; }

	UFUNCTION(BlueprintPure, Category = "Cosmere|Weapons")
	bool HasActiveWeapon() const { return IsSlotEquipped(ActiveSlotIndex); }

	UFUNCTION(BlueprintPure, Category = "Cosmere|Weapons")
	FName GetWeaponIdInSlot(EWeaponSlot Slot) const;

	UFUNCTION(BlueprintPure, Category = "Cosmere|Weapons")
	int32 GetAmmoInSlot(EWeaponSlot Slot) const;

	/**
	 * True when the active weapon is equipped, loaded, switched on and clear of its row's fire
	 * interval. A switched-off weapon is the dead-pawn case - see SetWeaponEnabled.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Weapons")
	bool CanFire() const;

	/**
	 * False only once death has switched this component off. A respawned pawn is handed a brand new
	 * component, and a new component starts enabled, so nothing ever has to switch it back on.
	 */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Weapons")
	bool IsWeaponEnabled() const { return bWeaponEnabled; }
	
	/** True while a reload is in flight. Firing and swapping are both refused until it ends. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Weapons")
	bool IsReloading() const { return bIsReloading; }
	
	/** How far through the in-flight reload we are, 0..1. Returns 0 when not reloading. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Weapons")
	float GetReloadProgress() const;
	
	/** Seconds the active weapon takes to reload, or 0 when nothing is equipped. */
	UFUNCTION(BlueprintPure, Category = "Cosmere|Weapons")
	float GetReloadDuration() const;
	
	//~ End queries

	//~ Begin actions

	/**
	 * Switches the whole weapon layer on or off. Death passes false.
	 *
	 * Needed because input keeps arriving from a client that has not yet learned it died, with the
	 * camera left wherever the pawn fell - so a dead player would otherwise keep firing. Refusing the
	 * shot inside CanFire rather than at the input leg means no caller has to remember to ask first,
	 * and it covers the tick's held-to-fire path as well as the input one.
	 *
	 * Replicated so the owning client's HUD can stop offering a weapon it can no longer use. Only the
	 * authority ever takes a shot, so the flag is presentation as much as it is a gate.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Weapons")
	void SetWeaponEnabled(bool bEnabled);

	/**
	 * Client-side fire request. Forwards to the authority, which is the only side that traces and
	 * deals damage, so a client cannot produce a divergent local shot.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Weapons")
	void RequestFire();

	/**
	 * Client-side trigger state: true on press, false on release.
	 *
	 * A press always produces one shot, which is the whole of semi-auto. A row whose bAutomatic is set
	 * keeps firing at its own FireInterval for as long as the trigger stays down, so one call covers
	 * both fire modes and an input leg never has to branch on the weapon being held.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Weapons")
	void SetTriggerHeld(bool bHeld);

	/**
	 * Client-side reload request. Forwards to the authority, which is the only side that refills the
	 * magazine, so a client cannot top itself up.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Weapons")
	void RequestReload();

	/** Client-side swap request. Forwards to the authority, which owns ActiveSlotIndex. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Weapons")
	void RequestSwapToSlot(EWeaponSlot Slot);

	/** Client-side quickswap. The destination is picked server-side, the only side that knows it. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Weapons")
	void RequestSwapToOtherSlot();

	/** Fires the active weapon. Returns true only when a shot was actually taken. Authority only. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Weapons")
	bool TryFire();

	/**
	 * Refills the active magazine. Returns false when there is no weapon, or it is already full.
	 * Authority only; a client goes through RequestReload.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Weapons")
	bool TryReload();

	/**
	 * Switches to Slot. Returns false when that slot is empty, invalid, or already active.
	 * Authority only; a client goes through RequestSwapToSlot.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Weapons")
	bool SwapToSlot(EWeaponSlot Slot);

	/** Quickswap: switches to whichever slot is not active, if it holds a weapon. Authority only. */
	UFUNCTION(BlueprintCallable, Category = "Cosmere|Weapons")
	bool SwapToOtherSlot();

	//~ End actions

protected:
	//~ Begin UActorComponent interface
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent interface

	/** Bound to ACosmereCharacter::OnBuildApplied. */
	UFUNCTION()
	void HandleBuildApplied(const FSpiritwebCharacterSave& Build);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestFire();
	void ServerRequestFire_Implementation();
	bool ServerRequestFire_Validate() { return true; }

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetTriggerHeld(bool bHeld);
	void ServerSetTriggerHeld_Implementation(bool bHeld);
	bool ServerSetTriggerHeld_Validate(bool /*bHeld*/) { return true; }

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestReload();
	void ServerRequestReload_Implementation();
	bool ServerRequestReload_Validate() { return true; }

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestSwapToSlot(EWeaponSlot Slot);
	void ServerRequestSwapToSlot_Implementation(EWeaponSlot Slot);
	bool ServerRequestSwapToSlot_Validate(EWeaponSlot /*Slot*/) { return true; }

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestSwapToOtherSlot();
	void ServerRequestSwapToOtherSlot_Implementation();
	bool ServerRequestSwapToOtherSlot_Validate() { return true; }

	/** Applies a trigger transition on the authority, taking the shot a press implies. Authority only. */
	void ApplyTriggerHeld(bool bHeld);

	/** True when the active row asks for held-to-fire. False when nothing is equipped. */
	bool IsActiveWeaponAutomatic() const;

	/** Re-reads the active slot's row into ActiveWeaponRow. */
	void RefreshActiveWeaponRow();

	/** Resolves a DT_Weapons row, or false when the catalog is unavailable or the row is missing. */
	bool GetWeaponRow(FName WeaponId, FWeaponDefinition& OutRow) const;

	/** The catalog, or null when the game instance or the subsystem is not there yet. */
	UCosmereCatalogSubsystem* GetCatalog() const;

	bool IsValidSlotIndex(int32 Index) const;
	bool IsSlotEquipped(int32 Index) const;
	int32 GetAmmoInSlotByIndex(int32 Index) const;

	/** The hitscan. Authority only. */
	void PerformHitscan(const FWeaponDefinition& Row);

	/** Pushes -Damage into Victim's Health. Authority only, enforced by the ability system. */
	void ApplyWeaponDamage(AActor* Victim, float Damage);

	/** Carries out the switch on the authority; clients follow the replicated ActiveSlotIndex. */
	void ApplySlotSwitch(int32 NewSlotIndex);
	
	/** Applies the magazine refill and clears the reload state. Authority only. */
	void FinishReload();
	
	/** Aborts an in-flight reload without refilling it. Authority only. */
	void CancelReload();
	
	/** The reload duration for WeaponId: its override if there is one, otherwise the default. */
	float ResolveReloadTime(FName WeaponId) const;
	
	/** Kicks the locally-controlled view by the recoil tuning. Inert while that tuning is 0. */
	void ApplyRecoil();
	
	/** Advances the placeholder reload spin. Cosmetic, and runs independently on each machine. */
	void UpdateReloadSpin(float DeltaTime);
	
	/** Binds WeaponMesh to this actor's own component named by WeaponMeshReference. */
	void ResolveWeaponMesh();
	
	/** Stamps this machine's reload clock, so progress never depends on server world time. */
	UFUNCTION()
	void OnRep_IsReloading();
	
	//~ Begin replicated state
	
	/** Per-slot state. Replicated so the owning client can read ammo without a round trip. */
	UPROPERTY(Replicated, Transient, BlueprintReadOnly, Category = "Cosmere|Weapons")
	TArray<FCosmereWeaponSlotState> Slots;
	
	/** Index into Slots. Replicated so the owning client renders the same active weapon. */
	UPROPERTY(Replicated, Transient, BlueprintReadOnly, Category = "Cosmere|Weapons")
	int32 ActiveSlotIndex = 0;
	
	/**
	 * True while a reload runs. Replicated so every machine can run matching presentation; the clock
	 * behind it is not, because each machine times the spin off its own OnRep.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_IsReloading, Transient, BlueprintReadOnly, Category = "Cosmere|Weapons")
	bool bIsReloading = false;
	
	/**
	 * False once this pawn has been killed, until the respawn hands it a new component. Replicated so
	 * the owning client's HUD can stop offering a weapon it can no longer use.
	 */
	UPROPERTY(Replicated, Transient, BlueprintReadOnly, Category = "Cosmere|Weapons")
	bool bWeaponEnabled = true;
	
	//~ End replicated state

	/** Cached row for the active weapon, so a HUD query does not hit the catalog every frame. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Cosmere|Weapons")
	FWeaponDefinition ActiveWeaponRow;

	/**
	 * World seconds at the last shot, used only for the fire interval. Not replicated: the authority
	 * is the only side that fires, so no other side needs it.
	 */
	UPROPERTY(Transient)
	float LastFireTime = -1000.f;

	/**
	 * Authority-only trigger state, which drives automatic fire from the tick. Not replicated: only the
	 * authority fires, so no other machine has a use for it.
	 */
	UPROPERTY(Transient)
	bool bFireHeld = false;

	/** How far the hitscan reaches. EffectiveRange from the row is where falloff begins, not the end. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Weapons|Tuning")
	float MaxTraceDistance = 20000.f;

	/** Damage retains at least this fraction of the row's Damage by MaxTraceDistance. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Weapons|Tuning", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinDamageFraction = 0.4f;

	/**
	 * Channel the shot traces against. Defaults to Visibility, which world geometry and character
	 * capsules both block, so walls stop shots without any extra setup.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Weapons|Tuning")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;
	
	//~ Begin spread and recoil tuning
	//
	// Knobs only for now: both default to 0, which reproduces the perfectly accurate fire this
	// component shipped with. Setting either starts shaping the weapon - spread is applied inside the
	// authoritative trace, and recoil kicks the view.
	
	/** Cone half-angle, in degrees, the shot direction is scattered within. 0 = accurate. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Weapons|Spread", meta = (ClampMin = "0.0"))
	float SpreadDegrees = 0.f;
	
	/** Degrees of upward view kick per shot. 0 = none. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Weapons|Spread", meta = (ClampMin = "0.0"))
	float RecoilPitchDegrees = 0.f;
	
	/** Maximum degrees of horizontal view kick per shot, applied either way. 0 = none. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Weapons|Spread", meta = (ClampMin = "0.0"))
	float RecoilYawDegrees = 0.f;
	
	//~ End spread and recoil tuning
	
	//~ Begin reload tuning
	
	/**
	 * Seconds a reload takes for any weapon without an entry in ReloadTimeOverrides.
	 *
	 * Reload timing lives here rather than in FWeaponDefinition because that struct backs the
	 * DT_Weapons DataTable, and adding a field to it would force the rows to be rewritten. Per-weapon
	 * values go in ReloadTimeOverrides instead, keyed by the DT_Weapons row name.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Weapons|Reload", meta = (ClampMin = "0.0"))
	float DefaultReloadTime = 1.5f;
	
	/** Per-weapon reload overrides, keyed by DT_Weapons row name, e.g. W_PewterSMG -> 2.1. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Weapons|Reload")
	TMap<FName, float> ReloadTimeOverrides;
	
	/** Seconds the in-flight reload will take. Also handed to OnReloadStarted. */
	float ActiveReloadDuration = 0.f;
	
	/** World seconds the in-flight reload completes at. Authority only. */
	float ReloadEndTime = 0.f;
	
	/** This machine's own world seconds when the reload began, so progress is computed locally. */
	float LocalReloadStartTime = 0.f;
	
	//~ End reload tuning
	
	//~ Begin placeholder presentation
	
	/**
	 * Names the mesh the reload spin drives. Left unset by default, in which case the spin is skipped
	 * and the delegates above are the whole presentation surface.
	 *
	 * A component reference rather than a raw object pointer, because a pointer authored on a
	 * component template does not follow the component onto the pawn: it arrives null, or still
	 * pointing at the class's template, and the spin then drives an object that is not in the world.
	 * FComponentReference is resolved against the owning actor at runtime instead.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Weapons|Presentation")
	FComponentReference WeaponMeshReference;
	
	/**
	 * This pawn's own copy of the mesh above, resolved once in BeginPlay. Null when the reference is
	 * unset or does not name a UStaticMeshComponent on this actor, which skips the spin.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> WeaponMesh;
	
	/** Full turns the mesh makes over one reload, so the spin lands back on its starting pose. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmere|Weapons|Presentation", meta = (ClampMin = "0.0"))
	float ReloadSpinTurns = 1.f;
	
	/** Mesh rotation captured when a spin starts, restored when it ends. */
	FRotator SpinStartMeshRotation = FRotator::ZeroRotator;
	
	//~ End placeholder presentation
};
