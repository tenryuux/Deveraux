// Projeto Deveraux - quanto da luz atravessa a atmosfera (DeverauxSim)

#include "DeverauxAtmosphere.h"

namespace DeverauxAtmospherePrivate
{
	/** Amostras que a Unreal usa na cor das luzes da atmosfera. */
	constexpr int32 LightSampleCount = 15;

	/** A Unreal calcula a cor das luzes como se a camera estivesse meio km acima do chao. */
	constexpr double LightStartAltitudeKm = 0.5;

	/** Distancia sem limite: o raio so para ao sair da atmosfera. */
	constexpr double UnlimitedKm = 1.0e9;

	/** Quanto o ar de uma altitude apaga cada cor, por km. */
	FVector Extinction(const FDeverauxAtmosphereParams& Params, double AltitudeKm)
	{
		const double Rayleigh = FMath::Exp(-AltitudeKm / FMath::Max(Params.RayleighScaleHeightKm, 0.001));
		const double Mie = FMath::Exp(-AltitudeKm / FMath::Max(Params.MieScaleHeightKm, 0.001));
		const double Ozone = FMath::Clamp(Params.OzoneTipValue * (1.0 - FMath::Abs(AltitudeKm - Params.OzoneTipAltitudeKm) / FMath::Max(Params.OzoneWidthKm, 0.01)), 0.0, 1.0);
		return Params.RayleighScattering * Rayleigh + Params.MieExtinction * Mie + Params.OzoneAbsorption * Ozone;
	}

	/**
	 * Integra o ar ao longo do raio. bMidpoint = amostra no meio de cada trecho (mais exato);
	 * sem ele, no comeco de cada trecho, como a Unreal faz nas luzes.
	 */
	FVector ComputeTransmittance(const FDeverauxAtmosphereParams& Params, double StartAltitudeKm, double SinElevation, double MaxDistanceKm, int32 SampleCount, bool bMidpoint)
	{
		const double GroundRadius = FMath::Max(Params.GroundRadiusKm, 1.0);
		const double TopRadius = GroundRadius + FMath::Max(Params.AtmosphereHeightKm, 0.001);
		const double Sin = FMath::Clamp(SinElevation, -1.0, 1.0);
		const double Cos = FMath::Sqrt(FMath::Max(0.0, 1.0 - Sin * Sin));
		const double StartRadius = GroundRadius + FMath::Max(StartAltitudeKm, 0.0);

		// Fora da atmosfera, para cima, nao ha ar no caminho
		const double ExitDistance = DeverauxAtmosphere::DistanceToAltitude(GroundRadius, StartRadius - GroundRadius, Sin, TopRadius - GroundRadius);
		const double Length = FMath::Min(ExitDistance, MaxDistanceKm);
		if (Length <= 0.0)
		{
			return FVector(1.0, 1.0, 1.0);
		}

		// O chao bloqueia o raio que desce ate ele
		const double B = StartRadius * Sin;
		const double GroundDiscriminant = B * B - (StartRadius * StartRadius - GroundRadius * GroundRadius);
		if (Sin < 0.0 && GroundDiscriminant >= 0.0 && -B - FMath::Sqrt(GroundDiscriminant) < Length)
		{
			return FVector(0.0, 0.0, 0.0);
		}

		const int32 Samples = FMath::Max(SampleCount, 1);
		const double Step = Length / Samples;
		const double Offset = bMidpoint ? 0.5 : 0.0;
		FVector OpticalDepth(0.0, 0.0, 0.0);
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			// Ponto do raio num plano que passa pelo centro do planeta
			const double Distance = (Index + Offset) * Step;
			const double Horizontal = Distance * Cos;
			const double Vertical = StartRadius + Distance * Sin;
			const double Altitude = FMath::Sqrt(Horizontal * Horizontal + Vertical * Vertical) - GroundRadius;
			OpticalDepth = OpticalDepth + Extinction(Params, Altitude) * Step;
		}
		return FVector(FMath::Exp(-OpticalDepth.X), FMath::Exp(-OpticalDepth.Y), FMath::Exp(-OpticalDepth.Z));
	}
}

FVector DeverauxAtmosphere::Transmittance(const FDeverauxAtmosphereParams& Params, double StartAltitudeKm, double SinElevation, double MaxDistanceKm, int32 SampleCount)
{
	return DeverauxAtmospherePrivate::ComputeTransmittance(Params, StartAltitudeKm, SinElevation, MaxDistanceKm, SampleCount, true);
}

FVector DeverauxAtmosphere::LightTransmittance(const FDeverauxAtmosphereParams& Params, double SinElevation)
{
	using namespace DeverauxAtmospherePrivate;
	return ComputeTransmittance(Params, LightStartAltitudeKm, SinElevation, UnlimitedKm, LightSampleCount, false);
}

double DeverauxAtmosphere::DistanceToAltitude(double GroundRadiusKm, double StartAltitudeKm, double SinElevation, double TargetAltitudeKm)
{
	// Saindo de dentro da esfera da altitude pedida: |O + t D| = R, com O = (0, r) e D = (cos, sen)
	const double StartRadius = FMath::Max(GroundRadiusKm, 1.0) + FMath::Max(StartAltitudeKm, 0.0);
	const double TargetRadius = FMath::Max(GroundRadiusKm, 1.0) + TargetAltitudeKm;
	if (TargetRadius <= StartRadius)
	{
		return -1.0;
	}
	const double B = StartRadius * FMath::Clamp(SinElevation, -1.0, 1.0);
	return -B + FMath::Sqrt(B * B + TargetRadius * TargetRadius - StartRadius * StartRadius);
}
