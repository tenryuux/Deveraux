// Projeto Deveraux - modulo DeverauxBattle

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FDeverauxBattleModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogTemp, Log, TEXT("DeverauxBattle carregado"));
	}
};

IMPLEMENT_MODULE(FDeverauxBattleModule, DeverauxBattle);
