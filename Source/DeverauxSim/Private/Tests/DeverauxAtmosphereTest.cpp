// Projeto Deveraux - testes da transmitancia da atmosfera (DeverauxSim)
// Para rodar no editor: Tools > Test Automation, marque Deveraux e clique em Start Tests.

#include "DeverauxAtmosphere.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeverauxAtmosphereTest, "Deveraux.Sim.Atmosfera",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace DeverauxAtmosphereTestPrivate
{
	double SinDegrees(double Degrees)
	{
		return FMath::Sin(FMath::DegreesToRadians(Degrees));
	}

	bool IsFinite01(const FVector& Color)
	{
		return Color.X >= 0.0 && Color.X <= 1.0 && Color.Y >= 0.0 && Color.Y <= 1.0 && Color.Z >= 0.0 && Color.Z <= 1.0;
	}
}

bool FDeverauxAtmosphereTest::RunTest(const FString& Parameters)
{
	using namespace DeverauxAtmosphereTestPrivate;

	const FDeverauxAtmosphereParams Earth;

	// Do chao ao espaco, para cima: a integral exata das camadas da Unreal
	// (Rayleigh 8 km, Mie 1,2 km, ozonio em tenda de 25 +- 15 km)
	{
		const FVector Zenith = DeverauxAtmosphere::Transmittance(Earth, 0.0, 1.0, 1000.0, 4096);
		TestTrue(TEXT("Zenite do chao: vermelho 0,9404"), FMath::IsNearlyEqual(Zenith.X, 0.940383, 1.e-5));
		TestTrue(TEXT("Zenite do chao: verde 0,8677"), FMath::IsNearlyEqual(Zenith.Y, 0.867667, 1.e-5));
		TestTrue(TEXT("Zenite do chao: azul 0,7624"), FMath::IsNearlyEqual(Zenith.Z, 0.762421, 1.e-5));
	}

	// A conta da Unreal para a cor das luzes: meio km acima do chao, 15 amostras
	{
		const FVector Zenith = DeverauxAtmosphere::LightTransmittance(Earth, 1.0);
		TestTrue(TEXT("Luz no zenite: vermelho 0,9258"), FMath::IsNearlyEqual(Zenith.X, 0.925825, 1.e-5));
		TestTrue(TEXT("Luz no zenite: verde 0,8442"), FMath::IsNearlyEqual(Zenith.Y, 0.844161, 1.e-5));
		TestTrue(TEXT("Luz no zenite: azul 0,7199"), FMath::IsNearlyEqual(Zenith.Z, 0.719899, 1.e-5));

		const FVector Low = DeverauxAtmosphere::LightTransmittance(Earth, SinDegrees(10.0));
		TestTrue(TEXT("Luz a 10 graus: vermelho 0,6613"), FMath::IsNearlyEqual(Low.X, 0.6613, 1.e-4));
		TestTrue(TEXT("Luz a 10 graus: azul 0,1677"), FMath::IsNearlyEqual(Low.Z, 0.1677, 1.e-4));
	}

	// Quanto mais baixo, mais ar no caminho, e o azul some antes do vermelho
	{
		FVector Previous(1.0, 1.0, 1.0);
		bool bDarker = true;
		bool bWarm = true;
		bool bValid = true;
		for (int32 Degrees = 90; Degrees >= 0; Degrees -= 2)
		{
			const FVector Light = DeverauxAtmosphere::LightTransmittance(Earth, SinDegrees(Degrees));
			bDarker = bDarker && Light.X <= Previous.X && Light.Y <= Previous.Y && Light.Z <= Previous.Z;
			bWarm = bWarm && Light.X >= Light.Y && Light.Y >= Light.Z;
			bValid = bValid && IsFinite01(Light);
			Previous = Light;
		}
		TestTrue(TEXT("Descendo do zenite ao horizonte a luz so enfraquece"), bDarker);
		TestTrue(TEXT("Em toda altura passa mais vermelho que verde, e mais verde que azul"), bWarm);
		TestTrue(TEXT("Valores entre 0 e 1"), bValid);
	}

	// Abaixo do horizonte o chao bloqueia tudo, sem conta estourada
	{
		const FVector Below = DeverauxAtmosphere::LightTransmittance(Earth, SinDegrees(-3.0));
		TestTrue(TEXT("3 graus abaixo do horizonte: nada passa"), Below.Equals(FVector(0.0, 0.0, 0.0), 1.e-12));
		const FVector Down = DeverauxAtmosphere::Transmittance(Earth, 1.0, -1.0, 100.0, 32);
		TestTrue(TEXT("Para baixo, ate depois do chao: nada passa"), Down.Equals(FVector(0.0, 0.0, 0.0), 1.e-12));
		const FVector Short = DeverauxAtmosphere::Transmittance(Earth, 1.0, -1.0, 0.5, 32);
		TestTrue(TEXT("Para baixo, parando antes do chao: passa quase tudo"), IsFinite01(Short) && Short.X > 0.99);
	}

	// Raio curto: pouco ar. Distancia zero: nada muda. Mais longe: mais escuro, ate o total
	{
		TestTrue(TEXT("Distancia zero"), DeverauxAtmosphere::Transmittance(Earth, 0.0, 0.5, 0.0, 32).Equals(FVector(1.0, 1.0, 1.0), 1.e-12));
		const FVector Near = DeverauxAtmosphere::Transmittance(Earth, 0.0, 0.5, 10.0, 32);
		const FVector Far = DeverauxAtmosphere::Transmittance(Earth, 0.0, 0.5, 50.0, 32);
		const FVector All = DeverauxAtmosphere::Transmittance(Earth, 0.0, 0.5, 1000.0, 32);
		TestTrue(TEXT("10 km passa mais que 50 km"), Near.Z > Far.Z && Near.X > Far.X);
		TestTrue(TEXT("50 km passa mais que a atmosfera inteira"), Far.Z >= All.Z);
		TestTrue(TEXT("A 30 graus, ate 10 km de distancia: azul 0,774"), FMath::IsNearlyEqual(Near.Z, 0.7737, 1.e-3));
	}

	// Sem ar, tudo passa
	{
		FDeverauxAtmosphereParams Vacuum;
		Vacuum.RayleighScattering = FVector(0.0, 0.0, 0.0);
		Vacuum.MieExtinction = FVector(0.0, 0.0, 0.0);
		Vacuum.OzoneAbsorption = FVector(0.0, 0.0, 0.0);
		TestTrue(TEXT("Vacuo: luz no horizonte passa inteira"), DeverauxAtmosphere::LightTransmittance(Vacuum, 0.0).Equals(FVector(1.0, 1.0, 1.0), 1.e-12));
	}

	// Distancia ate uma altitude, com o planeta redondo
	{
		TestTrue(TEXT("Reto para cima, de 0 a 16 km: 16 km"), FMath::IsNearlyEqual(DeverauxAtmosphere::DistanceToAltitude(6360.0, 0.0, 1.0, 16.0), 16.0, 1.e-9));
		TestTrue(TEXT("A 60 graus: 18,47 km (um pouco menos que no plano)"),
			FMath::IsNearlyEqual(DeverauxAtmosphere::DistanceToAltitude(6360.0, 0.0, SinDegrees(60.0), 16.0), 18.467488, 1.e-5));
		TestTrue(TEXT("Na horizontal: a curva do planeta faz chegar a 451 km"),
			FMath::IsNearlyEqual(DeverauxAtmosphere::DistanceToAltitude(6360.0, 0.0, 0.0, 16.0), 451.415551, 1.e-5));
		TestTrue(TEXT("Partindo acima da altitude pedida: negativo"), DeverauxAtmosphere::DistanceToAltitude(6360.0, 20.0, 1.0, 16.0) < 0.0);
		TestTrue(TEXT("Partindo de 2 km para 16 km, reto: 14 km"), FMath::IsNearlyEqual(DeverauxAtmosphere::DistanceToAltitude(6360.0, 2.0, 1.0, 16.0), 14.0, 1.e-9));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
