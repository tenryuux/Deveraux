// Projeto Deveraux - plugin DeverauxGroom, modulo de editor
// Exporta grooms para Alembic. So existe no editor; o gravador do .abc nao usa biblioteca externa.

using UnrealBuildTool;

public class DeverauxGroomEditor : ModuleRules
{
	public DeverauxGroomEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });

		// HairStrandsCore: o groom asset e a descricao dos fios; MeshDescription: os atributos dela
		// ToolMenus, ContentBrowser, DesktopPlatform, Slate: o menu do clique direito, o dialogo de salvar e o aviso
		PrivateDependencyModuleNames.AddRange(new string[] { "HairStrandsCore", "MeshDescription", "UnrealEd", "ToolMenus", "ContentBrowser",
			"DesktopPlatform", "Slate", "SlateCore", "InputCore" });
	}
}
