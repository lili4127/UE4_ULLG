// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "HoloPlayEditor.h"

#include "HoloPlayEditorStyle.h"
#include "HoloPlayEditorCommands.h"
#include "HoloPlayToolbar.h"

#include "IHoloPlayRuntime.h"
#include "HoloPlaySettings.h"
#include "Game/HoloPlaySceneCaptureComponent2D.h"
#include "Customizations/HoloPlayCaptureResolutionCustomization.h"

#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "ISettingsContainer.h"
#include "LevelEditor.h"

#include "Engine/GameViewportClient.h"
#include "Editor.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "DetailCustomizations.h"

#define LOCTEXT_NAMESPACE "FHoloPlayEditorModule"

void FHoloPlayEditorModule::StartupModule()
{
	// Initialize play button ui style
	FHoloPlayEditorStyle::Initialize();
	FHoloPlayEditorStyle::ReloadTextures();

	// Add HoloPlayToolbar section
	FHoloPlayToolbarCommand::Register();
	HoloPlayToolbar = MakeShareable(new FHoloPlayToolbar);

	// Add settings to project settings
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FHoloPlayEditorModule::AddEditorSettings);
	GetMutableDefault<UHoloPlaySettings>()->AddToRoot();

	UGameViewportClient::OnViewportCreated().AddRaw(this, &FHoloPlayEditorModule::OnPIEViewportStarted);
	FEditorDelegates::EndPIE.AddRaw(this, &FHoloPlayEditorModule::OnEndPIE);

	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	// Unregister the engine customization
	FModuleManager::LoadModuleChecked<FDetailCustomizationsModule>("DetailCustomizations");
	PropertyModule.UnregisterCustomPropertyTypeLayout("CaptureResolution");

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.OnActorSelectionChanged().AddRaw(this, &FHoloPlayEditorModule::OnEditorSelectionChanged);

	// Register Detail Customization
	RegisterCustomPropertyTypeLayout("CaptureResolution", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FHoloPlayCaptureResolutionCustomization::MakeInstance));
}

void FHoloPlayEditorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	FHoloPlayEditorStyle::Shutdown();

	// Release Toolbar
	HoloPlayToolbar.Reset();

	// Release editor settings
	RemoveEditorSettings();

	// Stop player if running
	IHoloPlayRuntime::Get().StopPlayer();

	if (FModuleManager::Get().IsModuleLoaded("LevelEditor"))
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditorModule.OnActorSelectionChanged().RemoveAll(this);
	}

	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		// Unregister all structures
		for (auto It = RegisteredPropertyTypes.CreateConstIterator(); It; ++It)
		{
			if (It->IsValid())
			{
				PropertyModule.UnregisterCustomPropertyTypeLayout(*It);
			}
		}

		PropertyModule.NotifyCustomizationModuleChanged();
	}
}

// Check MagicLeapHMD settings loading
void FHoloPlayEditorModule::AddEditorSettings()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

	if (SettingsModule)
	{
		ISettingsSectionPtr SettingsSection = SettingsModule->RegisterSettings("Project", "Plugins", "HoloPlay",
			LOCTEXT("HoloPlaySettingsName", "HoloPlay Plugin"),
			LOCTEXT("HoloPlaySettingsDescription", "Configure the HoloPlay plug-in."),
			GetMutableDefault<UHoloPlaySettings>()
		);

		if (SettingsSection.IsValid())
		{
			SettingsSection->OnModified().BindRaw(this, &FHoloPlayEditorModule::OnSettingsSaved);
		}
	}
}

void FHoloPlayEditorModule::RemoveEditorSettings()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule)
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "HoloPlay");
	}
}

bool FHoloPlayEditorModule::OnSettingsSaved()
{
	UHoloPlaySettings* HoloPlaySettings = GetMutableDefault<UHoloPlaySettings>();
	bool ResaveSettings = true;

	if (ResaveSettings)
	{
		HoloPlaySettings->HoloPlaySave();
	}

	return true;
}

void FHoloPlayEditorModule::OnPIEViewportStarted()
{
	if (GetDefault<UHoloPlaySettings>()->HoloPlayWindowSettings.bLockInMainViewport)
	{
		IHoloPlayRuntime::Get().StartPlayer(EHoloPlayModeType::PlayMode_InMainViewport);
	}
}

void FHoloPlayEditorModule::OnEndPIE(bool bIsSimulating)
{
	if (GetDefault<UHoloPlaySettings>()->HoloPlayWindowSettings.bLockInMainViewport)
	{
		IHoloPlayRuntime::Get().StopPlayer();
	}
}

void FHoloPlayEditorModule::RegisterCustomPropertyTypeLayout(FName PropertyTypeName, FOnGetPropertyTypeCustomizationInstance PropertyTypeLayoutDelegate)
{
	check(PropertyTypeName != NAME_None);

	RegisteredPropertyTypes.Add(PropertyTypeName);

	static FName PropertyEditor("PropertyEditor");
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(PropertyEditor);
	PropertyModule.RegisterCustomPropertyTypeLayout(PropertyTypeName, PropertyTypeLayoutDelegate);
}

void FHoloPlayEditorModule::OnEditorSelectionChanged(const TArray<UObject*>& NewSelection, bool bForceRefresh)
{
	if (NewSelection.Num() == 1)
	{
		AActor* Actor = Cast<AActor>(NewSelection[0]);
		if (Actor != nullptr)
		{
			UHoloPlaySceneCaptureComponent2D* CaptureComponent = Actor->FindComponentByClass<UHoloPlaySceneCaptureComponent2D>();
			if (CaptureComponent)
			{
				// Make the selected component first, so it will be activated in HoloPlayRuntime
				IHoloPlayRuntime::Get().EditorHoloPlayCaptureComponents.Remove(CaptureComponent);
				IHoloPlayRuntime::Get().EditorHoloPlayCaptureComponents.Insert(CaptureComponent, 0);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FHoloPlayEditorModule, HoloPlayEditor)
