// Projeto Deveraux - contas da fisica do corpo, sem componente nem engine (modulo Deveraux)

#include "Corpo/DeverauxBodyMath.h"

namespace DeverauxBodyMathPrivate
{
	/** Escala da malha para converter tamanhos do mundo para o espaco dela. Malha com escala diferente por eixo usa a maior. */
	double MeshScale(const FTransform& MeshToWorld)
	{
		return FMath::Max(MeshToWorld.GetMaximumAxisScale(), UE_KINDA_SMALL_NUMBER);
	}

	FVector ToVector(const FVector4& Value)
	{
		return FVector(Value.X, Value.Y, Value.Z);
	}

	/** Plano ligado: normal com tamanho perto de 1. */
	bool IsPlaneOn(const FVector4& Plane)
	{
		return Plane.X * Plane.X + Plane.Y * Plane.Y + Plane.Z * Plane.Z > 0.25;
	}

	/** Distancia com sinal do ponto ao plano: positiva do lado da normal. */
	double PlaneDistance(const FVector4& Plane, const FVector& Point)
	{
		return FVector::DotProduct(ToVector(Plane), Point) - Plane.W;
	}

	int32 RoundCount(double Value, int32 Max)
	{
		return FMath::Clamp(static_cast<int32>(Value + 0.5), 0, Max);
	}
}

FVector DeverauxBody::GravityInRestFrame(const FQuat& ComponentRotation, const FQuat& BoneRotation, const FQuat& BoneRefRotation)
{
	const FVector GravityInComponent = ComponentRotation.UnrotateVector(FVector(0.0, 0.0, -1.0));

	// O osso leva a pose de referencia ate a de agora (Atual = Giro * Ref); Ref * Atual^-1 desfaz o giro
	const FQuat CurrentToRest = BoneRefRotation * BoneRotation.Inverse();
	return CurrentToRest.RotateVector(GravityInComponent).GetSafeNormal();
}

float DeverauxBody::GravityMorphWeight(const FVector& Gravity, const FVector& Direction)
{
	const FVector Unit = Direction.GetSafeNormal();
	if (Unit.IsZero())
	{
		return 0.0f;
	}
	const float Alignment = FMath::Max(static_cast<float>(FVector::DotProduct(Gravity.GetSafeNormal(), Unit)), 0.0f);
	return Alignment * Alignment;
}

void DeverauxBody::LimitMorphWeights(TArray<float>& Weights)
{
	float Total = 0.0f;
	for (const float Weight : Weights)
	{
		Total += FMath::Max(Weight, 0.0f);
	}
	if (Total > 1.0f)
	{
		for (float& Weight : Weights)
		{
			Weight /= Total;
		}
	}
}

float DeverauxBody::SmoothTowards(float Current, float Target, float DeltaSeconds, float ResponseSeconds)
{
	if (ResponseSeconds <= 0.0f)
	{
		return Target;
	}
	if (DeltaSeconds <= 0.0f)
	{
		return Current;
	}
	const float Blend = 1.0f - static_cast<float>(FMath::Exp(-static_cast<double>(DeltaSeconds) / ResponseSeconds));
	return Current + (Target - Current) * Blend;
}

FVector DeverauxBody::SmoothTowards(const FVector& Current, const FVector& Target, float DeltaSeconds, float ResponseSeconds)
{
	if (ResponseSeconds <= 0.0f)
	{
		return Target;
	}
	if (DeltaSeconds <= 0.0f)
	{
		return Current;
	}
	const double Blend = 1.0 - FMath::Exp(-static_cast<double>(DeltaSeconds) / ResponseSeconds);
	return Current + (Target - Current) * Blend;
}

FVector DeverauxBody::SquashScale(const FVector& SwingLocal, float StrengthPerCm, float MaxChange)
{
	const double Limit = FMath::Clamp(static_cast<double>(MaxChange), 0.0, 0.9);
	if (Limit <= 0.0)
	{
		return FVector(1.0, 1.0, 1.0);
	}
	const double Strength = StrengthPerCm;
	const auto SquashAxis = [Limit, Strength](double Swing)
	{
		return FMath::Clamp(1.0 - FMath::Abs(Swing) * Strength, 1.0 - Limit, 1.0 + Limit);
	};
	FVector Scale(SquashAxis(SwingLocal.X), SquashAxis(SwingLocal.Y), SquashAxis(SwingLocal.Z));

	// Mesmo volume: o produto das tres escalas volta a 1, e os eixos menos achatados alargam
	Scale /= FMath::Pow(Scale.X * Scale.Y * Scale.Z, 1.0 / 3.0);
	return FVector(FMath::Clamp(Scale.X, 1.0 - Limit, 1.0 + Limit), FMath::Clamp(Scale.Y, 1.0 - Limit, 1.0 + Limit),
		FMath::Clamp(Scale.Z, 1.0 - Limit, 1.0 + Limit));
}

FVector4 DeverauxBody::PackSphere(const FTransform& MeshToWorld, const FVector& CenterWorld, float RadiusWorld)
{
	using namespace DeverauxBodyMathPrivate;

	return FVector4(MeshToWorld.InverseTransformPosition(CenterWorld), RadiusWorld / MeshScale(MeshToWorld));
}

void DeverauxBody::PackBox(const FTransform& MeshToWorld, const FTransform& BoxToWorld, const FVector& HalfExtentWorld,
	const FVector& BodyCenterWorld, FVector4 OutBox[4])
{
	using namespace DeverauxBodyMathPrivate;

	const double Scale = MeshScale(MeshToWorld);
	const FQuat BoxRotation = BoxToWorld.GetRotation();
	const FVector Axes[3] = { BoxRotation.GetAxisX(), BoxRotation.GetAxisY(), BoxRotation.GetAxisZ() };

	// A face virada para o corpo: o eixo em que o centro dele fica mais para fora da caixa. No empate, o Z da caixa
	const FVector BodyInBox = BoxRotation.UnrotateVector(BodyCenterWorld - BoxToWorld.GetLocation());
	int32 Up = 2;
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		if (FMath::Abs(BodyInBox[Axis]) - HalfExtentWorld[Axis] > FMath::Abs(BodyInBox[Up]) - HalfExtentWorld[Up])
		{
			Up = Axis;
		}
	}
	const double Side = BodyInBox[Up] >= 0.0 ? 1.0 : -1.0;
	const int32 First = (Up + 1) % 3;
	const int32 Second = (Up + 2) % 3;

	OutBox[0] = FVector4(MeshToWorld.InverseTransformPosition(BoxToWorld.GetLocation()), 1.0);
	OutBox[1] = FVector4(MeshToWorld.InverseTransformVectorNoScale(Axes[First]), HalfExtentWorld[First] / Scale);
	OutBox[2] = FVector4(MeshToWorld.InverseTransformVectorNoScale(Axes[Second]), HalfExtentWorld[Second] / Scale);
	OutBox[3] = FVector4(MeshToWorld.InverseTransformVectorNoScale(Axes[Up] * Side), HalfExtentWorld[Up] / Scale);
}

FVector DeverauxBody::ClosestPointOnSegment(const FVector& Point, const FVector& A, const FVector& B)
{
	const FVector Axis = B - A;
	const double LengthSquared = FVector::DotProduct(Axis, Axis);
	if (LengthSquared <= UE_SMALL_NUMBER)
	{
		return A;
	}
	const double T = FMath::Clamp(FVector::DotProduct(Point - A, Axis) / LengthSquared, 0.0, 1.0);
	return A + Axis * T;
}

void DeverauxBody::ClosestPointsBetweenSegments(const FVector& P0, const FVector& P1, const FVector& Q0, const FVector& Q1,
	FVector& OutOnP, FVector& OutOnQ)
{
	const FVector D1 = P1 - P0;
	const FVector D2 = Q1 - Q0;
	const FVector R = P0 - Q0;
	const double A = FVector::DotProduct(D1, D1);
	const double E = FVector::DotProduct(D2, D2);
	const double F = FVector::DotProduct(D2, R);
	double S = 0.0;
	double T = 0.0;
	if (A <= UE_SMALL_NUMBER && E <= UE_SMALL_NUMBER)
	{
		// Os dois sao pontos
	}
	else if (A <= UE_SMALL_NUMBER)
	{
		T = FMath::Clamp(F / E, 0.0, 1.0);
	}
	else
	{
		const double C = FVector::DotProduct(D1, R);
		if (E <= UE_SMALL_NUMBER)
		{
			S = FMath::Clamp(-C / A, 0.0, 1.0);
		}
		else
		{
			const double B = FVector::DotProduct(D1, D2);
			const double Denominator = A * E - B * B;
			// Paralelos: qualquer S serve; fica no comeco
			S = Denominator > UE_SMALL_NUMBER ? FMath::Clamp((B * F - C * E) / Denominator, 0.0, 1.0) : 0.0;
			T = (B * S + F) / E;
			if (T < 0.0)
			{
				T = 0.0;
				S = FMath::Clamp(-C / A, 0.0, 1.0);
			}
			else if (T > 1.0)
			{
				T = 1.0;
				S = FMath::Clamp((B - C) / A, 0.0, 1.0);
			}
		}
	}
	OutOnP = P0 + D1 * S;
	OutOnQ = Q0 + D2 * T;
}

FVector4 DeverauxBody::PairPlane(const FVector& LeftA, const FVector& LeftB, float LeftRadius, const FVector& RightA,
	const FVector& RightB, float RightRadius, const FVector& FallbackNormal, float& OutOverlap)
{
	FVector OnLeft;
	FVector OnRight;
	ClosestPointsBetweenSegments(LeftA, LeftB, RightA, RightB, OnLeft, OnRight);
	const FVector Between = OnRight - OnLeft;
	const double Distance = Between.Size();
	const FVector Normal = Distance > 0.01 ? Between / Distance : FallbackNormal.GetSafeNormal();
	OutOverlap = static_cast<float>(LeftRadius + RightRadius - Distance);

	// Da superficie esquerda (eixo + raio) ate a direita (distancia - raio), o plano fica no meio
	const FVector PlanePoint = OnLeft + Normal * ((Distance + LeftRadius - RightRadius) * 0.5);
	return FVector4(Normal, FVector::DotProduct(Normal, PlanePoint));
}

double DeverauxBody::SignedDistanceToBox(const FVector& Point, const FVector4 Box[4])
{
	using namespace DeverauxBodyMathPrivate;

	if (Box[0].W <= 0.0)
	{
		return UE_BIG_NUMBER;
	}
	const FVector Relative = Point - ToVector(Box[0]);
	double Outside = 0.0;
	double Inside = -UE_BIG_NUMBER;
	for (int32 Axis = 1; Axis <= 3; ++Axis)
	{
		const double Gap = FMath::Abs(FVector::DotProduct(Relative, ToVector(Box[Axis]))) - Box[Axis].W;
		Outside += FMath::Max(Gap, 0.0) * FMath::Max(Gap, 0.0);
		Inside = FMath::Max(Inside, Gap);
	}
	return FMath::Sqrt(Outside) + FMath::Min(Inside, 0.0);
}

float DeverauxBody::MassDepthInCapsule(const FVector& MassA, const FVector& MassB, float MassRadius, const FVector& A,
	const FVector& B, float Radius)
{
	FVector OnMass;
	FVector OnCapsule;
	ClosestPointsBetweenSegments(MassA, MassB, A, B, OnMass, OnCapsule);
	return static_cast<float>(FMath::Max(MassRadius + Radius - (OnCapsule - OnMass).Size(), 0.0));
}

float DeverauxBody::MassDepthInPlane(const FVector& MassA, const FVector& MassB, float MassRadius, const FVector4& Plane)
{
	using namespace DeverauxBodyMathPrivate;

	if (!IsPlaneOn(Plane))
	{
		return 0.0f;
	}
	const double Lowest = FMath::Min(PlaneDistance(Plane, MassA), PlaneDistance(Plane, MassB));
	return static_cast<float>(FMath::Max(MassRadius - Lowest, 0.0));
}

float DeverauxBody::MassDepthInBox(const FVector& MassA, const FVector& MassB, float MassRadius, const FVector4 Box[4])
{
	if (Box[0].W <= 0.0)
	{
		return 0.0f;
	}
	const FVector Samples[3] = { MassA, (MassA + MassB) * 0.5, MassB };
	double Depth = 0.0;
	for (const FVector& Sample : Samples)
	{
		Depth = FMath::Max(Depth, MassRadius - SignedDistanceToBox(Sample, Box));
	}
	return static_cast<float>(Depth);
}

FVector DeverauxBody::ResolveContactPoint(const TArray<FVector4>& Values, const FVector& Point, int32 Mass, float Radius)
{
	using namespace DeverauxBodyMathPrivate;
	using namespace ContactLayout;

	if (Values.Num() < Num || Mass < 0 || Mass >= ContactRegion::NumMasses)
	{
		return Point;
	}
	const int32 Region = Mass / 2;
	const bool bLeft = Mass % 2 == 0;
	const int32 Bit = ContactRegion::MassBit(Mass);
	const int32 NumCapsules = RoundCount(Values[Header].X, MaxCapsules);
	const int32 NumBoxes = RoundCount(Values[Header].Y, MaxBoxes);

	// Duas voltas: sair de uma forma pode jogar o ponto em outra (a capsula entre os seios e o plano do par)
	FVector P = Point;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 Index = 0; Index < NumCapsules; ++Index)
		{
			const FVector4& A = Values[Capsules + Index * 2];
			const FVector4& B = Values[Capsules + Index * 2 + 1];
			if (A.W <= 0.0 || (RoundCount(B.W, ContactRegion::AllBits) & Bit) == 0)
			{
				continue;
			}
			const FVector OnAxis = ClosestPointOnSegment(P, ToVector(A), ToVector(B));
			const FVector Out = P - OnAxis;
			const double Distance = Out.Size();
			const double Reach = A.W + Radius;
			if (Distance < Reach && Distance > UE_KINDA_SMALL_NUMBER)
			{
				P = OnAxis + Out * (Reach / Distance);
			}
		}

		// Caixas: sai pela face virada para o corpo ou por um lado, a mais perto, nunca pela de tras (como no kernel)
		for (int32 Index = 0; Index < NumBoxes; ++Index)
		{
			const FVector4* Box = &Values[Boxes + Index * 4];
			if (Box[0].W <= 0.0)
			{
				continue;
			}
			const FVector Relative = P - ToVector(Box[0]);
			const FVector Local(FVector::DotProduct(Relative, ToVector(Box[1])), FVector::DotProduct(Relative, ToVector(Box[2])),
				FVector::DotProduct(Relative, ToVector(Box[3])));
			const FVector Half(Box[1].W + Radius, Box[2].W + Radius, Box[3].W + Radius);
			if (FMath::Abs(Local.X) < Half.X && FMath::Abs(Local.Y) < Half.Y && FMath::Abs(Local.Z) < Half.Z)
			{
				const double DepthX = Half.X - FMath::Abs(Local.X);
				const double DepthY = Half.Y - FMath::Abs(Local.Y);
				const double DepthZ = Half.Z - Local.Z;
				if (DepthZ <= DepthX && DepthZ <= DepthY)
				{
					P += ToVector(Box[3]) * DepthZ;
				}
				else if (DepthX <= DepthY)
				{
					P += ToVector(Box[1]) * (Local.X >= 0.0 ? DepthX : -DepthX);
				}
				else
				{
					P += ToVector(Box[2]) * (Local.Y >= 0.0 ? DepthY : -DepthY);
				}
			}
		}

		// Chao: o ponto fica acima dele, com o raio
		const FVector4& Floor = Values[Floors + Mass];
		if (IsPlaneOn(Floor))
		{
			const double Height = PlaneDistance(Floor, P);
			if (Height < Radius)
			{
				P += ToVector(Floor) * (Radius - Height);
			}
		}

		// Par: a massa esquerda fica do lado de tras da normal, a direita do lado da frente
		const FVector4& Pair = Values[Pairs + Region];
		if (IsPlaneOn(Pair))
		{
			const double Side = PlaneDistance(Pair, P);
			if (bLeft && Side > -Radius)
			{
				P -= ToVector(Pair) * (Side + Radius);
			}
			else if (!bLeft && Side < Radius)
			{
				P += ToVector(Pair) * (Radius - Side);
			}
		}
	}
	return P;
}
