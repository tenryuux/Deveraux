// Projeto Deveraux - quanto da luz atravessa a atmosfera (DeverauxSim)

#pragma once

#include "CoreMinimal.h"

/**
 * Atmosfera do planeta, com os mesmos numeros do Sky Atmosphere da Unreal: distancias em km e
 * coeficientes por km, ja multiplicados pelas escalas. Os padroes sao os da Unreal (a Terra).
 * Cores em RGB, nos campos X, Y e Z.
 */
struct FDeverauxAtmosphereParams
{
	double GroundRadiusKm = 6360.0;
	double AtmosphereHeightKm = 60.0;

	/** Rayleigh: o ar, que espalha mais o azul. Cai pela metade a cada ~5,5 km de altura. */
	FVector RayleighScattering = FVector(0.005802, 0.013558, 0.0331);
	double RayleighScaleHeightKm = 8.0;

	/** Mie: nevoa e poeira, espalhamento mais absorcao. Fica perto do chao. */
	FVector MieExtinction = FVector(0.00444, 0.00444, 0.00444);
	double MieScaleHeightKm = 1.2;

	/** Ozonio: absorve um pouco de vermelho e verde, numa camada em forma de tenda. */
	FVector OzoneAbsorption = FVector(0.00065, 0.001881, 0.000085);
	double OzoneTipAltitudeKm = 25.0;
	double OzoneTipValue = 1.0;
	double OzoneWidthKm = 15.0;
};

namespace DeverauxAtmosphere
{
	/**
	 * Fracao de cada cor (RGB, de 0 a 1) que atravessa o ar num raio que sai de StartAltitudeKm, sobe
	 * com o seno da altura dada e anda MaxDistanceKm, ou ate sair da atmosfera. Planeta redondo.
	 * Um raio que bate no chao antes do fim nao passa nada.
	 */
	DEVERAUXSIM_API FVector Transmittance(const FDeverauxAtmosphereParams& Params, double StartAltitudeKm, double SinElevation, double MaxDistanceKm, int32 SampleCount);

	/**
	 * A mesma conta que a Unreal faz para a cor de uma luz da atmosfera (sol ou lua): do chao, meio km
	 * acima, ate o espaco, com 15 amostras. A Unreal divide o resultado pelo do zenite.
	 */
	DEVERAUXSIM_API FVector LightTransmittance(const FDeverauxAtmosphereParams& Params, double SinElevation);

	/**
	 * Distancia, em km, que um raio anda ate chegar a uma altitude, com o planeta redondo.
	 * Negativo quando a altitude pedida nao fica acima do ponto de partida.
	 */
	DEVERAUXSIM_API double DistanceToAltitude(double GroundRadiusKm, double StartAltitudeKm, double SinElevation, double TargetAltitudeKm);
}
