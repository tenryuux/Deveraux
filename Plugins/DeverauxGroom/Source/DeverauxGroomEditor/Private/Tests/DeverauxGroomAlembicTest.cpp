// Projeto Deveraux - testes do exportador de groom para Alembic (plugin DeverauxGroom)
// Para rodar no editor: Window > Output Log, digite no campo Cmd: Automation RunTests Deveraux.Groom
//
// O arquivo de exemplo daqui foi aberto no Blender 5.2 (biblioteca oficial do Alembic) e lido por um leitor
// independente que confere todos os hashes. O teste confere que o editor grava exatamente os mesmos bytes.

#include "DeverauxGroomAlembic.h"
#include "DeverauxGroomExport.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeverauxGroomAlembicTest, "Deveraux.Groom.Alembic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace DeverauxGroomAlembicTestPrivate
{
	using namespace DeverauxGroomAlembic;

	/** Hash do arquivo de exemplo inteiro (MurmurHash3), o mesmo gravado e conferido fora da engine. */
	constexpr uint64 SampleHash1 = 0xcda281f828a16d8bull;
	constexpr uint64 SampleHash2 = 0xbe890efce391adcaull;
	constexpr int32 SampleSize = 3072;

	FAttribute Ints(const TCHAR* Name, EScope Scope, EType Type, TArray<int32> Values)
	{
		FAttribute Attribute;
		Attribute.Name = Name;
		Attribute.Scope = Scope;
		Attribute.Type = Type;
		Attribute.Ints = MoveTemp(Values);
		return Attribute;
	}

	FAttribute Floats(const TCHAR* Name, EScope Scope, int32 Extent, const TCHAR* Interpretation, TArray<float> Values)
	{
		FAttribute Attribute;
		Attribute.Name = Name;
		Attribute.Scope = Scope;
		Attribute.Type = EType::Float32;
		Attribute.Extent = Extent;
		Attribute.Interpretation = Interpretation;
		Attribute.Floats = MoveTemp(Values);
		return Attribute;
	}

	/** Dois objetos: fios com largura, UV da raiz e cor por ponto; e uma guia. */
	TArray<FCurves> Sample()
	{
		TArray<FCurves> Objects;
		FCurves& Strands = Objects.AddDefaulted_GetRef();
		Strands.Name = TEXT("Cabelo_fios");
		Strands.NumVertices = { 3, 2 };
		Strands.Positions = { FVector3f(0.0f, 1.5f, 0.0f), FVector3f(0.0f, 1.625f, 0.0625f), FVector3f(0.0f, 1.75f, 0.25f),
			FVector3f(0.125f, 1.5f, 0.0f), FVector3f(0.125f, 1.625f, -0.0625f) };
		Strands.Widths = { 0.0008f, 0.0006f, 0.0004f, 0.0008f, 0.0004f };
		Strands.Attributes.Add(Ints(TEXT("groom_version_major"), EScope::Constant, EType::Int16, { 1 }));
		Strands.Attributes.Add(Ints(TEXT("groom_version_minor"), EScope::Constant, EType::Int16, { 5 }));
		Strands.Attributes.Add(Ints(TEXT("groom_group_id"), EScope::Uniform, EType::Int32, { 2, 2 }));
		Strands.Attributes.Add(Floats(TEXT("groom_root_uv"), EScope::Uniform, 2, TEXT("vector"), { 0.25f, 0.75f, 0.5f, 0.125f }));
		Strands.Attributes.Add(Floats(TEXT("groom_color"), EScope::Vertex, 3, TEXT("rgb"),
			{ 0.5f, 0.25f, 0.125f, 0.5f, 0.25f, 0.125f, 0.5f, 0.25f, 0.125f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f }));
		FCurves& Guides = Objects.AddDefaulted_GetRef();
		Guides.Name = TEXT("Cabelo_guias");
		Guides.NumVertices = { 2 };
		Guides.Positions = { FVector3f(0.0625f, 1.5f, 0.0f), FVector3f(0.0625f, 1.75f, 0.125f) };
		Guides.Widths = { 0.0008f, 0.0008f };
		Guides.Attributes.Add(Ints(TEXT("groom_version_major"), EScope::Constant, EType::Int16, { 1 }));
		Guides.Attributes.Add(Ints(TEXT("groom_version_minor"), EScope::Constant, EType::Int16, { 5 }));
		Guides.Attributes.Add(Ints(TEXT("groom_group_id"), EScope::Uniform, EType::Int32, { 2 }));
		Guides.Attributes.Add(Ints(TEXT("groom_guide"), EScope::Uniform, EType::Int32, { 1 }));
		return Objects;
	}

	FArchiveInfo SampleInfo()
	{
		FArchiveInfo Info;
		Info.Application = TEXT("Unreal Engine (Deveraux Groom Export)");
		Info.Description = TEXT("teste");
		return Info;
	}

	bool Hash(const TArray<uint8>& Bytes, uint64 Expected1, uint64 Expected2, bool bSpooky)
	{
		uint64 Hash1 = 0;
		uint64 Hash2 = 0;
		if (bSpooky)
		{
			SpookyHash(Bytes.GetData(), uint64(Bytes.Num()), Hash1, Hash2);
		}
		else
		{
			MurmurHash3(Bytes.GetData(), uint64(Bytes.Num()), Hash1, Hash2);
		}
		return Hash1 == Expected1 && Hash2 == Expected2;
	}

	bool Fails(const TArray<FCurves>& Objects)
	{
		TArray<uint8> Bytes;
		FString Error;
		return !WriteArchive(Objects, SampleInfo(), Bytes, Error) && !Error.IsEmpty() && Bytes.Num() == 0;
	}
}

bool FDeverauxGroomAlembicTest::RunTest(const FString& Parameters)
{
	using namespace DeverauxGroomAlembic;
	using namespace DeverauxGroomAlembicTestPrivate;

	// Hashes: os valores das bibliotecas de referencia (mmh3 e spookyhash), nos dois caminhos do Spooky (curto e longo)
	{
		const TArray<uint8> Empty;
		const TArray<uint8> Abc = { 'a', 'b', 'c' };
		TArray<uint8> Long;
		for (int32 Index = 0; Index < 256; ++Index)
		{
			Long.Add(uint8(Index));
		}
		TestTrue(TEXT("Murmur vazio"), Hash(Empty, 0, 0, false));
		TestTrue(TEXT("Murmur abc"), Hash(Abc, 0xb4963f3f3fad7867ull, 0x3ba2744126ca2d52ull, false));
		TestTrue(TEXT("Murmur 256 bytes"), Hash(Long, 0x1c99c313dc6f12b9ull, 0x70d6077fab34cc1eull, false));
		TestTrue(TEXT("Spooky vazio"), Hash(Empty, 0x232706fc6bf50919ull, 0x8b72ee65b4e851c7ull, true));
		TestTrue(TEXT("Spooky abc"), Hash(Abc, 0x8aab15f77537c967ull, 0xc61367f8ca7811b0ull, true));
		TestTrue(TEXT("Spooky 256 bytes"), Hash(Long, 0x203b9d8b51c50323ull, 0xc896a0657599ff50ull, true));
	}

	// O arquivo de exemplo, byte a byte como o que o Blender abriu
	{
		TArray<uint8> Bytes;
		FString Error;
		TestTrue(TEXT("Grava o exemplo"), WriteArchive(Sample(), SampleInfo(), Bytes, Error));
		TestEqual(TEXT("Tamanho do exemplo"), Bytes.Num(), SampleSize);
		TestTrue(TEXT("Os mesmos bytes que foram conferidos no Blender"), Hash(Bytes, SampleHash1, SampleHash2, false));
		TestTrue(TEXT("Comeca com a assinatura Ogawa, fechado"), Bytes.Num() > 16 && Bytes[0] == 'O' && Bytes[4] == 'a' && Bytes[5] == 0xff);
	}

	// Eixos e escala: Y e Z trocam; no Blender o arquivo vai em metros
	{
		const FVector3f Point(10.0f, 20.0f, 30.0f);
		const FVector3f Blender = DeverauxGroomExport::ToFile(Point, DeverauxGroomExport::ETarget::Blender);
		const FVector3f Unreal = DeverauxGroomExport::ToFile(Point, DeverauxGroomExport::ETarget::Unreal);
		TestTrue(TEXT("Blender: (x, z, y) em metros"), FMath::IsNearlyEqual(Blender.X, 0.1f, 1.0e-6f)
			&& FMath::IsNearlyEqual(Blender.Y, 0.3f, 1.0e-6f) && FMath::IsNearlyEqual(Blender.Z, 0.2f, 1.0e-6f));
		TestTrue(TEXT("Unreal: (x, z, y) em cm"), Unreal.X == 10.0f && Unreal.Y == 30.0f && Unreal.Z == 20.0f);
		TestTrue(TEXT("Blender sem guias, Unreal com"), !DeverauxGroomExport::DefaultOptions(DeverauxGroomExport::ETarget::Blender).bGuides
			&& DeverauxGroomExport::DefaultOptions(DeverauxGroomExport::ETarget::Unreal).bGuides);
	}

	// O que nao fecha nao vira arquivo
	{
		TestTrue(TEXT("Sem objetos"), Fails(TArray<FCurves>()));
		TArray<FCurves> Repeated = Sample();
		Repeated[1].Name = Repeated[0].Name;
		TestTrue(TEXT("Nome repetido"), Fails(Repeated));
		TArray<FCurves> Slash = Sample();
		Slash[0].Name = TEXT("a/b");
		TestTrue(TEXT("Nome com barra"), Fails(Slash));
		TArray<FCurves> Counts = Sample();
		Counts[0].NumVertices[0] = 4;
		TestTrue(TEXT("Contagem que nao bate com os pontos"), Fails(Counts));
		TArray<FCurves> Widths = Sample();
		Widths[0].Widths.SetNum(2);
		TestTrue(TEXT("Larguras que nao batem com os pontos"), Fails(Widths));
		TArray<FCurves> Attribute = Sample();
		Attribute[0].Attributes[2].Ints.Add(2);
		TestTrue(TEXT("Atributo por curva com valores a mais"), Fails(Attribute));
		TArray<FCurves> Empty = Sample();
		Empty[1].NumVertices[0] = 0;
		Empty[1].Positions.Reset();
		Empty[1].Widths.Reset();
		TestTrue(TEXT("Curva sem pontos"), Fails(Empty));
	}
	return true;
}

#endif
