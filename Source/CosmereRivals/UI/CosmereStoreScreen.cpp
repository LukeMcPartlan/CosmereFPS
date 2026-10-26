#include "UI/CosmereStoreScreen.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "CosmereRivals.h"
#include "Data/CosmereCatalogSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Save/CosmereSaveSubsystem.h"
#include "UI/CosmereNativeMenus.h"
#include "UI/CosmereUIManager.h"

namespace
{
	FText CurrencyName(ECosmereCurrency Currency)
	{
		return FText::FromString(Currency == ECosmereCurrency::Gold ? TEXT("Gold") :
			Currency == ECosmereCurrency::Duralumin ? TEXT("Duralumin") : TEXT("Nicrosil"));
	}
	FText Text(const TCHAR* Value) { return FText::FromString(Value); }
	FSlateChildSize Fill(float Weight)
	{
		FSlateChildSize Size(ESlateSizeRule::Fill);
		Size.Value = Weight;
		return Size;
	}
}

UCosmereStoreScreen::UCosmereStoreScreen(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PresentationAsset = TSoftObjectPtr<UCosmereUIPresentationData>(FSoftObjectPath(TEXT("/Game/UI/Data/DA_UIPresentation.DA_UIPresentation")));
	DefaultFocusWidgetName = TEXT("BtnBack");
}
UCosmereNativeShopHubScreen::UCosmereNativeShopHubScreen(const FObjectInitializer& O) : Super(O) { ScreenRoute = ECosmereUIRoute::ShopHub; DefaultFocusWidgetName = TEXT("Gateway_Armory"); }
UCosmereNativeSpiritWebScreen::UCosmereNativeSpiritWebScreen(const FObjectInitializer& O) : Super(O) { ScreenRoute = ECosmereUIRoute::SpiritWeb; ActiveCategory = TEXT("Surges"); }
UCosmereNativeArmoryScreen::UCosmereNativeArmoryScreen(const FObjectInitializer& O) : Super(O) { ScreenRoute = ECosmereUIRoute::Armory; ActiveCategory = TEXT("Weapons"); }
UCosmereNativeIdentityStoreScreen::UCosmereNativeIdentityStoreScreen(const FObjectInitializer& O) : Super(O) { ScreenRoute = ECosmereUIRoute::IdentityStore; ActiveCategory = TEXT("Races"); }

UCosmereSaveSubsystem* UCosmereStoreScreen::ResolveSave() const
{
	if (DevelopmentSave) return DevelopmentSave;
	if (const UCosmereUIManager* Manager = ResolveManager()) return Manager->GetProfileSource();
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCosmereSaveSubsystem>() : nullptr;
}
UCosmereCatalogSubsystem* UCosmereStoreScreen::ResolveCatalog() const
{
	if (DevelopmentCatalog) return DevelopmentCatalog;
	if (const UCosmereUIManager* Manager = ResolveManager()) return Manager->GetCatalogSource();
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCosmereCatalogSubsystem>() : nullptr;
}
UCosmereUIManager* UCosmereStoreScreen::ResolveManager() const
{
	return GetOwningLocalPlayer() ? GetOwningLocalPlayer()->GetSubsystem<UCosmereUIManager>() : nullptr;
}
#if !UE_BUILD_SHIPPING
bool UCosmereStoreScreen::SetDevelopmentSources(UCosmereSaveSubsystem* FixtureSave, UCosmereCatalogSubsystem* FixtureCatalog)
{
 if (IsActivated() || !FixtureSave || !FixtureCatalog || !FixtureCatalog->IsDevelopmentFixture()
  || FixtureSave->GetDevelopmentSlotName().IsEmpty()
  || FixtureSave->GetGameInstance() != GetGameInstance()
  || FixtureSave->GetDevelopmentCatalog() != FixtureCatalog) return false;
	DevelopmentSave = FixtureSave;
	DevelopmentCatalog = FixtureCatalog;
	return true;
}
#endif

void UCosmereStoreScreen::NativeOnInitialized()
{
	if (WidgetTree && !WidgetTree->RootWidget) BuildLayout();
	Super::NativeOnInitialized();
}
void UCosmereStoreScreen::NativeOnActivated()
{
	Detach();
	Presentation = PresentationAsset.LoadSynchronous(); // Never load in a constructor.
	BoundSave = ResolveSave();
	if (BoundSave) BoundSave->OnProfileChanged.AddUniqueDynamic(this, &UCosmereStoreScreen::RefreshStore);
	RefreshStore();
	Super::NativeOnActivated();
}
void UCosmereStoreScreen::Detach()
{
	if (BoundSave) BoundSave->OnProfileChanged.RemoveDynamic(this, &UCosmereStoreScreen::RefreshStore);
	BoundSave = nullptr;
}
void UCosmereStoreScreen::NativeOnDeactivated() { Detach(); Super::NativeOnDeactivated(); }
void UCosmereStoreScreen::NativeDestruct() { Detach(); Super::NativeDestruct(); }
UWidget* UCosmereStoreScreen::NativeGetDesiredFocusTarget() const
{
	if (const TObjectPtr<UCosmereNativeButton>* Tile = Tiles.Find(FocusedEntryId)) return Tile->Get();
	if (const TObjectPtr<UCosmereNativeButton>* Tile = Tiles.Find(SelectedEntryId)) return Tile->Get();
	return Super::NativeGetDesiredFocusTarget();
}

void UCosmereStoreScreen::BuildLayout()
{
	UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StoreRoot"));
	Root->SetPadding(FMargin(32.f, 20.f));
	Root->SetBrushColor(FLinearColor(0.015f, 0.022f, 0.035f, 0.94f));
	WidgetTree->RootWidget = Root;
	UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>();
	Root->SetContent(Body);
	const auto Label = [this](UVerticalBox* Parent, FName Name, const FText& Value)
	{
		UTextBlock* W = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		W->SetAutoWrapText(true); W->SetText(Value);
		W->SetColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.82f, 0.73f, 1.f)));
		Parent->AddChildToVerticalBox(W)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
		return W;
	};
	const auto Button = [this](FName Name, const FText& Value)
	{
		UCosmereNativeButton* W = WidgetTree->ConstructWidget<UCosmereNativeButton>(UCosmereNativeButton::StaticClass(), Name);
		W->SetButtonText(Value); return W;
	};
	const TCHAR* Title = ScreenRoute == ECosmereUIRoute::ShopHub ? TEXT("SHOPS") :
		ScreenRoute == ECosmereUIRoute::SpiritWeb ? TEXT("SPIRIT WEB") : ScreenRoute == ECosmereUIRoute::Armory ? TEXT("ARMORY") : TEXT("IDENTITY STORE");
	Label(Body, TEXT("TxtStoreTitle"), Text(Title));
	Wallet = Label(Body, TEXT("TxtStoreWallet"), FText::GetEmpty());
	if (ScreenRoute == ECosmereUIRoute::ShopHub)
	{
		UScrollBox* GateScroll = WidgetTree->ConstructWidget<UScrollBox>();
		Body->AddChildToVerticalBox(GateScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UVerticalBox* Gates = WidgetTree->ConstructWidget<UVerticalBox>(); GateScroll->AddChild(Gates);
		const TPair<FName, ECosmereUIRoute> Gateways[] = {{TEXT("Armory"), ECosmereUIRoute::Armory}, {TEXT("SpiritWeb"), ECosmereUIRoute::SpiritWeb}, {TEXT("IdentityStore"), ECosmereUIRoute::IdentityStore}};
		for (const auto& Gate : Gateways)
		{
			UCosmereNativeButton* W = Button(FName(*FString::Printf(TEXT("Gateway_%s"), *Gate.Key.ToString())), FText::FromName(Gate.Key));
			Gates->AddChildToVerticalBox(W)->SetPadding(FMargin(8.f));
			W->OnClicked().AddUObject(this, &UCosmereStoreScreen::OpenRoute, Gate.Value);
			Tiles.Add(Gate.Key, W);
		}
	}
	else
	{
		UScrollBox* TabsScroll = WidgetTree->ConstructWidget<UScrollBox>(); TabsScroll->SetOrientation(Orient_Horizontal);
		Body->AddChildToVerticalBox(TabsScroll);
		UHorizontalBox* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>(); TabsScroll->AddChild(Tabs);
		TArray<FName> Categories;
		if (ScreenRoute == ECosmereUIRoute::SpiritWeb) Categories = {TEXT("Surges"), TEXT("Allomancy"), TEXT("Feruchemy")};
		else if (ScreenRoute == ECosmereUIRoute::IdentityStore) Categories = {TEXT("Races"), TEXT("Heroes")};
		else Categories = {TEXT("Weapons")};
		for (FName Category : Categories)
		{
			UCosmereNativeButton* Tab = Button(FName(*FString::Printf(TEXT("Category_%s"), *Category.ToString())), FText::FromName(Category));
			Tabs->AddChildToHorizontalBox(Tab)->SetPadding(FMargin(4.f));
			Tab->OnClicked().AddUObject(this, &UCosmereStoreScreen::SetStoreCategory, Category);
			CategoryButtons.Add(Tab);
		}
		UHorizontalBox* Columns = WidgetTree->ConstructWidget<UHorizontalBox>();
		Body->AddChildToVerticalBox(Columns)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UScrollBox* CatalogScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("CatalogScroll"));
		Columns->AddChildToHorizontalBox(CatalogScroll)->SetSize(Fill(0.6f));
		UVerticalBox* CatalogBody = WidgetTree->ConstructWidget<UVerticalBox>(); CatalogScroll->AddChild(CatalogBody);
		CatalogList = WidgetTree->ConstructWidget<UVerticalBox>(); CatalogBody->AddChildToVerticalBox(CatalogList);
		// Nested horizontal scrolling keeps a symbol board readable rather than shrinking its labels.
		UScrollBox* BoardScroll = WidgetTree->ConstructWidget<UScrollBox>(); BoardScroll->SetOrientation(Orient_Horizontal);
		CatalogBody->AddChildToVerticalBox(BoardScroll);
		USizeBox* BoardSize = WidgetTree->ConstructWidget<USizeBox>(); BoardSize->SetWidthOverride(840.f); BoardSize->SetHeightOverride(1100.f);
		BoardScroll->AddChild(BoardSize);
		SymbolBoard = WidgetTree->ConstructWidget<UCanvasPanel>(); BoardSize->SetContent(SymbolBoard);
		UScrollBox* DetailScroll = WidgetTree->ConstructWidget<UScrollBox>();
		UHorizontalBoxSlot* DetailSlot = Columns->AddChildToHorizontalBox(DetailScroll);
		DetailSlot->SetSize(Fill(0.4f)); DetailSlot->SetPadding(FMargin(24.f, 8.f));
		UVerticalBox* DetailBody = WidgetTree->ConstructWidget<UVerticalBox>(); DetailScroll->AddChild(DetailBody);
		Details = Label(DetailBody, TEXT("TxtStoreDetails"), Text(TEXT("Select an entry to inspect it.")));
		Unlock = Button(TEXT("BtnUnlock"), Text(TEXT("UNLOCK")));
		EquipOne = Button(TEXT("BtnEquipOne"), FText::GetEmpty());
		EquipTwo = Button(TEXT("BtnEquipTwo"), FText::GetEmpty());
		ClearHero = Button(TEXT("BtnClearHero"), Text(TEXT("RESTORE CUSTOM BUILD")));
		Unlock->OnClicked().AddUObject(this, &UCosmereStoreScreen::UnlockSelected);
		EquipOne->OnClicked().AddUObject(this, &UCosmereStoreScreen::EquipSelected, 0);
		EquipTwo->OnClicked().AddUObject(this, &UCosmereStoreScreen::EquipSelected, 1);
		ClearHero->OnClicked().AddUObject(this, &UCosmereStoreScreen::RestoreCustomBuild);
		// Bottom of the details scroll: reachable at every resolution without a fixed-height footer.
		for (UCosmereNativeButton* W : {Unlock.Get(), EquipOne.Get(), EquipTwo.Get(), ClearHero.Get()})
			DetailBody->AddChildToVerticalBox(W)->SetPadding(FMargin(0.f, 10.f));
	}
	Status = Label(Body, TEXT("TxtStoreStatus"), FText::GetEmpty());
	BackButton = Button(TEXT("BtnBack"), Text(TEXT("BACK")));
	Body->AddChildToVerticalBox(BackButton);
	BackButton->OnClicked().AddUObject(this, &UCosmereStoreScreen::Back);
}

FName UCosmereStoreScreen::CurrentKind() const
{
	return ScreenRoute == ECosmereUIRoute::SpiritWeb ? FName(TEXT("Power")) :
		ScreenRoute == ECosmereUIRoute::Armory ? FName(TEXT("Weapon")) :
		ActiveCategory == TEXT("Heroes") ? FName(TEXT("Hero")) : FName(TEXT("Race"));
}
int32 UCosmereStoreScreen::ResolveCharacterIndex(FSpiritwebCharacterSave& OutCharacter) const
{
	UCosmereSaveSubsystem* Save = ResolveSave();
	if (!Save || !SelectedCharacterId.IsValid()) return INDEX_NONE;
	const TArray<FSpiritwebCharacterSave> Characters = Save->GetCharacters();
	const int32 Index = Characters.IndexOfByPredicate([this](const FSpiritwebCharacterSave& C) { return C.CharacterId == SelectedCharacterId; });
	if (Index != INDEX_NONE) OutCharacter = Characters[Index];
	return Index;
}
void UCosmereStoreScreen::RefreshStore()
{
	UCosmereSaveSubsystem* Save = ResolveSave();
	if (!Save) { ShowStatus(Text(TEXT("The profile is unavailable."))); return; }
	FSpiritwebCharacterSave Character;
	SelectedCharacterId = Save->GetSelectedCharacter(Character) ? Character.CharacterId : FGuid();
	if (Wallet) Wallet->SetText(FText::Format(Text(TEXT("Shared Wallet: Nicrosil {0} | Gold {1} | Duralumin {2}")),
		FText::AsNumber(Save->GetCurrency(ECosmereCurrency::Nicrosil)), FText::AsNumber(Save->GetCurrency(ECosmereCurrency::Gold)), FText::AsNumber(Save->GetCurrency(ECosmereCurrency::Duralumin))));
	if (ScreenRoute == ECosmereUIRoute::ShopHub)
	{
		for (const auto& Pair : Tiles)
		{
			FCosmereUIEntryPresentation Art;
			if (Presentation && Presentation->FindPresentation(TEXT("Shop"), Pair.Key, Art)) Pair.Value->SetButtonIcon(Art.Icon);
		}
		return;
	}
	RefreshEntries(); RefreshDetails(); ConnectNavigation();
}
void UCosmereStoreScreen::RefreshEntries()
{
	UCosmereSaveSubsystem* Save = ResolveSave(); UCosmereCatalogSubsystem* Catalog = ResolveCatalog();
	if (!Save || !Catalog || !Catalog->IsCatalogReady()) { ShowStatus(Text(TEXT("The catalog is unavailable."))); return; }
	const FName Kind = CurrentKind();
	TArray<FName> Ids;
	if (Kind == TEXT("Power")) Ids = Catalog->GetPowerIdsBySource(ActiveCategory == TEXT("Allomancy") ? EPowerSource::Allomantic : ActiveCategory == TEXT("Feruchemy") ? EPowerSource::Feruchemical : EPowerSource::Surge);
	else if (Kind == TEXT("Weapon")) Ids = Catalog->GetAllWeaponIds();
	else if (Kind == TEXT("Hero")) Ids = Catalog->GetAllHeroIds();
	else Ids = Catalog->GetAllRaceIds();
	Entries.Reset();
	FSpiritwebCharacterSave Character; ResolveCharacterIndex(Character);
	TArray<FName> Weapons = Character.WeaponIds, Powers = Character.PowerIds;
	FName Race = Character.RaceId;
	if (Character.IsHeroBuild())
	{
		FHeroDefinition Hero;
		if (Catalog->GetHeroRow(Character.EquippedHeroId, Hero)) { Weapons = Hero.WeaponIds; Powers = Hero.PowerIds; Race = Hero.RaceId; }
	}
	for (FName Id : Ids)
	{
		FCosmereStoreEntryViewModel E; E.CatalogKind = Kind; E.CatalogId = Id; E.Category = ActiveCategory;
		FPowerDefinition P; FWeaponDefinition W; FRaceDefinition R; FHeroDefinition H;
		bool bRow = false;
		if (Kind == TEXT("Power") && (bRow = Catalog->GetPowerRow(Id, P))) { E.DisplayName = P.DisplayName; E.Description = P.Description; E.Price = P.NicrosilCost; E.bUnlocked = Save->IsPowerUnlocked(Id); }
		if (Kind == TEXT("Weapon") && (bRow = Catalog->GetWeaponRow(Id, W))) { E.DisplayName = W.DisplayName; E.Description = W.Description; E.Price = W.GoldCost; E.Currency = ECosmereCurrency::Gold; E.bUnlocked = Save->IsWeaponUnlocked(Id); }
		if (Kind == TEXT("Race") && (bRow = Catalog->GetRaceRow(Id, R))) { E.DisplayName = R.DisplayName; E.Description = R.Description; E.Price = R.DuraluminCost; E.Currency = ECosmereCurrency::Duralumin; E.bUnlocked = Save->IsRaceUnlocked(Id); }
		if (Kind == TEXT("Hero") && (bRow = Catalog->GetHeroRow(Id, H))) { E.DisplayName = H.DisplayName; E.Description = H.Description; E.Price = H.DuraluminCost; E.Currency = ECosmereCurrency::Duralumin; E.bUnlocked = Save->IsHeroUnlocked(Id); }
		if (!bRow) continue;
		E.bAffordable = E.Price >= 0 && Save->GetCurrency(E.Currency) >= E.Price;
		E.bHasPresentation = Presentation && Presentation->FindPresentation(Kind, Id, E.Presentation);
		if (E.bHasPresentation && !E.Presentation.Category.IsNone()) E.Category = E.Presentation.Category;
		const TArray<FName>* Slots = Kind == TEXT("Power") ? &Powers : Kind == TEXT("Weapon") ? &Weapons : nullptr;
		if (Slots && Slots->Num() == 2)
		{
			if ((*Slots)[0] == Id) E.EquippedStatus = Text(Kind == TEXT("Power") ? TEXT("Equipped Ability 1") : TEXT("Equipped Primary"));
			if ((*Slots)[1] == Id) E.EquippedStatus = Text(Kind == TEXT("Power") ? TEXT("Equipped Ability 2") : TEXT("Equipped Secondary"));
		}
		if ((Kind == TEXT("Race") && Race == Id) || (Kind == TEXT("Hero") && Character.EquippedHeroId == Id)) E.EquippedStatus = Text(TEXT("Equipped"));
		Entries.Add(E);
	}
	if (!Entries.ContainsByPredicate([this](const auto& E) { return E.CatalogId == SelectedEntryId; })) SelectedEntryId = NAME_None;
	bool bRebuild = RenderedCategory != ActiveCategory || Tiles.Num() != Entries.Num();
	for (const auto& E : Entries) bRebuild |= !Tiles.Contains(E.CatalogId);
	if (bRebuild && CatalogList && SymbolBoard)
	{
		const bool bHadFocus = HasUserFocusedDescendants(GetOwningPlayer());
		ClearFocusRestorationTarget();
		Tiles.Reset(); CatalogList->ClearChildren(); SymbolBoard->ClearChildren(); RenderedCategory = ActiveCategory;
		bool bBoard = ScreenRoute == ECosmereUIRoute::SpiritWeb && !Entries.IsEmpty();
		for (const auto& E : Entries) bBoard &= E.bHasPresentation;
		if (UWidget* BoardSize = SymbolBoard->GetParent())
			if (UWidget* BoardScroll = BoardSize->GetParent()) BoardScroll->SetVisibility(bBoard ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		CatalogList->SetVisibility(bBoard ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		for (int32 I = 0; I < Entries.Num(); ++I)
		{
			const auto& E = Entries[I];
			UCosmereNativeButton* Tile = CreateWidget<UCosmereNativeButton>(GetOwningPlayer(), UCosmereNativeButton::StaticClass());
			if (!Tile) continue;
			Tile->OnClicked().AddUObject(this, &UCosmereStoreScreen::SelectEntry, E.CatalogId);
			Tile->OnFocusReceived().AddUObject(this, &UCosmereStoreScreen::RememberFocus, E.CatalogId);
			Tiles.Add(E.CatalogId, Tile);
			if (bBoard)
			{
				UCanvasPanelSlot* TileSlot = SymbolBoard->AddChildToCanvas(Tile);
				TileSlot->SetPosition(FVector2D(FMath::Clamp(E.Presentation.BoardPosition.X, 0., 1.) * 630., FMath::Clamp(E.Presentation.BoardPosition.Y, 0., 1.) * 880.));
				TileSlot->SetSize(FVector2D(200., 210.));
			}
			else CatalogList->AddChildToVerticalBox(Tile)->SetPadding(FMargin(4.f));
		}
		if (!Tiles.Contains(FocusedEntryId)) FocusedEntryId = Entries.IsEmpty() ? NAME_None : Entries[0].CatalogId;
		if (bHadFocus)
		{
			if (const auto* Target = Tiles.Find(FocusedEntryId)) (*Target)->SetUserFocus(GetOwningPlayer());
			else if (BackButton) BackButton->SetUserFocus(GetOwningPlayer());
		}
	}
	for (const auto& E : Entries)
	{
		if (const auto* Tile = Tiles.Find(E.CatalogId))
		{
			(*Tile)->SetButtonText(FText::Format(Text(TEXT("{0}\n{1} {2}{3}")), E.DisplayName,
				E.bUnlocked ? Text(TEXT("Owned")) : Text(TEXT("LOCKED")), E.EquippedStatus,
				E.CatalogId == SelectedEntryId ? Text(TEXT(" | SELECTED")) : FText::GetEmpty()));
			(*Tile)->SetButtonIcon(!E.bUnlocked && !E.Presentation.LockedIcon.IsNull() ? E.Presentation.LockedIcon : E.Presentation.Icon, !E.bUnlocked);
		}
	}
	for (int32 I = 0; I < CategoryButtons.Num(); ++I)
	{
		FString Name = CategoryButtons[I]->GetName(); Name.RemoveFromStart(TEXT("Category_"));
		CategoryButtons[I]->SetButtonText(FText::Format(Text(TEXT("{0}{1}")), FText::FromString(Name), ActiveCategory == FName(*Name) ? Text(TEXT(" | ACTIVE")) : FText::GetEmpty()));
	}
}

const FCosmereStoreEntryViewModel* UCosmereStoreScreen::SelectedModel() const
{
	return Entries.FindByPredicate([this](const auto& E) { return E.CatalogId == SelectedEntryId; });
}
void UCosmereStoreScreen::RefreshDetails()
{
	const auto* E = SelectedModel();
	FSpiritwebCharacterSave C; const bool bCharacter = ResolveCharacterIndex(C) != INDEX_NONE;
	const bool bCustom = bCharacter && !C.IsHeroBuild();
	const bool bPair = E && (E->CatalogKind == TEXT("Power") || E->CatalogKind == TEXT("Weapon"));
	if (!Details || !Unlock) return;
	Details->SetText(E ? FText::Format(Text(TEXT("{0}\n{1}\n\n{2} {3} | {4}\n{5}\n{6}")), E->DisplayName, E->Description,
		FText::AsNumber(E->Price), CurrencyName(E->Currency), E->bUnlocked ? Text(TEXT("Owned")) : Text(TEXT("Locked")), E->EquippedStatus,
		!E->bUnlocked && !E->bAffordable ? Text(TEXT("Insufficient shared currency (or invalid price).")) :
		!bCharacter ? Text(TEXT("Confirm a character before equipping.")) : C.IsHeroBuild() && E->CatalogKind != TEXT("Hero") ? Text(TEXT("Preset hero kit is fixed. Restore the custom build to edit it.")) :
		E->CatalogKind == TEXT("Race") ? Text(TEXT("Race assignment changes gameplay stats, not the preview model.")) : FText::GetEmpty()) : Text(TEXT("Select an entry to inspect its description and actions.")));
	Unlock->SetVisibility(E && !E->bUnlocked ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	Unlock->SetIsEnabled(E && !E->bUnlocked && E->bAffordable && bCharacter && !bActionInProgress);
	EquipOne->SetVisibility(E && E->bUnlocked ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	EquipTwo->SetVisibility(E && E->bUnlocked && bPair ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	EquipOne->SetButtonText(Text(E && E->CatalogKind == TEXT("Power") ? TEXT("EQUIP ABILITY 1") : E && E->CatalogKind == TEXT("Weapon") ? TEXT("ASSIGN PRIMARY") : E && E->CatalogKind == TEXT("Hero") ? TEXT("EQUIP HERO") : TEXT("EQUIP RACE")));
	EquipTwo->SetButtonText(Text(E && E->CatalogKind == TEXT("Power") ? TEXT("EQUIP ABILITY 2") : TEXT("ASSIGN SECONDARY")));
	const bool bCanEquip = E && E->bUnlocked && bCharacter && (E->CatalogKind == TEXT("Hero") || bCustom) && !bActionInProgress;
	EquipOne->SetIsEnabled(bCanEquip); EquipTwo->SetIsEnabled(bCanEquip && bPair);
	ClearHero->SetVisibility(ScreenRoute == ECosmereUIRoute::IdentityStore && bCharacter && C.IsHeroBuild() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	ClearHero->SetIsEnabled(!bActionInProgress);
}
void UCosmereStoreScreen::ConnectNavigation()
{
	if (Entries.IsEmpty()) return;
	TArray<UWidget*> Actions;
	for (UCosmereNativeButton* W : {Unlock.Get(), EquipOne.Get(), EquipTwo.Get(), ClearHero.Get()})
		if (W && W->GetIsEnabled() && W->GetVisibility() == ESlateVisibility::Visible) Actions.Add(W);
	UWidget* ActionTarget = Actions.IsEmpty() ? BackButton.Get() : Actions[0];
	UWidget* FirstTile = Tiles.FindChecked(Entries[0].CatalogId).Get();
	UWidget* LastTile = Tiles.FindChecked(Entries.Last().CatalogId).Get();
	UWidget* FocusTile = Tiles.Contains(FocusedEntryId) ? Tiles.FindChecked(FocusedEntryId).Get() : FirstTile;
	for (int32 I = 0; I < Entries.Num(); ++I)
	{
		const auto& E = Entries[I]; const auto* Tile = Tiles.Find(E.CatalogId); if (!Tile) continue;
		const auto Link = [this, Tile](EUINavigation Direction, FName Neighbor, UWidget* Fallback)
		{
			const auto* Target = Tiles.Find(Neighbor); (*Tile)->SetNavigationRuleExplicit(Direction, Target ? Target->Get() : Fallback);
		};
		UWidget* Previous = I > 0 ? Tiles.FindChecked(Entries[I-1].CatalogId).Get() : CategoryButtons.IsEmpty() ? BackButton.Get() : CategoryButtons[0].Get();
		UWidget* Next = I+1 < Entries.Num() ? Tiles.FindChecked(Entries[I+1].CatalogId).Get() : ActionTarget;
		Link(EUINavigation::Up, E.Presentation.UpId, Previous); Link(EUINavigation::Down, E.Presentation.DownId, Next);
		Link(EUINavigation::Left, E.Presentation.LeftId, Previous); Link(EUINavigation::Right, E.Presentation.RightId, ActionTarget);
		(*Tile)->SetNavigationRuleExplicit(EUINavigation::Next, Next);
		(*Tile)->SetNavigationRuleExplicit(EUINavigation::Previous, Previous);
	}
	for (int32 I = 0; I < CategoryButtons.Num(); ++I)
	{
		UWidget* Previous = I > 0 ? CategoryButtons[I-1].Get() : BackButton.Get();
		UWidget* Next = I+1 < CategoryButtons.Num() ? CategoryButtons[I+1].Get() : FirstTile;
		CategoryButtons[I]->SetNavigationRuleExplicit(EUINavigation::Down, FirstTile);
		CategoryButtons[I]->SetNavigationRuleExplicit(EUINavigation::Left, Previous);
		CategoryButtons[I]->SetNavigationRuleExplicit(EUINavigation::Right, I+1 < CategoryButtons.Num() ? Next : ActionTarget);
		CategoryButtons[I]->SetNavigationRuleExplicit(EUINavigation::Next, Next);
		CategoryButtons[I]->SetNavigationRuleExplicit(EUINavigation::Previous, Previous);
	}
	for (int32 I = 0; I < Actions.Num(); ++I)
	{
		UWidget* Previous = I > 0 ? Actions[I-1] : LastTile;
		UWidget* Next = I+1 < Actions.Num() ? Actions[I+1] : BackButton.Get();
		Actions[I]->SetNavigationRuleExplicit(EUINavigation::Up, Previous);
		Actions[I]->SetNavigationRuleExplicit(EUINavigation::Down, Next);
		Actions[I]->SetNavigationRuleExplicit(EUINavigation::Left, FocusTile);
		Actions[I]->SetNavigationRuleExplicit(EUINavigation::Right, Next);
		Actions[I]->SetNavigationRuleExplicit(EUINavigation::Next, Next);
		Actions[I]->SetNavigationRuleExplicit(EUINavigation::Previous, Previous);
	}
	if (BackButton)
	{
		UWidget* Previous = Actions.IsEmpty() ? LastTile : Actions.Last();
		BackButton->SetNavigationRuleExplicit(EUINavigation::Up, Previous);
		BackButton->SetNavigationRuleExplicit(EUINavigation::Previous, Previous);
		BackButton->SetNavigationRuleExplicit(EUINavigation::Next, CategoryButtons.IsEmpty() ? FirstTile : CategoryButtons[0].Get());
	}
}
void UCosmereStoreScreen::RememberFocus(FName Id)
{
	FocusedEntryId = Id; // No selection side effect.
	if (UScrollBox* Scroll = Cast<UScrollBox>(GetWidgetFromName(TEXT("CatalogScroll"))))
		if (const auto* Tile = Tiles.Find(Id)) Scroll->ScrollWidgetIntoView(Tile->Get(), false);
}
void UCosmereStoreScreen::SelectEntry(FName Id)
{
	if (!Entries.ContainsByPredicate([Id](const auto& E) { return E.CatalogId == Id; })) return;
	SelectedEntryId = Id; ShowStatus(FText::GetEmpty()); RefreshStore();
	UE_LOG(LogCosmereUI, Log, TEXT("Store selected %s/%s"), *CurrentKind().ToString(), *Id.ToString());
}
void UCosmereStoreScreen::SetStoreCategory(FName Category)
{
	const bool bValid = ScreenRoute == ECosmereUIRoute::SpiritWeb ? (Category == TEXT("Surges") || Category == TEXT("Allomancy") || Category == TEXT("Feruchemy")) :
		ScreenRoute == ECosmereUIRoute::IdentityStore ? (Category == TEXT("Races") || Category == TEXT("Heroes")) : Category == TEXT("Weapons");
	if (!bValid || ActiveCategory == Category) return;
	ActiveCategory = Category; SelectedEntryId = NAME_None; ShowStatus(FText::GetEmpty()); RefreshStore();
}
void UCosmereStoreScreen::UnlockSelected()
{
	if (bActionInProgress) return;
	const auto* E = SelectedModel(); UCosmereSaveSubsystem* Save = ResolveSave();
	if (!E || !Save || E->bUnlocked) return;
	FSpiritwebCharacterSave Character;
	if (ResolveCharacterIndex(Character) == INDEX_NONE)
	{
		ShowStatus(Text(TEXT("Confirm a valid character before purchasing."))); return;
	}
	const FName Id = E->CatalogId, Kind = E->CatalogKind;
	bool bSuccess;
	{
		TGuardValue<bool> Guard(bActionInProgress, true);
		bSuccess = Kind == TEXT("Power") ? Save->PurchasePower(Id) : Kind == TEXT("Weapon") ? Save->PurchaseWeapon(Id) : Kind == TEXT("Hero") ? Save->PurchaseHero(Id) : Save->PurchaseRace(Id);
	}
	ShowStatus(bSuccess ? Text(TEXT("Unlocked. Choose an equip action when ready.")) : Save->GetLastError());
	RefreshStore();
	if (bSuccess) if (const auto* Tile = Tiles.Find(Id)) (*Tile)->SetUserFocus(GetOwningPlayer());
	UE_LOG(LogCosmereUI, Log, TEXT("Store unlock %s/%s success=%d"), *Kind.ToString(), *Id.ToString(), bSuccess);
}
void UCosmereStoreScreen::EquipSelected(int32 SlotIndex)
{
	if (bActionInProgress || SlotIndex < 0 || SlotIndex > 1) return;
	const auto* E = SelectedModel(); UCosmereSaveSubsystem* Save = ResolveSave(); FSpiritwebCharacterSave C;
	const int32 Index = ResolveCharacterIndex(C);
	if (!E || !Save) return;
	if (Index == INDEX_NONE) { ShowStatus(Text(TEXT("That confirmed character no longer exists."))); return; }
	if (C.IsHeroBuild() && E->CatalogKind != TEXT("Hero")) { ShowStatus(Text(TEXT("Restore the custom build before changing a preset hero's fixed kit."))); return; }
	const FName Kind = E->CatalogKind, Id = E->CatalogId;
	bool bSuccess;
	{
		TGuardValue<bool> Guard(bActionInProgress, true);
		bSuccess = Kind == TEXT("Power") ? Save->EquipPower(Index, SlotIndex == 0 ? EPowerSlot::Alpha : EPowerSlot::Beta, Id) :
			Kind == TEXT("Weapon") ? Save->EquipWeapon(Index, SlotIndex == 0 ? EWeaponSlot::Primary : EWeaponSlot::Secondary, Id) :
			Kind == TEXT("Hero") ? Save->EquipHero(Index, Id) : Save->EquipRace(Index, Id);
	}
	ShowStatus(bSuccess ? Text(TEXT("Saved build updated. Practice entry applies the saved kit.")) : Save->GetLastError()); RefreshStore();
	UE_LOG(LogCosmereUI, Log, TEXT("Store equip character=%s %s/%s slot=%d success=%d"), *C.CharacterId.ToString(), *Kind.ToString(), *Id.ToString(), SlotIndex, bSuccess);
}
void UCosmereStoreScreen::RestoreCustomBuild()
{
	if (bActionInProgress) return;
	UCosmereSaveSubsystem* Save = ResolveSave(); FSpiritwebCharacterSave C; const int32 Index = ResolveCharacterIndex(C);
	if (!Save || Index == INDEX_NONE || !C.IsHeroBuild()) return;
	bool bSuccess;
	{ TGuardValue<bool> Guard(bActionInProgress, true); bSuccess = Save->ClearHero(Index); }
	ShowStatus(bSuccess ? Text(TEXT("Preserved custom build restored.")) : Save->GetLastError()); RefreshStore();
}
void UCosmereStoreScreen::ShowStatus(const FText& Message)
{
	if (Status) Status->SetText(Message);
	if (!Message.IsEmpty()) UE_LOG(LogCosmereUI, Log, TEXT("Store: %s"), *Message.ToString());
}
void UCosmereStoreScreen::OpenRoute(ECosmereUIRoute Route) { if (auto* Manager = ResolveManager()) if (!Manager->Navigate(Route)) ShowStatus(Manager->GetLastError()); }
void UCosmereStoreScreen::Back() { if (auto* Manager = ResolveManager()) Manager->NavigateBack(); }
