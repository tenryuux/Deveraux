// Projeto Deveraux - testes das contas da fisica do corpo (modulo Deveraux)
// Para rodar no editor: Window > Output Log, digite no campo Cmd: Automation RunTests Deveraux

#include "Corpo/DeverauxBodyMath.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeverauxBodyMathTest, "Deveraux.Corpo.Contas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace DeverauxBodyMathTestPrivate
{
	constexpr double Tolerance = 1.0e-4;

	bool Near(const FVector& A, const FVector& B)
	{
		return A.Equals(B, Tolerance);
	}

	bool Near4(const FVector4& A, double X, double Y, double Z, double W)
	{
		return FMath::IsNearlyEqual(A.X, X, Tolerance) && FMath::IsNearlyEqual(A.Y, Y, Tolerance)
			&& FMath::IsNearlyEqual(A.Z, Z, Tolerance) && FMath::IsNearlyEqual(A.W, W, Tolerance);
	}
}

bool FDeverauxBodyMathTest::RunTest(const FString& Parameters)
{
	using namespace DeverauxBodyMathTestPrivate;

	// Gravidade no espaco do corpo em pe, com a malha olhando para +Y (costas em -Y, esquerda em +X)
	{
		const FQuat Ref = FRotator(10.0, 20.0, 30.0).Quaternion();
		TestTrue(TEXT("Em pe: a gravidade puxa para baixo"), Near(DeverauxBody::GravityInRestFrame(FQuat::Identity, Ref, Ref), FVector(0.0, 0.0, -1.0)));

		// Deitada de costas: o tronco gira a frente (+Y) para cima
		const FQuat FrontUp = FQuat::FindBetweenNormals(FVector(0.0, 1.0, 0.0), FVector(0.0, 0.0, 1.0));
		TestTrue(TEXT("Deitada de costas: a gravidade puxa para as costas"),
			Near(DeverauxBody::GravityInRestFrame(FQuat::Identity, FrontUp * Ref, Ref), FVector(0.0, -1.0, 0.0)));
		TestTrue(TEXT("A malha inteira deitada da o mesmo que o tronco deitado"),
			Near(DeverauxBody::GravityInRestFrame(FrontUp, Ref, Ref), FVector(0.0, -1.0, 0.0)));

		// Deitada sobre o lado esquerdo: o lado esquerdo (+X) fica para baixo
		const FQuat LeftDown = FQuat::FindBetweenNormals(FVector(1.0, 0.0, 0.0), FVector(0.0, 0.0, -1.0));
		TestTrue(TEXT("Deitada de lado: a gravidade puxa para a esquerda do corpo"),
			Near(DeverauxBody::GravityInRestFrame(FQuat::Identity, LeftDown * Ref, Ref), FVector(1.0, 0.0, 0.0)));
	}

	// Peso do morph pela gravidade
	{
		TestTrue(TEXT("Gravidade na direcao do morph: peso 1"), FMath::IsNearlyEqual(DeverauxBody::GravityMorphWeight(FVector(0.0, -1.0, 0.0), FVector(0.0, -1.0, 0.0)), 1.0f, 1.0e-4f));
		TestTrue(TEXT("Gravidade a 90 graus: peso 0"), FMath::IsNearlyEqual(DeverauxBody::GravityMorphWeight(FVector(0.0, 0.0, -1.0), FVector(0.0, -1.0, 0.0)), 0.0f, 1.0e-4f));
		TestTrue(TEXT("Gravidade do lado oposto: peso 0"), FMath::IsNearlyEqual(DeverauxBody::GravityMorphWeight(FVector(0.0, 1.0, 0.0), FVector(0.0, -1.0, 0.0)), 0.0f, 1.0e-4f));
		const FVector SixtyDegrees(0.0, -0.5, -FMath::Sqrt(0.75));
		TestTrue(TEXT("Gravidade a 60 graus: peso 0,5, pela curva suave"), FMath::IsNearlyEqual(DeverauxBody::GravityMorphWeight(SixtyDegrees, FVector(0.0, -2.0, 0.0)), 0.5f, 1.0e-4f));
		TestTrue(TEXT("Morph sem direcao: peso 0"), DeverauxBody::GravityMorphWeight(FVector(0.0, -1.0, 0.0), FVector::ZeroVector) == 0.0f);
	}

	// Suavizacao
	{
		TestTrue(TEXT("Sem tempo de resposta: chega na hora"), DeverauxBody::SmoothTowards(0.0f, 1.0f, 0.016f, 0.0f) == 1.0f);
		TestTrue(TEXT("Sem tempo passado: fica onde esta"), DeverauxBody::SmoothTowards(0.25f, 1.0f, 0.0f, 0.15f) == 0.25f);
		TestTrue(TEXT("Depois de um tempo de resposta: 63% do caminho"), FMath::IsNearlyEqual(DeverauxBody::SmoothTowards(0.0f, 1.0f, 0.15f, 0.15f), 1.0f - FMath::Exp(-1.0f), 1.0e-4f));
		TestTrue(TEXT("Em vetor, o mesmo"), Near(DeverauxBody::SmoothTowards(FVector::ZeroVector, FVector(10.0, 0.0, -10.0), 0.3f, 0.3f), FVector(10.0, 0.0, -10.0) * (1.0 - FMath::Exp(-1.0))));
	}

	// Squash
	{
		TestTrue(TEXT("Sem balanco: escala 1"), Near(DeverauxBody::SquashScale(FVector::ZeroVector, 0.02f, 0.3f), FVector(1.0, 1.0, 1.0)));

		const FVector Squash = DeverauxBody::SquashScale(FVector(10.0, 0.0, 0.0), 0.02f, 0.3f);
		TestTrue(TEXT("Balanco em X: achata em X e alarga em Y e Z, por igual"), Squash.X < 1.0 && Squash.Y > 1.0 && FMath::IsNearlyEqual(Squash.Y, Squash.Z, Tolerance));
		TestTrue(TEXT("Mesmo volume"), FMath::IsNearlyEqual(Squash.X * Squash.Y * Squash.Z, 1.0, Tolerance));
		TestTrue(TEXT("10 cm a 2% por cm: X em 0,8 antes do volume, 0,8^(2/3) depois"), FMath::IsNearlyEqual(Squash.X, FMath::Pow(0.8, 2.0 / 3.0), Tolerance));

		const FVector Stretch = DeverauxBody::SquashScale(FVector(0.0, 0.0, -10.0), -0.02f, 0.3f);
		TestTrue(TEXT("Forca negativa estica: Z maior, X e Y menores"), Stretch.Z > 1.0 && Stretch.X < 1.0 && Stretch.Y < 1.0);

		const FVector Hard = DeverauxBody::SquashScale(FVector(100.0, 0.0, 0.0), 0.02f, 0.3f);
		TestTrue(TEXT("Balanco enorme fica no limite de 30%"), Hard.X >= 0.7 - Tolerance && Hard.Y <= 1.3 + Tolerance);
		TestTrue(TEXT("Limite 0: sem squash"), Near(DeverauxBody::SquashScale(FVector(10.0, 0.0, 0.0), 0.02f, 0.0f), FVector(1.0, 1.0, 1.0)));
	}

	// Contato no espaco da malha
	{
		const FTransform MeshToWorld(FRotator(0.0, 90.0, 0.0).Quaternion(), FVector(100.0, 0.0, 0.0), FVector(1.0, 1.0, 1.0));
		TestTrue(TEXT("Esfera no espaco da malha girada e deslocada"), Near4(DeverauxBody::PackSphere(MeshToWorld, FVector(100.0, 50.0, 0.0), 10.0f), 50.0, 0.0, 0.0, 10.0));

		const FTransform BigMesh(FQuat::Identity, FVector::ZeroVector, FVector(2.0, 2.0, 2.0));
		TestTrue(TEXT("Malha com escala 2: posicao e raio pela metade"), Near4(DeverauxBody::PackSphere(BigMesh, FVector(0.0, 40.0, 0.0), 10.0f), 0.0, 20.0, 0.0, 5.0));

		FVector4 Box[4];
		DeverauxBody::PackBox(MeshToWorld, FTransform(FQuat::Identity, FVector(100.0, 0.0, -25.0)), FVector(200.0, 100.0, 25.0), Box);
		TestTrue(TEXT("Caixa: centro ligado (W 1)"), Near4(Box[0], 0.0, 0.0, -25.0, 1.0));
		TestTrue(TEXT("Caixa: eixo X do mundo vira -Y da malha, com a meia largura"), Near4(Box[1], 0.0, -1.0, 0.0, 200.0));
		TestTrue(TEXT("Caixa: eixo Y do mundo vira X da malha"), Near4(Box[2], 1.0, 0.0, 0.0, 100.0));
		TestTrue(TEXT("Caixa: eixo Z fica Z"), Near4(Box[3], 0.0, 0.0, 1.0, 25.0));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
