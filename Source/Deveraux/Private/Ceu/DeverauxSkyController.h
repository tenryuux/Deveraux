// Projeto Deveraux - ceu que anda com o relogio (modulo Deveraux)

#pragma once

#include "CoreMinimal.h"
#include "DeverauxSunPath.h"
#include "GameFramework/Actor.h"

#include "DeverauxSkyController.generated.h"

class UDeverauxClockSubsystem;
class UDeverauxSkySettings;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UVolumetricCloudComponent;
class UMaterialInstanceDynamic;
class UMaterialParameterCollectionInstance;
class UPostProcessComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UStaticMeshComponent;

/** Ajustes das nuvens que o ceu aplica nos atores do mapa, vindos do Project Settings. */
struct FDeverauxCloudSettings
{
	bool bShadows = false;
	float ShadowStrength = 0.0f;
	float ShadowExtentKm = 0.0f;
	float ShadowResolutionScale = 0.0f;
	float ShadowAtmosphereStrength = 0.0f;
	float AtmosphereTraceScale = 0.0f;
	bool bSkyOcclusion = false;
	bool bLightShafts = false;
	float DensityScale = 0.0f;
	float CoverageOffset = 0.0f;
	float BottomOcclusion = 0.0f;

	bool Equals(const FDeverauxCloudSettings& Other) const
	{
		return bShadows == Other.bShadows && ShadowStrength == Other.ShadowStrength && ShadowExtentKm == Other.ShadowExtentKm
			&& ShadowResolutionScale == Other.ShadowResolutionScale && ShadowAtmosphereStrength == Other.ShadowAtmosphereStrength
			&& AtmosphereTraceScale == Other.AtmosphereTraceScale
			&& bSkyOcclusion == Other.bSkyOcclusion && bLightShafts == Other.bLightShafts && DensityScale == Other.DensityScale
			&& CoverageOffset == Other.CoverageOffset && BottomOcclusion == Other.BottomOcclusion;
	}
};

/** Neblina volumetrica da Exponential Height Fog: a do Project Settings, ou a original do mapa. */
struct FDeverauxVolumetricFogSettings
{
	bool bEnabled = false;
	float DistanceCm = 0.0f;
	float ExtinctionScale = 0.0f;
	float ScatteringDistribution = 0.0f;

	bool Equals(const FDeverauxVolumetricFogSettings& Other) const
	{
		return bEnabled == Other.bEnabled && DistanceCm == Other.DistanceCm && ExtinctionScale == Other.ExtinctionScale
			&& ScatteringDistribution == Other.ScatteringDistribution;
	}
};

/**
 * Faz o ceu andar com o relogio: gira o Directional Light do mapa como sol, cria e gira a lua,
 * e ajusta exposicao e neblina para a noite.
 *
 * Nao vai no mapa: o UDeverauxSkySubsystem cria um quando o jogo comeca. O sol, o Sky Light e a
 * neblina continuam sendo os do mapa, editados normalmente.
 */
UCLASS(NotPlaceable, Transient)
class ADeverauxSkyController : public AActor
{
	GENERATED_BODY()

public:
	ADeverauxSkyController();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Resumo para o comando Deveraux.Ceu.Info. */
	FString GetDebugDescription() const;

	/** O que o ceu fez com as nuvens, para o comando Deveraux.Ceu.Info. */
	FString GetCloudDescription() const;

	/** O que esta de fato nas luzes e na neblina agora (lido delas, nao do Project Settings), para o Deveraux.Ceu.Info. */
	FString GetLightDescription() const;

private:
	void FindSceneComponents();
	void ConfigureSceneComponents();
	void UpdateSky(bool bForceRotation);
	void ApplyLights(const FDeverauxSkyState& State, const UDeverauxSkySettings& Settings, bool bForceRotation);
	void ApplyExposure(const FDeverauxSkyState& State, const UDeverauxSkySettings& Settings);
	void ApplyFog(const FDeverauxSkyState& State, const UDeverauxSkySettings& Settings);
	void ApplyMaterialParameters(double TimeOfDayFraction, const FDeverauxSkyState& State);
	void ApplyMoonImage(const FDeverauxSkyState& State, const UDeverauxSkySettings& Settings);
	void ApplyClouds(const UDeverauxSkySettings& Settings);
	void ApplyVolumetricFog(const UDeverauxSkySettings& Settings);

	/** Cria a copia do material das nuvens quando ele ja carregou. Devolve true no quadro em que criou. */
	bool SetupCloudMaterial(UVolumetricCloudComponent* CloudComponent);

	UPROPERTY(VisibleAnywhere, Category = "Ceu")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Ceu")
	TObjectPtr<UDirectionalLightComponent> MoonLight;

	UPROPERTY(VisibleAnywhere, Category = "Ceu")
	TObjectPtr<UPostProcessComponent> Exposure;

	/** Imagem da lua: um plano virado para a camera, longe, atras das nuvens. So aparece com o material configurado. */
	UPROPERTY(VisibleAnywhere, Category = "Ceu")
	TObjectPtr<UStaticMeshComponent> MoonImage;

	/** Copia do material da lua que recebe DeverauxLuaLux (intensidade da luz) e DeverauxLuaAtmosfera (efeito do ar). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MoonImageMaterial;

	/** Copia do material das nuvens do mapa, com a densidade e a cobertura do Project Settings. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CloudMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UDeverauxClockSubsystem> ClockSubsystem;

	// Pecas do mapa, achadas no BeginPlay
	TWeakObjectPtr<UDirectionalLightComponent> Sun;
	TWeakObjectPtr<USkyLightComponent> SkyLight;
	TWeakObjectPtr<UExponentialHeightFogComponent> Fog;
	TWeakObjectPtr<UVolumetricCloudComponent> Clouds;
	TWeakObjectPtr<USkyAtmosphereComponent> Atmosphere;
	TWeakObjectPtr<UMaterialParameterCollectionInstance> ParameterCollectionInstance;

	/** Ate onde vai a tabela da perspectiva aerea da Unreal, em km. Mais longe, ela repete o ultimo valor. */
	double AerialPerspectiveDepthKm = 96.0;

	// Valores originais do mapa, que servem de base para a noite
	float SunIntensity = 0.0f;
	bool bSunCastsShadows = true;
	FLinearColor FogInscatteringOriginal = FLinearColor(0.0f, 0.0f, 0.0f);
	FLinearColor FogDirectionalOriginal = FLinearColor(0.0f, 0.0f, 0.0f);

	// O que ja foi aplicado, para nao refazer trabalho a cada quadro
	FVector AppliedSunDirection = FVector(0.0, 0.0, 0.0);
	FVector AppliedMoonDirection = FVector(0.0, 0.0, 0.0);
	double AppliedNorthYaw = 0.0;
	bool bHasAppliedRotation = false;
	float AppliedMoonIntensity = -1.0f;
	FLinearColor AppliedMoonColor = FLinearColor(-1.0f, -1.0f, -1.0f);
	float AppliedMoonAngle = -1.0f;
	float AppliedFogScale = -1.0f;
	double AppliedMoonDistanceKm = 0.0;
	FLinearColor AppliedMoonAtmosphere = FLinearColor(-1.0f, -1.0f, -1.0f);
	double MoonAerialTransmittance = 1.0;

	/** O que falta no material da lua, para o Info e o log. Vazio = tudo certo. */
	FString MoonMaterialProblems;

	// Nuvens: o que ja foi aplicado e os valores originais do material delas
	FDeverauxCloudSettings AppliedClouds;
	bool bHasAppliedClouds = false;
	float CloudDensityOriginal = 0.0f;
	float CloudCoverageOriginal = 0.0f;
	bool bHasCloudDensity = false;
	bool bHasCloudCoverage = false;
	bool bCheckedCloudMaterial = false;

	// Neblina volumetrica: como o mapa veio (volta a isso quando desligada) e o que ja foi aplicado
	FDeverauxVolumetricFogSettings VolumetricFogOriginal;
	FDeverauxVolumetricFogSettings AppliedVolumetricFog;
	bool bHasAppliedVolumetricFog = false;

	bool bMoonImageVisible = false;
	bool bHasMoonImage = false;
};
