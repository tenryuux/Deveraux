// Projeto Deveraux - relogio do jogo (DeverauxSim)

#include "DeverauxGameClock.h"

#include "Serialization/Archive.h"

namespace DeverauxGameClockPrivate
{
	constexpr int64 TicksPerMinute = ETimespan::TicksPerMinute;
	constexpr int64 TicksPerDay = ETimespan::TicksPerDay;
	constexpr double TicksPerSecond = static_cast<double>(ETimespan::TicksPerSecond);
}

FDeverauxGameClock::FDeverauxGameClock(int32 InTimeScale)
{
	SetTimeScale(InTimeScale);
}

void FDeverauxGameClock::AdvanceReal(double RealSeconds)
{
	// A comparacao negada tambem barra NaN
	if (bPaused || !(RealSeconds > 0.0))
	{
		return;
	}

	// Arredonda para o tick inteiro mais proximo; a sobra entra na proxima chamada
	const double ClampedSeconds = FMath::Min(RealSeconds, MaxRealSecondsPerAdvance);
	const double ExactRealTicks = ClampedSeconds * DeverauxGameClockPrivate::TicksPerSecond + RealTickRemainder;
	const int64 WholeRealTicks = FMath::FloorToInt64(ExactRealTicks + 0.5);
	RealTickRemainder = ExactRealTicks - static_cast<double>(WholeRealTicks);

	AdvanceGameTicks(WholeRealTicks * TimeScale);
}

void FDeverauxGameClock::AdvanceGameMinutes(int32 Minutes)
{
	if (Minutes > 0)
	{
		AdvanceGameTicks(static_cast<int64>(Minutes) * DeverauxGameClockPrivate::TicksPerMinute);
	}
}

void FDeverauxGameClock::AdvanceGameTime(FTimespan GameDelta)
{
	AdvanceGameTicks(GameDelta.GetTicks());
}

void FDeverauxGameClock::SetGameTime(FTimespan NewGameTime)
{
	GameTicks = FMath::Max<int64>(0, NewGameTime.GetTicks());
	LastBroadcastMinute = GetTotalMinutes();
	RealTickRemainder = 0.0;
}

void FDeverauxGameClock::AdvanceToTimeOfDay(FTimespan TimeOfDay)
{
	using namespace DeverauxGameClockPrivate;

	const int64 TargetTicks = (TimeOfDay.GetTicks() % TicksPerDay + TicksPerDay) % TicksPerDay;
	const int64 DeltaTicks = (TargetTicks - GameTicks % TicksPerDay + TicksPerDay) % TicksPerDay;

	// Segundos depois da marca, a conta pura daria quase um dia inteiro de pulo
	const bool bAlreadyThere = DeltaTicks > TicksPerDay - TicksPerMinute;
	AdvanceGameTicks(bAlreadyThere ? 0 : DeltaTicks);
}

int64 FDeverauxGameClock::GetTotalMinutes() const
{
	return GameTicks / DeverauxGameClockPrivate::TicksPerMinute;
}

int32 FDeverauxGameClock::GetDay() const
{
	return static_cast<int32>(GameTicks / DeverauxGameClockPrivate::TicksPerDay) + 1;
}

int32 FDeverauxGameClock::GetHour() const
{
	return GetTimeOfDay().GetHours();
}

int32 FDeverauxGameClock::GetMinute() const
{
	return GetTimeOfDay().GetMinutes();
}

FTimespan FDeverauxGameClock::GetTimeOfDay() const
{
	return FTimespan(GameTicks % DeverauxGameClockPrivate::TicksPerDay);
}

double FDeverauxGameClock::GetTimeOfDayFraction() const
{
	return static_cast<double>(GameTicks % DeverauxGameClockPrivate::TicksPerDay)
		/ static_cast<double>(DeverauxGameClockPrivate::TicksPerDay);
}

void FDeverauxGameClock::SetTimeScale(int32 NewTimeScale)
{
	TimeScale = FMath::Clamp(NewTimeScale, MinTimeScale, MaxTimeScale);
}

FTimespan FDeverauxGameClock::RealToGame(FTimespan RealDelta) const
{
	return FTimespan(RealDelta.GetTicks() * TimeScale);
}

FTimespan FDeverauxGameClock::GameToReal(FTimespan GameDelta) const
{
	return FTimespan(GameDelta.GetTicks() / TimeScale);
}

FString FDeverauxGameClock::ToDebugString() const
{
	return FString::Printf(TEXT("Dia %d, %02d:%02d"), GetDay(), GetHour(), GetMinute());
}

void FDeverauxGameClock::Serialize(FArchive& Ar)
{
	int32 Version = SaveVersion;
	Ar << Version;
	if (Ar.IsLoading() && (Version < 1 || Version > SaveVersion))
	{
		Ar.SetError();
		return;
	}

	int64 SavedTicks = GameTicks;
	Ar << SavedTicks;

	if (Ar.IsLoading() && !Ar.IsError())
	{
		SetGameTime(FTimespan(SavedTicks));
	}
}

void FDeverauxGameClock::AdvanceGameTicks(int64 GameDeltaTicks)
{
	if (GameDeltaTicks > 0)
	{
		GameTicks += GameDeltaTicks;
		BroadcastPendingMinutes();
	}
}

void FDeverauxGameClock::BroadcastPendingMinutes()
{
	// O laco segue o contador do relogio: se um ouvinte avancar o tempo aqui dentro,
	// os minutos novos saem na sequencia, sem repetir e sem sair de ordem
	while (LastBroadcastMinute < GetTotalMinutes())
	{
		++LastBroadcastMinute;
		OnMinutePassed.Broadcast(LastBroadcastMinute);
	}
}
