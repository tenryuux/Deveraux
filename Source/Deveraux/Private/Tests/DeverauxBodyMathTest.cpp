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
		TestTrue(TEXT("Gravidade a 60 graus: peso 0,25, o quadrado do alinhamento"), FMath::IsNearlyEqual(DeverauxBody::GravityMorphWeight(SixtyDegrees, FVector(0.0, -2.0, 0.0)), 0.25f, 1.0e-4f));
		const FVector FortyFive = FVector(0.0, -1.0, -1.0).GetSafeNormal();
		TestTrue(TEXT("Gravidade a 45 graus: peso 0,5, metade de cada escultura"), FMath::IsNearlyEqual(DeverauxBody::GravityMorphWeight(FortyFive, FVector(0.0, -1.0, 0.0)), 0.5f, 1.0e-4f));
		TestTrue(TEXT("Morph sem direcao: peso 0"), DeverauxBody::GravityMorphWeight(FVector(0.0, -1.0, 0.0), FVector::ZeroVector) == 0.0f);

		// Nas seis direcoes dos eixos (a de baixo e a escultura em pe), os pesos somam 1 em qualquer inclinacao
		const FVector Tilted = FVector(0.3, -0.5, 0.8).GetSafeNormal();
		const FVector Axes[6] = { FVector(1.0, 0.0, 0.0), FVector(-1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), FVector(0.0, -1.0, 0.0), FVector(0.0, 0.0, 1.0), FVector(0.0, 0.0, -1.0) };
		float Sum = 0.0f;
		for (const FVector& Axis : Axes)
		{
			Sum += DeverauxBody::GravityMorphWeight(Tilted, Axis);
		}
		TestTrue(TEXT("Seis direcoes: os pesos somam 1"), FMath::IsNearlyEqual(Sum, 1.0f, 1.0e-4f));

		// Pesos somados acima de 1 (direcoes que se sobrepoem) caem na mesma proporcao; abaixo de 1 ficam como estao
		TArray<float> Over;
		Over.Add(0.75f);
		Over.Add(0.75f);
		DeverauxBody::LimitMorphWeights(Over);
		TestTrue(TEXT("Pesos somando 1,5: viram 0,5 e 0,5"), FMath::IsNearlyEqual(Over[0], 0.5f, 1.0e-4f) && FMath::IsNearlyEqual(Over[1], 0.5f, 1.0e-4f));
		TArray<float> Under;
		Under.Add(0.2f);
		Under.Add(0.3f);
		DeverauxBody::LimitMorphWeights(Under);
		TestTrue(TEXT("Pesos somando 0,5: ficam"), FMath::IsNearlyEqual(Under[0], 0.2f, 1.0e-6f) && FMath::IsNearlyEqual(Under[1], 0.3f, 1.0e-6f));
		TArray<float> Negative;
		Negative.Add(1.0f);
		Negative.Add(0.5f);
		Negative.Add(-0.5f);
		DeverauxBody::LimitMorphWeights(Negative);
		TestTrue(TEXT("Peso negativo nao entra na soma"), FMath::IsNearlyEqual(Negative[0], 1.0f / 1.5f, 1.0e-4f) && FMath::IsNearlyEqual(Negative[1], 0.5f / 1.5f, 1.0e-4f));
	}

	// De ponta-cabeca: a gravidade puxa para a cabeca (+Z do corpo em pe)
	{
		const FQuat Ref = FRotator(10.0, 20.0, 30.0).Quaternion();
		const FQuat UpsideDown(1.0, 0.0, 0.0, 0.0);
		TestTrue(TEXT("De ponta-cabeca: a gravidade puxa para a cabeca"),
			Near(DeverauxBody::GravityInRestFrame(FQuat::Identity, UpsideDown * Ref, Ref), FVector(0.0, 0.0, 1.0)));
		TestTrue(TEXT("De ponta-cabeca: GravCabeca no maximo"), FMath::IsNearlyEqual(DeverauxBody::GravityMorphWeight(FVector(0.0, 0.0, 1.0), FVector(0.0, 0.0, 1.0)), 1.0f, 1.0e-4f));
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

		// Chao com o corpo em cima: a face de saida e a de cima, no terceiro eixo
		const FTransform FloorToWorld(FQuat::Identity, FVector(100.0, 0.0, -25.0));
		const FVector FloorHalf(200.0, 100.0, 25.0);
		FVector4 Box[4];
		DeverauxBody::PackBox(MeshToWorld, FloorToWorld, FloorHalf, FVector(100.0, 0.0, 90.0), Box);
		TestTrue(TEXT("Caixa: centro ligado (W 1)"), Near4(Box[0], 0.0, 0.0, -25.0, 1.0));
		TestTrue(TEXT("Caixa: eixo X do mundo vira -Y da malha, com a meia largura"), Near4(Box[1], 0.0, -1.0, 0.0, 200.0));
		TestTrue(TEXT("Caixa: eixo Y do mundo vira X da malha"), Near4(Box[2], 1.0, 0.0, 0.0, 100.0));
		TestTrue(TEXT("Caixa: o corpo em cima, a saida e o Z, para cima"), Near4(Box[3], 0.0, 0.0, 1.0, 25.0));

		// Corpo deitado longe do centro do chao, mas em cima dele: a saida continua sendo o topo
		DeverauxBody::PackBox(MeshToWorld, FloorToWorld, FloorHalf, FVector(250.0, 0.0, 30.0), Box);
		TestTrue(TEXT("Caixa: corpo em cima, perto da borda, a saida ainda e o Z"), Near4(Box[3], 0.0, 0.0, 1.0, 25.0));

		// Corpo ao lado, alem da face -Y do mundo: a saida vira essa face, apontando para ele
		DeverauxBody::PackBox(MeshToWorld, FloorToWorld, FloorHalf, FVector(100.0, -150.0, -25.0), Box);
		TestTrue(TEXT("Caixa: corpo do lado -Y, a saida e -Y do mundo (-X da malha)"), Near4(Box[3], -1.0, 0.0, 0.0, 100.0));
		TestTrue(TEXT("Caixa: os outros dois eixos seguem a ordem (Z e X do mundo)"), Near4(Box[1], 0.0, 0.0, 1.0, 25.0) && Near4(Box[2], 0.0, -1.0, 0.0, 200.0));

		// Corpo embaixo da caixa (um teto): a saida e para baixo
		DeverauxBody::PackBox(MeshToWorld, FloorToWorld, FloorHalf, FVector(100.0, 0.0, -200.0), Box);
		TestTrue(TEXT("Caixa: corpo embaixo, a saida e -Z"), Near4(Box[3], 0.0, 0.0, -1.0, 25.0));

		// Empate entre o lado e o topo (corpo na diagonal da quina): fica o topo, a saida do chao e do assento
		DeverauxBody::PackBox(FTransform::Identity, FTransform::Identity, FVector(10.0, 10.0, 10.0), FVector(15.0, 0.0, 15.0), Box);
		TestTrue(TEXT("Caixa: no empate, a saida e o Z"), Near4(Box[3], 0.0, 0.0, 1.0, 10.0));

		// Centro do corpo dentro da caixa: a face mais perto dele (aqui, a de cima, a mais fina)
		DeverauxBody::PackBox(MeshToWorld, FloorToWorld, FloorHalf, FVector(100.0, 0.0, -20.0), Box);
		TestTrue(TEXT("Caixa: corpo dentro, a face mais perto"), Near4(Box[3], 0.0, 0.0, 1.0, 25.0));

		// Caixa girada: o eixo de saida acompanha o giro dela
		const FQuat Tilt = FRotator(0.0, 0.0, 90.0).Quaternion();
		DeverauxBody::PackBox(FTransform::Identity, FTransform(Tilt, FVector::ZeroVector), FVector(10.0, 20.0, 30.0), Tilt.GetAxisZ() * 100.0, Box);
		TestTrue(TEXT("Caixa girada: a saida e o Z dela"), Near4(Box[3], Tilt.GetAxisZ().X, Tilt.GetAxisZ().Y, Tilt.GetAxisZ().Z, 30.0));
	}

	// Lugar de cada coisa no array Contatos: o kernel usa os mesmos numeros escritos a mao
	{
		using namespace DeverauxBody::ContactLayout;
		TestTrue(TEXT("Layout do array: pares, apertos, massas, chaos, capsulas e caixas nos lugares do kernel"),
			Pairs == 2 && Compression == 5 && Masses == 7 && Floors == 19 && Capsules == 26 && Boxes == 58 && MaxCapsules == 16 && MaxBoxes == 2);
		TestTrue(TEXT("Layout do array: 66 vetores"), Num == 66 && NumFloors == 7);
	}

	// Pontos mais perto
	{
		TestTrue(TEXT("Ponto no meio do segmento"), Near(DeverauxBody::ClosestPointOnSegment(FVector(5.0, 3.0, 0.0), FVector::ZeroVector, FVector(10.0, 0.0, 0.0)), FVector(5.0, 0.0, 0.0)));
		TestTrue(TEXT("Ponto alem da ponta: a ponta"), Near(DeverauxBody::ClosestPointOnSegment(FVector(15.0, 3.0, 0.0), FVector::ZeroVector, FVector(10.0, 0.0, 0.0)), FVector(10.0, 0.0, 0.0)));
		TestTrue(TEXT("Segmento de um ponto so: o ponto"), Near(DeverauxBody::ClosestPointOnSegment(FVector(15.0, 3.0, 0.0), FVector(1.0, 2.0, 3.0), FVector(1.0, 2.0, 3.0)), FVector(1.0, 2.0, 3.0)));

		FVector OnP;
		FVector OnQ;
		DeverauxBody::ClosestPointsBetweenSegments(FVector(-10.0, 0.0, 0.0), FVector(10.0, 0.0, 0.0), FVector(2.0, -10.0, 5.0), FVector(2.0, 10.0, 5.0), OnP, OnQ);
		TestTrue(TEXT("Segmentos em cruz, um em cima do outro"), Near(OnP, FVector(2.0, 0.0, 0.0)) && Near(OnQ, FVector(2.0, 0.0, 5.0)));
		DeverauxBody::ClosestPointsBetweenSegments(FVector(0.0, 0.0, 0.0), FVector(10.0, 0.0, 0.0), FVector(20.0, 5.0, 0.0), FVector(30.0, 5.0, 0.0), OnP, OnQ);
		TestTrue(TEXT("Segmentos na mesma linha, separados: as pontas que se olham"), Near(OnP, FVector(10.0, 0.0, 0.0)) && Near(OnQ, FVector(20.0, 5.0, 0.0)));
		DeverauxBody::ClosestPointsBetweenSegments(FVector(0.0, 0.0, 0.0), FVector(0.0, 0.0, 10.0), FVector(4.0, 0.0, 2.0), FVector(4.0, 0.0, 8.0), OnP, OnQ);
		TestTrue(TEXT("Segmentos paralelos: a distancia certa"), FMath::IsNearlyEqual((OnQ - OnP).Size(), 4.0, Tolerance));
		DeverauxBody::ClosestPointsBetweenSegments(FVector(3.0, 3.0, 3.0), FVector(3.0, 3.0, 3.0), FVector(0.0, 0.0, 0.0), FVector(0.0, 0.0, 10.0), OnP, OnQ);
		TestTrue(TEXT("Um dos segmentos e um ponto"), Near(OnP, FVector(3.0, 3.0, 3.0)) && Near(OnQ, FVector(0.0, 0.0, 3.0)));
	}

	// Plano do par: entre as duas massas, com a normal da esquerda (+X, como no manequim) para a direita
	{
		float Overlap = 0.0f;
		const FVector4 Equal = DeverauxBody::PairPlane(FVector(10.0, 0.0, 0.0), FVector(10.0, 10.0, 0.0), 8.0f,
			FVector(-10.0, 0.0, 0.0), FVector(-10.0, 10.0, 0.0), 8.0f, FVector(-1.0, 0.0, 0.0), Overlap);
		TestTrue(TEXT("Par igual: plano no meio, normal para a direita"), Near4(Equal, -1.0, 0.0, 0.0, 0.0));
		TestTrue(TEXT("Par com folga de 4 cm: sobreposicao -4"), FMath::IsNearlyEqual(Overlap, -4.0f, 1.0e-4f));

		const FVector4 Unequal = DeverauxBody::PairPlane(FVector(10.0, 0.0, 0.0), FVector(10.0, 10.0, 0.0), 10.0f,
			FVector(-10.0, 0.0, 0.0), FVector(-10.0, 10.0, 0.0), 6.0f, FVector(-1.0, 0.0, 0.0), Overlap);
		TestTrue(TEXT("Raios diferentes: o plano fica no meio da folga entre as superficies (x = -2)"), Near4(Unequal, -1.0, 0.0, 0.0, 2.0));

		const FVector4 Pressed = DeverauxBody::PairPlane(FVector(4.0, 0.0, 0.0), FVector(4.0, 10.0, 0.0), 8.0f,
			FVector(-2.0, 0.0, 0.0), FVector(-2.0, 10.0, 0.0), 8.0f, FVector(-1.0, 0.0, 0.0), Overlap);
		TestTrue(TEXT("Apertadas: o plano fica no meio da parte que se sobrepoe (x = 1)"), Near4(Pressed, -1.0, 0.0, 0.0, -1.0));
		TestTrue(TEXT("Apertadas: sobreposicao 10"), FMath::IsNearlyEqual(Overlap, 10.0f, 1.0e-4f));

		const FVector4 Crossed = DeverauxBody::PairPlane(FVector(-5.0, 0.0, 0.0), FVector(5.0, 0.0, 0.0), 5.0f,
			FVector(0.0, -5.0, 0.0), FVector(0.0, 5.0, 0.0), 5.0f, FVector(-1.0, 0.0, 0.0), Overlap);
		TestTrue(TEXT("Eixos se cruzando: usa a normal de reserva"), Near4(Crossed, -1.0, 0.0, 0.0, 0.0));

		// Coxas cruzadas: a esquerda passou para o outro lado; a normal continua da esquerda para a direita
		const FVector4 CrossedLegs = DeverauxBody::PairPlane(FVector(-12.0, 0.0, 90.0), FVector(-14.0, 5.0, 50.0), 9.0f,
			FVector(10.0, 0.0, 90.0), FVector(12.0, 2.0, 50.0), 9.0f, FVector(-1.0, 0.0, 0.0), Overlap);
		TestTrue(TEXT("Pernas cruzadas: a normal segue a massa esquerda para a direita"), CrossedLegs.X > 0.9);
	}

	// Distancia com sinal ate a caixa
	{
		FVector4 Box[4] = { FVector4(0.0, 0.0, 0.0, 1.0), FVector4(1.0, 0.0, 0.0, 10.0), FVector4(0.0, 1.0, 0.0, 5.0), FVector4(0.0, 0.0, 1.0, 2.0) };
		TestTrue(TEXT("Caixa: dentro e negativa, ate a face mais perto"), FMath::IsNearlyEqual(DeverauxBody::SignedDistanceToBox(FVector(0.0, 0.0, 1.0), Box), -1.0, Tolerance));
		TestTrue(TEXT("Caixa: fora, a distancia ate a quina"), FMath::IsNearlyEqual(DeverauxBody::SignedDistanceToBox(FVector(13.0, 9.0, 0.0), Box), 5.0, Tolerance));
		Box[0].W = 0.0;
		TestTrue(TEXT("Caixa desligada: muito longe"), DeverauxBody::SignedDistanceToBox(FVector::ZeroVector, Box) > 1.0e30);
	}

	// Quanto uma massa entra no que encosta
	{
		const FVector A(0.0, 0.0, 0.0);
		const FVector B(0.0, 10.0, 0.0);
		TestTrue(TEXT("Massa e capsula a 12 cm, raios 10 e 5: entra 3"), FMath::IsNearlyEqual(DeverauxBody::MassDepthInCapsule(A, B, 10.0f, FVector(12.0, 5.0, -5.0), FVector(12.0, 5.0, 5.0), 5.0f), 3.0f, 1.0e-4f));
		TestTrue(TEXT("Massa e capsula longe: 0"), DeverauxBody::MassDepthInCapsule(A, B, 10.0f, FVector(40.0, 5.0, -5.0), FVector(40.0, 5.0, 5.0), 5.0f) == 0.0f);
		TestTrue(TEXT("Massa deitada a 8 cm do chao, raio 10: entra 2"), FMath::IsNearlyEqual(DeverauxBody::MassDepthInPlane(FVector(0.0, 0.0, 8.0), FVector(0.0, 10.0, 12.0), 10.0f, FVector4(0.0, 0.0, 1.0, 0.0)), 2.0f, 1.0e-4f));
		TestTrue(TEXT("Chao desligado: 0"), DeverauxBody::MassDepthInPlane(FVector(0.0, 0.0, -50.0), FVector(0.0, 10.0, -50.0), 10.0f, FVector4(0.0, 0.0, 0.0, 0.0)) == 0.0f);
		const FVector4 Seat[4] = { FVector4(0.0, 5.0, -5.0, 1.0), FVector4(1.0, 0.0, 0.0, 20.0), FVector4(0.0, 1.0, 0.0, 20.0), FVector4(0.0, 0.0, 1.0, 5.0) };
		TestTrue(TEXT("Massa 6 cm acima do topo do assento, raio 10: entra 4"), FMath::IsNearlyEqual(DeverauxBody::MassDepthInBox(FVector(0.0, 0.0, 6.0), FVector(0.0, 10.0, 6.0), 10.0f, Seat), 4.0f, 1.0e-4f));
	}

	// Um osso dentro da massa sai do que encosta, com o raio dele
	{
		using namespace DeverauxBody::ContactLayout;
		TArray<FVector4> Values;
		Values.Init(FVector4(0.0, 0.0, 0.0, 0.0), Num);
		Values[Header] = FVector4(2.0, 1.0, 0.0, 0.0);
		// Plano dos seios em x = 0, normal para a direita (-X); capsula de raio 2 em y = 20, so para os seios
		Values[Pairs + DeverauxBody::ContactRegion::Seios] = FVector4(-1.0, 0.0, 0.0, 0.0);
		Values[Capsules] = FVector4(0.0, 20.0, 100.0, 2.0);
		Values[Capsules + 1] = FVector4(0.0, 20.0, 140.0, DeverauxBody::ContactRegion::RegionBits(DeverauxBody::ContactRegion::Seios));
		// Segunda capsula so para as coxas, no mesmo lugar
		Values[Capsules + 2] = FVector4(0.0, 20.0, 100.0, 2.0);
		Values[Capsules + 3] = FVector4(0.0, 20.0, 140.0, DeverauxBody::ContactRegion::RegionBits(DeverauxBody::ContactRegion::Coxas));
		// Chao da coxa esquerda em z = 40
		Values[Floors + 4] = FVector4(0.0, 0.0, 1.0, 40.0);
		// Assento: caixa de 10 cm de altura com o topo em z = 50, saida para cima
		Values[Boxes] = FVector4(100.0, 0.0, 45.0, 1.0);
		Values[Boxes + 1] = FVector4(1.0, 0.0, 0.0, 20.0);
		Values[Boxes + 2] = FVector4(0.0, 1.0, 0.0, 20.0);
		Values[Boxes + 3] = FVector4(0.0, 0.0, 1.0, 5.0);

		TestTrue(TEXT("Seio esquerdo atravessando o plano: volta para o seu lado, com o raio"),
			Near(DeverauxBody::ResolveContactPoint(Values, FVector(-1.0, 0.0, 120.0), 0, 3.0f), FVector(3.0, 0.0, 120.0)));
		TestTrue(TEXT("Seio direito atravessando: volta para o lado dele"),
			Near(DeverauxBody::ResolveContactPoint(Values, FVector(1.0, 0.0, 120.0), 1, 3.0f), FVector(-3.0, 0.0, 120.0)));
		TestTrue(TEXT("Seio direito no seu lado e longe: fica"),
			Near(DeverauxBody::ResolveContactPoint(Values, FVector(-10.0, 0.0, 120.0), 1, 3.0f), FVector(-10.0, 0.0, 120.0)));
		TestTrue(TEXT("Seio dentro da capsula e do plano: sai dos dois (capsula e depois o plano)"),
			FMath::IsNearlyEqual((DeverauxBody::ResolveContactPoint(Values, FVector(4.0, 21.0, 120.0), 0, 3.0f) - FVector(0.0, 20.0, 120.0)).Size(), 5.0, 1.0e-3)
			&& DeverauxBody::ResolveContactPoint(Values, FVector(4.0, 21.0, 120.0), 0, 3.0f).X >= 3.0 - 1.0e-3);
		TestTrue(TEXT("Capsula so dos seios e so das coxas nao empurram o gluteo (2,2 cm do eixo, dentro do alcance de 3)"),
			Near(DeverauxBody::ResolveContactPoint(Values, FVector(2.0, 21.0, 120.0), 2, 1.0f), FVector(2.0, 21.0, 120.0)));
		TestTrue(TEXT("Coxa esquerda abaixo do chao dela: sobe ate o raio acima"),
			Near(DeverauxBody::ResolveContactPoint(Values, FVector(30.0, -30.0, 38.0), 4, 3.0f), FVector(30.0, -30.0, 43.0)));
		TestTrue(TEXT("Coxa esquerda 1 cm acima do chao, raio 3: sobe ate o raio acima"),
			Near(DeverauxBody::ResolveContactPoint(Values, FVector(30.0, -30.0, 41.0), 4, 3.0f), FVector(30.0, -30.0, 43.0)));
		TestTrue(TEXT("Coxa direita sem chao: fica"),
			Near(DeverauxBody::ResolveContactPoint(Values, FVector(-30.0, -30.0, 38.0), 5, 3.0f), FVector(-30.0, -30.0, 38.0)));
		TestTrue(TEXT("Gluteo dentro do assento: sai pelo topo, com o raio"),
			Near(DeverauxBody::ResolveContactPoint(Values, FVector(100.0, 0.0, 48.0), 2, 2.0f), FVector(100.0, 0.0, 52.0)));
		TestTrue(TEXT("Massa fora da faixa: o ponto volta igual"),
			Near(DeverauxBody::ResolveContactPoint(Values, FVector(-1.0, 0.0, 120.0), 6, 3.0f), FVector(-1.0, 0.0, 120.0)));
		TArray<FVector4> Short;
		Short.Init(FVector4(0.0, 0.0, 0.0, 0.0), Num - 1);
		TestTrue(TEXT("Array curto: o ponto volta igual"),
			Near(DeverauxBody::ResolveContactPoint(Short, FVector(-1.0, 0.0, 120.0), 0, 3.0f), FVector(-1.0, 0.0, 120.0)));
	}

	// Duas voltas: a capsula joga o ponto para o outro lado do par, e o par o devolve para dentro da capsula
	{
		using namespace DeverauxBody::ContactLayout;
		TArray<FVector4> Values;
		Values.Init(FVector4(0.0, 0.0, 0.0, 0.0), Num);
		Values[Header] = FVector4(1.0, 0.0, 0.0, 0.0);
		Values[Pairs + DeverauxBody::ContactRegion::Seios] = FVector4(-1.0, 0.0, 0.0, 0.0);
		Values[Capsules] = FVector4(1.0, -10.0, 0.0, 2.0);
		Values[Capsules + 1] = FVector4(1.0, 10.0, 0.0, DeverauxBody::ContactRegion::RegionBits(DeverauxBody::ContactRegion::Seios));
		// Volta 1: (0,5; 0; 0,5) sai da capsula para (-0,41; 0; 1,41) e o par leva para x = 0. Volta 2: sai de novo e volta ao plano
		TestTrue(TEXT("Capsula e plano do par: o ponto anda nas duas voltas"),
			Near(DeverauxBody::ResolveContactPoint(Values, FVector(0.5, 0.0, 0.5), 0, 0.0f), FVector(0.0, 0.0, 1.6329932)));
	}

	// Bits por massa: a capsula sem o bit da coxa esquerda (a panturrilha esquerda) ainda empurra a coxa direita
	{
		using namespace DeverauxBody::ContactLayout;
		TArray<FVector4> Values;
		Values.Init(FVector4(0.0, 0.0, 0.0, 0.0), Num);
		Values[Header] = FVector4(1.0, 0.0, 0.0, 0.0);
		Values[Capsules] = FVector4(0.0, 0.0, 0.0, 5.0);
		Values[Capsules + 1] = FVector4(0.0, 0.0, 10.0, DeverauxBody::ContactRegion::AllBits & ~DeverauxBody::ContactRegion::MassBit(4));
		TestTrue(TEXT("Bits por massa: a coxa esquerda fica"), Near(DeverauxBody::ResolveContactPoint(Values, FVector(3.0, 0.0, 5.0), 4, 0.0f), FVector(3.0, 0.0, 5.0)));
		TestTrue(TEXT("Bits por massa: a coxa direita sai"), Near(DeverauxBody::ResolveContactPoint(Values, FVector(3.0, 0.0, 5.0), 5, 0.0f), FVector(5.0, 0.0, 5.0)));
		TestEqual(TEXT("Bits das duas coxas"), DeverauxBody::ContactRegion::RegionBits(DeverauxBody::ContactRegion::Coxas), 48);
		TestEqual(TEXT("Bit do resto da carne"), DeverauxBody::ContactRegion::RegionBits(DeverauxBody::ContactRegion::Outras), 64);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
