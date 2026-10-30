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

private:
	void FindSceneComponents();
	void ConfigureSceneComponents();
	void UpdateSky(bool bForceRotation);
	void ApplyLights(const FDeverauxSkyState& State, const UDeverauxSkySettings& Settings, bool bForceRotation);
	void ApplyExposure(const FDeverauxSkyState& State, const UDeverauxSkySettings& Settings);
	void ApplyFog(const FDeverauxSkyState& State, const UDeverauxSkySettings& Settings);
	void ApplyMaterialParameters(double TimeOfDayFraction, const FDeverauxSkyState& State);
	void ApplyMoonImage(const FDeverauxSkyState& State, const UDeverauxSkySettings& Settings);

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
	bool bHasAppliedRotation = false;
	float AppliedMoonIntensity = -1.0f;
	FLinearColor AppliedMoonColor = FLinearColor(-1.0f, -1.0f, -1.0f);
	float AppliedMoonAngle = -1.0f;
	float AppliedFogScale = -1.0f;
	double AppliedMoonDistanceKm = 0.0;
	FLinearColor AppliedMoonAtmosphere = FLinearColor(-1.0f, -1.0f, -1.0f);
	double MoonAerialTransmittance = 1.0;
	bool bMoonImageVisible = false;
	bool bHasMoonImage = false;
};
