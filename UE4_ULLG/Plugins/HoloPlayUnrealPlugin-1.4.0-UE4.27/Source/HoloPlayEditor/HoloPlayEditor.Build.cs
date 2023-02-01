// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class HoloPlayEditor : ModuleRules
{
	public HoloPlayEditor(ReadOnlyTargetRules Target) : base(Target)
	{
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(
			new string[] {
			}
			);


		PrivateIncludePaths.AddRange(
			new string[] {
			}
			);


		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
            }
            );


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"DetailCustomizations",
				"MovieSceneCapture",
				"Slate",
				"SlateCore",
                "InputCore",
                "Projects",
				"PropertyEditor",
				"UnrealEd",
				"EditorStyle",
				"LevelEditor",
                "Settings",
#if UE_5_0_OR_LATER
				"ToolMenus",
				"ToolWidgets",
#endif
                "HoloPlayRuntime"
            }
			);
	}
}
