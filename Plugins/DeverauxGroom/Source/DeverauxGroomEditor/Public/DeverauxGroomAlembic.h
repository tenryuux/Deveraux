// Projeto Deveraux - gravador de Alembic (.abc, formato Ogawa) para grooms, sem biblioteca externa
//
// Grava curvas paradas (uma amostra, tempo 0) no mesmo formato que a biblioteca oficial do Alembic grava: a mesma
// arvore Ogawa, os mesmos cabecalhos, metadados e hashes (MurmurHash3 nas amostras, SpookyHash V2 nas propriedades
// e objetos). O Blender, o Maya, o Houdini e a propria Unreal abrem o arquivo como qualquer outro .abc.

#pragma once

#include "CoreMinimal.h"

namespace DeverauxGroomAlembic
{
	/** Onde um atributo tem valor: um para o objeto inteiro, um por curva ou um por ponto (geoScope do Alembic). */
	enum class EScope : uint8
	{
		Constant,
		Uniform,
		Vertex
	};

	/** Tipo de cada componente de um atributo. */
	enum class EType : uint8
	{
		Int16,
		Int32,
		Float32
	};

	/**
	 * Um atributo extra das curvas (fica em .geom/.arbGeomParams), como os groom_* que a Unreal le na importacao.
	 * Inteiros vao em Ints e floats em Floats, Extent valores seguidos por elemento (por exemplo 2 para UV).
	 */
	struct FAttribute
	{
		FString Name;
		EScope Scope = EScope::Uniform;
		EType Type = EType::Int32;
		int32 Extent = 1;
		/** "vector", "rgb" ou vazio, como a interpretacao dos tipos do Alembic (V2f, C3f, float). */
		FString Interpretation;
		TArray<int32> Ints;
		TArray<float> Floats;

		/** Quantos elementos o atributo tem (valores / Extent). */
		int32 NumElements() const;
	};

	/** Um objeto de curvas lineares (ICurves do Alembic), com os pontos ja no espaco do arquivo. */
	struct FCurves
	{
		FString Name;
		/** Pontos de cada curva, na ordem. */
		TArray<int32> NumVertices;
		TArray<FVector3f> Positions;
		/** Largura de cada ponto. Vazio: o arquivo vai sem largura. */
		TArray<float> Widths;
		TArray<FAttribute> Attributes;
	};

	/** Metadados do arquivo (o Blender e o Maya mostram como o programa que gravou). */
	struct FArchiveInfo
	{
		FString Application;
		FString Description;
	};

	/**
	 * Grava os objetos num arquivo Alembic, na memoria. Falha (com a mensagem em OutError) se algum objeto nao fecha:
	 * nome vazio ou repetido, contagens que nao batem com os pontos, curva com menos de 1 ponto.
	 */
	DEVERAUXGROOMEDITOR_API bool WriteArchive(const TArray<FCurves>& Objects, const FArchiveInfo& Info, TArray<uint8>& OutBytes,
		FString& OutError);

	/** MurmurHash3 x64 de 128 bits, semente 0 (a assinatura de cada amostra no Alembic). */
	DEVERAUXGROOMEDITOR_API void MurmurHash3(const uint8* Data, uint64 Length, uint64& OutHash1, uint64& OutHash2);

	/** SpookyHash V2 de 128 bits, sementes 0 (o hash das propriedades e dos objetos no Alembic). */
	DEVERAUXGROOMEDITOR_API void SpookyHash(const uint8* Data, uint64 Length, uint64& OutHash1, uint64& OutHash2);
}
