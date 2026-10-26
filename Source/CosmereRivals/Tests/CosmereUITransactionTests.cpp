#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Data/CosmereCatalogSubsystem.h"
#include "Save/CosmereSaveGame.h"
#include "Save/CosmereSaveSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCosmereUITransactionTest,
 "Cosmere.UI.IsolatedProfileTransactions",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCosmereUITransactionTest::RunTest(const FString&)
{
 // Do NOT call Init/InitializeStandalone or obtain any registered production subsystem.
 TStrongObjectPtr<UGameInstance> Owner(NewObject<UGameInstance>(GetTransientPackage(), NAME_None, RF_Transient));
 TStrongObjectPtr<UDataTable> Races(NewObject<UDataTable>(Owner.Get(), NAME_None, RF_Transient));
 TStrongObjectPtr<UDataTable> Weapons(NewObject<UDataTable>(Owner.Get(), NAME_None, RF_Transient));
 TStrongObjectPtr<UDataTable> Powers(NewObject<UDataTable>(Owner.Get(), NAME_None, RF_Transient));
 TStrongObjectPtr<UDataTable> Heroes(NewObject<UDataTable>(Owner.Get(), NAME_None, RF_Transient));
 Races->RowStruct = FRaceDefinition::StaticStruct();
 Weapons->RowStruct = FWeaponDefinition::StaticStruct();
 Powers->RowStruct = FPowerDefinition::StaticStruct();
 Heroes->RowStruct = FHeroDefinition::StaticStruct();
 const FName RaceId(TEXT("FixtureRace")), WeaponA(TEXT("FixtureWeaponA")), WeaponB(TEXT("FixtureWeaponB"));
 const FName PowerA(TEXT("FixturePowerA")), PowerB(TEXT("FixturePowerB")), PowerC(TEXT("FixtureLockedPower"));
 const FName Expensive(TEXT("FixtureUnaffordablePower")), HeroId(TEXT("FixtureHero"));
 FRaceDefinition Race; Race.DisplayName=FText::FromString(TEXT("Fixture race")); Race.bUnlockedByDefault=true;
 Races->AddRow(RaceId, Race);
 FWeaponDefinition Weapon; Weapon.DisplayName=FText::FromString(TEXT("Fixture weapon")); Weapon.bUnlockedByDefault=true;
 Weapons->AddRow(WeaponA, Weapon); Weapons->AddRow(WeaponB, Weapon);
 FPowerDefinition Power; Power.DisplayName=FText::FromString(TEXT("Fixture power")); Power.bUnlockedByDefault=true;
 Powers->AddRow(PowerA, Power); Powers->AddRow(PowerB, Power);
 Power.bUnlockedByDefault=false; Power.NicrosilCost=100;
 Powers->AddRow(PowerC, Power);
 Power.NicrosilCost=200; Powers->AddRow(Expensive, Power);
 FHeroDefinition Hero; Hero.DisplayName=FText::FromString(TEXT("Fixture hero")); Hero.RaceId=RaceId;
 Hero.WeaponIds={WeaponB,WeaponA}; Hero.PowerIds={PowerB,PowerA}; Hero.DuraluminCost=20;
 Heroes->AddRow(HeroId, Hero);
 TStrongObjectPtr<UCosmereCatalogSubsystem> Catalog(UCosmereCatalogSubsystem::CreateDevelopmentFixture(
  Owner.Get(),Races.Get(),Weapons.Get(),Powers.Get(),Heroes.Get()));
 if(!TestNotNull(TEXT("Separate fixture catalog"),Catalog.Get())) return false;
 TestTrue(TEXT("Catalog is transient and isolated"),Catalog->IsDevelopmentFixture());
 TestTrue(TEXT("Overlay is not the source table"),Catalog->GetDevelopmentTable(TEXT("Power"))!=Powers.Get());

 TStrongObjectPtr<UCosmereSaveGame> Seed(NewObject<UCosmereSaveGame>(Owner.Get(),NAME_None,RF_Transient));
 Seed->Nicrosil=150; Seed->Gold=70; Seed->Duralumin=200;
 FSpiritwebCharacterSave Original;
 Original.CharacterId=FGuid(1,2,3,4); Original.CharacterName=FText::FromString(TEXT("Isolated UI fixture"));
 Original.RaceId=RaceId; Original.WeaponIds={WeaponA,WeaponB}; Original.PowerIds={PowerA,PowerB};
 Seed->Characters.Add(Original); Seed->SelectedCharacterIndex=0;
 TArray<FString> TestSlots;
 ON_SCOPE_EXIT
 {
  // The only disk deletion is a slot returned by our isolated fixture factory, with a strict prefix.
  for(const FString& TestSlot:TestSlots)
   if(TestSlot.StartsWith(TEXT("AuraUITest_"))) UGameplayStatics::DeleteGameInSlot(TestSlot,0);
 };
 const auto MakeFixture=[&](const UCosmereSaveGame* InSeed)
 {
  UCosmereSaveSubsystem* Result=UCosmereSaveSubsystem::CreateDevelopmentFixture(Owner.Get(),InSeed,Catalog.Get());
  if(Result && Result->GetDevelopmentSlotName().StartsWith(TEXT("AuraUITest_"))) TestSlots.Add(Result->GetDevelopmentSlotName());
  else {AddError(TEXT("Fixture did not produce an isolated test slot."));return static_cast<UCosmereSaveSubsystem*>(nullptr);}
  return Result;
 };
 TStrongObjectPtr<UCosmereSaveSubsystem> Save(MakeFixture(Seed.Get()));
 if(!Save.IsValid()) return false;
 TestEqual(TEXT("Factory does not save"),Save->GetDevelopmentSaveAttempts(),0);
 TestEqual(TEXT("Factory does not notify"),Save->GetDevelopmentNotifications(),0);
 TestTrue(TEXT("Matching private catalog"),Save->GetDevelopmentCatalog()==Catalog.Get());
 const auto SameCustomKit=[](const FSpiritwebCharacterSave& A,const FSpiritwebCharacterSave& B)
 {return A.CharacterId==B.CharacterId && A.RaceId==B.RaceId && A.WeaponIds==B.WeaponIds && A.PowerIds==B.PowerIds && A.CosmeticIds==B.CosmeticIds;};
 FSpiritwebCharacterSave Observed;

 TestTrue(TEXT("Already equipped power is a successful no-op"),Save->EquipPower(0,EPowerSlot::Alpha,PowerA));
 TestEqual(TEXT("No-op saves zero times"),Save->GetDevelopmentSaveAttempts(),0);
 TestEqual(TEXT("No-op notifies zero times"),Save->GetDevelopmentNotifications(),0);
 TestFalse(TEXT("Proposed locked power is refused"),Save->EquipPower(0,EPowerSlot::Alpha,PowerC));
 TestFalse(TEXT("Unknown proposed power is refused"),Save->EquipPower(0,EPowerSlot::Alpha,TEXT("MissingFixturePower")));
 TestFalse(TEXT("Invalid character is refused"),Save->EquipPower(INDEX_NONE,EPowerSlot::Alpha,PowerB));
 TestFalse(TEXT("Invalid slot enum is refused"),Save->EquipPower(0,static_cast<EPowerSlot>(255),PowerB));
 Save->GetCharacter(0,Observed); TestTrue(TEXT("Validation preserves original kit"),SameCustomKit(Observed,Original));
 TestEqual(TEXT("Validation refusals do not save"),Save->GetDevelopmentSaveAttempts(),0);
 TestFalse(TEXT("Unaffordable purchase is refused"),Save->PurchasePower(Expensive));
 TestEqual(TEXT("Unaffordable wallet preserved"),Save->GetCurrency(ECosmereCurrency::Nicrosil),150);
 TestFalse(TEXT("Unaffordable power remains locked"),Save->IsPowerUnlocked(Expensive));

 Save->FailNextDevelopmentCommit();
 TestFalse(TEXT("Injected purchase write failure is refused"),Save->PurchasePower(PowerC));
 TestEqual(TEXT("Purchase failure wallet rollback"),Save->GetCurrency(ECosmereCurrency::Nicrosil),150);
 TestFalse(TEXT("Purchase failure ownership rollback"),Save->IsPowerUnlocked(PowerC));
 TestEqual(TEXT("Failed purchase has no notification"),Save->GetDevelopmentNotifications(),0);
 TestTrue(TEXT("Affordable purchase succeeds"),Save->PurchasePower(PowerC));
 TestEqual(TEXT("One debit"),Save->GetCurrency(ECosmereCurrency::Nicrosil),50);
 TestTrue(TEXT("Purchased power owned"),Save->IsPowerUnlocked(PowerC));
 Save->GetCharacter(0,Observed); TestTrue(TEXT("Purchase does not auto-equip"),SameCustomKit(Observed,Original));
 const int32 BeforeRepeat=Save->GetDevelopmentSaveAttempts();
 TestFalse(TEXT("Repeated unlock cannot double-debit"),Save->PurchasePower(PowerC));
 TestEqual(TEXT("Repeated unlock no save"),Save->GetDevelopmentSaveAttempts(),BeforeRepeat);

 int32 Attempts=Save->GetDevelopmentSaveAttempts(), Notifications=Save->GetDevelopmentNotifications();
 TestTrue(TEXT("Alpha opposite-ID swap"),Save->EquipPower(0,EPowerSlot::Alpha,PowerB));
 Save->GetCharacter(0,Observed);
 TestTrue(TEXT("Alpha swap yields B,A"),Observed.PowerIds==TArray<FName>({PowerB,PowerA}));
 TestEqual(TEXT("Swap saves once"),Save->GetDevelopmentSaveAttempts(),Attempts+1);
 TestEqual(TEXT("Swap notifies once"),Save->GetDevelopmentNotifications(),Notifications+1);
 const FSpiritwebCharacterSave Swapped=Observed;
 Attempts=Save->GetDevelopmentSaveAttempts(); Notifications=Save->GetDevelopmentNotifications();
 Save->FailNextDevelopmentCommit();
 TestFalse(TEXT("Injected equip write failure"),Save->EquipPower(0,EPowerSlot::Alpha,PowerC));
 Save->GetCharacter(0,Observed); TestTrue(TEXT("Equip failure restores full custom kit"),SameCustomKit(Observed,Swapped));
 TestEqual(TEXT("Equip failure tries once"),Save->GetDevelopmentSaveAttempts(),Attempts+1);
 TestEqual(TEXT("Equip failure sends zero notifications"),Save->GetDevelopmentNotifications(),Notifications);
 TestEqual(TEXT("Equip failure preserves wallet"),Save->GetCurrency(ECosmereCurrency::Nicrosil),50);
 TestEqual(TEXT("Equip failure preserves selected index"),Save->GetSelectedCharacterIndex(),0);
 TestTrue(TEXT("Reload reads the isolated slot"),Save->ReloadDevelopmentFixture());
 Save->GetCharacter(0,Observed); TestTrue(TEXT("Committed pair persists"),SameCustomKit(Observed,Swapped));
 TestTrue(TEXT("Owned replacement succeeds"),Save->EquipPower(0,EPowerSlot::Alpha,PowerC));
 Save->GetCharacter(0,Observed);
 TestTrue(TEXT("Replacement preserves opposite slot"),Observed.PowerIds==TArray<FName>({PowerC,PowerA}));

 TStrongObjectPtr<UCosmereSaveSubsystem> Symmetry(MakeFixture(Seed.Get()));
 if(!Symmetry.IsValid()) return false;
 TestTrue(TEXT("Beta opposite-ID swap from original A,B"),Symmetry->EquipPower(0,EPowerSlot::Beta,PowerA));
 Symmetry->GetCharacter(0,Observed);
 TestTrue(TEXT("Beta swap also yields B,A"),Observed.PowerIds==TArray<FName>({PowerB,PowerA}));
 TestEqual(TEXT("Beta swap saves once"),Symmetry->GetDevelopmentSaveAttempts(),1);
 TestEqual(TEXT("Beta swap notifies once"),Symmetry->GetDevelopmentNotifications(),1);
 TestFalse(TEXT("Armory opposite-slot duplicate remains refused"),Symmetry->EquipWeapon(0,EWeaponSlot::Primary,WeaponB));
 TestEqual(TEXT("Weapon refusal does not save"),Symmetry->GetDevelopmentSaveAttempts(),1);
 TestTrue(TEXT("Hero purchase"),Symmetry->PurchaseHero(HeroId));
 TestTrue(TEXT("Hero equip"),Symmetry->EquipHero(0,HeroId));
 Symmetry->GetCharacter(0,Observed); const FSpiritwebCharacterSave BeneathHero=Observed;
 TestTrue(TEXT("Hero marker active"),Observed.EquippedHeroId==HeroId);
 TestFalse(TEXT("Hero refuses custom powers"),Symmetry->EquipPower(0,EPowerSlot::Alpha,PowerA));
 TestTrue(TEXT("Hero clear restores underlying custom kit"),Symmetry->ClearHero(0));
 Symmetry->GetCharacter(0,Observed);
 TestTrue(TEXT("Hero no longer equipped"),Observed.EquippedHeroId.IsNone());
 TestTrue(TEXT("Custom race/weapons/powers survived hero"),SameCustomKit(Observed,BeneathHero));

 TStrongObjectPtr<UCosmereSaveGame> InvalidSeed(DuplicateObject<UCosmereSaveGame>(Seed.Get(),Owner.Get()));
 InvalidSeed->Characters[0].PowerIds={PowerA};
 TStrongObjectPtr<UCosmereSaveSubsystem> Invalid(MakeFixture(InvalidSeed.Get()));
 if(!Invalid.IsValid()) return false;
 TestFalse(TEXT("Incomplete original power kit is refused"),Invalid->EquipPower(0,EPowerSlot::Alpha,PowerB));
 TestEqual(TEXT("Original-kit refusal no save"),Invalid->GetDevelopmentSaveAttempts(),0);
 TestEqual(TEXT("Original-kit refusal no notification"),Invalid->GetDevelopmentNotifications(),0);
 Invalid->GetCharacter(0,Observed); TestTrue(TEXT("No manufactured missing slot"),Observed.PowerIds==TArray<FName>({PowerA}));
 InvalidSeed->Characters[0]=Original; InvalidSeed->Characters[0].WeaponIds={WeaponA,WeaponA};
 TStrongObjectPtr<UCosmereSaveSubsystem> InvalidWeapons(MakeFixture(InvalidSeed.Get()));
 if(!InvalidWeapons.IsValid()) return false;
 TestFalse(TEXT("Invalid original weapons block otherwise valid power swap"),InvalidWeapons->EquipPower(0,EPowerSlot::Alpha,PowerB));
 TestEqual(TEXT("Whole-kit refusal no save"),InvalidWeapons->GetDevelopmentSaveAttempts(),0);
 TestTrue(TEXT("Caller seed remains unchanged"),SameCustomKit(Seed->Characters[0],Original));
 TestEqual(TEXT("Caller seed wallet unchanged"),Seed->Nicrosil,150);
 return !HasAnyErrors();
}
#endif
