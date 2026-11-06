// Projeto Deveraux - contas da fisica do corpo, sem componente nem engine (modulo Deveraux)

#pragma once

#include "CoreMinimal.h"

namespace DeverauxBody
{
	/**
	 * Para onde a gravidade puxa, unitaria, no espaco do corpo parado na pose de referencia (em pe). Em pe e sem
	 * girar da (0, 0, -1); deitada de costas, aponta para as costas do corpo.
	 * ComponentRotation: a malha no mundo. BoneRotation: o osso do tronco agora, no espaco da malha.
	 * BoneRefRotation: o mesmo osso na pose de referencia, no espaco da malha.
	 */
	DEVERAUX_API FVector GravityInRestFrame(const FQuat& ComponentRotation, const FQuat& BoneRotation, const FQuat& BoneRefRotation);

	/**
	 * Peso de um morph de gravidade: o quadrado do alinhamento da gravidade com a direcao dele (1 com ela em cima,
	 * 0,5 a 45 graus, 0 a 90 graus ou mais). Nas seis direcoes dos eixos, os pesos somam 1 junto com o da escultura
	 * em pe: entre duas poses, o corpo fica numa mistura das duas esculturas, sem somar uma em cima da outra.
	 */
	DEVERAUX_API float GravityMorphWeight(const FVector& Gravity, const FVector& Direction);

	/**
	 * Os pesos de todos os morphs juntos nunca passam de 1: se passarem (direcoes que se sobrepoem), todos diminuem
	 * na mesma proporcao. Cada escultura ja tem a pose inteira; somar mais que 1 exagera a carne.
	 */
	DEVERAUX_API void LimitMorphWeights(TArray<float>& Weights);

	/** Anda de Current para Target sem passar do ponto: em ResponseSeconds cobre 63% do caminho. 0 ou menos: chega na hora. */
	DEVERAUX_API float SmoothTowards(float Current, float Target, float DeltaSeconds, float ResponseSeconds);
	DEVERAUX_API FVector SmoothTowards(const FVector& Current, const FVector& Target, float DeltaSeconds, float ResponseSeconds);

	/**
	 * Escala do squash nos eixos do osso: achata na direcao do balanco (em cm, no espaco do osso) e alarga nas
	 * outras, mantendo o volume. StrengthPerCm negativo estica em vez de achatar. Cada eixo fica entre
	 * 1 - MaxChange e 1 + MaxChange, com MaxChange de 0 a 0,9.
	 */
	DEVERAUX_API FVector SquashScale(const FVector& SwingLocal, float StrengthPerCm, float MaxChange);

	/** Uma esfera de contato no espaco da malha: centro em XYZ e raio em W. */
	DEVERAUX_API FVector4 PackSphere(const FTransform& MeshToWorld, const FVector& CenterWorld, float RadiusWorld);

	/**
	 * Uma caixa de contato no espaco da malha, em quatro vetores: o centro (W = 1: caixa ligada) e os tres eixos da
	 * caixa, cada um unitario em XYZ e com a meia largura naquele eixo em W. O terceiro e o da face virada para o
	 * corpo, apontando para ele: o eixo em que o centro do corpo fica mais para fora da caixa (o de cima, no chao e
	 * no assento). O kernel tira a carne por essa face ou pelos lados, nunca pela de tras.
	 */
	DEVERAUX_API void PackBox(const FTransform& MeshToWorld, const FTransform& BoxToWorld, const FVector& HalfExtentWorld,
		const FVector& BodyCenterWorld, FVector4 OutBox[4]);

	// ---- Contato das massas: seios, gluteos e coxas ----

	/**
	 * Partes com contato. Cada par tem duas massas, a esquerda e a direita, nesta ordem: seio esquerdo, seio direito,
	 * gluteo esquerdo, gluteo direito, coxa esquerda, coxa direita. Indice da massa = par * 2 + lado (0 = esquerda).
	 * Outras e o resto da carne macia (a barriga): ela so encosta no chao, nas capsulas e nas caixas.
	 */
	namespace ContactRegion
	{
		constexpr int32 Seios = 0;
		constexpr int32 Gluteos = 1;
		constexpr int32 Coxas = 2;
		constexpr int32 Outras = 3;
		constexpr int32 NumPairs = 3;
		constexpr int32 NumMasses = 6;

		/**
		 * Bit de uma massa (0 a 5) ou do resto da carne (NumMasses), para dizer o que uma capsula empurra. Por massa, e
		 * nao por parte: a panturrilha esquerda, que ja encosta na coxa esquerda parada, ainda empurra a direita.
		 */
		constexpr int32 MassBit(int32 Mass) { return 1 << Mass; }
		/** Os bits dos dois lados de uma parte (Seios, Gluteos, Coxas) ou o do resto da carne (Outras). */
		constexpr int32 RegionBits(int32 Region) { return Region < NumPairs ? MassBit(Region * 2) | MassBit(Region * 2 + 1) : MassBit(NumMasses); }
		constexpr int32 AllBits = 127;
	}

	/**
	 * Lugar de cada coisa no array Contatos (Vector 4) que o componente de contato manda ao deformer a cada quadro, e
	 * que o no de contato dos ossos le. Tudo no espaco da malha, em cm. Um plano e (normal unitaria, d): o ponto P fica
	 * nele quando dot(normal, P) = d. Normal zero = desligado.
	 */
	namespace ContactLayout
	{
		/** (capsulas, caixas, bojo em cm, faixa do bojo em cm). */
		constexpr int32 Header = 0;
		/** (sinal de X do lado esquerdo do corpo na pose de referencia, faixa do meio do corpo em cm, 0, 0). */
		constexpr int32 Settings = 1;
		/** Tres planos, um por par, com a normal saindo da massa esquerda para a direita. */
		constexpr int32 Pairs = 2;
		/** Dois vetores com o aperto de cada massa, de 0 a 1: (seio E, seio D, gluteo E, gluteo D) e (coxa E, coxa D, 0, 0). */
		constexpr int32 Compression = 5;
		/** Dois vetores por massa: (comeco do eixo, raio) e (fim do eixo, 0). Raio 0 = massa desligada. */
		constexpr int32 Masses = 7;
		/** Sete planos de chao, com a normal para cima: um por massa e, por ultimo, o do resto da carne. */
		constexpr int32 Floors = Masses + ContactRegion::NumMasses * 2;
		constexpr int32 NumFloors = ContactRegion::NumMasses + 1;
		/** Capsulas, dois vetores cada: (ponta A, raio) e (ponta B, bits das massas que ela empurra, MassBit). Esfera: A = B. */
		constexpr int32 Capsules = Floors + NumFloors;
		constexpr int32 MaxCapsules = 16;
		/** Caixas, quatro vetores cada, como sai do PackBox. */
		constexpr int32 Boxes = Capsules + MaxCapsules * 2;
		constexpr int32 MaxBoxes = 2;
		/** Tamanho do array. */
		constexpr int32 Num = Boxes + MaxBoxes * 4;
	}

	/** O ponto do segmento AB mais perto de Point. */
	DEVERAUX_API FVector ClosestPointOnSegment(const FVector& Point, const FVector& A, const FVector& B);

	/** Os pontos mais perto entre os segmentos P0-P1 e Q0-Q1, um em cada (Real-Time Collision Detection, 5.1.9). */
	DEVERAUX_API void ClosestPointsBetweenSegments(const FVector& P0, const FVector& P1, const FVector& Q0, const FVector& Q1,
		FVector& OutOnP, FVector& OutOnQ);

	/**
	 * Plano entre as duas massas de um par, cada uma uma capsula (eixo e raio). A normal vai da massa esquerda para a
	 * direita, pelos pontos mais perto dos dois eixos, e o plano fica no meio da folga entre as superficies (ou da
	 * parte em que elas entram uma na outra). Com os eixos se cruzando, a normal e FallbackNormal.
	 * OutOverlap: quanto as duas capsulas entram uma na outra, em cm; negativo e a folga.
	 */
	DEVERAUX_API FVector4 PairPlane(const FVector& LeftA, const FVector& LeftB, float LeftRadius, const FVector& RightA,
		const FVector& RightB, float RightRadius, const FVector& FallbackNormal, float& OutOverlap);

	/** Distancia com sinal ate uma caixa empacotada pelo PackBox: negativa dentro. Caixa desligada: muito longe. */
	DEVERAUX_API double SignedDistanceToBox(const FVector& Point, const FVector4 Box[4]);

	/**
	 * Quanto uma massa (capsula de eixo MassA-MassB) entra numa forma, em cm; 0 sem encostar. Capsula: a outra capsula.
	 * Plano: o chao, livre para o lado da normal. Caixa: o pior de tres pontos do eixo da massa.
	 */
	DEVERAUX_API float MassDepthInCapsule(const FVector& MassA, const FVector& MassB, float MassRadius, const FVector& A,
		const FVector& B, float Radius);
	DEVERAUX_API float MassDepthInPlane(const FVector& MassA, const FVector& MassB, float MassRadius, const FVector4& Plane);
	DEVERAUX_API float MassDepthInBox(const FVector& MassA, const FVector& MassB, float MassRadius, const FVector4 Box[4]);

	/**
	 * Tira um ponto com raio (um osso dentro da massa) de dentro do que esta no array Contatos, como o kernel faz com a
	 * carne: as capsulas que empurram a massa, as caixas, o chao da massa e o plano do par, duas voltas.
	 * Mass: 0 a 5 (par * 2 + lado). Array menor que ContactLayout::Num ou massa fora da faixa: o ponto volta igual.
	 */
	DEVERAUX_API FVector ResolveContactPoint(const TArray<FVector4>& Values, const FVector& Point, int32 Mass, float Radius);
}
