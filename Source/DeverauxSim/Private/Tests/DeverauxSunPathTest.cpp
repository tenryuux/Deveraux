// Projeto Deveraux - testes do caminho do sol e da lua (DeverauxSim)
// Para rodar no editor: Tools > Test Automation, marque Deveraux e clique em Start Tests.

#include "DeverauxSunPath.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeverauxSunPathTest, "Deveraux.Sim.Ceu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace DeverauxSunPathTestPrivate
{
	double Fraction(double Hours)
	{
		return Hours / 24.0;
	}
}

bool FDeverauxSunPathTest::RunTest(const FString& Parameters)
{
	using namespace DeverauxSunPathTestPrivate;

	const double Tolerance = 1.e-6;
	FDeverauxSunPathParams Params;
	Params.LatitudeDegrees = 30.0;

	// Equinocio a 30 graus de latitude: nasce no leste as 06:00, meio-dia a 60 graus no sul, se poe no oeste as 18:00
	{
		const FDeverauxSkyState Sunrise = DeverauxSky::ComputeSkyState(Fraction(6.0), Params);
		TestTrue(TEXT("06:00: sol no leste, no horizonte"), Sunrise.SunDirection.Equals(FVector(0.0, 1.0, 0.0), Tolerance));

		const FDeverauxSkyState Noon = DeverauxSky::ComputeSkyState(Fraction(12.0), Params);
		TestTrue(TEXT("12:00: sol a 60 graus"), FMath::IsNearlyEqual(Noon.SunElevationDegrees, 60.0, Tolerance));
		TestTrue(TEXT("12:00: sol no sul"), Noon.SunDirection.Equals(FVector(-0.5, 0.0, FMath::Sqrt(3.0) / 2.0), Tolerance));

		const FDeverauxSkyState Sunset = DeverauxSky::ComputeSkyState(Fraction(18.0), Params);
		TestTrue(TEXT("18:00: sol no oeste, no horizonte"), Sunset.SunDirection.Equals(FVector(0.0, -1.0, 0.0), Tolerance));

		const FDeverauxSkyState Midnight = DeverauxSky::ComputeSkyState(0.0, Params);
		TestTrue(TEXT("00:00: sol 60 graus abaixo do horizonte"), FMath::IsNearlyEqual(Midnight.SunElevationDegrees, -60.0, Tolerance));
	}

	// Direcoes sempre unitarias, e a lua no ponto oposto ao sol
	{
		for (int32 Minute = 0; Minute < 24 * 60; Minute += 7)
		{
			const FDeverauxSkyState State = DeverauxSky::ComputeSkyState(Minute / (24.0 * 60.0), Params);
			if (!TestTrue(*FString::Printf(TEXT("Minuto %d: sol unitario"), Minute), FMath::IsNearlyEqual(State.SunDirection.Size(), 1.0, Tolerance))
				|| !TestTrue(*FString::Printf(TEXT("Minuto %d: lua oposta ao sol"), Minute), State.MoonDirection.Equals(-State.SunDirection, Tolerance)))
			{
				break;
			}
		}
	}

	// O sol sobe a manha toda e desce a tarde toda
	{
		double Previous = -90.0;
		bool bRising = true;
		for (int32 Minute = 6 * 60; Minute <= 12 * 60; Minute += 5)
		{
			const double Elevation = DeverauxSky::ComputeSkyState(Minute / (24.0 * 60.0), Params).SunElevationDegrees;
			bRising = bRising && Elevation > Previous;
			Previous = Elevation;
		}
		TestTrue(TEXT("Das 06:00 ao meio-dia o sol so sobe"), bRising);

		const double Morning = DeverauxSky::ComputeSkyState(Fraction(9.0), Params).SunElevationDegrees;
		const double Afternoon = DeverauxSky::ComputeSkyState(Fraction(15.0), Params).SunElevationDegrees;
		TestTrue(TEXT("09:00 e 15:00 tem a mesma altura"), FMath::IsNearlyEqual(Morning, Afternoon, Tolerance));
	}

	// Quanto e dia: 1 de dia, 0 de noite, metade com o sol no horizonte
	{
		TestTrue(TEXT("Meio-dia: dia"), FMath::IsNearlyEqual(DeverauxSky::ComputeSkyState(Fraction(12.0), Params).DayAlpha, 1.0, Tolerance));
		TestTrue(TEXT("Meia-noite: noite"), FMath::IsNearlyEqual(DeverauxSky::ComputeSkyState(0.0, Params).DayAlpha, 0.0, Tolerance));
		TestTrue(TEXT("06:00: metade"), FMath::IsNearlyEqual(DeverauxSky::ComputeSkyState(Fraction(6.0), Params).DayAlpha, 0.5, Tolerance));
		TestFalse(TEXT("07:00 nao e noite"), DeverauxSky::ComputeSkyState(Fraction(7.0), Params).IsNight());
		TestTrue(TEXT("19:00 e noite"), DeverauxSky::ComputeSkyState(Fraction(19.0), Params).IsNight());
	}

	// Declinacao positiva: dia mais longo, o sol ja nasceu as 06:00
	{
		FDeverauxSunPathParams Summer = Params;
		Summer.DeclinationDegrees = 23.44;
		TestTrue(TEXT("Verao: as 06:00 o sol ja esta acima do horizonte"), DeverauxSky::ComputeSkyState(Fraction(6.0), Summer).SunElevationDegrees > 0.0);
		TestTrue(TEXT("Verao: ao meio-dia, 83,44 graus"), FMath::IsNearlyEqual(DeverauxSky::ComputeSkyState(Fraction(12.0), Summer).SunElevationDegrees, 83.44, Tolerance));
		const FDeverauxSkyState Night = DeverauxSky::ComputeSkyState(Fraction(22.0), Summer);
		TestTrue(TEXT("Verao: a lua continua no ponto oposto ao sol"), Night.MoonDirection.Equals(-Night.SunDirection, Tolerance));
	}

	// Hemisferio sul: o sol do meio-dia fica no norte
	{
		FDeverauxSunPathParams South = Params;
		South.LatitudeDegrees = -23.5;
		const FDeverauxSkyState Noon = DeverauxSky::ComputeSkyState(Fraction(12.0), South);
		TestTrue(TEXT("Sul: meio-dia com o sol no norte"), Noon.SunDirection.X > 0.0);
		TestTrue(TEXT("Sul: meio-dia a 66,5 graus"), FMath::IsNearlyEqual(Noon.SunElevationDegrees, 66.5, Tolerance));
	}

	// Lua atrasada 2 h: com o sol se pondo, ela ainda esta abaixo do horizonte e nasce as 20:00
	{
		FDeverauxSunPathParams LateMoon = Params;
		LateMoon.MoonDelayHours = 2.0;
		TestTrue(TEXT("Lua atrasada: 18:00 ainda abaixo do horizonte"), DeverauxSky::ComputeSkyState(Fraction(18.0), LateMoon).MoonElevationDegrees < 0.0);
		TestTrue(TEXT("Lua atrasada: nasce as 20:00"), FMath::IsNearlyEqual(DeverauxSky::ComputeSkyState(Fraction(20.0), LateMoon).MoonElevationDegrees, 0.0, Tolerance));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
