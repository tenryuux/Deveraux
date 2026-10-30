// Projeto Deveraux - contas do benchmark, sem nada da engine: medias, veredito e relatorio (modulo Deveraux)

#pragma once

#include "CoreMinimal.h"

struct FDeverauxSunPathParams;

/** Tempos de um quadro, em ms, os mesmos que o stat unit mostra. */
struct FDeverauxFrameSample
{
	float FrameMs = 0.0f;
	float GameThreadMs = 0.0f;
	float RenderThreadMs = 0.0f;
	float RhiThreadMs = 0.0f;
	float GpuMs = 0.0f;
};

/** Como o passo ficou: na meta, entre a meta e o minimo, ou abaixo do minimo. */
enum class EDeverauxBenchmarkVerdict : uint8
{
	Ok,
	Tolerable,
	Over
};

/** O que um passo do benchmark mediu. */
struct FDeverauxStepResult
{
	/** Hora do relogio no passo, de 0 a 24. */
	double Hour = 0.0;
	bool bTwilight = false;
	int32 NpcCount = 0;
	bool bOptimizedNpcs = false;
	bool bCountsForResult = true;

	int32 Frames = 0;
	double AvgFrameMs = 0.0;
	/** So 1% dos quadros foi mais lento que este: o "1% pior" dos benchmarks. */
	double Percentile99FrameMs = 0.0;
	double AvgGameThreadMs = 0.0;
	double AvgRenderThreadMs = 0.0;
	double AvgRhiThreadMs = 0.0;
	double AvgGpuMs = 0.0;
	/** Quadros acima de 33 ms, que se notam como engasgo. */
	int32 Hitches = 0;

	double AvgFps() const;
	double LowFps() const;

	/** "12:00" ou "crepusculo 18:12". */
	FString DescribeTime() const;

	/** "12:00, 40 NPCs". */
	FString Describe() const;

	/** O que segura o quadro: game thread, render thread, RHI ou GPU, o maior dos tempos medios. */
	const TCHAR* Bottleneck() const;
};

namespace DeverauxBenchmark
{
	/** Quadro acima disto conta como engasgo. */
	constexpr double HitchFrameMs = 1000.0 / 30.0;

	/** Junta os quadros medidos no resultado do passo. Sem quadros, os tempos ficam em 0. */
	void Summarize(const TArray<FDeverauxFrameSample>& Samples, FDeverauxStepResult& InOutResult);

	EDeverauxBenchmarkVerdict Judge(double Fps, double TargetFps, double MinimumFps);
	const TCHAR* VerdictText(EDeverauxBenchmarkVerdict Verdict);

	/**
	 * Hora do fim da tarde em que o sol desce ate essa altura, em graus (negativa = abaixo do horizonte).
	 * false quando ele nao chega la, como no dia ou na noite sem fim perto dos polos.
	 */
	bool FindEveningSunHour(const FDeverauxSunPathParams& Params, double ElevationDegrees, double& OutHour);

	/** 18.2 vira "18:12". */
	FString FormatHour(double Hour);

	/** Reta y = A + B x pelos minimos quadrados. false com menos de dois valores de x diferentes. */
	bool FitLine(const TArray<double>& X, const TArray<double>& Y, double& OutA, double& OutB);

	/** Uma linha por passo, para a tela, o log e o arquivo. */
	TArray<FString> BuildTable(const TArray<FDeverauxStepResult>& Results, double TargetFps, double MinimumFps);

	/**
	 * Conclusoes: custo do ceu, custo de cada NPC, maior multidao acima do minimo, efeito da otimizacao
	 * e o resultado. bOutPassed fica false se um passo que conta ficou abaixo do minimo.
	 */
	TArray<FString> BuildSummary(const TArray<FDeverauxStepResult>& Results, double TargetFps, double MinimumFps, bool& bOutPassed);

	/** Os mesmos numeros em CSV, para abrir numa planilha. */
	FString BuildCsv(const TArray<FDeverauxStepResult>& Results, double TargetFps, double MinimumFps);
}
