// Projeto Deveraux - modulo DeverauxEditor
// Ferramentas de editor (visualizadores, sweeps, validadores). So existe no editor.

using UnrealBuildTool;

public class DeverauxEditor : ModuleRules
{
	public DeverauxEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "Deveraux", "DeverauxSim", "DeverauxBattle" });
	}
}
