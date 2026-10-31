// Projeto Deveraux - contas do benchmark, sem nada da engine: medias, veredito e relatorio (modulo Deveraux)

#include "Benchmark/DeverauxBenchmarkStats.h"

#include "DeverauxSunPath.h"

namespace DeverauxBenchmarkStatsPrivate
{
	constexpr int32 MinutesPerDay = 24 * 60;

	/** A busca do crepusculo anda de minuto em minuto. */
	constexpr int32 SearchStepsPerHour = 60;

	/** Duas horas sao a mesma se ficam a menos de meio minuto uma da outra. */
	constexpr double SameHourTolerance = 0.5 / 60.0;

	double SunElevationAt(double Hour, const FDeverauxSunPathParams& Params)
	{
		double Fraction = FMath::Fmod(Hour / 24.0, 1.0);
		if (Fraction < 0.0)
		{
			Fraction += 1.0;
		}
		return DeverauxSky::ComputeSkyState(Fraction, Params).SunElevationDegrees;
	}

	bool SameTime(const FDeverauxStepResult& A, const FDeverauxStepResult& B)
	{
		return A.bTwilight == B.bTwilight && FMath::Abs(A.Hour - B.Hour) < SameHourTolerance;
	}

	/** Mesma hora e mesmo tipo de NPC: so assim quantidades diferentes de NPCs se comparam. */
	bool SameScene(const FDeverauxStepResult& A, const FDeverauxStepResult& B)
	{
		return SameTime(A, B) && A.bOptimizedNpcs == B.bOptimizedNpcs;
	}

	int32 CountDistinctNpcCounts(const TArray<FDeverauxStepResult>& Results, const TArray<int32>& Indices)
	{
		TArray<int32> Seen;
		for (const int32 Index : Indices)
		{
			Seen.AddUnique(Results[Index].NpcCount);
		}
		return Seen.Num();
	}

	/**
	 * A game thread ou a GPU que ocupa pelo menos esta parte do quadro e quem segura o quadro. Mesmo no limite, a GPU
	 * fica parada um pouco entre um quadro e outro: no benchmark de 31/10/2026, ela ficou ocupada 93% do quadro.
	 */
	constexpr double BusyFrameShare = 0.85;

	/** A game thread ou a GPU ocupa quase o quadro inteiro. Nenhuma das duas conta espera no stat unit. */
	bool IsGameOrGpuBound(const FDeverauxStepResult& Result)
	{
		return FMath::Max(Result.AvgGameThreadMs, Result.AvgGpuMs) >= BusyFrameShare * Result.AvgFrameMs;
	}
}

double FDeverauxStepResult::AvgFps() const
{
	return AvgFrameMs > 0.0 ? 1000.0 / AvgFrameMs : 0.0;
}

double FDeverauxStepResult::LowFps() const
{
	return Percentile99FrameMs > 0.0 ? 1000.0 / Percentile99FrameMs : 0.0;
}

FString FDeverauxStepResult::DescribeTime() const
{
	const FString Time = DeverauxBenchmark::FormatHour(Hour);
	return bTwilight ? FString::Printf(TEXT("crepusculo %s"), *Time) : Time;
}

FString FDeverauxStepResult::Describe() const
{
	return FString::Printf(TEXT("%s, %d NPCs%s"), *DescribeTime(), NpcCount, bOptimizedNpcs ? TEXT(" otimizados") : TEXT(""));
}

const TCHAR* FDeverauxStepResult::Bottleneck() const
{
	using namespace DeverauxBenchmarkStatsPrivate;

	// O stat unit conta, no render thread e no RHI, o tempo que eles passam esperando a GPU: com ela ocupada, eles
	// esticam ate perto do tempo do quadro sem estar trabalhando. A game thread e a GPU nao contam espera
	if (IsGameOrGpuBound(*this))
	{
		return AvgGpuMs > AvgGameThreadMs ? TEXT("GPU") : TEXT("game thread");
	}

	// Nenhuma das duas perto do quadro inteiro: o maior dos quatro
	const TCHAR* Name = TEXT("game thread");
	double Worst = AvgGameThreadMs;
	if (AvgRenderThreadMs > Worst)
	{
		Worst = AvgRenderThreadMs;
		Name = TEXT("render thread");
	}
	if (AvgRhiThreadMs > Worst)
	{
		Worst = AvgRhiThreadMs;
		Name = TEXT("RHI");
	}
	if (AvgGpuMs > Worst)
	{
		Name = TEXT("GPU");
	}
	return Name;
}

void DeverauxBenchmark::Summarize(const TArray<FDeverauxFrameSample>& Samples, FDeverauxStepResult& InOutResult)
{
	InOutResult.Frames = Samples.Num();
	InOutResult.AvgFrameMs = 0.0;
	InOutResult.Percentile99FrameMs = 0.0;
	InOutResult.AvgGameThreadMs = 0.0;
	InOutResult.AvgRenderThreadMs = 0.0;
	InOutResult.AvgRhiThreadMs = 0.0;
	InOutResult.AvgGpuMs = 0.0;
	InOutResult.Hitches = 0;
	if (Samples.Num() == 0)
	{
		return;
	}

	TArray<double> FrameTimes;
	FrameTimes.Reserve(Samples.Num());
	for (const FDeverauxFrameSample& Sample : Samples)
	{
		InOutResult.AvgFrameMs += Sample.FrameMs;
		InOutResult.AvgGameThreadMs += Sample.GameThreadMs;
		InOutResult.AvgRenderThreadMs += Sample.RenderThreadMs;
		InOutResult.AvgRhiThreadMs += Sample.RhiThreadMs;
		InOutResult.AvgGpuMs += Sample.GpuMs;
		FrameTimes.Add(Sample.FrameMs);
		if (Sample.FrameMs > HitchFrameMs)
		{
			++InOutResult.Hitches;
		}
	}

	const double Count = static_cast<double>(Samples.Num());
	InOutResult.AvgFrameMs /= Count;
	InOutResult.AvgGameThreadMs /= Count;
	InOutResult.AvgRenderThreadMs /= Count;
	InOutResult.AvgRhiThreadMs /= Count;
	InOutResult.AvgGpuMs /= Count;

	// 99o percentil: so 1% dos quadros foi mais lento que este
	FrameTimes.Sort();
	const int32 Index = FMath::Clamp(FMath::CeilToInt(static_cast<float>(0.99 * Count)) - 1, 0, FrameTimes.Num() - 1);
	InOutResult.Percentile99FrameMs = FrameTimes[Index];
}

EDeverauxBenchmarkVerdict DeverauxBenchmark::Judge(double Fps, double TargetFps, double MinimumFps)
{
	if (Fps >= TargetFps)
	{
		return EDeverauxBenchmarkVerdict::Ok;
	}
	return Fps >= MinimumFps ? EDeverauxBenchmarkVerdict::Tolerable : EDeverauxBenchmarkVerdict::Over;
}

const TCHAR* DeverauxBenchmark::VerdictText(EDeverauxBenchmarkVerdict Verdict)
{
	if (Verdict == EDeverauxBenchmarkVerdict::Ok)
	{
		return TEXT("ok");
	}
	return Verdict == EDeverauxBenchmarkVerdict::Tolerable ? TEXT("toleravel") : TEXT("estouro");
}

bool DeverauxBenchmark::FindEveningSunHour(const FDeverauxSunPathParams& Params, double ElevationDegrees, double& OutHour)
{
	using namespace DeverauxBenchmarkStatsPrivate;

	// Do meio-dia solar ate meia volta depois, minuto a minuto, procura a primeira vez abaixo da altura pedida
	double PreviousHour = Params.SolarNoonHour;
	double PreviousElevation = SunElevationAt(PreviousHour, Params);
	if (PreviousElevation <= ElevationDegrees)
	{
		return false;
	}

	for (int32 Step = 1; Step <= 12 * SearchStepsPerHour; ++Step)
	{
		const double Hour = Params.SolarNoonHour + static_cast<double>(Step) / SearchStepsPerHour;
		const double Elevation = SunElevationAt(Hour, Params);
		if (Elevation <= ElevationDegrees)
		{
			// Entre dois minutos, a altura do sol muda quase em linha reta
			const double Alpha = (PreviousElevation - ElevationDegrees) / (PreviousElevation - Elevation);
			OutHour = FMath::Fmod(PreviousHour + Alpha * (Hour - PreviousHour), 24.0);
			if (OutHour < 0.0)
			{
				OutHour += 24.0;
			}
			return true;
		}
		PreviousHour = Hour;
		PreviousElevation = Elevation;
	}
	return false;
}

FString DeverauxBenchmark::FormatHour(double Hour)
{
	using namespace DeverauxBenchmarkStatsPrivate;

	const int32 Minutes = (FMath::RoundToInt(static_cast<float>(Hour * 60.0)) % MinutesPerDay + MinutesPerDay) % MinutesPerDay;
	return FString::Printf(TEXT("%02d:%02d"), Minutes / 60, Minutes % 60);
}

bool DeverauxBenchmark::FitLine(const TArray<double>& X, const TArray<double>& Y, double& OutA, double& OutB)
{
	const int32 Count = FMath::Min(X.Num(), Y.Num());
	if (Count < 2)
	{
		return false;
	}

	double MeanX = 0.0;
	double MeanY = 0.0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		MeanX += X[Index];
		MeanY += Y[Index];
	}
	MeanX /= Count;
	MeanY /= Count;

	double SumXX = 0.0;
	double SumXY = 0.0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const double DeltaX = X[Index] - MeanX;
		SumXX += DeltaX * DeltaX;
		SumXY += DeltaX * (Y[Index] - MeanY);
	}
	if (SumXX <= 1.0e-9)
	{
		return false;
	}

	OutB = SumXY / SumXX;
	OutA = MeanY - OutB * MeanX;
	return true;
}

TArray<FString> DeverauxBenchmark::BuildTable(const TArray<FDeverauxStepResult>& Results, double TargetFps, double MinimumFps)
{
	TArray<FString> Lines;
	for (int32 Index = 0; Index < Results.Num(); ++Index)
	{
		const FDeverauxStepResult& Result = Results[Index];
		FString Line = FString::Printf(TEXT("%2d. %s: %.1f fps (1%% pior %.1f) | game %.1f | render %.1f | RHI %.1f | GPU %.1f ms | limite: %s | %s"),
			Index + 1, *Result.Describe(), Result.AvgFps(), Result.LowFps(), Result.AvgGameThreadMs, Result.AvgRenderThreadMs,
			Result.AvgRhiThreadMs, Result.AvgGpuMs, Result.Bottleneck(), VerdictText(Judge(Result.AvgFps(), TargetFps, MinimumFps)));
		if (Result.Hitches > 0)
		{
			Line += FString::Printf(TEXT(" | %d engasgos"), Result.Hitches);
		}
		if (!Result.bCountsForResult)
		{
			Line += FString(TEXT(" (so informativo)"));
		}
		Lines.Add(Line);
	}
	return Lines;
}

TArray<FString> DeverauxBenchmark::BuildSummary(const TArray<FDeverauxStepResult>& Results, double TargetFps, double MinimumFps, bool& bOutPassed)
{
	using namespace DeverauxBenchmarkStatsPrivate;

	TArray<FString> Lines;

	// O ceu sozinho: os passos sem NPCs
	FString Sky;
	for (const FDeverauxStepResult& Result : Results)
	{
		if (Result.NpcCount == 0)
		{
			Sky += FString::Printf(TEXT("%s%s %.1f ms (%.0f fps)"), Sky.IsEmpty() ? TEXT("") : TEXT(" | "), *Result.DescribeTime(),
				Result.AvgGpuMs, Result.AvgFps());
		}
	}
	if (!Sky.IsEmpty())
	{
		Lines.Add(FString(TEXT("Sem NPCs, GPU: ")) + Sky);
	}

	// A multidao: a sequencia da mesma cena com mais quantidades diferentes de NPCs
	TArray<int32> Crowd;
	for (int32 Index = 0; Index < Results.Num(); ++Index)
	{
		TArray<int32> Group;
		for (int32 Other = 0; Other < Results.Num(); ++Other)
		{
			if (SameScene(Results[Index], Results[Other]))
			{
				Group.Add(Other);
			}
		}
		if (CountDistinctNpcCounts(Results, Group) > CountDistinctNpcCounts(Results, Crowd))
		{
			Crowd = Group;
		}
	}
	if (CountDistinctNpcCounts(Results, Crowd) >= 2)
	{
		TArray<double> Counts;
		TArray<double> GameThread;
		TArray<double> Gpu;
		int32 MinCount = MAX_int32;
		int32 MaxCount = 0;
		for (const int32 Index : Crowd)
		{
			const FDeverauxStepResult& Result = Results[Index];
			Counts.Add(Result.NpcCount);
			GameThread.Add(Result.AvgGameThreadMs);
			Gpu.Add(Result.AvgGpuMs);
			MinCount = FMath::Min(MinCount, Result.NpcCount);
			MaxCount = FMath::Max(MaxCount, Result.NpcCount);
		}

		// Sem o render thread: ele conta a espera pela GPU, que some quando a game thread passa a segurar o quadro,
		// e a conta por NPC sai ate negativa
		double Intercept = 0.0;
		double GameSlope = 0.0;
		double GpuSlope = 0.0;
		FitLine(Counts, GameThread, Intercept, GameSlope);
		FitLine(Counts, Gpu, Intercept, GpuSlope);
		Lines.Add(FString::Printf(TEXT("Cada NPC custa (%s, de %d a %d NPCs): game thread %.3f ms, GPU %.3f ms"),
			*Results[Crowd[0]].DescribeTime(), MinCount, MaxCount, GameSlope, GpuSlope));

		int32 Largest = 0;
		double LargestFps = 0.0;
		for (const int32 Index : Crowd)
		{
			const FDeverauxStepResult& Result = Results[Index];
			if (Result.NpcCount > Largest && Result.AvgFps() >= MinimumFps)
			{
				Largest = Result.NpcCount;
				LargestFps = Result.AvgFps();
			}
		}
		Lines.Add(Largest > 0
			? FString::Printf(TEXT("Maior multidao medida com %.0f fps ou mais: %d NPCs (%.1f fps)"), MinimumFps, Largest, LargestFps)
			: FString::Printf(TEXT("Nenhuma multidao medida ficou em %.0f fps ou mais"), MinimumFps));
	}

	// Otimizacao: o mesmo numero de NPCs, na mesma hora, com e sem
	for (const FDeverauxStepResult& Optimized : Results)
	{
		if (!Optimized.bOptimizedNpcs || Optimized.NpcCount == 0)
		{
			continue;
		}
		for (const FDeverauxStepResult& Plain : Results)
		{
			if (!Plain.bOptimizedNpcs && Plain.NpcCount == Optimized.NpcCount && SameTime(Plain, Optimized))
			{
				Lines.Add(FString::Printf(TEXT("Otimizacao com %d NPCs: game thread %.1f -> %.1f ms, GPU %.1f -> %.1f ms, %.1f -> %.1f fps"),
					Optimized.NpcCount, Plain.AvgGameThreadMs, Optimized.AvgGameThreadMs, Plain.AvgGpuMs, Optimized.AvgGpuMs,
					Plain.AvgFps(), Optimized.AvgFps()));
				break;
			}
		}
	}

	// O render thread ou o RHI marcando mais que o limite apontado: sem esta nota, a tabela parece errada
	for (const FDeverauxStepResult& Result : Results)
	{
		if (IsGameOrGpuBound(Result)
			&& FMath::Max(Result.AvgRenderThreadMs, Result.AvgRhiThreadMs) > FMath::Max(Result.AvgGameThreadMs, Result.AvgGpuMs))
		{
			Lines.Add(TEXT("Limite: o render thread e o RHI contam tambem o tempo esperando a GPU. Quando a game thread ou a GPU ocupa quase o quadro inteiro, o limite e ela, mesmo com o render thread marcando mais."));
			break;
		}
	}

	// Resultado: todo passo que conta precisa ficar no minimo
	bOutPassed = true;
	FString Failed;
	for (int32 Index = 0; Index < Results.Num(); ++Index)
	{
		const FDeverauxStepResult& Result = Results[Index];
		if (Result.bCountsForResult && Result.AvgFps() < MinimumFps)
		{
			bOutPassed = false;
			Failed += FString::Printf(TEXT("%s%d (%s, %.1f fps)"), Failed.IsEmpty() ? TEXT("") : TEXT("; "), Index + 1, *Result.Describe(), Result.AvgFps());
		}
	}
	Lines.Add(bOutPassed
		? FString::Printf(TEXT("RESULTADO: dentro do orcamento. Os passos que contam ficaram em %.0f fps ou mais (meta %.0f)."), MinimumFps, TargetFps)
		: FString::Printf(TEXT("RESULTADO: fora do orcamento. Abaixo de %.0f fps: passo %s."), MinimumFps, *Failed));
	return Lines;
}

FString DeverauxBenchmark::BuildCsv(const TArray<FDeverauxStepResult>& Results, double TargetFps, double MinimumFps)
{
	FString Csv(TEXT("passo,hora,crepusculo,npcs,otimizados,conta,quadros,fps_medio,fps_1pct_pior,quadro_ms,quadro_p99_ms,game_ms,render_ms,rhi_ms,gpu_ms,engasgos,limite,resultado\n"));
	for (int32 Index = 0; Index < Results.Num(); ++Index)
	{
		const FDeverauxStepResult& Result = Results[Index];
		Csv += FString::Printf(TEXT("%d,%s,%d,%d,%d,%d,%d,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%s,%s\n"),
			Index + 1, *FormatHour(Result.Hour), Result.bTwilight ? 1 : 0, Result.NpcCount, Result.bOptimizedNpcs ? 1 : 0,
			Result.bCountsForResult ? 1 : 0, Result.Frames, Result.AvgFps(), Result.LowFps(), Result.AvgFrameMs, Result.Percentile99FrameMs,
			Result.AvgGameThreadMs, Result.AvgRenderThreadMs, Result.AvgRhiThreadMs, Result.AvgGpuMs, Result.Hitches, Result.Bottleneck(),
			VerdictText(Judge(Result.AvgFps(), TargetFps, MinimumFps)));
	}
	return Csv;
}
