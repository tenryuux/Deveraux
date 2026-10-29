// Projeto Deveraux - ceu que anda com o relogio (modulo Deveraux)

#include "Ceu/DeverauxSkyController.h"

#include "Camera/PlayerCameraManager.h"
#include "Ceu/DeverauxSkySettings.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Math/RotationMatrix.h"
#include "Relogio/DeverauxClockSubsystem.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogDeverauxCeu, Log, All);

namespace DeverauxSkyControllerPrivate
{
	/** Menor giro que conta como movimento, para o relogio pausado nao refazer sombras. */
	constexpr double MinStepDegrees = 0.0001;

	/** Abaixo disto a atmosfera ja apagou a luz direta, entao a sombra sai. */
	constexpr double ShadowCutoffDegrees = -1.0;

	/** Ate quantas faixas de crepusculo abaixo do horizonte a luz continua acesa, colorindo o ceu. */
	constexpr double VisibleTwilightBands = 2.0;

	constexpr double CentimetersPerKm = 100000.0;

	/**
	 * Distancia da imagem da lua, em km. Ela fica logo acima do topo das nuvens, na direcao da lua:
	 * as nuvens passam na frente, e a nevoa da atmosfera (perspectiva aerea), que cresce com a
	 * distancia e apagava a lua a 60 km, fica o mais fraca possivel.
	 */
	constexpr double MoonImageMinDistanceKm = 5.0;
	constexpr double MoonImageMaxDistanceKm = 60.0;
	constexpr double MoonAboveCloudsMarginKm = 1.0;

	/** Topo das nuvens quando o mapa nao tem nuvens volumetricas, em km (o padrao da Unreal: 5 + 10). */
	constexpr double DefaultCloudTopKm = 15.0;

	/** Lado do plano basico da engine, em cm. */
	constexpr double PlaneMeshSize = 100.0;

	/** Abaixo desta altura a imagem da lua some, para nao aparecer por baixo do horizonte. */
	constexpr double MoonImageHideDegrees = -3.0;

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

	// Imagem da lua: plano basico da engine, sem colisao, sombra nem luz indireta. Escondido ate ter material
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	MoonImage = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ImagemDaLua"));
	MoonImage->SetupAttachment(Root);
	MoonImage->SetMobility(EComponentMobility::Movable);
	MoonImage->SetStaticMesh(PlaneMesh.Object);
	MoonImage->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MoonImage->SetCastShadow(false);
	MoonImage->bAffectDistanceFieldLighting = false;
	MoonImage->bAffectDynamicIndirectLighting = false;
	MoonImage->SetHiddenInGame(true);
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
	const FString MoonText = !bHasMoonImage ? FString(TEXT("disco"))
		: (bMoonImageVisible ? FString::Printf(TEXT("imagem a %.1f km"), AppliedMoonDistanceKm) : FString(TEXT("imagem, abaixo do horizonte")));

	return FString::Printf(TEXT("sol do mapa: %s, %.2f lux | sombra: %s | lua: %s, %.2f lux | exposicao %.2f EV | sky light: %s | neblina: %s | nuvens: %s"),
		SunOwner ? *SunOwner->GetActorNameOrLabel() : TEXT("nenhum"),
		SunIntensity,
		bSunShadows ? TEXT("sol") : (bMoonShadows ? TEXT("lua") : TEXT("nenhuma")),
		*MoonText,
		MoonLight->Intensity,
		Exposure->bEnabled ? Exposure->Settings.AutoExposureBias : 0.0f,
		SkyLight.IsValid() ? TEXT("sim") : TEXT("nao"),
		Fog.IsValid() ? TEXT("sim") : TEXT("nao"),
		Clouds.IsValid() ? TEXT("sim") : TEXT("nao"));
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
		if (!Clouds.IsValid())
		{
			Clouds = Actor->FindComponentByClass<UVolumetricCloudComponent>();
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

	const UDeverauxSkySettings* Settings = GetDefault<UDeverauxSkySettings>();
	if (UMaterialParameterCollection* Collection = Settings->ParameterCollection.LoadSynchronous())
	{
		ParameterCollectionInstance = GetWorld()->GetParameterCollectionInstance(Collection);
	}

	if (UMaterialInterface* MoonMaterialAsset = Settings->MoonMaterial.LoadSynchronous())
	{
		// Uma copia so deste ator, para o codigo mandar a intensidade da lua ao material
		MoonImageMaterial = UMaterialInstanceDynamic::Create(MoonMaterialAsset, this);
		MoonImage->SetMaterial(0, MoonImageMaterial);
		bHasMoonImage = true;

		// A imagem toma o lugar do disco branco que a atmosfera desenha para a lua
		MoonLight->SetAtmosphereSunDiskColorScale(FLinearColor(0.0f, 0.0f, 0.0f));
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
	ApplyMoonImage(State, *Settings);
	ApplyExposure(State, *Settings);
	ApplyFog(State, *Settings);
	ApplyMaterialParameters(TimeOfDay, State);
}

void ADeverauxSkyController::ApplyLights(const FDeverauxSkyState& State, const UDeverauxSkySettings& Settings, bool bForceRotation)
{
	using namespace DeverauxSkyControllerPrivate;

	UDirectionalLightComponent* SunComponent = Sun.Get();
	if (SunComponent)
	{
		// Lida a cada quadro: mudar a intensidade do sol com o jogo rodando tambem muda a lua
		SunIntensity = SunComponent->Intensity;
	}

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

		// A imagem da lua brilha em proporcao a luz da lua. A exposicao da noite se adapta a essa
		// mesma luz, entao a lua aparece igual com o sol do mapa a 10 lux ou a 100 mil lux
		if (MoonImageMaterial)
		{
			static const FName MoonLuxName(TEXT("DeverauxLuaLux"));
			MoonImageMaterial->SetScalarParameterValue(MoonLuxName, MoonIntensity);
		}
	}
	if (Settings.MoonColor != AppliedMoonColor)
	{
		MoonLight->SetLightColor(Settings.MoonColor);
		AppliedMoonColor = Settings.MoonColor;
	}
	if (Settings.MoonAngularSizeDegrees != AppliedMoonAngle)
	{
		// O tamanho da fonte de luz e o tamanho do disco no ceu e a maciez da sombra da lua
		MoonLight->SetLightSourceAngle(Settings.MoonAngularSizeDegrees);
		AppliedMoonAngle = Settings.MoonAngularSizeDegrees;
	}

	// So uma luz faz sombra por vez: a mais alta. A troca acontece com as duas no horizonte,
	// onde a atmosfera ja deixou as duas fracas, e assim o sol virtual nunca e pago duas vezes
	const bool bSunIsHigher = State.SunElevationDegrees >= State.MoonElevationDegrees;
	UpdateLightState(SunComponent, State.SunElevationDegrees, bSunCastsShadows && bSunIsHigher, Settings.TwilightDegrees);
	UpdateLightState(MoonLight, State.MoonElevationDegrees, Settings.bMoonCastsShadows && !bSunIsHigher, Settings.TwilightDegrees);
}

void ADeverauxSkyController::ApplyMoonImage(const FDeverauxSkyState& State, const UDeverauxSkySettings& Settings)
{
	using namespace DeverauxSkyControllerPrivate;

	if (!bHasMoonImage)
	{
		return;
	}

	const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	const bool bVisible = Camera && State.MoonElevationDegrees > MoonImageHideDegrees;
	MoonImage->SetHiddenInGame(!bVisible);
	bMoonImageVisible = bVisible;
	if (!bVisible)
	{
		return;
	}

	// Sempre na direcao da lua a partir da camera, entao nao muda de tamanho nem de lugar quando o jogador anda
	const FVector Direction = FRotator(0.0, Settings.NorthYawDegrees, 0.0).RotateVector(State.MoonDirection);
	const FVector CameraLocation = Camera->GetCameraLocation();

	double DistanceKm = Settings.MoonImageDistanceKm;
	if (DistanceKm <= 0.0)
	{
		// Logo acima do topo das nuvens ao longo da direcao da lua. O chao do planeta fica na altura zero do mundo
		const UVolumetricCloudComponent* CloudComponent = Clouds.Get();
		const double CloudTopKm = CloudComponent ? CloudComponent->LayerBottomAltitude + CloudComponent->LayerHeight : DefaultCloudTopKm;
		const double CameraAltitudeKm = FMath::Max(0.0, CameraLocation.Z / CentimetersPerKm);
		DistanceKm = (CloudTopKm + MoonAboveCloudsMarginKm - CameraAltitudeKm) / FMath::Max(Direction.Z, 0.01);
	}
	DistanceKm = FMath::Clamp(DistanceKm, MoonImageMinDistanceKm, MoonImageMaxDistanceKm);
	AppliedMoonDistanceKm = DistanceKm;

	// Tamanho no mundo que da o tamanho angular pedido nessa distancia, com o plano virado para a camera
	const double Distance = DistanceKm * CentimetersPerKm;
	const double Size = 2.0 * Distance * FMath::Tan(FMath::DegreesToRadians(0.5 * Settings.MoonAngularSizeDegrees));
	MoonImage->SetWorldLocationAndRotation(CameraLocation + Direction * Distance,
		FRotationMatrix::MakeFromZX(-Direction, FVector(0.0, 0.0, 1.0)).Rotator());
	MoonImage->SetWorldScale3D(FVector(Size / PlaneMeshSize));
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
