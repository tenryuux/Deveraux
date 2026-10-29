// Projeto Deveraux - ceu que anda com o relogio (modulo Deveraux)

#include "Ceu/DeverauxSkyController.h"

#include "Ceu/DeverauxSkySettings.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Relogio/DeverauxClockSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogDeverauxCeu, Log, All);

namespace DeverauxSkyControllerPrivate
{
	/** Menor giro que conta como movimento, para o relogio pausado nao refazer sombras. */
	constexpr double MinStepDegrees = 0.0001;

	/** Abaixo disto a atmosfera ja apagou a luz direta, entao a sombra sai. */
	constexpr double ShadowCutoffDegrees = -1.0;

	/** Ate quantas faixas de crepusculo abaixo do horizonte a luz continua acesa, colorindo o ceu. */
	constexpr double VisibleTwilightBands = 2.0;

	void UpdateLightState(UDirectionalLightComponent* Light, double ElevationDegrees, bool bAllowShadows, double TwilightDegrees)
	{
		if (!Light)
		{
			return;
		}

		const bool bVisible = ElevationDegrees > -VisibleTwilightBands * TwilightDegrees;
		const bool bShadows = bAllowShadows && ElevationDegrees > ShadowCutoffDegrees;
		if (Light->GetVisibleFlag() != bVisible)
		{
			Light->SetVisibility(bVisible);
		}
		if ((Light->CastShadows != 0) != bShadows)
		{
			Light->SetCastShadows(bShadows);
		}
	}

	FRotator LightRotation(const FVector& MapDirection, double NorthYawDegrees)
	{
		// A luz aponta do astro para o chao: o contrario da direcao do astro
		const FVector WorldDirection = FRotator(0.0, NorthYawDegrees, 0.0).RotateVector(MapDirection);
		return (-WorldDirection).Rotation();
	}
}

ADeverauxSkyController::ADeverauxSkyController()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Raiz"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	// A lua e a segunda luz da atmosfera: o ceu fica levemente azul a noite e ganha um disco
	MoonLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Lua"));
	MoonLight->SetupAttachment(Root);
	MoonLight->SetMobility(EComponentMobility::Movable);
	MoonLight->Intensity = 0.0f;
	MoonLight->bAtmosphereSunLight = true;
	MoonLight->AtmosphereSunLightIndex = 1;

	Exposure = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Exposicao"));
	Exposure->SetupAttachment(Root);
	Exposure->bUnbound = true;
	Exposure->Priority = 100.0f;
	Exposure->Settings.bOverride_AutoExposureBias = true;
}

void ADeverauxSkyController::BeginPlay()
{
	Super::BeginPlay();

	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		ClockSubsystem = GameInstance->GetSubsystem<UDeverauxClockSubsystem>();
	}

	FindSceneComponents();
	if (!Sun.IsValid() || !ClockSubsystem)
	{
		UE_LOG(LogDeverauxCeu, Log, TEXT("Mapa sem Directional Light: o ciclo dia/noite fica desligado aqui."));
		Exposure->bEnabled = false;
		SetActorTickEnabled(false);
		return;
	}

	ConfigureSceneComponents();
	UpdateSky(true);

	UE_LOG(LogDeverauxCeu, Log, TEXT("Ciclo dia/noite ligado: %s"), *GetDebugDescription());
}

void ADeverauxSkyController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateSky(false);
}

FString ADeverauxSkyController::GetDebugDescription() const
{
	const UDirectionalLightComponent* SunComponent = Sun.Get();
	const AActor* SunOwner = SunComponent ? SunComponent->GetOwner() : nullptr;
	const bool bSunShadows = SunComponent && SunComponent->GetVisibleFlag() && SunComponent->CastShadows;
	const bool bMoonShadows = MoonLight->GetVisibleFlag() && MoonLight->CastShadows;

	return FString::Printf(TEXT("sol do mapa: %s | sombra: %s | exposicao %.2f EV | sky light: %s | neblina: %s"),
		SunOwner ? *SunOwner->GetActorNameOrLabel() : TEXT("nenhum"),
		bSunShadows ? TEXT("sol") : (bMoonShadows ? TEXT("lua") : TEXT("nenhuma")),
		Exposure->bEnabled ? Exposure->Settings.AutoExposureBias : 0.0f,
		SkyLight.IsValid() ? TEXT("sim") : TEXT("nao"),
		Fog.IsValid() ? TEXT("sim") : TEXT("nao"));
}

void ADeverauxSkyController::FindSceneComponents()
{
	UDirectionalLightComponent* FirstDirectional = nullptr;
	TArray<UDirectionalLightComponent*> Directionals;

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor == this)
		{
			continue;
		}

		// O sol e a luz marcada como Atmosphere Sun Light 0; sem ela, a primeira luz direcional
		Actor->GetComponents(Directionals);
		for (UDirectionalLightComponent* Light : Directionals)
		{
			if (!Sun.IsValid() && Light->IsUsedAsAtmosphereSunLight() && Light->GetAtmosphereSunLightIndex() == 0)
			{
				Sun = Light;
			}
			if (!FirstDirectional)
			{
				FirstDirectional = Light;
			}
		}

		if (!SkyLight.IsValid())
		{
			SkyLight = Actor->FindComponentByClass<USkyLightComponent>();
		}
		if (!Fog.IsValid())
		{
			Fog = Actor->FindComponentByClass<UExponentialHeightFogComponent>();
		}
	}

	if (!Sun.IsValid())
	{
		Sun = FirstDirectional;
	}
}

void ADeverauxSkyController::ConfigureSceneComponents()
{
	UDirectionalLightComponent* SunComponent = Sun.Get();
	if (SunComponent->GetMobility() != EComponentMobility::Movable)
	{
		SunComponent->SetMobility(EComponentMobility::Movable);
	}
	SunComponent->SetAtmosphereSunLight(true);
	SunComponent->SetAtmosphereSunLightIndex(0);
	SunIntensity = SunComponent->Intensity;
	bSunCastsShadows = SunComponent->CastShadows != 0;

	if (USkyLightComponent* SkyLightComponent = SkyLight.Get())
	{
		if (SkyLightComponent->GetMobility() != EComponentMobility::Movable)
		{
			SkyLightComponent->SetMobility(EComponentMobility::Movable);
		}
		if (!SkyLightComponent->IsRealTimeCaptureEnabled())
		{
			SkyLightComponent->SetRealTimeCaptureEnabled(true);
			UE_LOG(LogDeverauxCeu, Log, TEXT("Sky Light passou a capturar o ceu em tempo real, para acompanhar o sol."));
		}
	}
	else
	{
		UE_LOG(LogDeverauxCeu, Warning, TEXT("Mapa sem Sky Light: a luz ambiente nao vai acompanhar o dia e a noite."));
	}

	if (const UExponentialHeightFogComponent* FogComponent = Fog.Get())
	{
		FogInscatteringOriginal = FogComponent->FogInscatteringLuminance;
		FogDirectionalOriginal = FogComponent->DirectionalInscatteringLuminance;
	}

	if (UMaterialParameterCollection* Collection = GetDefault<UDeverauxSkySettings>()->ParameterCollection.LoadSynchronous())
	{
		ParameterCollectionInstance = GetWorld()->GetParameterCollectionInstance(Collection);
	}
}

void ADeverauxSkyController::UpdateSky(bool bForceRotation)
{
	const UDeverauxSkySettings* Settings = GetDefault<UDeverauxSkySettings>();
	if (!Settings->bEnabled || !ClockSubsystem)
	{
		// Desligado no meio do jogo: o ceu fica onde esta, sem a exposicao da noite
		Exposure->bEnabled = false;
		return;
	}

	const double TimeOfDay = ClockSubsystem->GetClock().GetTimeOfDayFraction();
	const FDeverauxSkyState State = DeverauxSky::ComputeSkyState(TimeOfDay, Settings->MakeSunPathParams());

	ApplyLights(State, *Settings, bForceRotation);
	ApplyExposure(State, *Settings);
	ApplyFog(State, *Settings);
	ApplyMaterialParameters(TimeOfDay, State);
}

void ADeverauxSkyController::ApplyLights(const FDeverauxSkyState& State, const UDeverauxSkySettings& Settings, bool bForceRotation)
{
	using namespace DeverauxSkyControllerPrivate;

	UDirectionalLightComponent* SunComponent = Sun.Get();

	// Cada giro do sol refaz as sombras virtuais dele inteiras, entao ele so gira quando andou o passo
	const double Cosine = FMath::Clamp(FVector::DotProduct(State.SunDirection, AppliedSunDirection), -1.0, 1.0);
	const double MovedDegrees = FMath::RadiansToDegrees(FMath::Acos(Cosine));
	const double StepDegrees = FMath::Max(static_cast<double>(Settings.UpdateStepDegrees), MinStepDegrees);
	if (bForceRotation || !bHasAppliedRotation || MovedDegrees >= StepDegrees)
	{
		if (SunComponent)
		{
			SunComponent->SetWorldRotation(LightRotation(State.SunDirection, Settings.NorthYawDegrees));
		}
		MoonLight->SetWorldRotation(LightRotation(State.MoonDirection, Settings.NorthYawDegrees));
		AppliedSunDirection = State.SunDirection;
		bHasAppliedRotation = true;
	}

	const float MoonIntensity = SunIntensity * Settings.MoonIntensityFraction;
	if (MoonIntensity != AppliedMoonIntensity)
	{
		MoonLight->SetIntensity(MoonIntensity);
		AppliedMoonIntensity = MoonIntensity;
	}
	if (Settings.MoonColor != AppliedMoonColor)
	{
		MoonLight->SetLightColor(Settings.MoonColor);
		AppliedMoonColor = Settings.MoonColor;
	}

	// So uma luz faz sombra por vez: a mais alta. A troca acontece com as duas no horizonte,
	// onde a atmosfera ja deixou as duas fracas, e assim o sol virtual nunca e pago duas vezes
	const bool bSunIsHigher = State.SunElevationDegrees >= State.MoonElevationDegrees;
	UpdateLightState(SunComponent, State.SunElevationDegrees, bSunCastsShadows && bSunIsHigher, Settings.TwilightDegrees);
	UpdateLightState(MoonLight, State.MoonElevationDegrees, Settings.bMoonCastsShadows && !bSunIsHigher, Settings.TwilightDegrees);
}

void ADeverauxSkyController::ApplyExposure(const FDeverauxSkyState& State, const UDeverauxSkySettings& Settings)
{
	Exposure->bEnabled = Settings.bControlExposure;
	Exposure->Settings.AutoExposureBias = FMath::Lerp(Settings.NightExposureBias, Settings.DayExposureBias, static_cast<float>(State.DayAlpha));
}

void ADeverauxSkyController::ApplyFog(const FDeverauxSkyState& State, const UDeverauxSkySettings& Settings)
{
	UExponentialHeightFogComponent* FogComponent = Fog.Get();
	if (!FogComponent)
	{
		return;
	}

	// A noite a neblina recebe a mesma fracao de luz que a lua
	const float Scale = Settings.bControlFog ? FMath::Lerp(Settings.MoonIntensityFraction, 1.0f, static_cast<float>(State.DayAlpha)) : 1.0f;
	if (FMath::Abs(Scale - AppliedFogScale) > 0.005f)
	{
		FogComponent->SetFogInscatteringColor(FogInscatteringOriginal * Scale);
		FogComponent->SetDirectionalInscatteringColor(FogDirectionalOriginal * Scale);
		AppliedFogScale = Scale;
	}
}

void ADeverauxSkyController::ApplyMaterialParameters(double TimeOfDayFraction, const FDeverauxSkyState& State)
{
	if (UMaterialParameterCollectionInstance* Instance = ParameterCollectionInstance.Get())
	{
		static const FName HourName(TEXT("DeverauxHora"));
		static const FName NightName(TEXT("DeverauxNoite"));
		Instance->SetScalarParameterValue(HourName, static_cast<float>(TimeOfDayFraction));
		Instance->SetScalarParameterValue(NightName, 1.0f - static_cast<float>(State.DayAlpha));
	}
}
