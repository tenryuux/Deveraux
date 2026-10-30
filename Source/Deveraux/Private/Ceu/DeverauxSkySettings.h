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
	 * O ceu cuida das nuvens: sombra, raios na tela, luz do ceu e o material das nuvens do mapa (Volumetric
	 * Cloud). Desligado, o ceu nao mexe em nada disso e vale o que estiver nos atores do mapa (ao dar Play de novo).
	 * A neblina volumetrica (Raios de luz) tem a propria chave.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Nuvens")
	bool bControlClouds = true;

	/**
	 * As nuvens fazem sombra no chao, com o sol e com a lua: quando uma nuvem passa na frente, o chao escurece.
	 * A mesma sombra desenha raios de luz no ar (Cast Cloud Shadows nas duas luzes).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Nuvens", meta = (EditCondition = "bControlClouds"))
	bool bCloudShadows = true;

	/** Forca da sombra das nuvens. 1 = cada nuvem tapa toda a luz que a densidade dela tapa. */
	UPROPERTY(Config, EditAnywhere, Category = "Nuvens", meta = (EditCondition = "bControlClouds", ClampMin = "0", ClampMax = "1"))
	float CloudShadowStrength = 1.0f;

	/**
	 * Raio, em km, em volta da camera onde as nuvens fazem sombra. A sombra e uma imagem de tamanho fixo
	 * esticada nesse raio: quanto MAIOR o raio, mais borrada a sombra no chao. Com 25 km, cada ponto da sombra
	 * cobre uns 100 m (resolucao 1) ou 50 m (resolucao 2); no padrao da Unreal (150 km, resolucao 1), quase 600 m.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Nuvens", meta = (EditCondition = "bControlClouds", ClampMin = "5", ClampMax = "200"))
	float CloudShadowExtentKm = 25.0f;

	/**
	 * Resolucao da sombra das nuvens (Cloud Shadow Map Resolution Scale): 1 = 512 pontos de lado, 2 = 1024.
	 * E a parte mais cara do ceu: com 2 a sombra custa umas 4 vezes mais que com 1 (medido no jogo: perto de
	 * 4,5 ms contra pouco mais de 1 ms). No crepusculo o sol e a lua desenham uma cada um.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Nuvens", meta = (EditCondition = "bControlClouds", ClampMin = "0.5", ClampMax = "4"))
	float CloudShadowResolutionScale = 1.0f;

	/**
	 * Forca dos raios que a sombra das nuvens desenha no ar da atmosfera, de longe (Cloud Shadow on Atmosphere
	 * Strength). 0 = sem esses raios. Se o ceu em volta do sol tremer, abaixe isto.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Nuvens", meta = (EditCondition = "bControlClouds", ClampMin = "0", ClampMax = "1"))
	float CloudShadowAtmosphereStrength = 1.0f;

	/**
	 * Quantos passos a atmosfera da em cada direcao do ceu para desenhar a sombra das nuvens no ar (Trace Sample
	 * Count Scale do Sky Atmosphere). Com poucos passos essa sombra sai em faixas e aneis duros no ceu. 1 = o
	 * padrao da Unreal. Nao muda a sombra no chao nem a neblina perto do jogador. Mais alto custa mais.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Nuvens", meta = (EditCondition = "bControlClouds", ClampMin = "1", ClampMax = "8"))
	float AtmosphereShadowQuality = 4.0f;

	/** As nuvens tapam a luz do ceu (Cloud Ambient Occlusion no Sky Light): com o ceu nublado, o chao fica mais escuro. */
	UPROPERTY(Config, EditAnywhere, Category = "Nuvens", meta = (EditCondition = "bControlClouds"))
	bool bCloudSkyOcclusion = true;

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

	/**
	 * Raios de luz no ar, perto do jogador: liga a neblina volumetrica (Volumetric Fog) na Exponential Height Fog
	 * do mapa. O sol, a lua, o terreno, as arvores e as nuvens fazem sombra dentro dela, entao os raios existem
	 * no mundo e aparecem olhando para qualquer lado, com o astro na tela ou nao. Desligado, a neblina volta a
	 * ser como no mapa.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Raios de luz")
	bool bVolumetricFog = true;

	/**
	 * Ate onde vai a neblina volumetrica, em metros (o padrao da Unreal e 60). Mais longe pega a sombra das
	 * nuvens, que e grande; longe demais perde detalhe perto da camera.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Raios de luz", meta = (EditCondition = "bVolumetricFog", ClampMin = "50", ClampMax = "3000"))
	float VolumetricFogDistanceMeters = 300.0f;

	/** Multiplica a densidade da neblina so na parte volumetrica (Extinction Scale). Mais alto = raios mais fortes e ar mais carregado. */
	UPROPERTY(Config, EditAnywhere, Category = "Raios de luz", meta = (EditCondition = "bVolumetricFog", ClampMin = "0.1", ClampMax = "10"))
	float VolumetricFogDensityScale = 1.0f;

	/**
	 * Para onde a neblina espalha a luz (Scattering Distribution). 0 = igual para todo lado: raios visiveis de
	 * lado e de costas para o sol. Perto de 0,9 = raios fortes olhando para o sol e quase nada de lado.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Raios de luz", meta = (EditCondition = "bVolumetricFog", ClampMin = "0", ClampMax = "0.9"))
	float VolumetricFogScattering = 0.3f;

	/**
	 * Raios na tela (Light Shaft Bloom nas duas luzes): riscos de luz saindo do sol e da lua. E um efeito da
	 * imagem, nao do mundo: so existe com o astro na tela e treme com as nuvens passando na frente. Ligado, o
	 * brilho do sol dentro deles e limitado para tremer menos. Os raios no ar (acima) nao dependem disto.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Raios de luz", meta = (EditCondition = "bControlClouds"))
	bool bLightShafts = false;

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
