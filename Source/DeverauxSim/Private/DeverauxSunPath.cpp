// Projeto Deveraux - caminho do sol e da lua (DeverauxSim)

#include "DeverauxSunPath.h"

FVector DeverauxSky::CelestialDirection(double HourAngleDegrees, double DeclinationDegrees, double LatitudeDegrees)
{
	// Rotacao do equador celeste para o horizonte local. O polo celeste fica a "latitude" graus
	// de altura no norte; o angulo horario cresce para o oeste.
	const double HourAngle = FMath::DegreesToRadians(HourAngleDegrees);
	const double Declination = FMath::DegreesToRadians(DeclinationDegrees);
	const double Latitude = FMath::DegreesToRadians(LatitudeDegrees);

	const double CosDeclination = FMath::Cos(Declination);
	const double SinDeclination = FMath::Sin(Declination);
	const double OnMeridian = CosDeclination * FMath::Cos(HourAngle);

	const double North = SinDeclination * FMath::Cos(Latitude) - OnMeridian * FMath::Sin(Latitude);
	const double East = -CosDeclination * FMath::Sin(HourAngle);
	const double Up = SinDeclination * FMath::Sin(Latitude) + OnMeridian * FMath::Cos(Latitude);
	return FVector(North, East, Up);
}

double DeverauxSky::ElevationDegrees(const FVector& Direction)
{
	return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Direction.Z, -1.0, 1.0)));
}

FDeverauxSkyState DeverauxSky::ComputeSkyState(double TimeOfDayFraction, const FDeverauxSunPathParams& Params)
{
	// 15 graus por hora; ao meio-dia solar o angulo horario e zero
	const double Hours = TimeOfDayFraction * 24.0;
	const double SunHourAngle = (Hours - Params.SolarNoonHour) * 15.0;
	const double MoonHourAngle = SunHourAngle + 180.0 - Params.MoonDelayHours * 15.0;

	FDeverauxSkyState State;
	State.SunDirection = CelestialDirection(SunHourAngle, Params.DeclinationDegrees, Params.LatitudeDegrees);
	State.MoonDirection = CelestialDirection(MoonHourAngle, -Params.DeclinationDegrees, Params.LatitudeDegrees);
	State.SunElevationDegrees = ElevationDegrees(State.SunDirection);
	State.MoonElevationDegrees = ElevationDegrees(State.MoonDirection);

	const double Twilight = FMath::Max(Params.TwilightDegrees, 0.01);
	State.DayAlpha = FMath::SmoothStep(-Twilight, Twilight, State.SunElevationDegrees);
	return State;
}
