// Copyright Epic Games, Inc. All Rights Reserved.
#include "UI/CosmereNativeMenus.h"

#include "Blueprint/WidgetTree.h"
#include "CommonTextBlock.h"
#include "Components/Border.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "CosmereRivals.h"
#include "Data/CosmereCatalogSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Save/CosmereSaveSubsystem.h"
#include "Misc/EngineVersion.h"
#include "UI/CosmereUIManager.h"

namespace CosmereNativeUI
{
	FText CardText(const FCosmereCharacterCardViewModel& Card)
	{
		return FText::Format(NSLOCTEXT("CosmereUI", "NativeCharacterCard",
			"{0}  |  {1}\nWeapons: {2} / {3}\nPowers: {4} / {5}\nShared Wallet: Nicrosil {6}  |  Gold {7}  |  Duralumin {8}{9}"),
			Card.CharacterName, Card.RaceName, Card.PrimaryWeaponName, Card.SecondaryWeaponName,
			Card.AlphaPowerName, Card.BetaPowerName, FText::AsNumber(Card.Nicrosil),
			FText::AsNumber(Card.Gold), FText::AsNumber(Card.Duralumin),
			Card.bIsHeroBuild ? FText::Format(NSLOCTEXT("CosmereUI", "HeroCard", "\nPreset hero: {0} (fixed kit)"), Card.HeroName) : FText::GetEmpty());
	}
}

UCosmereNativeButton::UCosmereNativeButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer), ButtonText(NSLOCTEXT("CosmereUI", "Action", "ACTION"))
{
	DefaultButtonStyle = TSoftClassPtr<UCommonButtonStyle>(FSoftObjectPath(
		TEXT("/Game/UI/Styles/BP_CosmereButtonStyle.BP_CosmereButtonStyle_C")));
	bShouldUseFallbackDefaultInputAction = true;
}

void UCosmereNativeButton::NativeOnInitialized()
{
	// CommonUI wraps the content and binds its button events after Super::Initialize().
	// Populate it here: RebuildWidget runs too late to receive that clickable wrapper.
	if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	if (!WidgetTree->RootWidget)
	{
		USizeBox* Content = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ButtonContent"));
		Content->SetMinDesiredHeight(56.f);
		NativeLabel = WidgetTree->ConstructWidget<UCommonTextBlock>(UCommonTextBlock::StaticClass(), TEXT("LabelText"));
		NativeLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
		NativeLabel->SetAutoWrapText(true);
		UVerticalBox* Inner = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ButtonInner"));
		NativeIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("ButtonIcon"));
		NativeIcon->SetDesiredSizeOverride(FVector2D(96.f, 96.f));
		Inner->AddChildToVerticalBox(NativeIcon)->SetHorizontalAlignment(HAlign_Center);
		USizeBoxSlot* ContentSlot = CastChecked<USizeBoxSlot>(Content->AddChild(Inner));
		ContentSlot->SetPadding(FMargin(20.f, 12.f));
		ContentSlot->SetHorizontalAlignment(HAlign_Center);
		ContentSlot->SetVerticalAlignment(VAlign_Center);
		Inner->AddChildToVerticalBox(NativeLabel);
		NativeFocusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FocusText"));
		NativeFocusText->SetText(NSLOCTEXT("CosmereUI", "FocusMarker", "FOCUSED"));
		NativeFocusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.10f, 0.50f, 0.48f, 1.f)));
		NativeFocusText->SetVisibility(ESlateVisibility::Hidden);
		Inner->AddChildToVerticalBox(NativeFocusText)->SetHorizontalAlignment(HAlign_Center);
		WidgetTree->RootWidget = Content;
	}
	NativeLabel = Cast<UCommonTextBlock>(GetWidgetFromName(TEXT("LabelText")));
	NativeIcon = Cast<UImage>(GetWidgetFromName(TEXT("ButtonIcon")));
	NativeFocusText = Cast<UTextBlock>(GetWidgetFromName(TEXT("FocusText")));
	RefreshIcon();
	RefreshLabel();
	Super::NativeOnInitialized();
}

TSharedRef<SWidget> UCosmereNativeButton::RebuildWidget()
{
	// Resolve only on an instance, never during native/Blueprint CDO construction.
	if (!Style)
		if (UClass* LoadedStyle = DefaultButtonStyle.LoadSynchronous()) SetStyle(LoadedStyle);
	return Super::RebuildWidget();
}

void UCosmereNativeButton::NativePreConstruct()
{
	Super::NativePreConstruct();
	NativeLabel = Cast<UCommonTextBlock>(GetWidgetFromName(TEXT("LabelText")));
	RefreshLabel();
}

void UCosmereNativeButton::NativeOnCurrentTextStyleChanged()
{
	Super::NativeOnCurrentTextStyleChanged();
	RefreshLabel();
}

void UCosmereNativeButton::SetButtonText(const FText& InText)
{
	ButtonText = InText;
	RefreshLabel();
}

void UCosmereNativeButton::RefreshLabel()
{
	if (!NativeLabel) return;
	NativeLabel->SetText(ButtonText);
	if (const TSubclassOf<UCommonTextStyle> TextStyle = GetCurrentTextStyleClass())
		NativeLabel->SetStyle(TextStyle);
}

void UCosmereNativeButton::SetButtonIcon(TSoftObjectPtr<UTexture2D> InIcon, bool bInLocked)
{
	ButtonIcon = InIcon;
	bIconLocked = bInLocked;
	RefreshIcon();
}

void UCosmereNativeButton::RefreshIcon()
{
	if (!NativeIcon) return;
	NativeIcon->SetBrushFromSoftTexture(ButtonIcon, false);
	NativeIcon->SetColorAndOpacity(bIconLocked ? FLinearColor(0.35f, 0.35f, 0.35f, 1.f) : FLinearColor::White);
	NativeIcon->SetVisibility(ButtonIcon.IsNull() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void UCosmereNativeButton::NativeOnAddedToFocusPath(const FFocusEvent& InFocusEvent)
{
	Super::NativeOnAddedToFocusPath(InFocusEvent);
	if (NativeFocusText) NativeFocusText->SetVisibility(ESlateVisibility::HitTestInvisible);
}
void UCosmereNativeButton::NativeOnRemovedFromFocusPath(const FFocusEvent& InFocusEvent)
{
	Super::NativeOnRemovedFromFocusPath(InFocusEvent);
	if (NativeFocusText) NativeFocusText->SetVisibility(ESlateVisibility::Hidden);
}

UCosmereNativeCharacterCard::UCosmereNativeCharacterCard(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSelectable = true;
	bInteractableWhenSelected = true;
	bShouldSelectUponReceivingFocus = false;
}

void UCosmereNativeCharacterCard::SetCard(const FCosmereCharacterCardViewModel& InCard, bool bInSelected)
{
	Card = InCard;
	SetButtonText(FText::Format(NSLOCTEXT("CosmereUI", "CardSelection", "{0}{1}"),
		bInSelected ? NSLOCTEXT("CosmereUI", "SelectedCard", "SELECTED\n") : FText::GetEmpty(),
		CosmereNativeUI::CardText(Card)));
	SetIsSelected(bInSelected, false);
	SetMinDimensions(160, 144);
}

void UCosmereNativeCharacterCard::NativeOnClicked()
{
	const FGuid Id = Card.CharacterId;
	Super::NativeOnClicked();
	if (ULocalPlayer* Player = GetOwningLocalPlayer())
		if (UCosmereUIManager* Manager = Player->GetSubsystem<UCosmereUIManager>())
			Manager->HighlightCharacter(Id);
}

UCosmereNativeCharacterSelectScreen::UCosmereNativeCharacterSelectScreen(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ScreenRoute = ECosmereUIRoute::CharacterSelect;
	DefaultFocusWidgetName = TEXT("BtnCreateNew");
}
UCosmereNativeCharacterCreateScreen::UCosmereNativeCharacterCreateScreen(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ScreenRoute = ECosmereUIRoute::CharacterCreate;
	DefaultFocusWidgetName = TEXT("TxtCharacterName");
}
UCosmereNativeMainMenuScreen::UCosmereNativeMainMenuScreen(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ScreenRoute = ECosmereUIRoute::MainMenu;
	DefaultFocusWidgetName = TEXT("BtnPlay");
}
UCosmereNativeGameSelectScreen::UCosmereNativeGameSelectScreen(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ScreenRoute = ECosmereUIRoute::GameSelect;
	DefaultFocusWidgetName = TEXT("BtnPractice");
}

UCosmereUIManager* UCosmereNativeMenuScreen::GetUIManager() const
{
	ULocalPlayer* Player = GetOwningLocalPlayer();
	return Player ? Player->GetSubsystem<UCosmereUIManager>() : nullptr;
}
UCosmereSaveSubsystem* UCosmereNativeMenuScreen::GetSave() const
{
	if (const UCosmereUIManager* Manager = GetUIManager()) return Manager->GetProfileSource();
	UGameInstance* Instance = GetGameInstance();
	return Instance ? Instance->GetSubsystem<UCosmereSaveSubsystem>() : nullptr;
}

UCosmereNativeMenuScreen::UCosmereNativeMenuScreen(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UCosmereNativeMenuScreen::NativeOnInitialized()
{
	if (WidgetTree && !WidgetTree->RootWidget) BuildDefaultLayout();
	Super::NativeOnInitialized();
	BindActions();
}

void UCosmereNativeMenuScreen::BuildDefaultLayout()
{
	// Native defaults are usable without a Widget Blueprint. Designer-only children may replace them.
	UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RootBorder"));
	Root->SetPadding(FMargin(40.f, 28.f));
	Root->SetBrushColor(FLinearColor(0.015f, 0.022f, 0.035f,
		ScreenRoute == ECosmereUIRoute::MainMenu ? 0.f : 0.96f));
	WidgetTree->RootWidget = Root;
	UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RootVBox"));
	Root->SetContent(Body);
	const auto AddText = [this](UVerticalBox* Parent, const TCHAR* Name, const FText& Text)
	{
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(Name));
		Label->SetText(Text);
		Label->SetAutoWrapText(true);
		Label->SetColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.82f, 0.73f, 1.f)));
		Parent->AddChildToVerticalBox(Label)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
		return Label;
	};
	const auto AddButton = [this](UVerticalBox* Parent, const TCHAR* Name, const FText& Text)
	{
		UCosmereNativeButton* Button = WidgetTree->ConstructWidget<UCosmereNativeButton>(UCosmereNativeButton::StaticClass(), FName(Name));
		Button->SetButtonText(Text);
		Parent->AddChildToVerticalBox(Button)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		return Button;
	};
	FText Title = NSLOCTEXT("CosmereUI", "MainTitle", "COSMERE RIVALS");
	if (ScreenRoute == ECosmereUIRoute::MainMenu)
	{
		UHorizontalBox* Columns = WidgetTree->ConstructWidget<UHorizontalBox>();
		Body->AddChildToVerticalBox(Columns)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UBorder* Left = WidgetTree->ConstructWidget<UBorder>();
		Left->SetBrushColor(FLinearColor(0.015f, 0.022f, 0.035f, 0.94f)); Left->SetPadding(FMargin(20.f));
		FSlateChildSize LeftSize(ESlateSizeRule::Fill); LeftSize.Value = 0.32f;
		Columns->AddChildToHorizontalBox(Left)->SetSize(LeftSize);
		UScrollBox* ActionsScroll = WidgetTree->ConstructWidget<UScrollBox>(); Left->SetContent(ActionsScroll);
		UVerticalBox* Actions = WidgetTree->ConstructWidget<UVerticalBox>(); ActionsScroll->AddChild(Actions);
		AddText(Actions, TEXT("TxtHeader"), Title);
		AddText(Actions, TEXT("TxtWallet"), FText::GetEmpty());
		AddButton(Actions, TEXT("BtnPlay"), NSLOCTEXT("CosmereUI", "Play", "PLAY"));
		AddButton(Actions, TEXT("BtnShop"), NSLOCTEXT("CosmereUI", "Shop", "SHOP"));
		AddButton(Actions, TEXT("BtnSettings"), NSLOCTEXT("CosmereUI", "Settings", "SETTINGS"));
		AddButton(Actions, TEXT("BtnCharacterSelect"), NSLOCTEXT("CosmereUI", "Characters", "CHARACTER SELECT"));
		AddButton(Actions, TEXT("BtnQuit"), NSLOCTEXT("CosmereUI", "Quit", "QUIT"));
		AddText(Actions, TEXT("TxtStatus"), FText::GetEmpty());
		UVerticalBox* PreviewColumn = WidgetTree->ConstructWidget<UVerticalBox>();
		FSlateChildSize RightSize(ESlateSizeRule::Fill); RightSize.Value = 0.68f;
		UHorizontalBoxSlot* RightSlot = Columns->AddChildToHorizontalBox(PreviewColumn);
		RightSlot->SetSize(RightSize); RightSlot->SetPadding(FMargin(24.f, 0.f, 0.f, 0.f));
		USpacer* PreviewSpace = WidgetTree->ConstructWidget<USpacer>();
		PreviewColumn->AddChildToVerticalBox(PreviewSpace)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		USizeBox* FooterLimit = WidgetTree->ConstructWidget<USizeBox>(); FooterLimit->SetMaxDesiredHeight(260.f);
		PreviewColumn->AddChildToVerticalBox(FooterLimit);
		UBorder* Footer = WidgetTree->ConstructWidget<UBorder>(); Footer->SetPadding(FMargin(16.f));
		Footer->SetBrushColor(FLinearColor(0.015f, 0.022f, 0.035f, 0.88f)); FooterLimit->SetContent(Footer);
		UScrollBox* FooterScroll = WidgetTree->ConstructWidget<UScrollBox>(); Footer->SetContent(FooterScroll);
		UVerticalBox* FooterBody = WidgetTree->ConstructWidget<UVerticalBox>(); FooterScroll->AddChild(FooterBody);
		AddText(FooterBody, TEXT("TxtSelectedBuild"), FText::GetEmpty());
		AddText(FooterBody, TEXT("TxtBuildVersion"), FText::Format(NSLOCTEXT("CosmereUI", "BuildVersion", "Cosmere Rivals | Engine build {0}"), FText::FromString(FEngineVersion::Current().ToString())));
		return;
	}
	if (ScreenRoute == ECosmereUIRoute::CharacterSelect) Title = NSLOCTEXT("CosmereUI", "RosterTitle", "CHARACTER SELECT");
	if (ScreenRoute == ECosmereUIRoute::CharacterCreate) Title = NSLOCTEXT("CosmereUI", "CreationTitle", "CREATE CHARACTER");
	if (ScreenRoute == ECosmereUIRoute::GameSelect) Title = NSLOCTEXT("CosmereUI", "GameTitle", "PLAY");
	AddText(Body, TEXT("TxtHeader"), Title);
	AddText(Body, TEXT("TxtWallet"), FText::GetEmpty());
	const FName ScrollName = ScreenRoute == ECosmereUIRoute::CharacterSelect
		? FName(TEXT("CardScroll")) : FName(TEXT("ContentScroll"));
	UScrollBox* ContentScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), ScrollName);
	Body->AddChildToVerticalBox(ContentScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ContentVBox"));
	if (ScreenRoute != ECosmereUIRoute::CharacterSelect) ContentScroll->AddChild(Content);
	if (ScreenRoute == ECosmereUIRoute::CharacterSelect)
	{
		AddText(Body, TEXT("TxtEmpty"), NSLOCTEXT("CosmereUI", "EmptyRoster", "No characters yet. Create a character to begin."));
		AddButton(Body, TEXT("BtnConfirm"), NSLOCTEXT("CosmereUI", "Confirm", "CONFIRM CHARACTER"));
		AddButton(Body, TEXT("BtnCreateNew"), NSLOCTEXT("CosmereUI", "CreateNew", "CREATE CHARACTER"));
		AddButton(Body, TEXT("BtnQuit"), NSLOCTEXT("CosmereUI", "Quit", "QUIT"));
	}
	else if (ScreenRoute == ECosmereUIRoute::CharacterCreate)
	{
		AddText(Content, TEXT("TxtNameLabel"), NSLOCTEXT("CosmereUI", "Name", "NAME"));
		UEditableTextBox* Name = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("TxtCharacterName"));
		Name->SetHintText(NSLOCTEXT("CosmereUI", "NameHint", "Character name"));
		Content->AddChildToVerticalBox(Name)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
		const auto AddCombo = [this, Content, &AddText](const TCHAR* Name, const FText& Label)
		{
			AddText(Content, *FString::Printf(TEXT("%sLabel"), Name), Label);
			UComboBoxString* Combo = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), FName(Name));
			Combo->SetEnableGamepadNavigationMode(true);
			Content->AddChildToVerticalBox(Combo)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
		};
		AddCombo(TEXT("CmbRace"), NSLOCTEXT("CosmereUI", "Race", "RACE"));
		AddCombo(TEXT("CmbWeaponPrimary"), NSLOCTEXT("CosmereUI", "PrimaryWeapon", "PRIMARY WEAPON"));
		AddCombo(TEXT("CmbWeaponSecondary"), NSLOCTEXT("CosmereUI", "SecondaryWeapon", "SECONDARY WEAPON"));
		AddCombo(TEXT("CmbPowerAlpha"), NSLOCTEXT("CosmereUI", "FirstPower", "ABILITY 1"));
		AddCombo(TEXT("CmbPowerBeta"), NSLOCTEXT("CosmereUI", "SecondPower", "ABILITY 2"));
		AddButton(Body, TEXT("BtnCreate"), NSLOCTEXT("CosmereUI", "Create", "CREATE"));
		AddButton(Body, TEXT("BtnCancel"), NSLOCTEXT("CosmereUI", "Cancel", "CANCEL"));
	}
	else if (ScreenRoute == ECosmereUIRoute::GameSelect)
	{
		AddButton(Content, TEXT("BtnPractice"), NSLOCTEXT("CosmereUI", "Practice", "PRACTICE RANGE"));
		AddButton(Content, TEXT("BtnOnline"), NSLOCTEXT("CosmereUI", "Online", "ONLINE PLAY (LAN)"));
		AddButton(Content, TEXT("BtnBack"), NSLOCTEXT("CosmereUI", "Back", "BACK"));
	}
	AddText(Body, TEXT("TxtStatus"), FText::GetEmpty());
}

void UCosmereNativeMenuScreen::BindActions()
{
	const auto Bind = [this](const TCHAR* Name, void (UCosmereNativeMenuScreen::*Handler)())
	{
		if (UCommonButtonBase* Button = Cast<UCommonButtonBase>(GetWidgetFromName(FName(Name))))
		{
			Button->OnClicked().RemoveAll(this);
			Button->OnClicked().AddUObject(this, Handler);
		}
	};
	Bind(TEXT("BtnConfirm"), &UCosmereNativeMenuScreen::ConfirmCharacter);
	Bind(TEXT("BtnCreate"), &UCosmereNativeMenuScreen::CreateCharacter);
	Bind(TEXT("BtnPractice"), &UCosmereNativeMenuScreen::EnterPractice);
	Bind(TEXT("BtnBack"), &UCosmereNativeMenuScreen::GoBack);
	Bind(TEXT("BtnCancel"), &UCosmereNativeMenuScreen::GoBack);
	Bind(TEXT("BtnQuit"), &UCosmereNativeMenuScreen::Quit);
	const auto BindRoute = [this](const TCHAR* Name, ECosmereUIRoute Route)
	{
		if (UCommonButtonBase* Button = Cast<UCommonButtonBase>(GetWidgetFromName(FName(Name))))
		{
			Button->OnClicked().RemoveAll(this);
			Button->OnClicked().AddUObject(this, &UCosmereNativeMenuScreen::NavigateTo, Route);
		}
	};
	BindRoute(TEXT("BtnCreateNew"), ECosmereUIRoute::CharacterCreate);
	BindRoute(TEXT("BtnPlay"), ECosmereUIRoute::GameSelect);
	BindRoute(TEXT("BtnShop"), ECosmereUIRoute::ShopHub);
	BindRoute(TEXT("BtnSettings"), ECosmereUIRoute::Settings);
	BindRoute(TEXT("BtnCharacterSelect"), ECosmereUIRoute::CharacterSelect);
	BindRoute(TEXT("BtnOnline"), ECosmereUIRoute::ServerMenu);
	for (const TCHAR* Name : {TEXT("CmbRace"), TEXT("CmbWeaponPrimary"), TEXT("CmbWeaponSecondary"), TEXT("CmbPowerAlpha"), TEXT("CmbPowerBeta")})
		if (UComboBoxString* Combo = Cast<UComboBoxString>(GetWidgetFromName(FName(Name))))
			Combo->OnSelectionChanged.AddUniqueDynamic(this, &UCosmereNativeMenuScreen::CreationOptionChanged);
	if (UEditableTextBox* Name = Cast<UEditableTextBox>(GetWidgetFromName(TEXT("TxtCharacterName"))))
		Name->OnTextChanged.AddUniqueDynamic(this, &UCosmereNativeMenuScreen::CreationNameChanged);
}

void UCosmereNativeMenuScreen::NativeOnActivated()
{
	DetachManager();
	BoundManager = GetUIManager();
	if (BoundManager) BoundManager->OnViewModelsChanged.AddUniqueDynamic(this, &UCosmereNativeMenuScreen::RefreshView);
	if (ScreenRoute == ECosmereUIRoute::CharacterCreate) PopulateCreationOptions();
	RefreshView();
	Super::NativeOnActivated();
}
void UCosmereNativeMenuScreen::DetachManager()
{
	if (BoundManager) BoundManager->OnViewModelsChanged.RemoveDynamic(this, &UCosmereNativeMenuScreen::RefreshView);
	BoundManager = nullptr;
}
void UCosmereNativeMenuScreen::NativeOnDeactivated()
{
	DetachManager();
	Super::NativeOnDeactivated();
}
void UCosmereNativeMenuScreen::NativeDestruct()
{
	DetachManager();
	Super::NativeDestruct();
}

UWidget* UCosmereNativeMenuScreen::NativeGetDesiredFocusTarget() const
{
	if (ScreenRoute == ECosmereUIRoute::CharacterSelect)
		if (const UCosmereUIManager* Manager = GetUIManager())
			for (const auto& Card : CharacterCards)
				if (Card && Card->GetCharacterId() == Manager->GetPendingCharacterId()) return Card;
	return Super::NativeGetDesiredFocusTarget();
}

void UCosmereNativeMenuScreen::ScrollToCard(FGuid CharacterId)
{
	if (UScrollBox* List = Cast<UScrollBox>(GetWidgetFromName(TEXT("CardScroll"))))
		for (const auto& Card : CharacterCards)
			if (Card && Card->GetCharacterId() == CharacterId) { List->ScrollWidgetIntoView(Card, false); break; }
}

void UCosmereNativeMenuScreen::RefreshView()
{
	UCosmereUIManager* Manager = GetUIManager();
	UCosmereSaveSubsystem* Save = GetSave();
	if (!Manager || !Save) return;
	if (UTextBlock* Wallet = Cast<UTextBlock>(GetWidgetFromName(TEXT("TxtWallet"))))
		Wallet->SetText(FText::Format(NSLOCTEXT("CosmereUI", "Wallet", "Shared Wallet: Nicrosil {0}  |  Gold {1}  |  Duralumin {2}"),
			FText::AsNumber(Save->GetCurrency(ECosmereCurrency::Nicrosil)), FText::AsNumber(Save->GetCurrency(ECosmereCurrency::Gold)),
			FText::AsNumber(Save->GetCurrency(ECosmereCurrency::Duralumin))));
	if (ScreenRoute == ECosmereUIRoute::CharacterSelect) RefreshCards();
	if (ScreenRoute == ECosmereUIRoute::CharacterCreate) ValidateCreation();
	if (UTextBlock* Build = Cast<UTextBlock>(GetWidgetFromName(TEXT("TxtSelectedBuild"))))
	{
		FCosmereCharacterCardViewModel Card;
		Build->SetText(Manager->GetSelectedCharacterCard(Card) ? CosmereNativeUI::CardText(Card) : FText::GetEmpty());
	}
}

void UCosmereNativeMenuScreen::RefreshCards()
{
	UCosmereUIManager* Manager = GetUIManager();
	UScrollBox* List = Cast<UScrollBox>(GetWidgetFromName(TEXT("CardScroll")));
	if (!Manager || !List) return;
	const TArray<FCosmereCharacterCardViewModel> Models = Manager->GetCharacterCards();
	bool bSameRoster = Models.Num() == CharacterCards.Num();
	for (int32 Index = 0; bSameRoster && Index < Models.Num(); ++Index)
		bSameRoster = CharacterCards[Index] && CharacterCards[Index]->GetCharacterId() == Models[Index].CharacterId;
	if (!bSameRoster)
	{
		List->ClearChildren();
		ClearFocusRestorationTarget();
		CharacterCards.Reset();
		for (const FCosmereCharacterCardViewModel& Model : Models)
		{
			UCosmereNativeCharacterCard* Card = CreateWidget<UCosmereNativeCharacterCard>(GetOwningPlayer(), UCosmereNativeCharacterCard::StaticClass());
			if (!Card) continue;
			Card->SetCard(Model, Model.CharacterId == Manager->GetPendingCharacterId());
			Card->OnFocusReceived().AddUObject(this, &UCosmereNativeMenuScreen::ScrollToCard, Model.CharacterId);
			List->AddChild(Card);
			CharacterCards.Add(Card);
		}
	}
	else
	{
		// Do not destroy the focused card merely because highlight or wallet changed.
		for (int32 Index = 0; Index < Models.Num(); ++Index)
			CharacterCards[Index]->SetCard(Models[Index], Models[Index].CharacterId == Manager->GetPendingCharacterId());
	}
	if (UWidget* Empty = GetWidgetFromName(TEXT("TxtEmpty")))
		Empty->SetVisibility(Models.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (UWidget* Confirm = GetWidgetFromName(TEXT("BtnConfirm"))) Confirm->SetIsEnabled(Manager->CanConfirmCharacter());
	UWidget* End = Manager->CanConfirmCharacter() ? GetWidgetFromName(TEXT("BtnConfirm")) : GetWidgetFromName(TEXT("BtnCreateNew"));
	for (int32 Index = 0; Index < CharacterCards.Num(); ++Index)
	{
		UWidget* Previous = Index > 0 ? CharacterCards[Index-1].Get() : GetWidgetFromName(TEXT("BtnCreateNew"));
		UWidget* Next = Index+1 < CharacterCards.Num() ? CharacterCards[Index+1].Get() : End;
		CharacterCards[Index]->SetNavigationRuleExplicit(EUINavigation::Up, Previous);
		CharacterCards[Index]->SetNavigationRuleExplicit(EUINavigation::Down, Next);
		CharacterCards[Index]->SetNavigationRuleExplicit(EUINavigation::Next, Next);
		CharacterCards[Index]->SetNavigationRuleExplicit(EUINavigation::Previous, Previous);
	}
	if (!CharacterCards.IsEmpty())
	{
		if (UWidget* Confirm = GetWidgetFromName(TEXT("BtnConfirm"))) Confirm->SetNavigationRuleExplicit(EUINavigation::Up, CharacterCards.Last());
		if (UWidget* Create = GetWidgetFromName(TEXT("BtnCreateNew"))) Create->SetNavigationRuleExplicit(EUINavigation::Previous, CharacterCards.Last());
	}
}

void UCosmereNativeMenuScreen::PopulateCreationOptions()
{
	UCosmereSaveSubsystem* Save = GetSave();
	const UCosmereUIManager* Manager = GetUIManager();
	UCosmereCatalogSubsystem* Catalog = Manager ? Manager->GetCatalogSource() :
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UCosmereCatalogSubsystem>() : nullptr;
	if (!Save || !Catalog) return;
	TGuardValue<bool> Guard(bPopulatingOptions, true);
	RaceIds = Save->GetUnlockedRaceIds();
	WeaponIds = Save->GetUnlockedWeaponIds();
	PowerIds = Save->GetUnlockedPowerIds();
	const auto Populate = [this, Catalog](const TCHAR* Name, const TArray<FName>& Ids, int32 Kind)
	{
		UComboBoxString* Combo = Cast<UComboBoxString>(GetWidgetFromName(FName(Name)));
		if (!Combo) return;
		Combo->ClearOptions();
		for (FName Id : Ids)
		{
			FText Label = FText::FromName(Id);
			FRaceDefinition Race; FWeaponDefinition Weapon; FPowerDefinition Power;
			if (Kind == 0 && Catalog->GetRaceRow(Id, Race)) Label = Race.DisplayName;
			if (Kind == 1 && Catalog->GetWeaponRow(Id, Weapon)) Label = Weapon.DisplayName;
			if (Kind == 2 && Catalog->GetPowerRow(Id, Power)) Label = Power.DisplayName;
			Combo->AddOption(FString::Printf(TEXT("%s [%s]"), *Label.ToString(), *Id.ToString()));
		}
	};
	Populate(TEXT("CmbRace"), RaceIds, 0);
	Populate(TEXT("CmbWeaponPrimary"), WeaponIds, 1);
	Populate(TEXT("CmbWeaponSecondary"), WeaponIds, 1);
	Populate(TEXT("CmbPowerAlpha"), PowerIds, 2);
	Populate(TEXT("CmbPowerBeta"), PowerIds, 2);
}

void UCosmereNativeMenuScreen::ValidateCreation()
{
	const auto Index = [this](const TCHAR* Name)
	{
		const UComboBoxString* Combo = Cast<UComboBoxString>(GetWidgetFromName(FName(Name)));
		return Combo ? Combo->GetSelectedIndex() : INDEX_NONE;
	};
	const UEditableTextBox* Name = Cast<UEditableTextBox>(GetWidgetFromName(TEXT("TxtCharacterName")));
	const int32 Race = Index(TEXT("CmbRace"));
	const int32 Primary = Index(TEXT("CmbWeaponPrimary"));
	const int32 Secondary = Index(TEXT("CmbWeaponSecondary"));
	const int32 Alpha = Index(TEXT("CmbPowerAlpha"));
	const int32 Beta = Index(TEXT("CmbPowerBeta"));
	const bool bValid = Name && !Name->GetText().ToString().TrimStartAndEnd().IsEmpty()
		&& RaceIds.IsValidIndex(Race) && WeaponIds.IsValidIndex(Primary) && WeaponIds.IsValidIndex(Secondary)
		&& PowerIds.IsValidIndex(Alpha) && PowerIds.IsValidIndex(Beta) && Primary != Secondary && Alpha != Beta;
	if (UWidget* Create = GetWidgetFromName(TEXT("BtnCreate"))) Create->SetIsEnabled(bValid && !bCreatingCharacter);
}
void UCosmereNativeMenuScreen::CreationOptionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (!bPopulatingOptions) ValidateCreation();
}
void UCosmereNativeMenuScreen::CreationNameChanged(const FText& Text)
{
	ValidateCreation();
}

void UCosmereNativeMenuScreen::CreateCharacter()
{
	if (bCreatingCharacter) return;
	UCosmereSaveSubsystem* Save = GetSave();
	UCosmereUIManager* Manager = GetUIManager();
	const UEditableTextBox* Name = Cast<UEditableTextBox>(GetWidgetFromName(TEXT("TxtCharacterName")));
	if (!Save || !Manager || !Name) return;
	const auto Index = [this](const TCHAR* WidgetName)
	{
		const UComboBoxString* Combo = Cast<UComboBoxString>(GetWidgetFromName(FName(WidgetName)));
		return Combo ? Combo->GetSelectedIndex() : INDEX_NONE;
	};
	const int32 Race = Index(TEXT("CmbRace")), Primary = Index(TEXT("CmbWeaponPrimary")), Secondary = Index(TEXT("CmbWeaponSecondary"));
	const int32 Alpha = Index(TEXT("CmbPowerAlpha")), Beta = Index(TEXT("CmbPowerBeta"));
	if (!RaceIds.IsValidIndex(Race) || !WeaponIds.IsValidIndex(Primary) || !WeaponIds.IsValidIndex(Secondary)
		|| !PowerIds.IsValidIndex(Alpha) || !PowerIds.IsValidIndex(Beta))
	{
		ShowError(NSLOCTEXT("CosmereUI", "IncompleteBuild", "Choose one race, two distinct weapons, and two distinct powers."));
		return;
	}
	TGuardValue<bool> Guard(bCreatingCharacter, true);
	const bool bCreated = Save->CreateCharacter(Name->GetText(), RaceIds[Race],
		TArray<FName>{WeaponIds[Primary], WeaponIds[Secondary]}, TArray<FName>{PowerIds[Alpha], PowerIds[Beta]});
	if (!bCreated)
	{
		bCreatingCharacter = false;
		ValidateCreation();
		ShowError(Save->GetLastError());
		return; // No error-path loop, navigation, or selection commit.
	}
	const TArray<FSpiritwebCharacterSave> Builds = Save->GetCharacters();
	if (!Builds.IsEmpty()) Manager->HighlightCharacter(Builds.Last().CharacterId);
	if (!Manager->Navigate(ECosmereUIRoute::CharacterSelect, true)) ShowError(Manager->GetLastError());
}

void UCosmereNativeMenuScreen::ShowError(const FText& Error)
{
	if (UTextBlock* Status = Cast<UTextBlock>(GetWidgetFromName(TEXT("TxtStatus"))))
	{
		Status->SetText(Error);
		Status->SetColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.13f, 0.10f, 1.f)));
	}
	if (!Error.IsEmpty()) UE_LOG(LogCosmereUI, Warning, TEXT("Native UI: %s"), *Error.ToString());
}
void UCosmereNativeMenuScreen::NavigateTo(ECosmereUIRoute Route)
{
	if (UCosmereUIManager* Manager = GetUIManager())
		if (!Manager->Navigate(Route, Route == ECosmereUIRoute::CharacterSelect)) ShowError(Manager->GetLastError());
}
void UCosmereNativeMenuScreen::ConfirmCharacter()
{
	if (UCosmereUIManager* Manager = GetUIManager())
		if (!Manager->ConfirmCharacter()) ShowError(Manager->GetLastError());
}
void UCosmereNativeMenuScreen::EnterPractice()
{
	if (UCosmereUIManager* Manager = GetUIManager())
		if (!Manager->EnterPractice()) ShowError(Manager->GetLastError());
}
void UCosmereNativeMenuScreen::GoBack()
{
	if (UCosmereUIManager* Manager = GetUIManager()) Manager->NavigateBack();
}
void UCosmereNativeMenuScreen::Quit()
{
	if (UCosmereUIManager* Manager = GetUIManager()) Manager->RequestQuit();
}
