// Projeto Deveraux - testes do relogio do jogo (DeverauxSim)
// Para rodar no editor: Window > Output Log, digite no campo Cmd: Automation RunTests Deveraux

#include "DeverauxGameClock.h"

#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeverauxGameClockTest, "Deveraux.Sim.Relogio",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDeverauxGameClockTest::RunTest(const FString& Parameters)
{
	// Escala padrao: 5 s reais viram 1 minuto de jogo
	{
		FDeverauxGameClock Clock;
		for (int32 Second = 0; Second < 5; ++Second)
		{
			Clock.AdvanceReal(1.0);
		}
		TestEqual(TEXT("5 s reais = 1 minuto de jogo"), Clock.GetGameTime().GetTicks(), FTimespan::FromMinutes(1.0).GetTicks());
	}

	// Quadros de qualquer tamanho somam o tempo exato: 10 min reais = 2 h de jogo
	{
		const int32 FrameRates[] = { 30, 60, 144 };
		for (const int32 FrameRate : FrameRates)
		{
			FDeverauxGameClock Clock;
			const int32 Frames = FrameRate * 10 * 60;
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Clock.AdvanceReal(1.0 / FrameRate);
			}
			TestEqual(*FString::Printf(TEXT("%d fps: 10 min reais = 2 h de jogo exatas"), FrameRate),
				Clock.GetGameTime().GetTicks(), FTimespan::FromHours(2.0).GetTicks());
		}
	}

	// 2 h reais a 60 fps, comecando as 08:00 do dia 1, terminam as 08:00 do dia 2
	{
		FDeverauxGameClock Clock;
		Clock.SetGameTime(FTimespan::FromHours(8.0));
		const int32 Frames = 2 * 60 * 60 * 60;
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Clock.AdvanceReal(1.0 / 60.0);
		}
		TestEqual(TEXT("2 h reais = 24 h de jogo"), Clock.ToDebugString(), FString(TEXT("Dia 2, 08:00")));
		TestEqual(TEXT("2 h reais = 24 h de jogo, ao tick"), Clock.GetGameTime().GetTicks(), FTimespan::FromHours(32.0).GetTicks());
	}

	// Virada do dia
	{
		FDeverauxGameClock Clock;
		Clock.SetGameTime(FTimespan::FromMinutes(23.0 * 60.0 + 59.0));
		TestEqual(TEXT("Antes da virada"), Clock.ToDebugString(), FString(TEXT("Dia 1, 23:59")));
		Clock.AdvanceGameMinutes(1);
		TestEqual(TEXT("Depois da virada"), Clock.ToDebugString(), FString(TEXT("Dia 2, 00:00")));
		TestEqual(TEXT("Dia 2 em numero"), Clock.GetDay(), 2);
		TestEqual(TEXT("Hora 0"), Clock.GetHour(), 0);
		TestEqual(TEXT("Minuto 0"), Clock.GetMinute(), 0);
	}

	// Fracao do dia, para o sol
	{
		FDeverauxGameClock Clock;
		Clock.SetGameTime(FTimespan::FromDays(2.0) + FTimespan::FromHours(18.0));
		TestTrue(TEXT("18:00 = 0,75 do dia"), FMath::IsNearlyEqual(Clock.GetTimeOfDayFraction(), 0.75));
	}

	// Pausado, o tempo real nao conta, mas o pulso da batalha conta
	{
		FDeverauxGameClock Clock;
		Clock.SetPaused(true);
		Clock.AdvanceReal(0.5);
		TestEqual(TEXT("Pausado: tempo real nao anda"), Clock.GetGameTime().GetTicks(), static_cast<int64>(0));
		Clock.AdvanceGameMinutes(1);
		TestEqual(TEXT("Pausado: o pulso da batalha anda 1 minuto"), Clock.GetTotalMinutes(), static_cast<int64>(1));
	}

	// O tempo nunca volta
	{
		FDeverauxGameClock Clock;
		Clock.AdvanceReal(-1.0);
		Clock.AdvanceReal(0.0);
		Clock.AdvanceGameMinutes(-5);
		Clock.AdvanceGameTime(FTimespan::FromMinutes(-10.0));
		TestEqual(TEXT("Valores negativos sao ignorados"), Clock.GetGameTime().GetTicks(), static_cast<int64>(0));
	}

	// Um evento por minuto inteiro: 12,5 s reais a partir das 08:00 = 08:01 e 08:02
	{
		FDeverauxGameClock Clock;
		Clock.SetGameTime(FTimespan::FromHours(8.0));
		TArray<int64> Minutes;
		Clock.OnMinutePassed.AddLambda([&Minutes](int64 TotalMinutes) { Minutes.Add(TotalMinutes); });
		for (int32 Step = 0; Step < 25; ++Step)
		{
			Clock.AdvanceReal(0.5);
		}
		if (TestEqual(TEXT("Dois minutos inteiros passaram"), Minutes.Num(), 2))
		{
			TestEqual(TEXT("Primeiro evento: 08:01"), Minutes[0], static_cast<int64>(8 * 60 + 1));
			TestEqual(TEXT("Segundo evento: 08:02"), Minutes[1], static_cast<int64>(8 * 60 + 2));
		}
	}

	// Pulo grande de uma vez dispara um evento para cada minuto
	{
		FDeverauxGameClock Clock;
		TArray<int64> Minutes;
		Clock.OnMinutePassed.AddLambda([&Minutes](int64 TotalMinutes) { Minutes.Add(TotalMinutes); });
		Clock.AdvanceGameMinutes(90);
		if (TestEqual(TEXT("90 minutos de uma vez = 90 eventos"), Minutes.Num(), 90))
		{
			TestEqual(TEXT("Primeiro evento: minuto 1"), Minutes[0], static_cast<int64>(1));
			TestEqual(TEXT("Ultimo evento: minuto 90"), Minutes[89], static_cast<int64>(90));
		}
	}

	// Um ouvinte que avanca o relogio dentro do evento nao tira os minutos de ordem
	{
		FDeverauxGameClock Clock;
		TArray<int64> Minutes;
		Clock.OnMinutePassed.AddLambda([&Minutes, &Clock](int64 TotalMinutes)
		{
			Minutes.Add(TotalMinutes);
			if (TotalMinutes == 2)
			{
				Clock.AdvanceGameMinutes(3);
			}
		});
		Clock.AdvanceGameMinutes(3);
		if (TestEqual(TEXT("3 + 3 minutos = 6 eventos"), Minutes.Num(), 6))
		{
			for (int32 Index = 0; Index < Minutes.Num(); ++Index)
			{
				TestEqual(*FString::Printf(TEXT("Evento %d em ordem"), Index + 1), Minutes[Index], static_cast<int64>(Index + 1));
			}
		}
		TestEqual(TEXT("Relogio em 6 minutos"), Clock.GetTotalMinutes(), static_cast<int64>(6));
	}

	// Quadro travado conta no maximo 1 s real
	{
		FDeverauxGameClock Clock;
		Clock.AdvanceReal(300.0);
		TestEqual(TEXT("5 min reais travados contam como 1 s real"), Clock.GetGameTime().GetTicks(), FTimespan::FromSeconds(12.0).GetTicks());
	}

	// Escala presa nos limites, e conversoes
	{
		FDeverauxGameClock Clock;
		TestEqual(TEXT("5 s reais = 1 min de jogo"), Clock.RealToGame(FTimespan::FromSeconds(5.0)).GetTicks(), FTimespan::FromMinutes(1.0).GetTicks());
		TestEqual(TEXT("1 dia de jogo = 2 h reais"), Clock.GameToReal(FTimespan::FromDays(1.0)).GetTicks(), FTimespan::FromHours(2.0).GetTicks());
		Clock.SetTimeScale(0);
		TestEqual(TEXT("Escala minima"), Clock.GetTimeScale(), FDeverauxGameClock::MinTimeScale);
		Clock.SetTimeScale(100000);
		TestEqual(TEXT("Escala maxima"), Clock.GetTimeScale(), FDeverauxGameClock::MaxTimeScale);
	}

	// O save guarda o instante; escala e pausa ficam com quem roda o jogo
	{
		FDeverauxGameClock Original;
		Original.SetGameTime(FTimespan::FromDays(3.0) + FTimespan::FromMinutes(615.0));
		Original.SetTimeScale(24);
		Original.SetPaused(true);

		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		Original.Serialize(Writer);

		FDeverauxGameClock Loaded;
		int32 Events = 0;
		Loaded.OnMinutePassed.AddLambda([&Events](int64) { ++Events; });
		FMemoryReader Reader(Bytes);
		Loaded.Serialize(Reader);

		TestFalse(TEXT("Leitura sem erro"), Reader.IsError());
		TestEqual(TEXT("Mesmo instante"), Loaded.GetGameTime().GetTicks(), Original.GetGameTime().GetTicks());
		TestEqual(TEXT("Texto do instante"), Loaded.ToDebugString(), FString(TEXT("Dia 4, 10:15")));
		TestEqual(TEXT("Carregar nao dispara eventos"), Events, 0);
		TestEqual(TEXT("A escala nao vem do save"), Loaded.GetTimeScale(), FDeverauxGameClock::DefaultTimeScale);
		TestFalse(TEXT("A pausa nao vem do save"), Loaded.IsPaused());

		Loaded.AdvanceGameMinutes(1);
		TestEqual(TEXT("Depois de carregar, o proximo evento e o minuto seguinte"), Events, 1);
	}

	// Versao desconhecida: marca erro e nao mexe no relogio
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		int32 FutureVersion = FDeverauxGameClock::SaveVersion + 1;
		int64 Ticks = FTimespan::FromDays(9.0).GetTicks();
		Writer << FutureVersion;
		Writer << Ticks;

		FDeverauxGameClock Clock;
		Clock.SetGameTime(FTimespan::FromHours(8.0));
		FMemoryReader Reader(Bytes);
		Clock.Serialize(Reader);

		TestTrue(TEXT("Versao desconhecida marca erro"), Reader.IsError());
		TestEqual(TEXT("Relogio continua como estava"), Clock.ToDebugString(), FString(TEXT("Dia 1, 08:00")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
