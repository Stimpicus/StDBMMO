// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class client_unreal : ModuleRules
{
    public client_unreal(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "EnhancedInput",
            "SpacetimeDbSdk",
            "Paper2D"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UMG",
            "SlateCore",
            "Slate"
        });
    }
}
