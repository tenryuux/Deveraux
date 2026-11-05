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
}
