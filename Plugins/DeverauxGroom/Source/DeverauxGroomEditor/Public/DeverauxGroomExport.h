// Projeto Deveraux - exporta um groom (cabelo) da Unreal para Alembic, para editar no Blender ou em outro programa
//
// Le os fios de origem que o editor guarda dentro do groom asset (os do .abc que foi importado, sem a interpolacao,
// a decimacao nem os LODs da Unreal) e grava um objeto de curvas por grupo. As guias vao em objetos proprios.

#pragma once

#include "CoreMinimal.h"
#include "DeverauxGroomAlembic.h"

class UGroomAsset;
class FHairDescription;

namespace DeverauxGroomExport
{
	/**
	 * Para onde vai o arquivo.
	 * Blender: metros, Y para cima (o Blender vira para Z na importacao), sem as guias. O cabelo cai no mesmo lugar da
	 * cabeca exportada da Unreal em FBX.
	 * Unreal: centimetros, Y para cima, com as guias. Importado de volta com Rotation X = 90 e Scale Y = -1 (o padrao
	 * para Alembic com Y para cima), volta exatamente para onde estava.
	 */
	enum class ETarget : uint8
	{
		Blender,
		Unreal
	};

	struct FOptions
	{
		ETarget Target = ETarget::Blender;
		/** Grava as guias (os fios que a simulacao usa) em objetos proprios, com groom_guide = 1. */
		bool bGuides = false;
	};

	/** As opcoes padrao de cada destino: o Blender sem as guias, a Unreal com elas. */
	DEVERAUXGROOMEDITOR_API FOptions DefaultOptions(ETarget Target);

	struct FStats
	{
		int32 Groups = 0;
		int32 Strands = 0;
		int32 Guides = 0;
		/** Guias que o groom tem e ficaram fora (bGuides desligado). */
		int32 SkippedGuides = 0;
		int64 Points = 0;
		int32 Objects = 0;
		int64 Bytes = 0;
		bool bWidths = false;
		bool bRootUV = false;
	};

	/** Escala do arquivo: 0,01 (cm para metros) no Blender, 1 na Unreal. */
	DEVERAUXGROOMEDITOR_API float FileScale(ETarget Target);

	/**
	 * Um ponto do espaco da Unreal (cm, Z para cima, mao esquerda) no espaco do arquivo (Y para cima, mao direita):
	 * troca Y com Z e aplica a escala. E o inverso da conversao padrao da importacao de groom (Scale Y = -1 e depois
	 * Rotation X = 90).
	 */
	DEVERAUXGROOMEDITOR_API FVector3f ToFile(const FVector3f& Point, ETarget Target);

	/**
	 * Monta os objetos de curvas a partir da descricao dos fios: um por grupo para os fios ("<grupo>_fios") e, com
	 * bGuides, outro para as guias ("<grupo>_guias"). Cada um leva largura por ponto e os atributos groom_* que existirem
	 * (grupo, guia, UV da raiz, id, clump, cor, rugosidade, AO).
	 */
	DEVERAUXGROOMEDITOR_API bool BuildCurves(const FHairDescription& Description, const FOptions& Options,
		TArray<DeverauxGroomAlembic::FCurves>& OutObjects, FStats& OutStats, FString& OutError);

	/** Exporta o groom para um arquivo .abc. Falha com a mensagem em OutError (por exemplo, groom sem os fios de origem). */
	DEVERAUXGROOMEDITOR_API bool ExportGroom(UGroomAsset& Groom, const FString& Filename, const FOptions& Options, FStats& OutStats,
		FString& OutError);
}
