// Projeto Deveraux - modulo DeverauxEditor

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FDeverauxEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogTemp, Log, TEXT("DeverauxEditor carregado"));
	}
};

IMPLEMENT_MODULE(FDeverauxEditorModule, DeverauxEditor);
