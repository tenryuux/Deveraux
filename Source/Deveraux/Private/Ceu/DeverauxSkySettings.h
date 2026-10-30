// Projeto Deveraux - configuracao do ceu (Project Settings > Game > Deveraux Ceu)

#pragma once

#include "CoreMinimal.h"
#include "DeverauxSunPath.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPtr.h"

#include "DeverauxSkySettings.generated.h"

class UMaterialInterface;
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

	/** Tamanho da lua no ceu, em graus (a de verdade tem 0,5). Vale para o disco e para a imagem, e tambem amacia a sombra da lua. */
	UPROPERTY(Config, EditAnywhere, Category = "Lua", meta = (ClampMin = "0.1", ClampMax = "10"))
	float MoonAngularSizeDegrees = 1.5f;

	/**
	 * Opcional. Material com a imagem da lua: Unlit, Additive, Two Sided e, em Translucency, Apply Fogging,
	 * Apply Cloud Fogging e Compute Fog Per Pixel ligados. Additive soma a luz da lua ao ceu, entao o halo e
	 * o lado escuro se misturam com a atmosfera; as nuvens e a neblina passam na frente. Sem material, a lua
	 * e um disco branco igual ao do sol. Lido quando o jogo comeca. Multiplique a cor emissiva (com a
	 * imagem ja multiplicada pelo proprio alfa) pelos dois parametros que o codigo preenche:
	 * DeverauxLuaLux (escalar), a intensidade da luz da lua, para o brilho acompanhar a exposicao da noite;
	 * DeverauxLuaAtmosfera (vetor), o que a atmosfera faz com a lua: mais fraca e alaranjada perto do horizonte.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Lua")
	TSoftObjectPtr<UMaterialInterface> MoonMaterial;

	/**
	 * Forca da atmosfera na imagem da lua (parametro DeverauxLuaAtmosfera). 1 = igual a luz da lua: no alto
	 * quase nao muda, perto do horizonte fica mais fraca e alaranjada. 0 = a imagem nao muda com a altura.
	 * 2 = o dobro de ar. A neblina e as nuvens passam na frente de qualquer jeito.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Lua", meta = (ClampMin = "0", ClampMax = "4"))
	float MoonAtmosphereStrength = 1.0f;

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

	/**
	 * O ceu cuida das nuvens: sombra, raios de luz, luz do ceu e o material das nuvens do mapa (Volumetric
	 * Cloud). Desligado, o ceu nao mexe em nada disso e vale o que estiver nos atores do mapa (ao dar Play de novo).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Nuvens")
	bool bControlClouds = true;

	/**
	 * As nuvens fazem sombra no chao, com o sol e com a lua: quando uma nuvem passa na frente, o chao escurece.
	 * A mesma sombra desenha raios de luz na atmosfera (Cast Cloud Shadows nas duas luzes).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Nuvens", meta = (EditCondition = "bControlClouds"))
	bool bCloudShadows = true;

	/** Forca da sombra das nuvens. 1 = cada nuvem tapa toda a luz que a densidade dela tapa. */
	UPROPERTY(Config, EditAnywhere, Category = "Nuvens", meta = (EditCondition = "bControlClouds", ClampMin = "0", ClampMax = "1"))
	float CloudShadowStrength = 1.0f;

	/** Raio, em km, em volta da camera onde as nuvens fazem sombra. Menor = sombra mais nitida (o padrao da Unreal e 150). */
	UPROPERTY(Config, EditAnywhere, Category = "Nuvens", meta = (EditCondition = "bControlClouds", ClampMin = "10", ClampMax = "200"))
	float CloudShadowExtentKm = 50.0f;

	/** As nuvens tapam a luz do ceu (Cloud Ambient Occlusion no Sky Light): com o ceu nublado, o chao fica mais escuro. */
	UPROPERTY(Config, EditAnywhere, Category = "Nuvens", meta = (EditCondition = "bControlClouds"))
	bool bCloudSkyOcclusion = true;

	/** Raios de luz saindo do sol e da lua quando nuvens ou montanhas tapam parte deles (Light Shaft Bloom nas duas luzes). */
	UPROPERTY(Config, EditAnywhere, Category = "Nuvens", meta = (EditCondition = "bControlClouds"))
	bool bLightShafts = true;

	/**
	 * Multiplica a densidade das nuvens (Cloud_GlobalDensity, do material padrao da Unreal). Mais densas, elas
	 * tapam o sol e a lua e ficam mais escuras por baixo. 1 = como o material veio.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Nuvens", meta = (EditCondition = "bControlClouds", ClampMin = "0", ClampMax = "10"))
	float CloudDensityScale = 2.0f;

	/** Soma na cobertura do ceu (Layout_GlobalCoverage, do material padrao). Positivo = mais nuvens; negativo = menos. */
	UPROPERTY(Config, EditAnywhere, Category = "Nuvens", meta = (EditCondition = "bControlClouds", ClampMin = "-1", ClampMax = "1"))
	float CloudCoverageOffset = 0.0f;

	/**
	 * Quanto a base das nuvens deixa de receber a luz do ceu, de 0 a 1 (o padrao da Unreal e 0,5). Mais alto,
	 * mais escura por baixo. So vale se o material das nuvens nao define a propria oclusao.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Nuvens", meta = (EditCondition = "bControlClouds", ClampMin = "0", ClampMax = "1"))
	float CloudBottomOcclusion = 0.9f;

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
