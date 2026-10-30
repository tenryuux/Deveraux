// Projeto Deveraux - relogio do jogo enquanto o jogo roda (modulo Deveraux)

#include "Relogio/DeverauxClockSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Relogio/DeverauxClockSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogDeverauxRelogio, Log, All);

#if !UE_BUILD_SHIPPING
namespace DeverauxClockSubsystemPrivate
{
	constexpr int32 OnScreenMessageKey = 7101;
	constexpr int32 MaxMinutesPerCommand = 30 * 24 * 60;
}

static TAutoConsoleVariable<bool> CVarDeverauxRelogioMostrar(
	TEXT("Deveraux.Relogio.Mostrar"),
	false,
	TEXT("1 mostra o relogio do jogo na tela; 0 esconde."),
	ECVF_Default);
#endif

void UDeverauxClockSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UDeverauxClockSettings* Settings = GetDefault<UDeverauxClockSettings>();
	Clock.SetTimeScale(Settings->TimeScale);
	Clock.SetGameTime(FTimespan::FromHours(FMath::Clamp(Settings->StartHour, 0, 23)));
	Clock.OnMinutePassed.AddUObject(this, &UDeverauxClockSubsystem::HandleMinutePassed);
	bClockInitialized = true;

	UE_LOG(LogDeverauxRelogio, Log, TEXT("Relogio iniciado: %s, escala %dx"), *Clock.ToDebugString(), Clock.GetTimeScale());
}

void UDeverauxClockSubsystem::Deinitialize()
{
	bClockInitialized = false;
	Clock.OnMinutePassed.RemoveAll(this);

	Super::Deinitialize();
}

void UDeverauxClockSubsystem::Tick(float DeltaTime)
{
	Clock.AdvanceReal(DeltaTime);

#if !UE_BUILD_SHIPPING
	if (GEngine && CVarDeverauxRelogioMostrar.GetValueOnGameThread())
	{
		const FString Text = FString::Printf(TEXT("%s  |  %dx%s"), *Clock.ToDebugString(), Clock.GetTimeScale(),
			Clock.IsPaused() ? TEXT("  |  pausado") : TEXT(""));
		GEngine->AddOnScreenDebugMessage(DeverauxClockSubsystemPrivate::OnScreenMessageKey, 0.5f, FColor::White, Text);
	}
#endif
}

ETickableTickType UDeverauxClockSubsystem::GetTickableTickType() const
{
	// O objeto padrao da classe tambem nasce como FTickableGameObject e nunca deve andar
	return HasAnyFlags(RF_ClassDefaultObject) ? ETickableTickType::Never : ETickableTickType::Conditional;
}

bool UDeverauxClockSubsystem::IsTickable() const
{
	return bClockInitialized && GetTickableGameObjectWorld() != nullptr;
}

UWorld* UDeverauxClockSubsystem::GetTickableGameObjectWorld() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetWorld() : nullptr;
}

TStatId UDeverauxClockSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDeverauxClockSubsystem, STATGROUP_Tickables);
}

void UDeverauxClockSubsystem::HandleMinutePassed(int64 TotalMinutes)
{
	OnGameMinute.Broadcast(TotalMinutes);
}

// Comandos de console para testar. Ficam fora da versao final (Shipping).
#if !UE_BUILD_SHIPPING
namespace DeverauxClockSubsystemPrivate
{
	UDeverauxClockSubsystem* FindClockSubsystem(UWorld* World, FOutputDevice& Ar)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		UDeverauxClockSubsystem* ClockSubsystem = GameInstance ? GameInstance->GetSubsystem<UDeverauxClockSubsystem>() : nullptr;
		if (!ClockSubsystem)
		{
			Ar.Log(TEXT("Relogio indisponivel: use o comando com o jogo rodando (Play)."));
		}
		return ClockSubsystem;
	}

	bool ParseInt(const TArray<FString>& Args, int32 Index, int32& OutValue)
	{
		if (!Args.IsValidIndex(Index) || !Args[Index].IsNumeric())
		{
			return false;
		}
		OutValue = FCString::Atoi(*Args[Index]);
		return true;
	}

	void LogClock(const FDeverauxGameClock& GameClock, FOutputDevice& Ar)
	{
		Ar.Logf(TEXT("%s | escala %dx | %s"), *GameClock.ToDebugString(), GameClock.GetTimeScale(),
			GameClock.IsPaused() ? TEXT("pausado") : TEXT("rodando"));
	}

	void InfoCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (UDeverauxClockSubsystem* ClockSubsystem = FindClockSubsystem(World, Ar))
		{
			LogClock(ClockSubsystem->GetClock(), Ar);
		}
	}

	void ScaleCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (UDeverauxClockSubsystem* ClockSubsystem = FindClockSubsystem(World, Ar))
		{
			int32 NewScale = 0;
			if (ParseInt(Args, 0, NewScale))
			{
				ClockSubsystem->GetClock().SetTimeScale(NewScale);
			}
			LogClock(ClockSubsystem->GetClock(), Ar);
		}
	}

	void AdvanceCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (UDeverauxClockSubsystem* ClockSubsystem = FindClockSubsystem(World, Ar))
		{
			int32 Minutes = 0;
			if (!ParseInt(Args, 0, Minutes) || Minutes <= 0)
			{
				Ar.Log(TEXT("Uso: Deveraux.Relogio.Avancar <minutos>"));
				return;
			}
			ClockSubsystem->GetClock().AdvanceGameMinutes(FMath::Min(Minutes, MaxMinutesPerCommand));
			LogClock(ClockSubsystem->GetClock(), Ar);
		}
	}

	void PauseCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (UDeverauxClockSubsystem* ClockSubsystem = FindClockSubsystem(World, Ar))
		{
			FDeverauxGameClock& GameClock = ClockSubsystem->GetClock();
			int32 Value = 0;
			GameClock.SetPaused(ParseInt(Args, 0, Value) ? Value != 0 : !GameClock.IsPaused());
			LogClock(GameClock, Ar);
		}
	}

	void HourCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		if (UDeverauxClockSubsystem* ClockSubsystem = FindClockSubsystem(World, Ar))
		{
			int32 Hour = 0;
			int32 Minute = 0;
			const bool bValidHour = ParseInt(Args, 0, Hour) && Hour >= 0 && Hour <= 23;
			const bool bValidMinute = Args.Num() < 2 || (ParseInt(Args, 1, Minute) && Minute >= 0 && Minute <= 59);
			if (!bValidHour || !bValidMinute)
			{
				Ar.Log(TEXT("Uso: Deveraux.Relogio.Hora <hora 0-23> [minuto 0-59]"));
				return;
			}

			// Vai ate a proxima vez que o relogio marcar essa hora; o tempo nunca volta
			FDeverauxGameClock& GameClock = ClockSubsystem->GetClock();
			GameClock.AdvanceToTimeOfDay(FTimespan(static_cast<int64>(Hour) * ETimespan::TicksPerHour + static_cast<int64>(Minute) * ETimespan::TicksPerMinute));
			LogClock(GameClock, Ar);
		}
	}
}

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GDeverauxRelogioInfoCommand(
	TEXT("Deveraux.Relogio.Info"),
	TEXT("Mostra dia, hora, escala e se o relogio do jogo esta pausado."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&DeverauxClockSubsystemPrivate::InfoCommand),
	ECVF_Default);

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GDeverauxRelogioEscalaCommand(
	TEXT("Deveraux.Relogio.Escala"),
	TEXT("Deveraux.Relogio.Escala <n>: o tempo de jogo corre n vezes mais rapido que o real (1 a 3600; o jogo usa 12)."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&DeverauxClockSubsystemPrivate::ScaleCommand),
	ECVF_Default);

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GDeverauxRelogioAvancarCommand(
	TEXT("Deveraux.Relogio.Avancar"),
	TEXT("Deveraux.Relogio.Avancar <minutos>: avanca o relogio, mesmo pausado (ate 30 dias por vez)."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&DeverauxClockSubsystemPrivate::AdvanceCommand),
	ECVF_Default);

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GDeverauxRelogioPausarCommand(
	TEXT("Deveraux.Relogio.Pausar"),
	TEXT("Deveraux.Relogio.Pausar [1|0]: pausa ou solta so o relogio. Sem numero, alterna."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&DeverauxClockSubsystemPrivate::PauseCommand),
	ECVF_Default);

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GDeverauxRelogioHoraCommand(
	TEXT("Deveraux.Relogio.Hora"),
	TEXT("Deveraux.Relogio.Hora <hora> [minuto]: avanca ate a proxima vez que o relogio marcar essa hora."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&DeverauxClockSubsystemPrivate::HourCommand),
	ECVF_Default);
#endif // !UE_BUILD_SHIPPING
