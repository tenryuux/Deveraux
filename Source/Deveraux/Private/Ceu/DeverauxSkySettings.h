// Projeto Deveraux - configuracao do ceu (Project Settings > Game > Deveraux Ceu)

#pragma once

#include "CoreMinimal.h"
#include "DeverauxSunPath.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPtr.h"

#include "DeverauxSkySettings.generated.h"

class UMaterialParameterCollection;

/**
 * Ciclo dia/noite. Editado em Project Settings, fica no Config/DefaultGame.ini.
 * Com o jogo rodando no editor, as mudancas aparecem na hora.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Deveraux Ceu"))
class UDeverauxSkySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Liga o ciclo dia/noite nos mapas que tem um Directional Light. */
	UPROPERTY(Config, EditAnywhere, Category = "Ceu")
	bool bEnabled = true;

	/** Latitude do mapa, em graus. Com declinacao 0, o sol do meio-dia fica a 90 - latitude graus de altura. Negativo = hemisferio sul, sol ao norte. */
	UPROPERTY(Config, EditAnywhere, Category = "Caminho do sol", meta = (ClampMin = "-80", ClampMax = "80"))
	double LatitudeDegrees = 30.0;

	/** Declinacao do sol (a estacao do ano), em graus. 0 = dia de 12 h: nasce as 06:00 e se poe as 18:00. Positivo = dias mais longos. */
	UPROPERTY(Config, EditAnywhere, Category = "Caminho do sol", meta = (ClampMin = "-23.44", ClampMax = "23.44"))
	double DeclinationDegrees = 0.0;

	/** Para onde fica o norte no mapa, como giro (yaw) em graus. 0 = norte no eixo X. */
	UPROPERTY(Config, EditAnywhere, Category = "Caminho do sol", meta = (ClampMin = "-180", ClampMax = "180"))
	double NorthYawDegrees = 0.0;

	/** Faixa do crepusculo, em graus de altura do sol abaixo e acima do horizonte. */
	UPROPERTY(Config, EditAnywhere, Category = "Caminho do sol", meta = (ClampMin = "1", ClampMax = "18"))
	double TwilightDegrees = 6.0;

	/** Horas que a lua nasce depois do por do sol. 0 = nasce quando o sol se poe. */
	UPROPERTY(Config, EditAnywhere, Category = "Lua", meta = (ClampMin = "0", ClampMax = "12"))
	double MoonDelayHours = 0.0;

	/** Intensidade da lua como fracao da do sol. 0,05 = 20 vezes mais fraca. */
	UPROPERTY(Config, EditAnywhere, Category = "Lua", meta = (ClampMin = "0", ClampMax = "1"))
	float MoonIntensityFraction = 0.05f;

	UPROPERTY(Config, EditAnywhere, Category = "Lua")
	FLinearColor MoonColor = FLinearColor(0.55f, 0.65f, 1.0f);

	/** Sombra da lua. So uma luz faz sombra por vez: a lua faz sombra quando esta mais alta que o sol. */
	UPROPERTY(Config, EditAnywhere, Category = "Lua")
	bool bMoonCastsShadows = true;

	/** Controla a compensacao de exposicao abaixo. Desligue se outra coisa cuidar da exposicao. */
	UPROPERTY(Config, EditAnywhere, Category = "Exposicao")
	bool bControlExposure = true;

	/** Compensacao de exposicao de dia, em stops (EV). */
	UPROPERTY(Config, EditAnywhere, Category = "Exposicao", meta = (ClampMin = "-6", ClampMax = "6"))
	float DayExposureBias = 0.0f;

	/** Compensacao de exposicao de noite, em stops. Negativo deixa a noite mais escura que o dia mesmo com o olho se adaptando. */
	UPROPERTY(Config, EditAnywhere, Category = "Exposicao", meta = (ClampMin = "-6", ClampMax = "6"))
	float NightExposureBias = -2.0f;

	/** Escurece a cor da neblina a noite, na mesma proporcao da luz da lua. */
	UPROPERTY(Config, EditAnywhere, Category = "Neblina")
	bool bControlFog = true;

	/** Quanto o sol precisa andar, em graus, para a luz ser girada de novo. Cada giro refaz as sombras virtuais do sol. 0 = todo quadro. */
	UPROPERTY(Config, EditAnywhere, Category = "Desempenho", meta = (ClampMin = "0", ClampMax = "5"))
	float UpdateStepDegrees = 0.25f;

	/** Opcional. Recebe DeverauxHora (fracao do dia, de 0 a 1) e DeverauxNoite (0 = dia, 1 = noite) para materiais. */
	UPROPERTY(Config, EditAnywhere, Category = "Materiais")
	TSoftObjectPtr<UMaterialParameterCollection> ParameterCollection;

	FDeverauxSunPathParams MakeSunPathParams() const
	{
		FDeverauxSunPathParams Params;
		Params.LatitudeDegrees = LatitudeDegrees;
		Params.DeclinationDegrees = DeclinationDegrees;
		Params.TwilightDegrees = TwilightDegrees;
		Params.MoonDelayHours = MoonDelayHours;
		return Params;
	}
};
