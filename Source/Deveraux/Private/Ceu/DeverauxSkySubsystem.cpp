// Projeto Deveraux - ciclo dia/noite do mundo (modulo Deveraux)

#include "Ceu/DeverauxSkySubsystem.h"

#include "Ceu/DeverauxSkyController.h"
#include "Ceu/DeverauxSkySettings.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Relogio/DeverauxClockSubsystem.h"

#if !UE_BUILD_SHIPPING
namespace DeverauxSkySubsystemPrivate
{
	void RestoreSunTest();
}
#endif

void UDeverauxSkySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!GetDefault<UDeverauxSkySettings>()->bEnabled)
	{
		return;
	}

	// Criado em tempo de jogo, fora das celulas do World Partition: nunca sai da memoria com o mapa
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;
	Controller = InWorld.SpawnActor<ADeverauxSkyController>(SpawnParameters);
}

void UDeverauxSkySubsystem::Deinitialize()
{
#if !UE_BUILD_SHIPPING
	// O teste do sol mexe em console variables, que valem para o editor inteiro: o Play acabou, tudo volta
	DeverauxSkySubsystemPrivate::RestoreSunTest();
#endif

	Super::Deinitialize();
}

FDeverauxSkyState UDeverauxSkySubsystem::GetSkyState() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UDeverauxClockSubsystem* ClockSubsystem = GameInstance ? GameInstance->GetSubsystem<UDeverauxClockSubsystem>() : nullptr;
	const double TimeOfDay = ClockSubsystem ? ClockSubsystem->GetClock().GetTimeOfDayFraction() : 0.5;
	return DeverauxSky::ComputeSkyState(TimeOfDay, GetDefault<UDeverauxSkySettings>()->MakeSunPathParams());
}

double UDeverauxSkySubsystem::GetDayAlpha() const
{
	return GetSkyState().DayAlpha;
}

bool UDeverauxSkySubsystem::IsNight() const
{
	return GetSkyState().IsNight();
}

double UDeverauxSkySubsystem::GetSunElevationDegrees() const
{
	return GetSkyState().SunElevationDegrees;
}

ADeverauxSkyController* UDeverauxSkySubsystem::GetController() const
{
	return Controller;
}

bool UDeverauxSkySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

// Comando de console para testar. Fica fora da versao final (Shipping).
#if !UE_BUILD_SHIPPING
namespace DeverauxSkySubsystemPrivate
{
	void InfoCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		const UDeverauxSkySubsystem* SkySubsystem = World ? World->GetSubsystem<UDeverauxSkySubsystem>() : nullptr;
		if (!SkySubsystem)
		{
			Ar.Log(TEXT("Ceu indisponivel: use o comando com o jogo rodando (Play)."));
			return;
		}

		const FDeverauxSkyState State = SkySubsystem->GetSkyState();
		Ar.Logf(TEXT("Sol a %.1f graus | lua a %.1f graus | dia %.2f%s"), State.SunElevationDegrees, State.MoonElevationDegrees,
			State.DayAlpha, State.IsNight() ? TEXT(" (noite)") : TEXT(""));

		if (const ADeverauxSkyController* Controller = SkySubsystem->GetController())
		{
			Ar.Log(*Controller->GetDebugDescription());
			Ar.Log(*Controller->GetCloudDescription());
			Ar.Log(*Controller->GetLightDescription());
		}
		else
		{
			Ar.Log(TEXT("Sem controlador do ceu: ciclo desligado em Project Settings > Game > Deveraux Ceu."));
		}
	}

	/** Uma peca do desenho que pode fazer o sol tremer, e o valor do console variable que a desliga. */
	struct FSunTestStep
	{
		const TCHAR* Description;
		const TCHAR* Variable;
		const TCHAR* OffValue;
	};

	// Primeiro o que entrou com as nuvens, depois o resto da imagem, por ultimo o anti-aliasing
	const FSunTestStep SunTestSteps[] = {
		{ TEXT("raios na tela (Light Shaft Bloom)"), TEXT("ShowFlag.LightShafts"), TEXT("0") },
		{ TEXT("sombra das nuvens (no chao e no ar)"), TEXT("r.VolumetricCloud.ShadowMap"), TEXT("0") },
		{ TEXT("neblina volumetrica"), TEXT("ShowFlag.VolumetricFog"), TEXT("0") },
		{ TEXT("nuvens"), TEXT("ShowFlag.Cloud"), TEXT("0") },
		{ TEXT("lens flare"), TEXT("ShowFlag.LensFlares"), TEXT("0") },
		{ TEXT("bloom (o brilho em volta do sol)"), TEXT("ShowFlag.Bloom"), TEXT("0") },
		{ TEXT("exposicao local"), TEXT("ShowFlag.LocalExposure"), TEXT("0") },
		{ TEXT("exposicao automatica (o olho se adaptando)"), TEXT("ShowFlag.EyeAdaptation"), TEXT("0") },
		{ TEXT("anti-aliasing temporal (TSR)"), TEXT("ShowFlag.TemporalAA"), TEXT("0") },
	};
	constexpr int32 SunTestStepCount = UE_ARRAY_COUNT(SunTestSteps);
	constexpr int32 SunTestMessageKey = 7102;

	/** O aviso de um passo fica na tela ate o proximo passo; o de "tudo normal" so alguns segundos. */
	constexpr float SunTestStepSeconds = 600.0f;
	constexpr float SunTestDoneSeconds = 5.0f;

	/** Passo desligado agora (0 = nenhum) e o valor que o console variable dele tinha antes. */
	int32 SunTestStep = 0;
	FString SunTestSavedValue;

	void ShowSunTestMessage(const FString& Text, float Seconds)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(SunTestMessageKey, Seconds, FColor::Yellow, Text);
		}
	}

	void RestoreSunTest()
	{
		if (SunTestStep > 0)
		{
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(SunTestSteps[SunTestStep - 1].Variable))
			{
				Variable->Set(*SunTestSavedValue, ECVF_SetByConsole);
			}

			// Troca o aviso do passo, que ficaria minutos na tela, ate no proximo Play
			ShowSunTestMessage(TEXT("Teste do sol: tudo normal de novo."), SunTestDoneSeconds);
		}
		SunTestStep = 0;
	}

	/** Desliga uma peca de cada vez. Sem numero: a proxima; com numero, aquela; 0 volta tudo ao normal. */
	void SunTestCommand(const TArray<FString>& Args, UWorld* /*World*/, FOutputDevice& Ar)
	{
		int32 Next = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : SunTestStep + 1;
		RestoreSunTest();

		// Pula o que nao existe nesta versao da engine
		IConsoleVariable* Variable = nullptr;
		while (Next >= 1 && Next <= SunTestStepCount)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(SunTestSteps[Next - 1].Variable);
			if (Variable)
			{
				break;
			}
			Ar.Logf(TEXT("Teste do sol: %s nao existe nesta versao, pulando."), SunTestSteps[Next - 1].Variable);
			++Next;
		}
		if (!Variable || Next < 1 || Next > SunTestStepCount)
		{
			const FString Text(TEXT("Teste do sol: tudo normal de novo. Rode Deveraux.Ceu.TesteSol para comecar."));
			Ar.Log(*Text);
			ShowSunTestMessage(Text, SunTestDoneSeconds);
			return;
		}

		const FSunTestStep& Step = SunTestSteps[Next - 1];
		SunTestSavedValue = Variable->GetString();
		Variable->Set(Step.OffValue, ECVF_SetByConsole);
		SunTestStep = Next;
		const FString Text = FString::Printf(TEXT("Teste do sol %d/%d: SEM %s. Olhe para o sol: parou de tremer? Rode de novo para o proximo (0 volta tudo)."),
			Next, SunTestStepCount, Step.Description);
		Ar.Log(*Text);
		ShowSunTestMessage(Text, SunTestStepSeconds);
	}
}

bool UDeverauxSkySubsystem::ResetSunTest()
{
	const bool bWasOn = DeverauxSkySubsystemPrivate::SunTestStep > 0;
	DeverauxSkySubsystemPrivate::RestoreSunTest();
	return bWasOn;
}

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GDeverauxCeuInfoCommand(
	TEXT("Deveraux.Ceu.Info"),
	TEXT("Mostra a altura do sol e da lua, quanto e dia e quais pecas do mapa o ceu esta usando."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&DeverauxSkySubsystemPrivate::InfoCommand),
	ECVF_Default);

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GDeverauxCeuTesteSolCommand(
	TEXT("Deveraux.Ceu.TesteSol"),
	TEXT("Desliga, uma de cada vez, as pecas da imagem que podem fazer o sol tremer. Sem numero: a proxima. 0: tudo normal."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&DeverauxSkySubsystemPrivate::SunTestCommand),
	ECVF_Default);

#else // UE_BUILD_SHIPPING

bool UDeverauxSkySubsystem::ResetSunTest()
{
	return false;
}

#endif // !UE_BUILD_SHIPPING
