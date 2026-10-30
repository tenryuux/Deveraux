// Projeto Deveraux - ciclo dia/noite do mundo (modulo Deveraux)

#include "Ceu/DeverauxSkySubsystem.h"

#include "Ceu/DeverauxSkyController.h"
#include "Ceu/DeverauxSkySettings.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Relogio/DeverauxClockSubsystem.h"

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
		}
		else
		{
			Ar.Log(TEXT("Sem controlador do ceu: ciclo desligado em Project Settings > Game > Deveraux Ceu."));
		}
	}
}

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GDeverauxCeuInfoCommand(
	TEXT("Deveraux.Ceu.Info"),
	TEXT("Mostra a altura do sol e da lua, quanto e dia e quais pecas do mapa o ceu esta usando."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&DeverauxSkySubsystemPrivate::InfoCommand),
	ECVF_Default);
#endif // !UE_BUILD_SHIPPING
