using UnrealBuildTool;

public class ActionRPG : ModuleRules
{
	public ActionRPG(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"Json",                     // game-data.json
			"ProceduralMeshComponent",  // terrain
			"Niagara",                  // hit / spell effects
			"Slate",
			"SlateCore",                // all UI is Slate, built in code
			"UMG",
			"RenderCore",               // GWhiteTexture for HUD triangles
			"AIModule"                  // AAIController possesses enemies so CharacterMovement runs
		});

		// Sub-folders are include roots so headers can be included by name.
		PublicIncludePaths.AddRange(new string[]
		{
			"ActionRPG",
			"ActionRPG/Core",
			"ActionRPG/World",
			"ActionRPG/Characters",
			"ActionRPG/Combat",
			"ActionRPG/Game",
			"ActionRPG/UI"
		});
	}
}
