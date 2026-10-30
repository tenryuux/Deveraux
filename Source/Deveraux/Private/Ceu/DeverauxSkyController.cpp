// Projeto Deveraux - ceu que anda com o relogio (modulo Deveraux)

#include "Ceu/DeverauxSkyController.h"

#include "Camera/PlayerCameraManager.h"
#include "Ceu/DeverauxSkySettings.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "DeverauxAtmosphere.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Math/RotationMatrix.h"
#include "Relogio/DeverauxClockSubsystem.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"

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
	 * Distancia da imagem da lua, em km. Ela fica logo atras do topo das nuvens, na direcao da lua:
	 * todas as nuvens passam na frente, e a nevoa da atmosfera (perspectiva aerea), que apagava a
	 * lua a 60 km, fica o mais fraca possivel. Perto do horizonte esse ponto fica bem longe; alem do
	 * maximo nem as nuvens sao desenhadas.
	 */
	constexpr double MoonImageMinDistanceKm = 5.0;
	constexpr double MoonImageMaxDistanceKm = 200.0;
	constexpr double MoonAboveCloudsMarginKm = 1.0;

	/** Topo das nuvens quando o mapa nao tem nuvens volumetricas, em km (o padrao da Unreal: 5 + 10). */
	constexpr double DefaultCloudTopKm = 15.0;

	/** Lado do plano basico da engine, em cm. */
	constexpr double PlaneMeshSize = 100.0;

	/** Abaixo desta altura a imagem da lua some, para nao aparecer por baixo do horizonte. */
	constexpr double MoonImageHideDegrees = -3.0;

	/** Com menos luz que isto passando pelo ar, a imagem some: a lua ja se pos atras do horizonte. */
	constexpr double MinVisibleTint = 0.01;

	/** Amostras para estimar a perspectiva aerea entre a camera e a imagem da lua. */
	constexpr int32 AerialPerspectiveSamples = 32;

	/** A compensacao da perspectiva aerea nunca multiplica a lua por mais que 1 / isto. */
	constexpr double MinCompensatedTransmittance = 0.1;

	/**
	 * Brilho maximo (depois da exposicao) que entra nos raios na tela. O disco do sol e milhares de vezes mais
	 * forte que o ceu em volta: com nuvens passando na frente ele acende e apaga a cada quadro, e os raios
	 * piscavam junto. Limitado, os raios saem do ceu claro em volta do sol, que muda devagar.
	 */
	constexpr float LightShaftMaxBrightness = 16.0f;

	FVector ToVector(const FLinearColor& Color)
	{
		return FVector(Color.R, Color.G, Color.B);
	}

	/** Os numeros do Sky Atmosphere do mapa, como a Unreal usa. */
	FDeverauxAtmosphereParams MakeAtmosphereParams(const USkyAtmosphereComponent& Atmosphere)
	{
		FDeverauxAtmosphereParams Params;
		Params.GroundRadiusKm = Atmosphere.BottomRadius;
		Params.AtmosphereHeightKm = Atmosphere.AtmosphereHeight;
		Params.RayleighScattering = ToVector(Atmosphere.RayleighScattering) * Atmosphere.RayleighScatteringScale;
		Params.RayleighScaleHeightKm = Atmosphere.RayleighExponentialDistribution;
		Params.MieExtinction = ToVector(Atmosphere.MieScattering) * Atmosphere.MieScatteringScale
			+ ToVector(Atmosphere.MieAbsorption) * Atmosphere.MieAbsorptionScale;
		Params.MieScaleHeightKm = Atmosphere.MieExponentialDistribution;
		Params.OzoneAbsorption = ToVector(Atmosphere.OtherAbsorption) * Atmosphere.OtherAbsorptionScale;
		Params.OzoneTipAltitudeKm = Atmosphere.OtherTentDistribution.TipAltitude;
		Params.OzoneTipValue = Atmosphere.OtherTentDistribution.TipValue;
		Params.OzoneWidthKm = Atmosphere.OtherTentDistribution.Width;
		return Params;
	}

	/** Altura (Z, em cm) do chao no topo do planeta, que e onde o mapa fica. As nuvens contam a altitude dai. */
	double PlanetTopZ(const USkyAtmosphereComponent* Atmosphere)
	{
		if (!Atmosphere)
		{
			return 0.0;
		}
		switch (Atmosphere->TransformMode)
		{
		case ESkyAtmosphereTransformMode::PlanetTopAtComponentTransform:
			return Atmosphere->GetComponentLocation().Z;
		case ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform:
			return Atmosphere->GetComponentLocation().Z + Atmosphere->BottomRadius * CentimetersPerKm;
		default:
			return 0.0;
		}
	}

	/** Quanto de uma cor chega comparado com o zenite, elevado a forca pedida (2 = o dobro de ar). */
	double RelativeTransmittance(double Toward, double Zenith, double Strength)
	{
		if (Zenith <= 1.e-12)
		{
			return 1.0;
		}
		return FMath::Pow(FMath::Clamp(Toward / Zenith, 0.0, 1.0), Strength);
	}

	/** Le uma caixa de marcar do material pelo nome que a Unreal usa. Sem achar o nome, fica o padrao. */
	bool ReadMaterialFlag(const UMaterial& Material, const TCHAR* Name, bool bDefault)
	{
		const FBoolProperty* Property = FindFProperty<FBoolProperty>(UMaterial::StaticClass(), Name);
		return Property ? Property->GetPropertyValue_InContainer(&Material) : bDefault;
	}

	/**
	 * O que falta no material da lua para ela somar luz ao ceu, com as nuvens e a neblina na frente.
	 * Em Masked, tudo com um pouco de opacidade (o halo, o lado escuro) vira um disco solido de borda dura.
	 * Vazio quando esta tudo certo.
	 */
	FString DescribeMoonMaterialProblems(const UMaterialInterface& Material)
	{
		const EBlendMode BlendMode = Material.GetBlendMode();
		if (BlendMode != BLEND_Additive && BlendMode != BLEND_Translucent)
		{
			return TEXT("use Blend Mode Additive (em Masked o halo vira um disco solido)");
		}

		const UMaterial* BaseMaterial = Material.GetMaterial();
		if (!BaseMaterial)
		{
			return FString();
		}

		TArray<FString> Problems;
		if (ReadMaterialFlag(*BaseMaterial, TEXT("bIsSky"), false))
		{
			Problems.Add(TEXT("desligue Is Sky"));
		}
		TArray<FString> Missing;
		if (!ReadMaterialFlag(*BaseMaterial, TEXT("bUseTranslucencyVertexFog"), true))
		{
			Missing.Add(TEXT("Apply Fogging"));
		}
		if (!ReadMaterialFlag(*BaseMaterial, TEXT("bApplyCloudFogging"), true))
		{
			Missing.Add(TEXT("Apply Cloud Fogging"));
		}
		if (!ReadMaterialFlag(*BaseMaterial, TEXT("bComputeFogPerPixel"), true))
		{
			Missing.Add(TEXT("Compute Fog Per Pixel"));
		}
		if (Missing.Num() > 0)
		{
			Problems.Add(FString::Printf(TEXT("ligue em Translucency: %s"), *FString::Join(Missing, TEXT(", "))));
		}
		return FString::Join(Problems, TEXT("; "));
	}

	/**
	 * Chama um setter de Blueprint da Unreal pelo nome, como o Blueprint faz. Os setters das nuvens nao sao
	 * exportados para outros modulos, e so eles atualizam tudo o que a Unreal guarda por dentro (na 5.8 o
	 * material das nuvens fica em dois ponteiros, e o desenho usa o que so o setter muda).
	 * Devolve false se a funcao nao existe ou mudou de formato.
	 */
	template <typename TParams>
	bool CallBlueprintSetter(UObject& Object, const TCHAR* FunctionName, TParams Params)
	{
		UFunction* Function = Object.FindFunction(FName(FunctionName));
		if (!Function || Function->ParmsSize != sizeof(TParams))
		{
			return false;
		}
		Object.ProcessEvent(Function, &Params);
		return true;
	}

	/** Muda um campo pelo setter de Blueprint da Unreal; se nao der, direto no campo, e o componente e refeito. */
	template <typename TValue>
	void SetBySetter(UActorComponent& Component, const TCHAR* SetterName, TValue& Field, TValue Value)
	{
		struct FParams
		{
			TValue NewValue;
		};
		if (!CallBlueprintSetter(Component, SetterName, FParams{ Value }) || Field != Value)
		{
			Field = Value;
			Component.MarkRenderStateDirty();
		}
	}

	/** Liga ou desliga a neblina volumetrica pelo setter da Unreal; se nao der, direto no campo. */
	void SetVolumetricFogEnabled(UExponentialHeightFogComponent& FogComponent, bool bEnabled)
	{
		struct FParams
		{
			bool bNewValue;
		};
		if (!CallBlueprintSetter(FogComponent, TEXT("SetVolumetricFog"), FParams{ bEnabled }) || (FogComponent.bEnableVolumetricFog != 0) != bEnabled)
		{
			FogComponent.bEnableVolumetricFog = bEnabled;
			FogComponent.MarkRenderStateDirty();
		}
	}

	/** Valor de um console variable da Unreal, para o Info. "?" se ele nao existe nesta versao. */
	FString ConsoleValue(const TCHAR* Name)
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable ? Variable->GetString() : FString(TEXT("?"));
	}

	double AngleBetweenDegrees(const FVector& A, const FVector& B)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(A, B), -1.0, 1.0)));
	}

	/** Parametros do material padrao de nuvens da Unreal (5.5 em diante). Outro material pode nao ter. */
	const FName& CloudDensityParameter()
	{
		static const FName Name(TEXT("Cloud_GlobalDensity"));
		return Name;
	}

	const FName& CloudCoverageParameter()
	{
		static const FName Name(TEXT("Layout_GlobalCoverage"));
		return Name;
	}

	FDeverauxCloudSettings MakeCloudSettings(const UDeverauxSkySettings& Settings)
	{
		FDeverauxCloudSettings Clouds;
		Clouds.bShadows = Settings.bCloudShadows;
		Clouds.ShadowStrength = FMath::Clamp(Settings.CloudShadowStrength, 0.0f, 1.0f);
		Clouds.ShadowExtentKm = FMath::Clamp(Settings.CloudShadowExtentKm, 5.0f, 200.0f);
		Clouds.ShadowResolutionScale = FMath::Clamp(Settings.CloudShadowResolutionScale, 0.5f, 4.0f);
		Clouds.ShadowAtmosphereStrength = FMath::Clamp(Settings.CloudShadowAtmosphereStrength, 0.0f, 1.0f);
		Clouds.AtmosphereTraceScale = FMath::Clamp(Settings.AtmosphereShadowQuality, 1.0f, 8.0f);
		Clouds.bSkyOcclusion = Settings.bCloudSkyOcclusion;
		Clouds.bLightShafts = Settings.bLightShafts;
		Clouds.DensityScale = FMath::Max(Settings.CloudDensityScale, 0.0f);
		Clouds.CoverageOffset = Settings.CloudCoverageOffset;
		Clouds.BottomOcclusion = FMath::Clamp(Settings.CloudBottomOcclusion, 0.0f, 1.0f);
		return Clouds;
	}

	FDeverauxVolumetricFogSettings MakeVolumetricFogSettings(const UDeverauxSkySettings& Settings)
	{
		FDeverauxVolumetricFogSettings Result;
		Result.bEnabled = true;
		Result.DistanceCm = FMath::Clamp(Settings.VolumetricFogDistanceMeters, 50.0f, 3000.0f) * 100.0f;
		Result.ExtinctionScale = FMath::Clamp(Settings.VolumetricFogDensityScale, 0.1f, 10.0f);
		Result.ScatteringDistribution = FMath::Clamp(Settings.VolumetricFogScattering, 0.0f, 0.9f);
		return Result;
	}

	/**
	 * Sombra das nuvens (no chao e no ar da atmosfera, que da os raios de longe) e raios na tela numa luz da
	 * atmosfera. Cada mudanca refaz a luz e as sombras virtuais dela, entao so mexe no que mudou.
	 */
	void ConfigureLightForClouds(UDirectionalLightComponent* Light, const FDeverauxCloudSettings& Clouds)
	{
		if (!Light)
		{
			return;
		}

		bool bChanged = false;
		const auto SetValue = [&bChanged](float& Field, float Value)
		{
			if (Field != Value)
			{
				Field = Value;
				bChanged = true;
			}
		};

		if ((Light->bCastCloudShadows != 0) != Clouds.bShadows)
		{
			Light->bCastCloudShadows = Clouds.bShadows;
			bChanged = true;
		}
		SetValue(Light->CloudShadowStrength, Clouds.ShadowStrength);
		// No chao a sombra vale inteira: um mapa com isto em 0 nunca mostraria sombra de nuvem no chao
		SetValue(Light->CloudShadowOnSurfaceStrength, 1.0f);
		SetValue(Light->CloudShadowOnAtmosphereStrength, Clouds.ShadowAtmosphereStrength);
		SetValue(Light->CloudShadowExtent, Clouds.ShadowExtentKm);
		SetValue(Light->CloudShadowMapResolutionScale, Clouds.ShadowResolutionScale);
		if ((Light->bEnableLightShaftBloom != 0) != Clouds.bLightShafts)
		{
			Light->bEnableLightShaftBloom = Clouds.bLightShafts;
			bChanged = true;
		}
		if (Clouds.bLightShafts)
		{
			SetValue(Light->BloomMaxBrightness, FMath::Min(Light->BloomMaxBrightness, LightShaftMaxBrightness));
		}
		if (bChanged)
		{
			Light->MarkRenderStateDirty();
		}
	}

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

	// Com o jogo pausado o relogio para, mas a camera pode andar: a imagem da lua tem de ir junto
	PrimaryActorTick.bTickEvenWhenPaused = true;

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
	FString MoonText(TEXT("disco"));
	if (bHasMoonImage)
	{
		MoonText = !bMoonImageVisible ? FString(TEXT("imagem, abaixo do horizonte"))
			: FString::Printf(TEXT("imagem a %.1f km, DeverauxLuaAtmosfera %.2f %.2f %.2f (ar ate ela %.2f)"), AppliedMoonDistanceKm,
				AppliedMoonAtmosphere.R, AppliedMoonAtmosphere.G, AppliedMoonAtmosphere.B, MoonAerialTransmittance);
		MoonText += MoonMaterialProblems.IsEmpty() ? FString(TEXT(", material ok")) : FString::Printf(TEXT(", material: %s"), *MoonMaterialProblems);
	}

	return FString::Printf(TEXT("sol do mapa: %s, %.2f lux | sombra: %s | lua: %s, %.2f lux | exposicao %.2f EV | sky light: %s | neblina: %s | nuvens: %s | atmosfera: %s"),
		SunOwner ? *SunOwner->GetActorNameOrLabel() : TEXT("nenhum"),
		SunIntensity,
		bSunShadows ? TEXT("sol") : (bMoonShadows ? TEXT("lua") : TEXT("nenhuma")),
		*MoonText,
		MoonLight->Intensity,
		Exposure->bEnabled ? Exposure->Settings.AutoExposureBias : 0.0f,
		SkyLight.IsValid() ? TEXT("sim") : TEXT("nao"),
		Fog.IsValid() ? TEXT("sim") : TEXT("nao"),
		Clouds.IsValid() ? TEXT("sim") : TEXT("nao"),
		Atmosphere.IsValid() ? TEXT("sim") : TEXT("nao"));
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
		if (!Atmosphere.IsValid())
		{
			Atmosphere = Actor->FindComponentByClass<USkyAtmosphereComponent>();
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
		VolumetricFogOriginal.bEnabled = FogComponent->bEnableVolumetricFog != 0;
		VolumetricFogOriginal.DistanceCm = FogComponent->VolumetricFogDistance;
		VolumetricFogOriginal.ExtinctionScale = FogComponent->VolumetricFogExtinctionScale;
		VolumetricFogOriginal.ScatteringDistribution = FogComponent->VolumetricFogScatteringDistribution;
	}

	// A tabela da perspectiva aerea vai ate esta distancia; mais longe ela repete o ultimo valor
	if (const IConsoleVariable* DepthVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkyAtmosphere.AerialPerspectiveLUT.Depth")))
	{
		AerialPerspectiveDepthKm = FMath::Max(static_cast<double>(DepthVariable->GetFloat()), 1.0);
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

		MoonMaterialProblems = DeverauxSkyControllerPrivate::DescribeMoonMaterialProblems(*MoonMaterialAsset);
		if (!MoonMaterialProblems.IsEmpty())
		{
			UE_LOG(LogDeverauxCeu, Warning, TEXT("Material da lua (%s): %s."), *MoonMaterialAsset->GetName(), *MoonMaterialProblems);
		}
	}
}

void ADeverauxSkyController::UpdateSky(bool bForceRotation)
{
	const UDeverauxSkySettings* Settings = GetDefault<UDeverauxSkySettings>();
	if (!Settings->bEnabled || !ClockSubsystem)
	{
		// Desligado no meio do jogo: o ceu fica onde esta, sem a exposicao da noite e sem a imagem da
		// lua, que pararia num ponto do mundo em vez de acompanhar a camera
		Exposure->bEnabled = false;
		MoonImage->SetHiddenInGame(true);
		bMoonImageVisible = false;
		return;
	}

	const double TimeOfDay = ClockSubsystem->GetClock().GetTimeOfDayFraction();
	const FDeverauxSkyState State = DeverauxSky::ComputeSkyState(TimeOfDay, Settings->MakeSunPathParams());

	ApplyLights(State, *Settings, bForceRotation);
	ApplyMoonImage(State, *Settings);
	ApplyClouds(*Settings);
	ApplyExposure(State, *Settings);
	ApplyFog(State, *Settings);
	ApplyVolumetricFog(*Settings);
	ApplyMaterialParameters(TimeOfDay, State);
}

void ADeverauxSkyController::ApplyClouds(const UDeverauxSkySettings& Settings)
{
	using namespace DeverauxSkyControllerPrivate;

	if (!Settings.bControlClouds)
	{
		return;
	}

	// O material das nuvens pode carregar depois do comeco do jogo: tenta a cada quadro ate conseguir
	UVolumetricCloudComponent* CloudComponent = Clouds.Get();
	const bool bNewCloudMaterial = SetupCloudMaterial(CloudComponent);

	// Os ajustes mudam pouco (Project Settings): os componentes so sao mexidos quando algo muda
	const FDeverauxCloudSettings Wanted = MakeCloudSettings(Settings);
	const bool bSettingsChanged = !bHasAppliedClouds || !Wanted.Equals(AppliedClouds);
	if (!bSettingsChanged && !bNewCloudMaterial)
	{
		return;
	}
	AppliedClouds = Wanted;
	bHasAppliedClouds = true;

	if (bSettingsChanged)
	{
		ConfigureLightForClouds(Sun.Get(), Wanted);
		ConfigureLightForClouds(MoonLight, Wanted);

		// Com o ceu nublado, o Sky Light ilumina menos o chao
		if (USkyLightComponent* SkyLightComponent = SkyLight.Get())
		{
			if ((SkyLightComponent->bCloudAmbientOcclusion != 0) != Wanted.bSkyOcclusion)
			{
				SkyLightComponent->bCloudAmbientOcclusion = Wanted.bSkyOcclusion;
				SkyLightComponent->MarkRenderStateDirty();
			}
		}

		if (CloudComponent && CloudComponent->SkyLightCloudBottomOcclusion != Wanted.BottomOcclusion)
		{
			SetBySetter(*CloudComponent, TEXT("SetSkyLightCloudBottomOcclusion"), CloudComponent->SkyLightCloudBottomOcclusion, Wanted.BottomOcclusion);
		}

		// A sombra das nuvens no ar e desenhada passo a passo em cada direcao do ceu: com mais passos ela sai lisa,
		// sem faixas. Vale so para a atmosfera; o chao le a sombra direto. A Unreal nao tem setter para isto
		if (USkyAtmosphereComponent* AtmosphereComponent = Atmosphere.Get())
		{
			if (AtmosphereComponent->TraceSampleCountScale != Wanted.AtmosphereTraceScale)
			{
				AtmosphereComponent->TraceSampleCountScale = Wanted.AtmosphereTraceScale;
				AtmosphereComponent->MarkRenderStateDirty();
			}
		}
	}

	if (CloudMaterial)
	{
		if (bHasCloudDensity)
		{
			CloudMaterial->SetScalarParameterValue(CloudDensityParameter(), CloudDensityOriginal * Wanted.DensityScale);
		}
		if (bHasCloudCoverage)
		{
			CloudMaterial->SetScalarParameterValue(CloudCoverageParameter(), CloudCoverageOriginal + Wanted.CoverageOffset);
		}
	}
}

bool ADeverauxSkyController::SetupCloudMaterial(UVolumetricCloudComponent* CloudComponent)
{
	using namespace DeverauxSkyControllerPrivate;

	if (bCheckedCloudMaterial || !CloudComponent)
	{
		return false;
	}

	// Na 5.8 o material fica num ponteiro "soft": enquanto nao carrega, tenta de novo no proximo quadro
	UMaterialInterface* OriginalMaterial = CloudComponent->Material.Get();
	if (!OriginalMaterial)
	{
		return false;
	}
	bCheckedCloudMaterial = true;

	bHasCloudDensity = OriginalMaterial->GetScalarParameterValue(FHashedMaterialParameterInfo(CloudDensityParameter()), CloudDensityOriginal, false);
	bHasCloudCoverage = OriginalMaterial->GetScalarParameterValue(FHashedMaterialParameterInfo(CloudCoverageParameter()), CloudCoverageOriginal, false);
	if (!bHasCloudDensity && !bHasCloudCoverage)
	{
		UE_LOG(LogDeverauxCeu, Warning, TEXT("O material das nuvens (%s) nao tem Cloud_GlobalDensity nem Layout_GlobalCoverage: densidade e cobertura ficam como estao."),
			*OriginalMaterial->GetName());
		return false;
	}

	// Uma copia so deste jogo: o mapa e o material original nao mudam. A troca passa pelo SetMaterial da
	// Unreal, que tambem atualiza o ponteiro que o desenho das nuvens usa
	CloudMaterial = UMaterialInstanceDynamic::Create(OriginalMaterial, this);
	UMaterialInterface* NewMaterial = CloudMaterial;
	struct FParams
	{
		UMaterialInterface* NewValue;
	};
	if (!CallBlueprintSetter(*CloudComponent, TEXT("SetMaterial"), FParams{ NewMaterial }) || CloudComponent->Material.Get() != NewMaterial)
	{
		UE_LOG(LogDeverauxCeu, Warning, TEXT("SetMaterial das nuvens nao respondeu: trocando so o ponteiro publico. Se a densidade nao mudar, me avise."));
		CloudComponent->Material = NewMaterial;
		CloudComponent->MarkRenderStateDirty();
	}
	return true;
}

FString ADeverauxSkyController::GetCloudDescription() const
{
	if (!GetDefault<UDeverauxSkySettings>()->bControlClouds)
	{
		return TEXT("nuvens: o ceu nao mexe nelas (Control Clouds desligado)");
	}
	if (!bHasAppliedClouds)
	{
		return TEXT("nuvens: ainda nao aplicadas");
	}

	const auto YesNo = [](bool bValue) { return bValue ? TEXT("sim") : TEXT("nao"); };
	FString Material(TEXT("o mapa nao tem Volumetric Cloud"));
	if (Clouds.IsValid() && !bCheckedCloudMaterial)
	{
		Material = TEXT("o material das nuvens ainda nao carregou");
	}
	else if (Clouds.IsValid())
	{
		const FString Density = bHasCloudDensity
			? FString::Printf(TEXT("densidade %.3f x %.2f = %.3f"), CloudDensityOriginal, AppliedClouds.DensityScale, CloudDensityOriginal * AppliedClouds.DensityScale)
			: FString(TEXT("o material nao tem Cloud_GlobalDensity"));
		const FString Coverage = bHasCloudCoverage
			? FString::Printf(TEXT("cobertura %.3f %+.2f"), CloudCoverageOriginal, AppliedClouds.CoverageOffset)
			: FString(TEXT("o material nao tem Layout_GlobalCoverage"));
		Material = FString::Printf(TEXT("base %.2f | %s | %s"), AppliedClouds.BottomOcclusion, *Density, *Coverage);
	}

	return FString::Printf(TEXT("nuvens: sombra %s (forca %.2f, %.0f km, resolucao x%.1f, raios no ar %.2f, passos no ar x%.1f) | raios na tela %s | luz do ceu tapada %s | %s"),
		YesNo(AppliedClouds.bShadows), AppliedClouds.ShadowStrength, AppliedClouds.ShadowExtentKm, AppliedClouds.ShadowResolutionScale,
		AppliedClouds.ShadowAtmosphereStrength, AppliedClouds.AtmosphereTraceScale, YesNo(AppliedClouds.bLightShafts),
		YesNo(AppliedClouds.bSkyOcclusion && SkyLight.IsValid()), *Material);
}

FString ADeverauxSkyController::GetLightDescription() const
{
	using namespace DeverauxSkyControllerPrivate;

	const auto YesNo = [](bool bValue) { return bValue ? TEXT("sim") : TEXT("nao"); };
	const auto DescribeLight = [&YesNo](const UDirectionalLightComponent* Light)
	{
		if (!Light)
		{
			return FString(TEXT("nenhuma"));
		}
		const FString AtmosphereIndex = Light->IsUsedAsAtmosphereSunLight()
			? FString::Printf(TEXT("%d"), static_cast<int32>(Light->GetAtmosphereSunLightIndex())) : FString(TEXT("nao"));
		return FString::Printf(TEXT("sombra das nuvens %s (forca %.2f, chao %.2f, ar %.2f, %.0f km, resolucao x%.2f), raios na tela %s, luz da atmosfera %s"),
			YesNo(Light->bCastCloudShadows != 0), Light->CloudShadowStrength, Light->CloudShadowOnSurfaceStrength, Light->CloudShadowOnAtmosphereStrength,
			Light->CloudShadowExtent, Light->CloudShadowMapResolutionScale, YesNo(Light->bEnableLightShaftBloom != 0), *AtmosphereIndex);
	};

	FString FogText(TEXT("o mapa nao tem Exponential Height Fog"));
	if (const UExponentialHeightFogComponent* FogComponent = Fog.Get())
	{
		FogText = FString::Printf(TEXT("%s (%.0f m, densidade x%.2f, espalhamento %.2f)"), YesNo(FogComponent->bEnableVolumetricFog != 0),
			FogComponent->VolumetricFogDistance / 100.0f, FogComponent->VolumetricFogExtinctionScale, FogComponent->VolumetricFogScatteringDistribution);
	}

	const USkyAtmosphereComponent* AtmosphereComponent = Atmosphere.Get();
	const FString AtmosphereText = AtmosphereComponent
		? FString::Printf(TEXT("passos x%.1f (limite r.SkyAtmosphere.FastSkyLUT.SampleCountMax %s)"), AtmosphereComponent->TraceSampleCountScale,
			*ConsoleValue(TEXT("r.SkyAtmosphere.FastSkyLUT.SampleCountMax")))
		: FString(TEXT("o mapa nao tem Sky Atmosphere"));

	return FString::Printf(TEXT("luzes agora: sol: %s | lua: %s | neblina volumetrica: %s | atmosfera: %s | r.VolumetricCloud.ShadowMap %s, r.VolumetricFog %s, MegaLights %s"),
		*DescribeLight(Sun.Get()), *DescribeLight(MoonLight), *FogText, *AtmosphereText, *ConsoleValue(TEXT("r.VolumetricCloud.ShadowMap")),
		*ConsoleValue(TEXT("r.VolumetricFog")), *ConsoleValue(TEXT("r.MegaLights.EnableForProject")));
}

void ADeverauxSkyController::ApplyVolumetricFog(const UDeverauxSkySettings& Settings)
{
	using namespace DeverauxSkyControllerPrivate;

	UExponentialHeightFogComponent* FogComponent = Fog.Get();
	if (!FogComponent)
	{
		return;
	}

	// Desligado, volta ao que o mapa tinha: da para ligar e desligar com o jogo rodando
	const FDeverauxVolumetricFogSettings Wanted = Settings.bVolumetricFog ? MakeVolumetricFogSettings(Settings) : VolumetricFogOriginal;
	if (bHasAppliedVolumetricFog && Wanted.Equals(AppliedVolumetricFog))
	{
		return;
	}
	AppliedVolumetricFog = Wanted;
	bHasAppliedVolumetricFog = true;

	if (FogComponent->VolumetricFogDistance != Wanted.DistanceCm)
	{
		SetBySetter(*FogComponent, TEXT("SetVolumetricFogDistance"), FogComponent->VolumetricFogDistance, Wanted.DistanceCm);
	}
	if (FogComponent->VolumetricFogExtinctionScale != Wanted.ExtinctionScale)
	{
		SetBySetter(*FogComponent, TEXT("SetVolumetricFogExtinctionScale"), FogComponent->VolumetricFogExtinctionScale, Wanted.ExtinctionScale);
	}
	if (FogComponent->VolumetricFogScatteringDistribution != Wanted.ScatteringDistribution)
	{
		SetBySetter(*FogComponent, TEXT("SetVolumetricFogScatteringDistribution"), FogComponent->VolumetricFogScatteringDistribution,
			Wanted.ScatteringDistribution);
	}
	if ((FogComponent->bEnableVolumetricFog != 0) != Wanted.bEnabled)
	{
		SetVolumetricFogEnabled(*FogComponent, Wanted.bEnabled);
	}
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

	// Cada giro do sol refaz as sombras virtuais dele inteiras, entao as luzes so giram quando um dos
	// astros andou o passo, ou quando o norte muda (com o relogio parado, nada mais as giraria)
	const double MovedDegrees = FMath::Max(AngleBetweenDegrees(State.SunDirection, AppliedSunDirection),
		AngleBetweenDegrees(State.MoonDirection, AppliedMoonDirection));
	const double StepDegrees = FMath::Max(static_cast<double>(Settings.UpdateStepDegrees), MinStepDegrees);
	if (bForceRotation || !bHasAppliedRotation || Settings.NorthYawDegrees != AppliedNorthYaw || MovedDegrees >= StepDegrees)
	{
		if (SunComponent)
		{
			SunComponent->SetWorldRotation(LightRotation(State.SunDirection, Settings.NorthYawDegrees));
		}
		MoonLight->SetWorldRotation(LightRotation(State.MoonDirection, Settings.NorthYawDegrees));
		AppliedSunDirection = State.SunDirection;
		AppliedMoonDirection = State.MoonDirection;
		AppliedNorthYaw = Settings.NorthYawDegrees;
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

	// Sempre na direcao da lua a partir da camera, entao nao muda de tamanho nem de lugar quando o jogador anda
	const FVector Direction = FRotator(0.0, Settings.NorthYawDegrees, 0.0).RotateVector(State.MoonDirection);
	const double SinElevation = FMath::Clamp(Direction.Z, -1.0, 1.0);

	// O que o ar faz com a luz da lua, com a mesma conta que a Unreal usa para colorir a luz, dividida
	// pelo zenite: no alto quase nada muda, perto do horizonte a lua esmaece e fica alaranjada
	const USkyAtmosphereComponent* AtmosphereComponent = Atmosphere.Get();
	const FDeverauxAtmosphereParams AtmosphereParams = AtmosphereComponent ? MakeAtmosphereParams(*AtmosphereComponent) : FDeverauxAtmosphereParams();
	FVector Tint(1.0, 1.0, 1.0);
	if (AtmosphereComponent && Settings.MoonAtmosphereStrength > 0.0f)
	{
		const double MinLightSin = FMath::Sin(FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(AtmosphereComponent->TransmittanceMinLightElevationAngle), -90.0, 90.0)));
		const FVector Toward = DeverauxAtmosphere::LightTransmittance(AtmosphereParams, FMath::Max(SinElevation, MinLightSin));
		const FVector Zenith = DeverauxAtmosphere::LightTransmittance(AtmosphereParams, 1.0);
		const double Strength = Settings.MoonAtmosphereStrength;
		Tint = FVector(RelativeTransmittance(Toward.X, Zenith.X, Strength), RelativeTransmittance(Toward.Y, Zenith.Y, Strength),
			RelativeTransmittance(Toward.Z, Zenith.Z, Strength));
	}

	// Some abaixo do horizonte, e tambem quando o ar ja apagou a lua: senao sobra um disco escuro
	const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	const bool bVisible = Camera && State.MoonElevationDegrees > MoonImageHideDegrees && Tint.GetMax() >= MinVisibleTint;
	MoonImage->SetHiddenInGame(!bVisible);
	bMoonImageVisible = bVisible;
	if (!bVisible)
	{
		return;
	}

	// Logo atras do topo das nuvens, na direcao da lua, para todas passarem na frente. Com o planeta
	// redondo, perto do horizonte esse ponto fica bem longe
	const FVector CameraLocation = Camera->GetCameraLocation();
	const double CameraAltitudeKm = FMath::Max(0.0, (CameraLocation.Z - PlanetTopZ(AtmosphereComponent)) / CentimetersPerKm);
	const UVolumetricCloudComponent* CloudComponent = Clouds.Get();
	const double CloudTopKm = CloudComponent ? CloudComponent->LayerBottomAltitude + CloudComponent->LayerHeight : DefaultCloudTopKm;
	const double BehindCloudsKm = DeverauxAtmosphere::DistanceToAltitude(AtmosphereParams.GroundRadiusKm, CameraAltitudeKm, SinElevation,
		CloudTopKm + MoonAboveCloudsMarginKm);
	const double DistanceKm = FMath::Clamp(BehindCloudsKm, MoonImageMinDistanceKm, MoonImageMaxDistanceKm);
	AppliedMoonDistanceKm = DistanceKm;

	// A Unreal ainda escurece a imagem com a perspectiva aerea ate essa distancia (em cinza, a media das
	// cores). A cor acima ja inclui todo o ar ate o espaco, entao essa parte e descontada para nao contar duas vezes
	MoonAerialTransmittance = 1.0;
	if (AtmosphereComponent)
	{
		const double ViewDistanceKm = FMath::Min(DistanceKm, AerialPerspectiveDepthKm)
			* FMath::Max(static_cast<double>(AtmosphereComponent->AerialPespectiveViewDistanceScale), 0.0);
		const FVector Aerial = DeverauxAtmosphere::Transmittance(AtmosphereParams, CameraAltitudeKm, SinElevation, ViewDistanceKm, AerialPerspectiveSamples);
		MoonAerialTransmittance = (Aerial.X + Aerial.Y + Aerial.Z) / 3.0;
	}
	const FVector Factor = Tint * (1.0 / FMath::Max(MoonAerialTransmittance, MinCompensatedTransmittance));
	const FLinearColor AtmosphereColor(static_cast<float>(Factor.X), static_cast<float>(Factor.Y), static_cast<float>(Factor.Z), 1.0f);
	if (!AtmosphereColor.Equals(AppliedMoonAtmosphere, 0.0005f))
	{
		static const FName MoonAtmosphereName(TEXT("DeverauxLuaAtmosfera"));
		MoonImageMaterial->SetVectorParameterValue(MoonAtmosphereName, AtmosphereColor);
		AppliedMoonAtmosphere = AtmosphereColor;
	}

	// O alto da imagem aponta para o polo norte celeste, como na lua de verdade: ela gira devagar do nascer ao
	// poente. O alto do lugar nao serve: no zenite ele deixa de existir, e com a latitude perto de 0 a imagem
	// virava de ponta-cabeca de uma vez ao passar por cima. Com a declinacao da lua o polo nunca fica na frente dela
	const double LatitudeRadians = FMath::DegreesToRadians(Settings.LatitudeDegrees);
	const FVector CelestialNorth = FRotator(0.0, Settings.NorthYawDegrees, 0.0).RotateVector(
		FVector(FMath::Cos(LatitudeRadians), 0.0, FMath::Sin(LatitudeRadians)));

	// Tamanho no mundo que da o tamanho angular pedido nessa distancia, com o plano virado para a camera.
	// Lugar, giro e tamanho numa so atualizacao por quadro
	const double Distance = DistanceKm * CentimetersPerKm;
	const double Size = 2.0 * Distance * FMath::Tan(FMath::DegreesToRadians(0.5 * Settings.MoonAngularSizeDegrees));
	MoonImage->SetWorldTransform(FTransform(FRotationMatrix::MakeFromZX(-Direction, CelestialNorth).Rotator(),
		CameraLocation + Direction * Distance, FVector(Size / PlaneMeshSize)));
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
