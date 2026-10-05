using UnrealBuildTool;

public class ActionRPGEditorTarget : TargetRules
{
	public ActionRPGEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;

		// See ActionRPG.Target.cs: pin the VS 2026 toolchain.
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			WindowsPlatform.Compiler = WindowsCompiler.VisualStudio2026;
		}

		ExtraModuleNames.Add("ActionRPG");
	}
}
