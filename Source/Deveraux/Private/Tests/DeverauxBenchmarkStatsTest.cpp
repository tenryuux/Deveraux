// Projeto Deveraux - testes das contas do benchmark (modulo Deveraux)
// Para rodar no editor: Window > Output Log, digite no campo Cmd: Automation RunTests Deveraux

#include "Benchmark/DeverauxBenchmarkStats.h"

#include "DeverauxSunPath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeverauxBenchmarkStatsTest, "Deveraux.Benchmark.Contas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace DeverauxBenchmarkStatsTestPrivate
{
	FDeverauxFrameSample MakeFrame(float FrameMs, float GameThreadMs, float RenderThreadMs, float GpuMs)
	{
		FDeverauxFrameSample Sample;
		Sample.FrameMs = FrameMs;
		Sample.GameThreadMs = GameThreadMs;
		Sample.RenderThreadMs = RenderThreadMs;
		Sample.GpuMs = GpuMs;
		return Sample;
	}

	FDeverauxStepResult MakeStep(double Hour, bool bTwilight, int32 NpcCount, bool bOptimized, bool bCounts, double FrameMs,
		double GameThreadMs, double RenderThreadMs, double GpuMs)
	{
		FDeverauxStepResult Result;
		Result.Hour = Hour;
		Result.bTwilight = bTwilight;
		Result.NpcCount = NpcCount;
		Result.bOptimizedNpcs = bOptimized;
		Result.bCountsForResult = bCounts;
		Result.Frames = 100;
		Result.AvgFrameMs = FrameMs;
		Result.Percentile99FrameMs = FrameMs * 1.2;
		Result.AvgGameThreadMs = GameThreadMs;
		Result.AvgRenderThreadMs = RenderThreadMs;
		Result.AvgGpuMs = GpuMs;
		return Result;
	}

	/** O benchmark padrao: ceu em tres horas, multidao ao meio-dia e 40 NPCs no crepusculo. */
	TArray<FDeverauxStepResult> MakeRun(double LastStepFrameMs)
	{
		TArray<FDeverauxStepResult> Results;
		Results.Add(MakeStep(12.0, false, 0, false, true, 10.0, 3.0, 2.0, 9.0));
		Results.Add(MakeStep(18.2, true, 0, false, true, 12.5, 3.0, 2.0, 11.5));
		Results.Add(MakeStep(0.0, false, 0, false, true, 11.0, 3.0, 2.0, 10.0));
		Results.Add(MakeStep(12.0, false, 10, false, true, 11.0, 3.5, 2.2, 9.5));
		Results.Add(MakeStep(12.0, false, 20, false, true, 12.0, 4.0, 2.4, 10.0));
		Results.Add(MakeStep(12.0, false, 40, false, true, 14.0, 5.0, 2.8, 11.0));
		Results.Add(MakeStep(12.0, false, 60, false, false, 17.0, 6.0, 3.2, 12.0));
		Results.Add(MakeStep(12.0, false, 80, false, false, 19.0, 7.0, 3.6, 13.0));
		Results.Add(MakeStep(12.0, false, 100, false, false, 21.0, 8.0, 4.0, 14.0));
		Results.Add(MakeStep(12.0, false, 100, true, false, 18.0, 6.0, 3.0, 13.5));
		Results.Add(MakeStep(18.2, true, 40, false, true, LastStepFrameMs, 5.0, 2.8, 15.0));
		return Results;
	}

	bool HasLine(const TArray<FString>& Lines, const TCHAR* Text)
	{
		for (const FString& Line : Lines)
		{
			if (Line.Contains(Text))
			{
				return true;
			}
		}
		return false;
	}
}

bool FDeverauxBenchmarkStatsTest::RunTest(const FString& Parameters)
{
	using namespace DeverauxBenchmarkStatsTestPrivate;

	// Media e 1% pior: 99 quadros de 10 ms e 1 de 40 ms
	{
		TArray<FDeverauxFrameSample> Samples;
		for (int32 Index = 0; Index < 99; ++Index)
		{
			Samples.Add(MakeFrame(10.0f, 4.0f, 3.0f, 9.0f));
		}
		Samples.Add(MakeFrame(40.0f, 4.0f, 3.0f, 9.0f));
		FDeverauxStepResult Result;
		DeverauxBenchmark::Summarize(Samples, Result);
		TestEqual(TEXT("100 quadros"), Result.Frames, 100);
		TestTrue(TEXT("Quadro medio de 10,3 ms"), FMath::IsNearlyEqual(Result.AvgFrameMs, 10.3, 1.0e-6));
		TestTrue(TEXT("1% pior: so o quadro de 40 ms foi mais lento que 10 ms"), FMath::IsNearlyEqual(Result.Percentile99FrameMs, 10.0, 1.0e-6));
		TestEqual(TEXT("O quadro de 40 ms e um engasgo"), Result.Hitches, 1);
		TestTrue(TEXT("A GPU segura o quadro"), FString(Result.Bottleneck()) == FString(TEXT("GPU")));

		Samples.Add(MakeFrame(40.0f, 4.0f, 3.0f, 9.0f));
		DeverauxBenchmark::Summarize(Samples, Result);
		TestTrue(TEXT("Dois quadros lentos em 101: o 1% pior ja e 40 ms"), FMath::IsNearlyEqual(Result.Percentile99FrameMs, 40.0, 1.0e-6));

		DeverauxBenchmark::Summarize(TArray<FDeverauxFrameSample>(), Result);
		TestEqual(TEXT("Sem quadros: zero"), Result.Frames, 0);
		TestTrue(TEXT("Sem quadros: 0 fps, que reprova"), Result.AvgFps() == 0.0);
	}

	// Veredito contra a meta de 60 e o minimo de 55
	{
		TestTrue(TEXT("60 fps: ok"), DeverauxBenchmark::Judge(60.0, 60.0, 55.0) == EDeverauxBenchmarkVerdict::Ok);
		TestTrue(TEXT("57 fps: toleravel"), DeverauxBenchmark::Judge(57.0, 60.0, 55.0) == EDeverauxBenchmarkVerdict::Tolerable);
		TestTrue(TEXT("55 fps: toleravel"), DeverauxBenchmark::Judge(55.0, 60.0, 55.0) == EDeverauxBenchmarkVerdict::Tolerable);
		TestTrue(TEXT("54,9 fps: estouro"), DeverauxBenchmark::Judge(54.9, 60.0, 55.0) == EDeverauxBenchmarkVerdict::Over);
	}

	// Crepusculo: o sol a 3 graus abaixo do horizonte, no fim da tarde
	{
		FDeverauxSunPathParams Params;
		Params.DeclinationDegrees = 0.0;
		double Hour = 0.0;

		Params.LatitudeDegrees = 0.0;
		TestTrue(TEXT("No equador o sol chega a -3 graus"), DeverauxBenchmark::FindEveningSunHour(Params, -3.0, Hour));
		TestEqual(TEXT("No equador: 18:12 (15 graus por hora)"), DeverauxBenchmark::FormatHour(Hour), FString(TEXT("18:12")));

		Params.LatitudeDegrees = 30.0;
		TestTrue(TEXT("A 30 graus o sol chega a -3 graus"), DeverauxBenchmark::FindEveningSunHour(Params, -3.0, Hour));
		TestEqual(TEXT("A 30 graus: 18:14, o sol desce inclinado"), DeverauxBenchmark::FormatHour(Hour), FString(TEXT("18:14")));

		Params.LatitudeDegrees = 80.0;
		Params.DeclinationDegrees = 23.44;
		Hour = 5.0;
		TestFalse(TEXT("Dia sem fim perto do polo: nao ha crepusculo"), DeverauxBenchmark::FindEveningSunHour(Params, -3.0, Hour));
		TestTrue(TEXT("Sem crepusculo, a hora fica como estava"), Hour == 5.0);
		Params.DeclinationDegrees = -23.44;
		TestFalse(TEXT("Noite sem fim: tambem nao"), DeverauxBenchmark::FindEveningSunHour(Params, -3.0, Hour));
	}

	// Hora em texto e a reta
	{
		TestEqual(TEXT("24 h vira 00:00"), DeverauxBenchmark::FormatHour(24.0), FString(TEXT("00:00")));
		TestEqual(TEXT("-0,5 h vira 23:30"), DeverauxBenchmark::FormatHour(-0.5), FString(TEXT("23:30")));

		TArray<double> X;
		TArray<double> Y;
		X.Add(0.0);
		X.Add(10.0);
		X.Add(20.0);
		Y.Add(1.0);
		Y.Add(2.0);
		Y.Add(3.0);
		double A = 0.0;
		double B = 0.0;
		TestTrue(TEXT("Reta por tres pontos"), DeverauxBenchmark::FitLine(X, Y, A, B));
		TestTrue(TEXT("y = 1 + 0,1 x"), FMath::IsNearlyEqual(A, 1.0, 1.0e-9) && FMath::IsNearlyEqual(B, 0.1, 1.0e-9));

		TArray<double> SameX;
		SameX.Add(5.0);
		SameX.Add(5.0);
		TestFalse(TEXT("Dois pontos no mesmo x nao fazem reta"), DeverauxBenchmark::FitLine(SameX, Y, A, B));
	}

	// Conclusoes do benchmark padrao, com o ultimo passo abaixo do minimo
	{
		bool bPassed = true;
		const TArray<FDeverauxStepResult> Results = MakeRun(19.0);
		const TArray<FString> Summary = DeverauxBenchmark::BuildSummary(Results, 60.0, 55.0, bPassed);
		TestFalse(TEXT("40 NPCs no crepusculo a 52,6 fps reprova"), bPassed);
		TestTrue(TEXT("O ceu sozinho em cada hora"), HasLine(Summary, TEXT("Sem NPCs, GPU: 12:00 9.0 ms (100 fps) | crepusculo 18:12 11.5 ms (80 fps) | 00:00 10.0 ms (91 fps)")));
		TestTrue(TEXT("Custo por NPC pela multidao do meio-dia"), HasLine(Summary, TEXT("Cada NPC custa (12:00, de 0 a 100 NPCs): game thread 0.050 ms, render thread 0.020 ms, GPU 0.050 ms")));
		TestTrue(TEXT("Maior multidao acima de 55 fps"), HasLine(Summary, TEXT("Maior multidao medida com 55 fps ou mais: 60 NPCs (58.8 fps)")));
		TestTrue(TEXT("Efeito da otimizacao"), HasLine(Summary, TEXT("Otimizacao com 100 NPCs: game thread 8.0 -> 6.0 ms, GPU 14.0 -> 13.5 ms, 47.6 -> 55.6 fps")));
		TestTrue(TEXT("O resultado diz qual passo reprovou"), HasLine(Summary, TEXT("RESULTADO: fora do orcamento. Abaixo de 55 fps: passo 11 (crepusculo 18:12, 40 NPCs, 52.6 fps).")));
		TestFalse(TEXT("Passos so informativos nao reprovam"), HasLine(Summary, TEXT("passo 8")));

		const TArray<FString> Table = DeverauxBenchmark::BuildTable(Results, 60.0, 55.0);
		TestEqual(TEXT("Uma linha por passo"), Table.Num(), Results.Num());
		TestTrue(TEXT("Linha do passo 7"), Table[6].Contains(TEXT(" 7. 12:00, 60 NPCs: 58.8 fps")) && Table[6].Contains(TEXT("toleravel (so informativo)")));
		TestTrue(TEXT("Linha do passo 10"), Table[9].Contains(TEXT("100 NPCs otimizados")));

		const FString Csv = DeverauxBenchmark::BuildCsv(Results, 60.0, 55.0);
		TestTrue(TEXT("CSV com cabecalho"), Csv.StartsWith(TEXT("passo,hora,crepusculo,npcs")));
		TestTrue(TEXT("CSV do passo 11"), Csv.Contains(TEXT("\n11,18:12,1,40,0,1,100,52.63,")));
	}

	// O mesmo benchmark com o ultimo passo acima do minimo passa
	{
		bool bPassed = false;
		const TArray<FString> Summary = DeverauxBenchmark::BuildSummary(MakeRun(17.5), 60.0, 55.0, bPassed);
		TestTrue(TEXT("57,1 fps no crepusculo passa"), bPassed);
		TestTrue(TEXT("Resultado dentro do orcamento"), HasLine(Summary, TEXT("RESULTADO: dentro do orcamento.")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
