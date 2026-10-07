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
			"AIModule",                 // AAIController possesses enemies so CharacterMovement runs
			"NavigationSystem",         // runtime navmesh for click-to-move
			"TesseraCore",              // data, looks, assets (Plugins/Tessera)
			"TesseraWorld",             // world builder base, sky and day/night, navmesh
			"TesseraGameplay",          // characters, stats, combat, abilities, items, loot, feedback
			"Loom"                      // story engine (Plugins/Loom)
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
