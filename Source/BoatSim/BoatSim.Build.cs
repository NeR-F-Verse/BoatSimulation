// Fill out your copyright notice in the Description page of Project Settings.

using System.IO; //for opencv
using UnrealBuildTool;

public class BoatSim : ModuleRules
{
    public BoatSim(ReadOnlyTargetRules Target) : base(Target)
	{
		/*PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });

		PrivateDependencyModuleNames.AddRange(new string[] {  });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true*/

		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { });
		// Uncomment if you are using Slate UI
		
        //PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
        // Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");
		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true

		PublicDependencyModuleNames.AddRange(new string[] { "RHI", "RenderCore", "Media", "MediaAssets" });
        //LoadOpenCV(Target);
	}
}
