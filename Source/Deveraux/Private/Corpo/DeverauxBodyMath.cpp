// Projeto Deveraux - contas da fisica do corpo, sem componente nem engine (modulo Deveraux)

#include "Corpo/DeverauxBodyMath.h"

namespace DeverauxBodyMathPrivate
{
	/** Escala da malha para converter tamanhos do mundo para o espaco dela. Malha com escala diferente por eixo usa a maior. */
	double MeshScale(const FTransform& MeshToWorld)
	{
		return FMath::Max(MeshToWorld.GetMaximumAxisScale(), UE_KINDA_SMALL_NUMBER);
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
	const float Alignment = static_cast<float>(FVector::DotProduct(Gravity.GetSafeNormal(), Unit));
	return FMath::SmoothStep(0.0f, 1.0f, Alignment);
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

void DeverauxBody::PackBox(const FTransform& MeshToWorld, const FTransform& BoxToWorld, const FVector& HalfExtentWorld, FVector4 OutBox[4])
{
	using namespace DeverauxBodyMathPrivate;

	const double Scale = MeshScale(MeshToWorld);
	const FQuat BoxRotation = BoxToWorld.GetRotation();
	const FVector Axes[3] = { BoxRotation.GetAxisX(), BoxRotation.GetAxisY(), BoxRotation.GetAxisZ() };
	OutBox[0] = FVector4(MeshToWorld.InverseTransformPosition(BoxToWorld.GetLocation()), 1.0);
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		OutBox[Axis + 1] = FVector4(MeshToWorld.InverseTransformVectorNoScale(Axes[Axis]), HalfExtentWorld[Axis] / Scale);
	}
}
