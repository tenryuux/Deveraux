// Projeto Deveraux - ciclo dia/noite do mundo (modulo Deveraux)

#pragma once

#include "CoreMinimal.h"
#include "DeverauxSunPath.h"
#include "Subsystems/WorldSubsystem.h"

#include "DeverauxSkySubsystem.generated.h"

class ADeverauxSkyController;

/**
 * Ciclo dia/noite do mundo. Quando o jogo comeca, cria o controlador do ceu, que gira o
 * Directional Light do mapa como sol e acrescenta a lua. Mapa sem Directional Light fica como esta.
 *
 * Para a jogabilidade: IsNight, GetDayAlpha e GetSkyState seguem a hora do relogio e valem
 * em qualquer mapa, com ou sem sol.
 */
UCLASS()
class DEVERAUX_API UDeverauxSkySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Sol, lua e quanto ainda e dia, agora. */
	FDeverauxSkyState GetSkyState() const;

	/** 0 = noite, 1 = dia, com transicao suave no crepusculo. */
	UFUNCTION(BlueprintPure, Category = "Deveraux|Ceu")
	double GetDayAlpha() const;

	/** O sol esta abaixo do horizonte. */
	UFUNCTION(BlueprintPure, Category = "Deveraux|Ceu")
	bool IsNight() const;

	/** Altura do sol acima do horizonte, em graus. Negativa a noite. */
	UFUNCTION(BlueprintPure, Category = "Deveraux|Ceu")
	double GetSunElevationDegrees() const;

	/** O controlador criado neste mundo. Nulo se o ciclo esta desligado. */
	ADeverauxSkyController* GetController() const;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	UPROPERTY(Transient)
	TObjectPtr<ADeverauxSkyController> Controller;
};
