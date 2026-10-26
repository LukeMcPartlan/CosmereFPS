// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CosmereTypes.generated.h"

/** The playable races of the Cosmere. Race drives the base stats applied to the pawn. */
UENUM(BlueprintType)
enum class ECosmereRace : uint8
{
	Scadrian	UMETA(DisplayName = "Scadrian (Human)"),
	Terris		UMETA(DisplayName = "Terris"),
	Koloss		UMETA(DisplayName = "Koloss"),
	Kandra		UMETA(DisplayName = "Kandra"),
	Singer		UMETA(DisplayName = "Singer"),
	Returned	UMETA(DisplayName = "Returned")
};

/** Which magic system a power belongs to. */
UENUM(BlueprintType)
enum class EPowerSource : uint8
{
	Surge		UMETA(DisplayName = "Surge of Honor"),
	Allomantic	UMETA(DisplayName = "Allomantic Metal"),
	Feruchemical	UMETA(DisplayName = "Feruchemical Metal")
};

/** The three currencies the meta-game stores spend. */
UENUM(BlueprintType)
enum class ECosmereCurrency : uint8
{
	Nicrosil	UMETA(DisplayName = "Nicrosil (Spiritweb)"),
	Gold		UMETA(DisplayName = "Gold (Armory)"),
	Duralumin	UMETA(DisplayName = "Duralumin (Identity Store)")
};

/** The two power slots a character build can equip into. */
UENUM(BlueprintType)
enum class EPowerSlot : uint8
{
	Alpha	UMETA(DisplayName = "Slot 1 (Shift / Left Bumper)"),
	Beta	UMETA(DisplayName = "Slot 2 (E / Right Bumper)")
};

/**
 * Which side of the veil an actor stands on.
 *
 * Both realms share one map - the same floor, walls and geometry - and differ only in who else is real. Two
 * bodies on the same side collide, shoot and push each other exactly as they always did. Two bodies on
 * opposite sides pass through each other and cannot land a bullet, a strike, a field or a heal on each other.
 */
UENUM(BlueprintType)
enum class ECosmereRealm : uint8
{
	/** The Physical Realm: the match as it is normally played. */
	Material	UMETA(DisplayName = "Physical Realm"),

	/** Shadesmar, the Cognitive Realm: the same map with the colour taken out, and nobody else in it. */
	Shadesmar	UMETA(DisplayName = "Shadesmar")
};

/** The two weapon slots a character build can equip into, swapped with the quickswap input. */
UENUM(BlueprintType)
enum class EWeaponSlot : uint8
{
	Primary		UMETA(DisplayName = "Primary"),
	Secondary	UMETA(DisplayName = "Secondary")
};

/** A race row of DT_Races. Health / RunSpeed / JumpVelocity are the base stats applied to the pawn. */
USTRUCT(BlueprintType)
struct FRaceDefinition : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText Description;

	/** Starting and maximum health. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	float BaseHealth = 100.f;

	/** Ground run speed in cm/s, pushed to CharacterMovement->MaxWalkSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	float RunSpeed = 600.f;

	/** Jump launch velocity in cm/s, pushed to CharacterMovement->JumpZVelocity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	float JumpVelocity = 420.f;

	/** Multiplier on world gravity, pushed to CharacterMovement->GravityScale. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	float GravityScale = 1.f;

	/** Air control authority, pushed to CharacterMovement->AirControl. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	float AirControl = 0.6f;

	/** True when the race is selectable at character creation without a store purchase. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	bool bUnlockedByDefault = false;

	/** Duralumin price in the Identity Store when bUnlockedByDefault is false. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	int32 DuraluminCost = 0;
};

/** A weapon row of DT_Weapons. */
USTRUCT(BlueprintType)
struct FWeaponDefinition : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText Description;

	/** Damage dealt per shot before falloff. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	float Damage = 24.f;

	/** Seconds between shots. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	float FireInterval = 0.25f;

	/** Distance in cm at which damage begins to fall off. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	float EffectiveRange = 5000.f;

	/** Rounds per magazine. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	int32 MagazineSize = 12;

	/** True when holding the fire input keeps firing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	bool bAutomatic = false;

	/** True when a shot spawns a travelling projectile instead of resolving as an instant hitscan trace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	bool bProjectile = false;

	/** Seconds a reload takes, from the request to the magazine being full. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics", meta = (ClampMin = "0.0"))
	float ReloadTime = 2.f;

	/** Half-angle of the firing cone in degrees. 0 fires exactly down the barrel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics", meta = (ClampMin = "0.0"))
	float SpreadDegrees = 0.f;

	/** Projectiles spawned per trigger pull. Above 1 makes this a shotgun. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics", meta = (ClampMin = "1"))
	int32 PelletsPerShot = 1;

	/** Upward camera kick in degrees applied per shot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	float RecoilPitch = 0.f;

	/** Horizontal camera kick in degrees applied per shot, randomised left or right. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics")
	float RecoilYaw = 0.f;

	/** Damage multiplier at EffectiveRange and beyond. 1 means no falloff at all. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ballistics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinDamageMultiplier = 1.f;

	/** Actor spawned per pellet when bProjectile is set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile")
	TSubclassOf<AActor> ProjectileClass;

	/** Muzzle speed in cm/s at the moment of spawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile")
	float ProjectileSpeed = 12000.f;

	/** Sphere radius in cm used for the projectile's collision. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile")
	float ProjectileRadius = 12.f;

	/** How hard the projectile drops. 0 flies flat, 1 is normal world gravity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile")
	float ProjectileGravityScale = 0.f;

	/** Physics mass in kg. Drives the impulse a hit imparts, not the damage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile")
	float ProjectileMass = 0.5f;

	/** Seconds before an un-hit projectile despawns. 0 lives until it hits something. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile")
	float ProjectileLifeSeconds = 5.f;

	/** True when the weapon is available at character creation without a store purchase. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	bool bUnlockedByDefault = false;

	/** Gold price in the Armory when bUnlockedByDefault is false. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	int32 GoldCost = 0;
};

/** A power row of DT_Powers. One row per Surge, per Allomantic metal and per Feruchemical metal. */
USTRUCT(BlueprintType)
struct FPowerDefinition : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText Description;

	/** Which magic system the power belongs to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	EPowerSource Source = EPowerSource::Surge;

	/** Base cooldown in seconds before any modifiers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tuning")
	float Cooldown = 8.f;

	/** The ability this row activates. An empty row is inert. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tuning")
	TSubclassOf<class UGameplayAbility> AbilityClass;

	/** Resource charged to activate, and again on every interval. 0 makes the power free. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource", meta = (ClampMin = "0.0"))
	float ResourceCost = 0.f;

	/** Seconds between ongoing charges while active. 0 charges once, on activation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource", meta = (ClampMin = "0.0"))
	float ResourceCostInterval = 0.f;

	/** True when the power latches on until pressed again, rather than running only while held. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tuning")
	bool bToggle = false;

	/** Minimum seconds between two toggles, so a toggle cannot be flickered. 0 allows instant re-toggle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tuning", meta = (ClampMin = "0.0"))
	float ToggleLockSeconds = 0.f;

	/** True when the power is available at character creation without a store purchase. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	bool bUnlockedByDefault = false;

	/** Nicrosil price in the Spiritweb when bUnlockedByDefault is false. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	int32 NicrosilCost = 0;
};

/** A prebuilt hero row of DT_Heroes. Selecting a hero overrides the player build and locks the kit. */
USTRUCT(BlueprintType)
struct FHeroDefinition : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText Description;

	/** Race the hero is locked to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kit")
	FName RaceId;

	/** Exactly two weapon row names, locked. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kit")
	TArray<FName> WeaponIds;

	/** Exactly two power row names, locked. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kit")
	TArray<FName> PowerIds;

	/** Duralumin price in the Identity Store. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	int32 DuraluminCost = 0;
};

/** One locally saved player character build. */
USTRUCT(BlueprintType)
struct FSpiritwebCharacterSave
{
	GENERATED_BODY()

	/** Stable identity for this build, independent of list position. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FGuid CharacterId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText CharacterName;

	/** Row name in DT_Races. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Build")
	FName RaceId;

	/** Row names in DT_Weapons, one per EWeaponSlot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Build")
	TArray<FName> WeaponIds;

	/** Row names in DT_Powers, index 0 = EPowerSlot::Alpha, index 1 = EPowerSlot::Beta. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Build")
	TArray<FName> PowerIds;

	/** Cosmetic row names applied to this build. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmetics")
	TArray<FName> CosmeticIds;

	/** When set, this prebuilt hero overrides RaceId / WeaponIds / PowerIds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Build")
	FName EquippedHeroId;

	/** True when a prebuilt hero is overriding the manually built kit. Not exposed to Blueprint - test EquippedHeroId there. */
	bool IsHeroBuild() const { return !EquippedHeroId.IsNone(); }
};
