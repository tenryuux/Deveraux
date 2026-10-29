// Projeto Deveraux - modulo DeverauxSim
// Camada de dados da simulacao. Depende so de Core, de proposito:
// sem CoreUObject, nenhum UObject compila aqui (regra estrutural do roadmap).

using UnrealBuildTool;

public class DeverauxSim : ModuleRules
{
	public DeverauxSim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
	}
}
