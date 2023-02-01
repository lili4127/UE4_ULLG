#include "HoloPlayToolbar.h"
#include "HoloPlayEditorCommands.h"

#include "IHoloPlayRuntime.h"
#include "HoloPlaySettings.h"
#include "IHoloPlayEditor.h"

#include "LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "Internationalization/Internationalization.h"
#include "Widgets/SWidget.h"
#include "Widgets/Input/SSpinBox.h"

#include "Widgets/SBoxPanel.h"

#include "Launch/Resources/Version.h"

#if ENGINE_MAJOR_VERSION >= 5
#include "ToolMenuEntry.h"
#include "ToolMenus.h"
#endif

#define LOCTEXT_NAMESPACE "HoloPlayToolbarEditor"

FHoloPlayToolbar::FHoloPlayToolbar()
{
	ExtendLevelEditorToolbar();
}

FHoloPlayToolbar::~FHoloPlayToolbar()
{
	if (UObjectInitialized() && LevelToolbarExtender.IsValid() && !IsEngineExitRequested())
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		if (LevelEditorModule.GetToolBarExtensibilityManager().IsValid())
		{
			LevelEditorModule.GetToolBarExtensibilityManager()->RemoveExtender(LevelToolbarExtender);
		}
	}
}

void FHoloPlayToolbar::ExtendLevelEditorToolbar()
{
	check(!LevelToolbarExtender.IsValid());

#if ENGINE_MAJOR_VERSION < 5
	// Create Toolbar extension
	LevelToolbarExtender = MakeShareable(new FExtender);

	LevelToolbarExtender->AddToolBarExtension(
		"Game",
		EExtensionHook::After,
		FHoloPlayToolbarCommand::Get().CommandActionList,
		FToolBarExtensionDelegate::CreateRaw(this, &FHoloPlayToolbar::FillToolbar)
	);

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(LevelToolbarExtender);
#else
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<FUICommandList> CommandList = FHoloPlayToolbarCommand::Get().CommandActionList;

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
	FToolMenuSection& Section = Menu->FindOrAddSection("HoloPlay");

	// Combined Play/Close button. It is required to be combined, instead of showing/hiding separate buttons, in order to make the following
	// ComboButton always visible. It was always visible in UE4, however in UE5 it is alwaus bound to the previous button, and inherits
	// its visibility flags.
	FToolMenuEntry HoloPlayButtonEntry = FToolMenuEntry::InitToolBarButton(
		FHoloPlayToolbarCommand::Get().RepeatLastPlay,
		TAttribute< FText >::Create(TAttribute< FText >::FGetter::CreateStatic(&FHoloPlayToolbar::GetRepeatLastPlayName)),
		TAttribute< FText >::Create(TAttribute< FText >::FGetter::CreateStatic(&FHoloPlayToolbar::GetRepeatLastPlayToolTip)),
		TAttribute< FSlateIcon >::Create(TAttribute< FSlateIcon >::FGetter::CreateStatic(&FHoloPlayToolbar::GetRepeatLastPlayIcon))
	);
	HoloPlayButtonEntry.SetCommandList(CommandList);

	// Combo button
	const FToolMenuEntry HoloPlayComboEntry = FToolMenuEntry::InitComboButton(
		"HoloPlayMenu",
		FUIAction(),
		FOnGetContent::CreateStatic(&FHoloPlayToolbar::GenerateMenuContent, FHoloPlayToolbarCommand::Get().CommandActionList.ToSharedRef(), LevelEditorModule.GetMenuExtensibilityManager()->GetAllExtenders()),
		LOCTEXT("PlayCombo_Label", "Active Play Mode"),
		LOCTEXT("PIEComboToolTip", "Change Play Mode and Play Settings"),
		FSlateIcon(),
		true //bInSimpleComboBox
	);

	Section.AddEntry(HoloPlayButtonEntry);
	Section.AddEntry(HoloPlayComboEntry);
#endif
}

// UE4 version of toolbar extender
void FHoloPlayToolbar::FillToolbar(FToolBarBuilder & ToolbarBuilder)
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

	ToolbarBuilder.BeginSection("HoloPlayToolbar");
	{
		// Add a button to open a HoloPlay Window
		ToolbarBuilder.AddToolBarButton(
			FHoloPlayToolbarCommand::Get().RepeatLastPlay,
			NAME_None,
			TAttribute< FText >::Create(TAttribute< FText >::FGetter::CreateStatic(&FHoloPlayToolbar::GetRepeatLastPlayName)),
			TAttribute< FText >::Create(TAttribute< FText >::FGetter::CreateStatic(&FHoloPlayToolbar::GetRepeatLastPlayToolTip)),
			TAttribute< FSlateIcon >::Create(TAttribute< FSlateIcon >::FGetter::CreateStatic(&FHoloPlayToolbar::GetRepeatLastPlayIcon))
		);

		ToolbarBuilder.AddComboButton(
			FUIAction(),
			FOnGetContent::CreateStatic(&FHoloPlayToolbar::GenerateMenuContent, FHoloPlayToolbarCommand::Get().CommandActionList.ToSharedRef(), LevelEditorModule.GetMenuExtensibilityManager()->GetAllExtenders()),
			LOCTEXT("PlayCombo_Label", "Active Play Mode"),
			LOCTEXT("PIEComboToolTip", "Change Play Mode and Play Settings"),
			FSlateIcon(),
			true
		);
	}
	ToolbarBuilder.EndSection();
}

TSharedRef<SWidget> FHoloPlayToolbar::GenerateMenuContent(TSharedRef<FUICommandList> InCommandList, TSharedPtr<FExtender> Extender)
{
	struct FLocal
	{
		static void AddPlayModeMenuEntry(FMenuBuilder& MenuBuilder, EHoloPlayModeType PlayMode)
		{
			TSharedPtr<FUICommandInfo> PlayModeCommand;

			switch (PlayMode)
			{
			case EHoloPlayModeType::PlayMode_InSeparateWindow:
				PlayModeCommand = FHoloPlayToolbarCommand::Get().PlayInHoloPlayWindow;
				break;

			case EHoloPlayModeType::PlayMode_InMainViewport:
				PlayModeCommand = FHoloPlayToolbarCommand::Get().PlayInMainViewport;
				break;
			}

			if (PlayModeCommand.IsValid())
			{
				MenuBuilder.AddMenuEntry(PlayModeCommand);
			}
		}
	};

	bool bIsPlaying = IHoloPlayRuntime::Get().IsPlaying();
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, InCommandList, Extender);

	if (!bIsPlaying)
	{
		MenuBuilder.BeginSection("HoloPlayModes", LOCTEXT("HoloPlayButtonModesSection", "Modes"));
		{
			FLocal::AddPlayModeMenuEntry(MenuBuilder, EHoloPlayModeType::PlayMode_InSeparateWindow);
			FLocal::AddPlayModeMenuEntry(MenuBuilder, EHoloPlayModeType::PlayMode_InMainViewport);
		}
		MenuBuilder.EndSection();
	}

	if (!bIsPlaying)
	{
		MenuBuilder.AddMenuEntry(FHoloPlayToolbarCommand::Get().LockInMainViewport);

		MenuBuilder.BeginSection("Placement Mode", LOCTEXT("HoloPlayPlacementSection", "Placement Mode"));
		MenuBuilder.AddMenuEntry(FHoloPlayToolbarCommand::Get().PlacementInHoloPlayAuto);
		MenuBuilder.AddMenuEntry(FHoloPlayToolbarCommand::Get().PlacementInHoloPlayCustomWindow);
		MenuBuilder.AddMenuEntry(FHoloPlayToolbarCommand::Get().PlacementInHoloPlayDebug);
		MenuBuilder.EndSection();
	}

	MenuBuilder.BeginSection("HoloPlay Play Settings", LOCTEXT("HoloPlayPlaySettings", "HoloPlay Play Settings"));
	{
		MenuBuilder.AddMenuEntry(FHoloPlayToolbarCommand::Get().PlayInQuiltMode);

		MenuBuilder.AddMenuEntry(FHoloPlayToolbarCommand::Get().PlayIn2DMode);

		MenuBuilder.AddMenuEntry(FHoloPlayToolbarCommand::Get().CustomAspect);


		TSharedRef<SWidget> CustomAspectWidget =
			SNew(SHorizontalBox)
			.Visibility_Lambda([]() -> EVisibility
			{
				return FHoloPlayToolbarCommand::IsCustomAspect() ? EVisibility::Visible : EVisibility::Collapsed;
			})
			+ SHorizontalBox::Slot()
			.Padding(2)
			.HAlign(HAlign_Left)
			[
				SNew(SSpinBox<float>)
					.MinValue(0.f)
					.MaxValue(10000.f)
					.MinSliderValue(0.f)
					.MaxSliderValue(10000.f)
					.Value_Lambda([]() -> float 
					{ 
						UHoloPlaySettings* HoloPlaySettings = GetMutableDefault<UHoloPlaySettings>();
						return HoloPlaySettings->HoloPlayRenderingSettings.CustomAspect.X;
					})
					.OnValueCommitted_Lambda([](float InValue, ETextCommit::Type CommitInfo)
					{ 
						UHoloPlaySettings* HoloPlaySettings = GetMutableDefault<UHoloPlaySettings>();
						HoloPlaySettings->HoloPlayRenderingSettings.CustomAspect.X = InValue;
						HoloPlaySettings->HoloPlaySave();
					})
			]
			+ SHorizontalBox::Slot()
			.Padding(2)
			.HAlign(HAlign_Left)
			[
				SNew(SSpinBox<float>)
					.MinValue(0.f)
					.MaxValue(10000.f)
					.MinSliderValue(0.f)
					.MaxSliderValue(10000.f)
					.Value_Lambda([]() -> float 
					{ 
						UHoloPlaySettings* HoloPlaySettings = GetMutableDefault<UHoloPlaySettings>();
						return HoloPlaySettings->HoloPlayRenderingSettings.CustomAspect.Y;
					})
					.OnValueCommitted_Lambda([](float InValue, ETextCommit::Type CommitInfo)
					{ 
						UHoloPlaySettings* HoloPlaySettings = GetMutableDefault<UHoloPlaySettings>();
						HoloPlaySettings->HoloPlayRenderingSettings.CustomAspect.Y = InValue;
						HoloPlaySettings->HoloPlaySave();
					})				
			]
			+ SHorizontalBox::Slot()
			.Padding(FMargin(5, 0, 5, 0))
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([]() -> FText
				{
					UHoloPlaySettings* HoloPlaySettings = GetMutableDefault<UHoloPlaySettings>();
					FNumberFormattingOptions NumberFormat;	
					NumberFormat.MinimumIntegralDigits = 1;
					NumberFormat.MaximumIntegralDigits = 10000;
					NumberFormat.MinimumFractionalDigits = 2;
					NumberFormat.MaximumFractionalDigits = 2;
					return FText::AsNumber(HoloPlaySettings->HoloPlayRenderingSettings.GetCustomAspect(), &NumberFormat);
				})
			];

		MenuBuilder.AddWidget(CustomAspectWidget, FText::GetEmpty());

		MenuBuilder.AddMenuEntry(
			FHoloPlayToolbarCommand::Get().OpenHoloPlaySettings,
			NAME_None,
			LOCTEXT("OpenHoloPlaySettings_Label", "Settings"),
			LOCTEXT("OpenHoloPlaySettings_Tip", "Open HoloPlay Settings."),
			FSlateIcon(FHoloPlayEditorStyle::GetStyleSetName(), TEXT("HoloPlay.OpenSettings"))
			);

		MenuBuilder.EndSection();
	}

	if (!bIsPlaying)
	{
		MenuBuilder.BeginSection("HoloPlay Play Display", LOCTEXT("HoloPlayPlayDisplaySection", "Display Options"));
		{
			TSharedRef<SWidget> DisplayIndex = SNew(SSpinBox<int32>)
				.MinValue(0)
				.MaxValue(3)
				.MinSliderValue(0)
				.MaxSliderValue(3)
				.ToolTipText(LOCTEXT("HoloPlayPlayDisplayToolTip", "HoloPlay display index"))
				.Value(FHoloPlayToolbarCommand::GetCurrentHoloPlayDisplayIndex())
				.OnValueCommitted_Static(&FHoloPlayToolbarCommand::SetCurrentHoloPlayDisplayIndex);

			MenuBuilder.AddWidget(DisplayIndex, LOCTEXT("HoloPlayDisplayIndexWidget", "Display Index"));
		}
		MenuBuilder.EndSection();
	}

	return MenuBuilder.MakeWidget();
}

FSlateIcon FHoloPlayToolbar::GetRepeatLastPlayIcon()
{
	if (!IHoloPlayRuntime::Get().IsPlaying())
	{
		// Play button
		return FHoloPlayToolbarCommand::GetLastPlaySessionCommand()->GetIcon();
	}
	else
	{
		// Stop button
		return FSlateIcon(FHoloPlayEditorStyle::GetStyleSetName(), TEXT("HoloPlay.CloseWindow"));
	}
}

FText FHoloPlayToolbar::GetRepeatLastPlayName()
{
	if (!IHoloPlayRuntime::Get().IsPlaying())
	{
		return LOCTEXT("RepeatLastPlay_Label", "Play");
	}
	else
	{
		return LOCTEXT("CloseHoloPlayWindow_Label", "Stop");
	}
}

FText FHoloPlayToolbar::GetRepeatLastPlayToolTip()
{
	if (!IHoloPlayRuntime::Get().IsPlaying())
	{
		return FHoloPlayToolbarCommand::GetLastPlaySessionCommand()->GetDescription();
	}
	else
	{
		return LOCTEXT("CloseHoloPlayWindow_Tip", "Close HoloPlay Window.");
	}
}

#undef LOCTEXT_NAMESPACE
