// Projeto Deveraux - modulo DeverauxSim

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FDeverauxSimModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogTemp, Log, TEXT("DeverauxSim carregado"));
	}
};

IMPLEMENT_MODULE(FDeverauxSimModule, DeverauxSim);
