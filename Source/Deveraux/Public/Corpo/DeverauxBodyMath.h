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

	/** Peso de um morph de gravidade: 0 com a gravidade a 90 graus ou mais da direcao dele, 1 com ela em cima, suave no meio. */
	DEVERAUX_API float GravityMorphWeight(const FVector& Gravity, const FVector& Direction);

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
	 * caixa, cada um unitario em XYZ e com a meia largura naquele eixo em W.
	 */
	DEVERAUX_API void PackBox(const FTransform& MeshToWorld, const FTransform& BoxToWorld, const FVector& HalfExtentWorld, FVector4 OutBox[4]);
}
