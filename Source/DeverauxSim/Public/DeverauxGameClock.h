// Projeto Deveraux - relogio do jogo (DeverauxSim)

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "Misc/Timespan.h"

class FArchive;

/** Disparado uma vez para cada minuto inteiro de jogo que passa, em ordem, com o total de minutos desde o dia 1 as 00:00. */
DECLARE_MULTICAST_DELEGATE_OneParam(FDeverauxOnMinutePassed, int64 /* TotalMinutes */);

/**
 * Relogio do jogo, em C++ puro (sem UObject), para a simulacao, os sweeps e o save.
 *
 * O tempo de jogo fica em ticks inteiros de FTimespan (100 ns), contados a partir do dia 1
 * as 00:00, e nunca volta. O tempo real entra arredondado para ticks inteiros e a sobra passa
 * para a chamada seguinte, entao o erro nao se acumula, nem em horas de jogo.
 *
 * Na escala padrao (12x): 1 minuto de jogo = 5 s reais, 1 hora = 5 min, 1 dia = 2 h.
 * So o game thread mexe no relogio.
 */
class FDeverauxGameClock
{
public:
	/** Quantas vezes o tempo de jogo corre mais rapido que o real. 12 = 24 h de jogo em 2 h reais. */
	static constexpr int32 DefaultTimeScale = 12;
	static constexpr int32 MinTimeScale = 1;
	static constexpr int32 MaxTimeScale = 3600;

	/** Um quadro travado (breakpoint, carregamento) conta no maximo isto de tempo real. */
	static constexpr double MaxRealSecondsPerAdvance = 1.0;

	/** Versao do formato gravado por Serialize. */
	static constexpr int32 SaveVersion = 1;

	DEVERAUXSIM_API explicit FDeverauxGameClock(int32 InTimeScale = DefaultTimeScale);

	/** Avanca pelo tempo real decorrido, multiplicado pela escala. Pausado, nao anda. Zero, negativo ou NaN sao ignorados. */
	DEVERAUXSIM_API void AdvanceReal(double RealSeconds);

	/** Avanca minutos de jogo de uma vez, mesmo pausado. E o pulso da batalha: 1 acao = 1 minuto. */
	DEVERAUXSIM_API void AdvanceGameMinutes(int32 Minutes);

	/** Avanca um intervalo exato de tempo de jogo, mesmo pausado. Intervalo negativo e ignorado. */
	DEVERAUXSIM_API void AdvanceGameTime(FTimespan GameDelta);

	/** Poe o relogio num instante exato, por exemplo ao carregar um save. Nao dispara eventos. */
	DEVERAUXSIM_API void SetGameTime(FTimespan NewGameTime);

	/** Tempo de jogo desde o dia 1 as 00:00. */
	FTimespan GetGameTime() const { return FTimespan(GameTicks); }

	/** Minutos inteiros de jogo desde o dia 1 as 00:00. */
	DEVERAUXSIM_API int64 GetTotalMinutes() const;

	/** Dia de jogo, comecando em 1. */
	DEVERAUXSIM_API int32 GetDay() const;

	/** Hora do dia, de 0 a 23. */
	DEVERAUXSIM_API int32 GetHour() const;

	/** Minuto da hora, de 0 a 59. */
	DEVERAUXSIM_API int32 GetMinute() const;

	/** Hora do dia como intervalo desde a meia-noite. */
	DEVERAUXSIM_API FTimespan GetTimeOfDay() const;

	/** Fracao do dia, de 0 (meia-noite) ate quase 1. Para sol e luz; nao use para somar tempo. */
	DEVERAUXSIM_API double GetTimeOfDayFraction() const;

	int32 GetTimeScale() const { return TimeScale; }

	/** Muda a escala, presa entre MinTimeScale e MaxTimeScale. */
	DEVERAUXSIM_API void SetTimeScale(int32 NewTimeScale);

	bool IsPaused() const { return bPaused; }
	void SetPaused(bool bInPaused) { bPaused = bInPaused; }

	/** Converte tempo real em tempo de jogo na escala atual. */
	DEVERAUXSIM_API FTimespan RealToGame(FTimespan RealDelta) const;

	/** Converte tempo de jogo em tempo real na escala atual. */
	DEVERAUXSIM_API FTimespan GameToReal(FTimespan GameDelta) const;

	/** "Dia 1, 08:00". Para log e depuracao, sem traducao. */
	DEVERAUXSIM_API FString ToDebugString() const;

	/**
	 * Grava ou le o instante do relogio, com numero de versao. Escala e pausa ficam de fora:
	 * sao estado de quem roda o jogo, e um save feito com o relogio pausado nao pode carregar travado.
	 * Versao desconhecida ou arquivo curto marcam erro no FArchive e deixam o relogio como estava.
	 */
	DEVERAUXSIM_API void Serialize(FArchive& Ar);

	/** Ver FDeverauxOnMinutePassed. Um ouvinte pode avancar o relogio de dentro do evento; os minutos continuam saindo em ordem. */
	FDeverauxOnMinutePassed OnMinutePassed;

private:
	void AdvanceGameTicks(int64 GameDeltaTicks);
	void BroadcastPendingMinutes();

	int64 GameTicks = 0;

	/** Ultimo minuto ja anunciado em OnMinutePassed. */
	int64 LastBroadcastMinute = 0;

	int32 TimeScale = DefaultTimeScale;
	bool bPaused = false;

	/** Sobra de tempo real abaixo de meio tick, levada para o proximo AdvanceReal. Fica entre -0,5 e 0,5. */
	double RealTickRemainder = 0.0;
};
