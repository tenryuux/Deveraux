// Projeto Deveraux - modulo DeverauxBattle
// Logica pura da batalha (porte do core TS). Depende so de Core, de proposito:
// sem CoreUObject, a logica fica separada da apresentacao por construcao.

using UnrealBuildTool;

public class DeverauxBattle : ModuleRules
{
	public DeverauxBattle(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
	}
}
