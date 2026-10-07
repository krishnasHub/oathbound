using UnrealBuildTool;

public class OathboundTarget : TargetRules
{
	public OathboundTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;

		// This machine has VS 2022 (MSVC 14.38/14.39: too old / banned for UE 5.8) and VS 2026 (14.5x).
		// Pin the compiler so UBT can't drift onto the broken toolchain.
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			WindowsPlatform.Compiler = WindowsCompiler.VisualStudio2026;
		}

		ExtraModuleNames.Add("Oathbound");
	}
}
