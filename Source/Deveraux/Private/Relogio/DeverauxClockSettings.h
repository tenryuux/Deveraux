// Projeto Deveraux - configuracao do relogio (Project Settings > Game > Deveraux Relogio)

#pragma once

#include "CoreMinimal.h"
#include "DeverauxGameClock.h"
#include "Engine/DeveloperSettings.h"

#include "DeverauxClockSettings.generated.h"

/** Valores iniciais do relogio. Editados em Project Settings, ficam no Config/DefaultGame.ini. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Deveraux Relogio"))
class UDeverauxClockSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Quantas vezes o tempo de jogo corre mais rapido que o real. 12 = 1 dia de jogo em 2 h reais. */
	UPROPERTY(Config, EditAnywhere, Category = "Relogio", meta = (ClampMin = "1", ClampMax = "3600"))
	int32 TimeScale = FDeverauxGameClock::DefaultTimeScale;

	/** Hora em que o dia 1 comeca num jogo novo. */
	UPROPERTY(Config, EditAnywhere, Category = "Relogio", meta = (ClampMin = "0", ClampMax = "23"))
	int32 StartHour = 8;
};
