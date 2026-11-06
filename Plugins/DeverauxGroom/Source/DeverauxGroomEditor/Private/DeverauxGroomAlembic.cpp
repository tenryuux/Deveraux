// Projeto Deveraux - gravador de Alembic (.abc, formato Ogawa) para grooms, sem biblioteca externa
//
// O arquivo segue o layout que a biblioteca oficial grava (AbcCoreOgawa, versao de arquivo 0):
// - Ogawa: cabecalho de 16 bytes ("Ogawa", byte de fechado, versao 0.1, posicao do grupo raiz) e blocos. Um dado e
//   (tamanho u64, bytes); um grupo e (quantidade u64, referencias u64). Referencia com o bit de cima ligado e dado.
// - Raiz: versao do arquivo, versao da biblioteca, objeto de cima, metadados do arquivo, amostragens de tempo e a
//   tabela de metadados curtos.
// - Objeto: grupo com o compound das propriedades, os objetos filhos e, por ultimo, os cabecalhos dos filhos mais
//   32 bytes de hash. Compound: as propriedades e, por ultimo, os cabecalhos delas.
// - Propriedade: um dado por amostra (assinatura de 16 bytes + valores) e, nos arrays, as dimensoes.
// Os hashes sao os mesmos da biblioteca: MurmurHash3 x64 128 (Austin Appleby, dominio publico) na assinatura de cada
// amostra e SpookyHash V2 (Bob Jenkins, dominio publico) nas propriedades e objetos.

#include "DeverauxGroomAlembic.h"

namespace DeverauxGroomAlembicPrivate
{
	using namespace DeverauxGroomAlembic;

	constexpr uint64 EmptyData = 0x8000000000000000ull;
	/** Versao do layout dentro do Ogawa e versao da biblioteca que o arquivo declara (1.8.3, a do Blender 5.2). */
	constexpr int32 OgawaFileVersion = 0;
	constexpr int32 LibraryVersion = 10803;
	/** Tipos do Alembic (PlainOldDataType) usados aqui. */
	constexpr uint8 PodUint8 = 1;
	constexpr uint8 PodInt16 = 4;
	constexpr uint8 PodInt32 = 6;
	constexpr uint8 PodFloat32 = 10;
	constexpr uint8 PodFloat64 = 11;
	/** Curvas lineares, abertas e sem base (o tipo, a periodicidade e a base duas vezes, como a biblioteca grava). */
	constexpr uint8 CurveLinear = 1;
	constexpr uint8 CurveNonPeriodic = 0;
	constexpr uint8 CurveNoBasis = 0;

	// ---- Bytes em little endian ----

	void Put8(TArray<uint8>& Out, uint8 Value)
	{
		Out.Add(Value);
	}

	void Put32(TArray<uint8>& Out, uint32 Value)
	{
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Out.Add(uint8(Value >> (8 * Index)));
		}
	}

	void Put64(TArray<uint8>& Out, uint64 Value)
	{
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Out.Add(uint8(Value >> (8 * Index)));
		}
	}

	void PutDouble(TArray<uint8>& Out, double Value)
	{
		uint64 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		Put64(Out, Bits);
	}

	void PutFloat(TArray<uint8>& Out, float Value)
	{
		uint32 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		Put32(Out, Bits);
	}

	/** Um inteiro sem sinal com 1, 2 ou 4 bytes (dica 0, 1 ou 2), como os tamanhos nos cabecalhos. */
	void PutWithHint(TArray<uint8>& Out, uint32 Value, uint32 Hint)
	{
		const int32 NumBytes = Hint == 0 ? 1 : (Hint == 1 ? 2 : 4);
		for (int32 Index = 0; Index < NumBytes; ++Index)
		{
			Out.Add(uint8(Value >> (8 * Index)));
		}
	}

	void PutBytes(TArray<uint8>& Out, const TArray<uint8>& Bytes)
	{
		Out.Append(Bytes);
	}

	TArray<uint8> Utf8(const FString& Text)
	{
		TArray<uint8> Bytes;
		const FTCHARToUTF8 Converted(*Text);
		Bytes.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
		return Bytes;
	}

	uint64 Read64(const uint8* Data)
	{
		uint64 Value = 0;
		for (int32 Index = 7; Index >= 0; --Index)
		{
			Value = (Value << 8) | Data[Index];
		}
		return Value;
	}

	uint32 Read32(const uint8* Data)
	{
		return uint32(Data[0]) | (uint32(Data[1]) << 8) | (uint32(Data[2]) << 16) | (uint32(Data[3]) << 24);
	}

	uint64 Rot64(uint64 Value, int32 Bits)
	{
		return (Value << Bits) | (Value >> (64 - Bits));
	}

	// ---- SpookyHash V2 ----

	constexpr uint64 SpookyConst = 0xdeadbeefdeadbeefull;
	constexpr uint64 SpookyBlock = 96;
	constexpr uint64 SpookyBuffer = 192;

	void ShortMix(uint64& H0, uint64& H1, uint64& H2, uint64& H3)
	{
		H2 = Rot64(H2, 50); H2 += H3; H0 ^= H2;
		H3 = Rot64(H3, 52); H3 += H0; H1 ^= H3;
		H0 = Rot64(H0, 30); H0 += H1; H2 ^= H0;
		H1 = Rot64(H1, 41); H1 += H2; H3 ^= H1;
		H2 = Rot64(H2, 54); H2 += H3; H0 ^= H2;
		H3 = Rot64(H3, 48); H3 += H0; H1 ^= H3;
		H0 = Rot64(H0, 38); H0 += H1; H2 ^= H0;
		H1 = Rot64(H1, 37); H1 += H2; H3 ^= H1;
		H2 = Rot64(H2, 62); H2 += H3; H0 ^= H2;
		H3 = Rot64(H3, 34); H3 += H0; H1 ^= H3;
		H0 = Rot64(H0, 5); H0 += H1; H2 ^= H0;
		H1 = Rot64(H1, 36); H1 += H2; H3 ^= H1;
	}

	void ShortEnd(uint64& H0, uint64& H1, uint64& H2, uint64& H3)
	{
		H3 ^= H2; H2 = Rot64(H2, 15); H3 += H2;
		H0 ^= H3; H3 = Rot64(H3, 52); H0 += H3;
		H1 ^= H0; H0 = Rot64(H0, 26); H1 += H0;
		H2 ^= H1; H1 = Rot64(H1, 51); H2 += H1;
		H3 ^= H2; H2 = Rot64(H2, 28); H3 += H2;
		H0 ^= H3; H3 = Rot64(H3, 9); H0 += H3;
		H1 ^= H0; H0 = Rot64(H0, 47); H1 += H0;
		H2 ^= H1; H1 = Rot64(H1, 54); H2 += H1;
		H3 ^= H2; H2 = Rot64(H2, 32); H3 += H2;
		H0 ^= H3; H3 = Rot64(H3, 25); H0 += H3;
		H1 ^= H0; H0 = Rot64(H0, 63); H1 += H0;
	}

	void SpookyShort(const uint8* Data, uint64 Length, uint64& Hash1, uint64& Hash2)
	{
		uint64 Remainder = Length % 32;
		uint64 A = Hash1;
		uint64 B = Hash2;
		uint64 C = SpookyConst;
		uint64 D = SpookyConst;
		const uint8* At = Data;
		if (Length > 15)
		{
			const uint8* End = Data + (Length / 32) * 32;
			for (; At < End; At += 32)
			{
				C += Read64(At);
				D += Read64(At + 8);
				ShortMix(A, B, C, D);
				A += Read64(At + 16);
				B += Read64(At + 24);
			}
			if (Remainder >= 16)
			{
				C += Read64(At);
				D += Read64(At + 8);
				ShortMix(A, B, C, D);
				At += 16;
				Remainder -= 16;
			}
		}
		D += Length << 56;
		switch (Remainder)
		{
		case 15: D += uint64(At[14]) << 48; [[fallthrough]];
		case 14: D += uint64(At[13]) << 40; [[fallthrough]];
		case 13: D += uint64(At[12]) << 32; [[fallthrough]];
		case 12: D += Read32(At + 8); C += Read64(At); break;
		case 11: D += uint64(At[10]) << 16; [[fallthrough]];
		case 10: D += uint64(At[9]) << 8; [[fallthrough]];
		case 9: D += uint64(At[8]); [[fallthrough]];
		case 8: C += Read64(At); break;
		case 7: C += uint64(At[6]) << 48; [[fallthrough]];
		case 6: C += uint64(At[5]) << 40; [[fallthrough]];
		case 5: C += uint64(At[4]) << 32; [[fallthrough]];
		case 4: C += Read32(At); break;
		case 3: C += uint64(At[2]) << 16; [[fallthrough]];
		case 2: C += uint64(At[1]) << 8; [[fallthrough]];
		case 1: C += uint64(At[0]); break;
		default: C += SpookyConst; D += SpookyConst; break;
		}
		ShortEnd(A, B, C, D);
		Hash1 = A;
		Hash2 = B;
	}

	void SpookyMix(const uint8* Data, uint64 (&S)[12])
	{
		S[0] += Read64(Data); S[2] ^= S[10]; S[11] ^= S[0]; S[0] = Rot64(S[0], 11); S[11] += S[1];
		S[1] += Read64(Data + 8); S[3] ^= S[11]; S[0] ^= S[1]; S[1] = Rot64(S[1], 32); S[0] += S[2];
		S[2] += Read64(Data + 16); S[4] ^= S[0]; S[1] ^= S[2]; S[2] = Rot64(S[2], 43); S[1] += S[3];
		S[3] += Read64(Data + 24); S[5] ^= S[1]; S[2] ^= S[3]; S[3] = Rot64(S[3], 31); S[2] += S[4];
		S[4] += Read64(Data + 32); S[6] ^= S[2]; S[3] ^= S[4]; S[4] = Rot64(S[4], 17); S[3] += S[5];
		S[5] += Read64(Data + 40); S[7] ^= S[3]; S[4] ^= S[5]; S[5] = Rot64(S[5], 28); S[4] += S[6];
		S[6] += Read64(Data + 48); S[8] ^= S[4]; S[5] ^= S[6]; S[6] = Rot64(S[6], 39); S[5] += S[7];
		S[7] += Read64(Data + 56); S[9] ^= S[5]; S[6] ^= S[7]; S[7] = Rot64(S[7], 57); S[6] += S[8];
		S[8] += Read64(Data + 64); S[10] ^= S[6]; S[7] ^= S[8]; S[8] = Rot64(S[8], 55); S[7] += S[9];
		S[9] += Read64(Data + 72); S[11] ^= S[7]; S[8] ^= S[9]; S[9] = Rot64(S[9], 54); S[8] += S[10];
		S[10] += Read64(Data + 80); S[0] ^= S[8]; S[9] ^= S[10]; S[10] = Rot64(S[10], 22); S[9] += S[11];
		S[11] += Read64(Data + 88); S[1] ^= S[9]; S[10] ^= S[11]; S[11] = Rot64(S[11], 46); S[10] += S[0];
	}

	void SpookyEndPartial(uint64 (&H)[12])
	{
		H[11] += H[1]; H[2] ^= H[11]; H[1] = Rot64(H[1], 44);
		H[0] += H[2]; H[3] ^= H[0]; H[2] = Rot64(H[2], 15);
		H[1] += H[3]; H[4] ^= H[1]; H[3] = Rot64(H[3], 34);
		H[2] += H[4]; H[5] ^= H[2]; H[4] = Rot64(H[4], 21);
		H[3] += H[5]; H[6] ^= H[3]; H[5] = Rot64(H[5], 38);
		H[4] += H[6]; H[7] ^= H[4]; H[6] = Rot64(H[6], 33);
		H[5] += H[7]; H[8] ^= H[5]; H[7] = Rot64(H[7], 10);
		H[6] += H[8]; H[9] ^= H[6]; H[8] = Rot64(H[8], 13);
		H[7] += H[9]; H[10] ^= H[7]; H[9] = Rot64(H[9], 38);
		H[8] += H[10]; H[11] ^= H[8]; H[10] = Rot64(H[10], 53);
		H[9] += H[11]; H[0] ^= H[9]; H[11] = Rot64(H[11], 42);
		H[10] += H[0]; H[1] ^= H[10]; H[0] = Rot64(H[0], 54);
	}

	void SpookyLong(const uint8* Data, uint64 Length, uint64& Hash1, uint64& Hash2)
	{
		uint64 H[12];
		H[0] = H[3] = H[6] = H[9] = Hash1;
		H[1] = H[4] = H[7] = H[10] = Hash2;
		H[2] = H[5] = H[8] = H[11] = SpookyConst;
		const uint64 NumBlocks = Length / SpookyBlock;
		for (uint64 Block = 0; Block < NumBlocks; ++Block)
		{
			SpookyMix(Data + Block * SpookyBlock, H);
		}
		const uint64 Remainder = Length - NumBlocks * SpookyBlock;
		uint8 Last[SpookyBlock] = {};
		if (Remainder > 0)
		{
			FMemory::Memcpy(Last, Data + NumBlocks * SpookyBlock, Remainder);
		}
		Last[SpookyBlock - 1] = uint8(Remainder);
		for (int32 Index = 0; Index < 12; ++Index)
		{
			H[Index] += Read64(Last + 8 * Index);
		}
		SpookyEndPartial(H);
		SpookyEndPartial(H);
		SpookyEndPartial(H);
		Hash1 = H[0];
		Hash2 = H[1];
	}

	void Spooky(const TArray<uint8>& Bytes, uint64 (&OutHash)[2])
	{
		SpookyHash(Bytes.GetData(), uint64(Bytes.Num()), OutHash[0], OutHash[1]);
	}

	// ---- Metadados ----

	struct FMetaPair
	{
		const TCHAR* Key;
		FString Value;
	};

	/** Junta pares chave=valor em ordem de chave, sem os de valor vazio, como o MetaData do Alembic serializa. */
	FString MetaData(TArray<FMetaPair> Pairs)
	{
		Pairs.Sort([](const FMetaPair& A, const FMetaPair& B)
		{
			return FCString::Strcmp(A.Key, B.Key) < 0;
		});
		FString Out;
		for (const FMetaPair& Pair : Pairs)
		{
			if (Pair.Value.IsEmpty())
			{
				continue;
			}
			if (!Out.IsEmpty())
			{
				Out += TEXT(";");
			}
			Out += Pair.Key;
			Out += TEXT("=");
			Out += Pair.Value;
		}
		return Out;
	}

	/** Valor livre de metadado: sem os separadores ; e = (a biblioteca recusa). */
	FString CleanValue(const FString& Value)
	{
		return Value.Replace(TEXT(";"), TEXT(" ")).Replace(TEXT("="), TEXT(" "));
	}

	FString GeomParamMetaData(const TCHAR* Scope, const FString& Interpretation, int32 Extent, const TCHAR* PodName)
	{
		return MetaData({
			{ TEXT("arrayExtent"), TEXT("1") },
			{ TEXT("geoScope"), Scope },
			{ TEXT("interpretation"), Interpretation },
			{ TEXT("isGeomParam"), TEXT("true") },
			{ TEXT("podExtent"), FString::FromInt(Extent) },
			{ TEXT("podName"), PodName } });
	}

	// ---- Propriedades ----

	enum class EKind : uint8
	{
		Compound,
		Scalar,
		Array
	};

	struct FProp
	{
		EKind Kind = EKind::Compound;
		TArray<uint8> Name;
		TArray<uint8> Meta;
		uint8 Pod = 0;
		uint8 Extent = 1;
		/** A amostra unica (escalar ou array), em little endian. */
		TArray<uint8> Sample;
		/** Array: quantos valores (cada um com Extent componentes). */
		uint64 NumPoints = 0;
		TArray<FProp> Children;
	};

	FProp Compound(const FString& Name, const FString& Meta)
	{
		FProp Prop;
		Prop.Name = Utf8(Name);
		Prop.Meta = Utf8(Meta);
		return Prop;
	}

	FProp Leaf(EKind Kind, const FString& Name, const FString& Meta, uint8 Pod, uint8 Extent)
	{
		FProp Prop = Compound(Name, Meta);
		Prop.Kind = Kind;
		Prop.Pod = Pod;
		Prop.Extent = Extent;
		return Prop;
	}

	/** Os bytes que entram no hash do cabecalho de uma propriedade (nome, metadado, tipo e a amostragem de tempo 0). */
	TArray<uint8> HeaderHashBytes(const FProp& Prop)
	{
		TArray<uint8> Bytes = Prop.Name;
		Bytes.Append(Prop.Meta);
		if (Prop.Kind != EKind::Compound)
		{
			Put8(Bytes, Prop.Pod);
			Put8(Bytes, Prop.Extent);
			if (Prop.Kind == EKind::Scalar)
			{
				Put8(Bytes, 0);
			}
			// Amostragem padrao: um ciclo de 1 s com uma amostra em 0
			PutDouble(Bytes, 1.0);
			Put32(Bytes, 1);
			PutDouble(Bytes, 0.0);
		}
		return Bytes;
	}

	class FWriter
	{
	public:
		TArray<uint8> Out;
		/** Tabela de metadados curtos: o indice gravado e a posicao + 1 (0 e o metadado vazio, 255 vai escrito). */
		TArray<TArray<uint8>> MetaTable;

		FWriter()
		{
			static const uint8 Header[16] = { 'O', 'g', 'a', 'w', 'a', 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
			Out.Append(Header, 16);
		}

		uint64 AddData(const TArray<uint8>& Bytes)
		{
			if (Bytes.Num() == 0)
			{
				return EmptyData;
			}
			const uint64 Pos = uint64(Out.Num());
			Put64(Out, uint64(Bytes.Num()));
			Out.Append(Bytes);
			return Pos | EmptyData;
		}

		/** Amostra: a assinatura de 16 bytes e os valores. Sem valores: dado vazio, sem assinatura. */
		uint64 AddSample(const uint64 (&Digest)[2], const TArray<uint8>& Bytes)
		{
			if (Bytes.Num() == 0)
			{
				return EmptyData;
			}
			const uint64 Pos = uint64(Out.Num());
			Put64(Out, uint64(Bytes.Num()) + 16);
			Put64(Out, Digest[0]);
			Put64(Out, Digest[1]);
			Out.Append(Bytes);
			return Pos | EmptyData;
		}

		uint64 AddGroup(const TArray<uint64>& Children)
		{
			if (Children.Num() == 0)
			{
				return 0;
			}
			const uint64 Pos = uint64(Out.Num());
			Put64(Out, uint64(Children.Num()));
			for (uint64 Child : Children)
			{
				Put64(Out, Child);
			}
			return Pos;
		}

		uint32 MetaIndex(const TArray<uint8>& Meta)
		{
			if (Meta.Num() == 0)
			{
				return 0;
			}
			if (Meta.Num() < 256)
			{
				const int32 Found = MetaTable.IndexOfByKey(Meta);
				if (Found != INDEX_NONE)
				{
					return uint32(Found) + 1;
				}
				if (MetaTable.Num() < 254)
				{
					MetaTable.Add(Meta);
					return uint32(MetaTable.Num());
				}
			}
			return 255;
		}

		void AppendPropertyInfo(TArray<uint8>& Headers, const FProp& Prop)
		{
			const uint32 MetaSize = uint32(Prop.Meta.Num());
			const uint32 NameSize = uint32(Prop.Name.Num());
			const uint32 NumSamples = Prop.Kind == EKind::Compound ? 0 : 1;
			const uint32 MaxSize = FMath::Max(FMath::Max(MetaSize, NameSize), NumSamples);
			const uint32 Hint = MaxSize >= 65536 ? 2 : (MaxSize > 255 ? 1 : 0);
			const uint32 Index = MetaIndex(Prop.Meta);
			uint32 Info = ((Hint << 2) & 0x000c) | ((Index << 20) & 0xff00000);
			if (Prop.Kind != EKind::Compound)
			{
				const bool bScalar = Prop.Kind == EKind::Scalar;
				// Array com um valor so e "parecido com escalar" (tipo 3)
				Info |= bScalar ? 1u : 2u;
				Info |= (bScalar || Prop.NumPoints == 1) ? 1u : 0u;
				Info |= (uint32(Prop.Pod) << 4) & 0x00f0;
				// Primeira e ultima amostra que mudam: 0, a mesma em todo o tempo
				Info |= 0x0800;
				Info |= (uint32(Prop.Extent) << 12) & 0xff000;
				// Mesmo tamanho em todas as amostras; nos arrays a biblioteca compara pontos com valores
				if (bScalar || Prop.Extent == 1 || Prop.NumPoints == 0)
				{
					Info |= 0x0400;
				}
				Put32(Headers, Info);
				PutWithHint(Headers, NumSamples, Hint);
			}
			else
			{
				Put32(Headers, Info);
			}
			PutWithHint(Headers, NameSize, Hint);
			PutBytes(Headers, Prop.Name);
			if (Index == 255)
			{
				PutWithHint(Headers, MetaSize, Hint);
				PutBytes(Headers, Prop.Meta);
			}
		}

		/** Grava a propriedade (filhos antes) e devolve o grupo dela. OutHash: o hash que o pai junta. */
		uint64 WriteProperty(const FProp& Prop, uint64 (&OutHash)[2])
		{
			if (Prop.Kind == EKind::Compound)
			{
				TArray<uint64> Refs;
				TArray<uint8> ChildHashes;
				const uint64 Ref = WriteCompoundBody(Prop.Children, Refs, ChildHashes);
				TArray<uint8> Bytes = ChildHashes;
				Bytes.Append(HeaderHashBytes(Prop));
				Spooky(Bytes, OutHash);
				return Ref;
			}
			uint64 Digest[2] = { 0, 0 };
			MurmurHash3(Prop.Sample.GetData(), uint64(Prop.Sample.Num()), Digest[0], Digest[1]);
			TArray<uint64> Refs;
			Refs.Add(AddSample(Digest, Prop.Sample));
			uint64 Mixed[2] = { Digest[0], Digest[1] };
			if (Prop.Kind == EKind::Array)
			{
				// Arrays de uma dimensao: as dimensoes saem do tamanho dos dados (dado vazio)
				Refs.Add(EmptyData);
				TArray<uint8> Dims;
				Put64(Dims, Prop.NumPoints);
				Put64(Dims, Digest[0]);
				Put64(Dims, Digest[1]);
				Spooky(Dims, Mixed);
			}
			TArray<uint8> Bytes = HeaderHashBytes(Prop);
			Put64(Bytes, Mixed[0]);
			Put64(Bytes, Mixed[1]);
			Spooky(Bytes, OutHash);
			return AddGroup(Refs);
		}

		/** Os filhos de um compound e os cabecalhos deles, por ultimo. */
		uint64 WriteCompoundBody(const TArray<FProp>& Children, TArray<uint64>& Refs, TArray<uint8>& ChildHashes)
		{
			for (const FProp& Child : Children)
			{
				uint64 Hash[2];
				Refs.Add(WriteProperty(Child, Hash));
				Put64(ChildHashes, Hash[0]);
				Put64(ChildHashes, Hash[1]);
			}
			TArray<uint8> Headers;
			for (const FProp& Child : Children)
			{
				AppendPropertyInfo(Headers, Child);
			}
			if (Headers.Num() > 0)
			{
				Refs.Add(AddData(Headers));
			}
			return AddGroup(Refs);
		}

		void AppendObjectHeader(TArray<uint8>& Headers, const TArray<uint8>& Name, const TArray<uint8>& Meta)
		{
			Put32(Headers, uint32(Name.Num()));
			PutBytes(Headers, Name);
			const uint32 Index = MetaIndex(Meta);
			Put8(Headers, uint8(Index));
			if (Index == 255)
			{
				Put32(Headers, uint32(Meta.Num()));
				PutBytes(Headers, Meta);
			}
		}
	};

	// ---- Curvas ----

	void PutInts(FProp& Prop, const TArray<int32>& Values, EType Type)
	{
		for (int32 Value : Values)
		{
			if (Type == EType::Int16)
			{
				const uint16 Bits = uint16(int16(Value));
				Put8(Prop.Sample, uint8(Bits));
				Put8(Prop.Sample, uint8(Bits >> 8));
			}
			else
			{
				Put32(Prop.Sample, uint32(Value));
			}
		}
	}

	bool AttributeProp(const FAttribute& Attribute, int32 NumCurves, int32 NumPoints, FProp& Out, FString& OutError)
	{
		if (Attribute.Name.IsEmpty() || Attribute.Name.Contains(TEXT("/")))
		{
			OutError = FString::Printf(TEXT("atributo com nome invalido: '%s'"), *Attribute.Name);
			return false;
		}
		const bool bFloat = Attribute.Type == EType::Float32;
		if (Attribute.Extent < 1 || Attribute.Extent > 4 || (!bFloat && Attribute.Extent != 1))
		{
			OutError = FString::Printf(TEXT("atributo %s: componentes por valor fora da faixa"), *Attribute.Name);
			return false;
		}
		const int32 NumValues = bFloat ? Attribute.Floats.Num() : Attribute.Ints.Num();
		if (NumValues % Attribute.Extent != 0)
		{
			OutError = FString::Printf(TEXT("atributo %s: valores que nao fecham os componentes"), *Attribute.Name);
			return false;
		}
		const int32 Expected = Attribute.Scope == EScope::Constant ? 1 : (Attribute.Scope == EScope::Uniform ? NumCurves : NumPoints);
		if (Attribute.NumElements() != Expected)
		{
			OutError = FString::Printf(TEXT("atributo %s: %d valores, esperava %d"), *Attribute.Name, Attribute.NumElements(), Expected);
			return false;
		}
		const TCHAR* Scope = Attribute.Scope == EScope::Constant ? TEXT("con") : (Attribute.Scope == EScope::Uniform ? TEXT("uni") : TEXT("vtx"));
		const TCHAR* PodName = bFloat ? TEXT("float32_t") : (Attribute.Type == EType::Int16 ? TEXT("int16_t") : TEXT("int32_t"));
		const uint8 Pod = bFloat ? PodFloat32 : (Attribute.Type == EType::Int16 ? PodInt16 : PodInt32);
		Out = Leaf(EKind::Array, Attribute.Name, GeomParamMetaData(Scope, Attribute.Interpretation, Attribute.Extent, PodName), Pod,
			uint8(Attribute.Extent));
		Out.NumPoints = uint64(Expected);
		if (bFloat)
		{
			Out.Sample.Reserve(Attribute.Floats.Num() * 4);
			for (float Value : Attribute.Floats)
			{
				PutFloat(Out.Sample, Value);
			}
		}
		else
		{
			PutInts(Out, Attribute.Ints, Attribute.Type);
		}
		return true;
	}

	/** As propriedades de um objeto de curvas: .geom com P, .selfBnds, nVertices, curveBasisAndType, width e os extras. */
	bool CurvesProps(const FCurves& Curves, FProp& OutGeom, FString& OutError)
	{
		int64 Total = 0;
		for (int32 Count : Curves.NumVertices)
		{
			if (Count < 1)
			{
				OutError = FString::Printf(TEXT("objeto %s: curva sem pontos"), *Curves.Name);
				return false;
			}
			Total += Count;
		}
		if (Curves.NumVertices.Num() == 0 || Total != Curves.Positions.Num())
		{
			OutError = FString::Printf(TEXT("objeto %s: %d pontos, mas as curvas somam %lld"), *Curves.Name, Curves.Positions.Num(), Total);
			return false;
		}
		if (Curves.Widths.Num() != 0 && Curves.Widths.Num() != Curves.Positions.Num())
		{
			OutError = FString::Printf(TEXT("objeto %s: %d larguras para %d pontos"), *Curves.Name, Curves.Widths.Num(), Curves.Positions.Num());
			return false;
		}
		const FString Schema = MetaData({ { TEXT("schema"), TEXT("AbcGeom_Curve_v2") }, { TEXT("schemaBaseType"), TEXT("AbcGeom_GeomBase_v1") } });
		OutGeom = Compound(TEXT(".geom"), Schema);

		FProp Positions = Leaf(EKind::Array, TEXT("P"), MetaData({ { TEXT("geoScope"), TEXT("vtx") }, { TEXT("interpretation"), TEXT("point") } }),
			PodFloat32, 3);
		Positions.NumPoints = uint64(Curves.Positions.Num());
		Positions.Sample.Reserve(Curves.Positions.Num() * 12);
		const double Big = TNumericLimits<double>::Max();
		double Min[3] = { Big, Big, Big };
		double Max[3] = { -Big, -Big, -Big };
		for (const FVector3f& Point : Curves.Positions)
		{
			const float Coords[3] = { Point.X, Point.Y, Point.Z };
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				PutFloat(Positions.Sample, Coords[Axis]);
				Min[Axis] = FMath::Min(Min[Axis], double(Coords[Axis]));
				Max[Axis] = FMath::Max(Max[Axis], double(Coords[Axis]));
			}
		}
		OutGeom.Children.Add(MoveTemp(Positions));

		FProp Bounds = Leaf(EKind::Scalar, TEXT(".selfBnds"), MetaData({ { TEXT("interpretation"), TEXT("box") } }), PodFloat64, 6);
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			PutDouble(Bounds.Sample, Min[Axis]);
		}
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			PutDouble(Bounds.Sample, Max[Axis]);
		}
		OutGeom.Children.Add(MoveTemp(Bounds));

		FProp Counts = Leaf(EKind::Array, TEXT("nVertices"), FString(), PodInt32, 1);
		Counts.NumPoints = uint64(Curves.NumVertices.Num());
		PutInts(Counts, Curves.NumVertices, EType::Int32);
		OutGeom.Children.Add(MoveTemp(Counts));

		FProp BasisAndType = Leaf(EKind::Scalar, TEXT("curveBasisAndType"), FString(), PodUint8, 4);
		Put8(BasisAndType.Sample, CurveLinear);
		Put8(BasisAndType.Sample, CurveNonPeriodic);
		Put8(BasisAndType.Sample, CurveNoBasis);
		Put8(BasisAndType.Sample, CurveNoBasis);
		OutGeom.Children.Add(MoveTemp(BasisAndType));

		if (Curves.Widths.Num() > 0)
		{
			FProp Widths = Leaf(EKind::Array, TEXT("width"), GeomParamMetaData(TEXT("vtx"), FString(), 1, TEXT("float32_t")), PodFloat32, 1);
			Widths.NumPoints = uint64(Curves.Widths.Num());
			Widths.Sample.Reserve(Curves.Widths.Num() * 4);
			for (float Width : Curves.Widths)
			{
				PutFloat(Widths.Sample, Width);
			}
			OutGeom.Children.Add(MoveTemp(Widths));
		}

		if (Curves.Attributes.Num() > 0)
		{
			FProp Extras = Compound(TEXT(".arbGeomParams"), FString());
			for (const FAttribute& Attribute : Curves.Attributes)
			{
				for (const FProp& Existing : Extras.Children)
				{
					if (Existing.Name == Utf8(Attribute.Name))
					{
						OutError = FString::Printf(TEXT("objeto %s: atributo %s repetido"), *Curves.Name, *Attribute.Name);
						return false;
					}
				}
				FProp Prop;
				if (!AttributeProp(Attribute, Curves.NumVertices.Num(), Curves.Positions.Num(), Prop, OutError))
				{
					OutError = FString::Printf(TEXT("objeto %s: %s"), *Curves.Name, *OutError);
					return false;
				}
				Extras.Children.Add(MoveTemp(Prop));
			}
			OutGeom.Children.Add(MoveTemp(Extras));
		}
		return true;
	}
}

int32 DeverauxGroomAlembic::FAttribute::NumElements() const
{
	const int32 NumValues = Type == EType::Float32 ? Floats.Num() : Ints.Num();
	return Extent > 0 ? NumValues / Extent : 0;
}

void DeverauxGroomAlembic::MurmurHash3(const uint8* Data, uint64 Length, uint64& OutHash1, uint64& OutHash2)
{
	using namespace DeverauxGroomAlembicPrivate;
	constexpr uint64 C1 = 0x87c37b91114253d5ull;
	constexpr uint64 C2 = 0x4cf5ad432745937full;
	uint64 H1 = 0;
	uint64 H2 = 0;
	const uint64 NumBlocks = Length / 16;
	for (uint64 Block = 0; Block < NumBlocks; ++Block)
	{
		uint64 K1 = Read64(Data + Block * 16);
		uint64 K2 = Read64(Data + Block * 16 + 8);
		K1 *= C1; K1 = Rot64(K1, 31); K1 *= C2; H1 ^= K1;
		H1 = Rot64(H1, 27); H1 += H2; H1 = H1 * 5 + 0x52dce729;
		K2 *= C2; K2 = Rot64(K2, 33); K2 *= C1; H2 ^= K2;
		H2 = Rot64(H2, 31); H2 += H1; H2 = H2 * 5 + 0x38495ab5;
	}
	const uint8* Tail = Length > 0 ? Data + NumBlocks * 16 : Data;
	uint64 K1 = 0;
	uint64 K2 = 0;
	switch (Length & 15)
	{
	case 15: K2 ^= uint64(Tail[14]) << 48; [[fallthrough]];
	case 14: K2 ^= uint64(Tail[13]) << 40; [[fallthrough]];
	case 13: K2 ^= uint64(Tail[12]) << 32; [[fallthrough]];
	case 12: K2 ^= uint64(Tail[11]) << 24; [[fallthrough]];
	case 11: K2 ^= uint64(Tail[10]) << 16; [[fallthrough]];
	case 10: K2 ^= uint64(Tail[9]) << 8; [[fallthrough]];
	case 9: K2 ^= uint64(Tail[8]);
		K2 *= C2; K2 = Rot64(K2, 33); K2 *= C1; H2 ^= K2;
		[[fallthrough]];
	case 8: K1 ^= uint64(Tail[7]) << 56; [[fallthrough]];
	case 7: K1 ^= uint64(Tail[6]) << 48; [[fallthrough]];
	case 6: K1 ^= uint64(Tail[5]) << 40; [[fallthrough]];
	case 5: K1 ^= uint64(Tail[4]) << 32; [[fallthrough]];
	case 4: K1 ^= uint64(Tail[3]) << 24; [[fallthrough]];
	case 3: K1 ^= uint64(Tail[2]) << 16; [[fallthrough]];
	case 2: K1 ^= uint64(Tail[1]) << 8; [[fallthrough]];
	case 1: K1 ^= uint64(Tail[0]);
		K1 *= C1; K1 = Rot64(K1, 31); K1 *= C2; H1 ^= K1;
		break;
	default:
		break;
	}
	H1 ^= Length;
	H2 ^= Length;
	H1 += H2;
	H2 += H1;
	auto Finalize = [](uint64 Value)
	{
		Value ^= Value >> 33;
		Value *= 0xff51afd7ed558ccdull;
		Value ^= Value >> 33;
		Value *= 0xc4ceb9fe1a85ec53ull;
		Value ^= Value >> 33;
		return Value;
	};
	H1 = Finalize(H1);
	H2 = Finalize(H2);
	H1 += H2;
	H2 += H1;
	OutHash1 = H1;
	OutHash2 = H2;
}

void DeverauxGroomAlembic::SpookyHash(const uint8* Data, uint64 Length, uint64& OutHash1, uint64& OutHash2)
{
	using namespace DeverauxGroomAlembicPrivate;
	OutHash1 = 0;
	OutHash2 = 0;
	if (Length < SpookyBuffer)
	{
		static const uint8 Nothing[1] = { 0 };
		SpookyShort(Length > 0 ? Data : Nothing, Length, OutHash1, OutHash2);
	}
	else
	{
		SpookyLong(Data, Length, OutHash1, OutHash2);
	}
}

bool DeverauxGroomAlembic::WriteArchive(const TArray<FCurves>& Objects, const FArchiveInfo& Info, TArray<uint8>& OutBytes, FString& OutError)
{
	using namespace DeverauxGroomAlembicPrivate;
	OutBytes.Reset();
	OutError.Reset();
	if (Objects.Num() == 0)
	{
		OutError = TEXT("nenhum objeto para gravar");
		return false;
	}
	for (int32 Index = 0; Index < Objects.Num(); ++Index)
	{
		const FString& Name = Objects[Index].Name;
		if (Name.IsEmpty() || Name.Contains(TEXT("/")))
		{
			OutError = FString::Printf(TEXT("objeto com nome invalido: '%s'"), *Name);
			return false;
		}
		for (int32 Other = 0; Other < Index; ++Other)
		{
			if (Objects[Other].Name == Name)
			{
				OutError = FString::Printf(TEXT("dois objetos com o nome %s"), *Name);
				return false;
			}
		}
	}

	FWriter Writer;
	TArray<uint8> Version;
	Put32(Version, uint32(OgawaFileVersion));
	TArray<uint8> Library;
	Put32(Library, uint32(LibraryVersion));
	TArray<uint64> Root;
	Root.Add(Writer.AddData(Version));
	Root.Add(Writer.AddData(Library));

	// Objeto de cima: sem propriedades, com os objetos de curvas como filhos
	const FString CurvesMeta = MetaData({ { TEXT("schema"), TEXT("AbcGeom_Curve_v2") }, { TEXT("schemaBaseType"), TEXT("AbcGeom_GeomBase_v1") },
		{ TEXT("schemaObjTitle"), TEXT("AbcGeom_Curve_v2:.geom") } });
	TArray<uint64> TopRefs;
	// O compound de cima, sem propriedades: grupo vazio (referencia 0, como a biblioteca grava)
	TopRefs.Add(Writer.AddGroup(TArray<uint64>()));
	TArray<uint8> ChildHashes;
	for (const FCurves& Curves : Objects)
	{
		FProp Geom;
		if (!CurvesProps(Curves, Geom, OutError))
		{
			return false;
		}
		TArray<FProp> TopProps;
		TopProps.Add(MoveTemp(Geom));
		// Objeto: o compound das propriedades, nenhum filho e os 32 bytes de hash por ultimo
		TArray<uint64> CompoundRefs;
		TArray<uint8> PropHashes;
		TArray<uint64> ObjectRefs;
		ObjectRefs.Add(Writer.WriteCompoundBody(TopProps, CompoundRefs, PropHashes));
		uint64 DataHash[2];
		Spooky(PropHashes, DataHash);
		// Sem filhos: os 32 bytes de hash (dados, filhos zerados)
		TArray<uint8> Headers;
		Put64(Headers, DataHash[0]);
		Put64(Headers, DataHash[1]);
		Put64(Headers, 0);
		Put64(Headers, 0);
		ObjectRefs.Add(Writer.AddData(Headers));
		TopRefs.Add(Writer.AddGroup(ObjectRefs));
		TArray<uint8> ObjectHashBytes;
		Put64(ObjectHashBytes, DataHash[0]);
		Put64(ObjectHashBytes, DataHash[1]);
		ObjectHashBytes.Append(Utf8(CurvesMeta));
		ObjectHashBytes.Append(Utf8(Curves.Name));
		uint64 ObjectHash[2];
		Spooky(ObjectHashBytes, ObjectHash);
		Put64(ChildHashes, ObjectHash[0]);
		Put64(ChildHashes, ObjectHash[1]);
	}
	TArray<uint8> TopHeaders;
	for (const FCurves& Curves : Objects)
	{
		Writer.AppendObjectHeader(TopHeaders, Utf8(Curves.Name), Utf8(CurvesMeta));
	}
	uint64 TopDataHash[2];
	Spooky(TArray<uint8>(), TopDataHash);
	uint64 TopChildHash[2];
	Spooky(ChildHashes, TopChildHash);
	Put64(TopHeaders, TopDataHash[0]);
	Put64(TopHeaders, TopDataHash[1]);
	Put64(TopHeaders, TopChildHash[0]);
	Put64(TopHeaders, TopChildHash[1]);
	TopRefs.Add(Writer.AddData(TopHeaders));
	Root.Add(Writer.AddGroup(TopRefs));

	const FString ArchiveMeta = MetaData({
		{ TEXT("_ai_AlembicVersion"), TEXT("Deveraux Groom Export 1.0 (formato Alembic 1.8)") },
		{ TEXT("_ai_Application"), CleanValue(Info.Application) },
		{ TEXT("_ai_Description"), CleanValue(Info.Description) } });
	Root.Add(Writer.AddData(Utf8(ArchiveMeta)));

	// Uma amostragem de tempo, a padrao: no maximo 1 amostra, ciclo de 1 s, uma amostra em 0
	TArray<uint8> Times;
	Put32(Times, 1);
	PutDouble(Times, 1.0);
	Put32(Times, 1);
	PutDouble(Times, 0.0);
	Root.Add(Writer.AddData(Times));

	TArray<uint8> Table;
	for (const TArray<uint8>& Meta : Writer.MetaTable)
	{
		Put8(Table, uint8(Meta.Num()));
		PutBytes(Table, Meta);
	}
	Root.Add(Writer.AddData(Table));

	const uint64 RootRef = Writer.AddGroup(Root);
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Writer.Out[8 + Index] = uint8(RootRef >> (8 * Index));
	}
	// Arquivo fechado: a biblioteca so confia no conteudo com 0xff aqui
	Writer.Out[5] = 0xff;
	OutBytes = MoveTemp(Writer.Out);
	return true;
}
