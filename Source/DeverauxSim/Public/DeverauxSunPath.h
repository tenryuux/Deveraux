// Projeto Deveraux - caminho do sol e da lua (DeverauxSim)

#pragma once

#include "CoreMinimal.h"

/** Parametros do caminho do sol e da lua. Valem para o mapa inteiro. */
struct FDeverauxSunPathParams
{
	/** Latitude do mapa, em graus. Com declinacao 0, o sol do meio-dia fica a 90 - latitude graus de altura. Negativo = hemisferio sul. */
	double LatitudeDegrees = 30.0;

	/** Declinacao do sol (a estacao do ano), em graus. 0 = dia de 12 h: nasce as 06:00 e se poe as 18:00. Positivo = dias mais longos. */
	double DeclinationDegrees = 0.0;

	/** Hora em que o sol fica mais alto. */
	double SolarNoonHour = 12.0;

	/** Horas que a lua nasce depois do por do sol. 0 = lua sempre no ponto oposto ao sol. */
	double MoonDelayHours = 0.0;

	/** Faixa do crepusculo, em graus de altura do sol abaixo e acima do horizonte. */
	double TwilightDegrees = 6.0;
};

/**
 * Onde estao o sol e a lua, e quanto ainda e dia.
 * Direcoes do chao para o astro, unitarias, no espaco do mapa: X = norte, Y = leste, Z = para cima.
 */
struct FDeverauxSkyState
{
	FVector SunDirection = FVector(0.0, 0.0, 1.0);
	FVector MoonDirection = FVector(0.0, 0.0, -1.0);
	double SunElevationDegrees = 90.0;
	double MoonElevationDegrees = -90.0;

	/** 0 = noite, 1 = dia. Passa por 0,5 quando o sol cruza o horizonte, com transicao suave no crepusculo. */
	double DayAlpha = 1.0;

	/** O sol esta abaixo do horizonte. */
	bool IsNight() const { return SunElevationDegrees < 0.0; }
};

namespace DeverauxSky
{
	/** Direcao de um astro pelo angulo horario (0 = passando pelo meridiano, positivo = depois), declinacao e latitude. */
	DEVERAUXSIM_API FVector CelestialDirection(double HourAngleDegrees, double DeclinationDegrees, double LatitudeDegrees);

	/** Altura da direcao acima do horizonte, em graus. */
	DEVERAUXSIM_API double ElevationDegrees(const FVector& Direction);

	/** Estado do ceu numa fracao do dia (0 = meia-noite, 0,5 = meio-dia). */
	DEVERAUXSIM_API FDeverauxSkyState ComputeSkyState(double TimeOfDayFraction, const FDeverauxSunPathParams& Params);
}
