// Projeto Deveraux - exporta um groom (cabelo) da Unreal para Alembic

#include "DeverauxGroomExport.h"

#include "GroomAsset.h"
#include "HairAttributes.h"
#include "HairDescription.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace DeverauxGroomExportPrivate
{
	using namespace DeverauxGroomAlembic;
	using namespace DeverauxGroomExport;

	/** Versao da especificacao de Alembic para grooms que o arquivo segue (1.5, a da documentacao da 5.8). */
	constexpr int32 SpecMajor = 1;
	constexpr int32 SpecMinor = 5;

	/** Os fios de um grupo que vao para um mesmo objeto (os fios de um lado, as guias do outro). */
	struct FBucket
	{
		int32 GroupID = 0;
		bool bGuide = false;
		FString GroupName;
		TArray<int32> Strands;
	};

	/** Nome que o Alembic e o Blender aceitam: sem barra e sem os separadores de metadado. */
	FString CleanName(const FString& Name)
	{
		return Name.Replace(TEXT("/"), TEXT("_")).Replace(TEXT(";"), TEXT("_")).Replace(TEXT("="), TEXT("_"));
	}

	FAttribute IntAttribute(const TCHAR* Name, EScope Scope, EType Type, TArray<int32>&& Values)
	{
		FAttribute Attribute;
		Attribute.Name = Name;
		Attribute.Scope = Scope;
		Attribute.Type = Type;
		Attribute.Ints = MoveTemp(Values);
		return Attribute;
	}

	FAttribute FloatAttribute(const TCHAR* Name, EScope Scope, int32 Extent, const TCHAR* Interpretation, TArray<float>&& Values)
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
}

DeverauxGroomExport::FOptions DeverauxGroomExport::DefaultOptions(ETarget Target)
{
	FOptions Options;
	Options.Target = Target;
	Options.bGuides = Target == ETarget::Unreal;
	return Options;
}

float DeverauxGroomExport::FileScale(ETarget Target)
{
	return Target == ETarget::Blender ? 0.01f : 1.0f;
}

FVector3f DeverauxGroomExport::ToFile(const FVector3f& Point, ETarget Target)
{
	const float Scale = FileScale(Target);
	return FVector3f(Point.X * Scale, Point.Z * Scale, Point.Y * Scale);
}

bool DeverauxGroomExport::BuildCurves(const FHairDescription& Description, const FOptions& Options,
	TArray<DeverauxGroomAlembic::FCurves>& OutObjects, FStats& OutStats, FString& OutError)
{
	using namespace DeverauxGroomExportPrivate;
	OutObjects.Reset();
	OutStats = FStats();
	OutError.Reset();

	const int32 NumStrands = Description.GetNumStrands();
	const int32 NumVertices = Description.GetNumVertices();
	if (NumStrands <= 0 || NumVertices <= 0)
	{
		OutError = TEXT("a descricao do groom nao tem fios");
		return false;
	}

	// Cada atributo so vale se existir com esse tipo; os que faltam ficam fora do arquivo
	const auto& StrandSet = Description.StrandAttributes();
	const auto& VertexSet = Description.VertexAttributes();
	const auto Counts = StrandSet.GetAttributesRef<int>(HairAttribute::Strand::VertexCount);
	const auto Positions = VertexSet.GetAttributesRef<FVector3f>(HairAttribute::Vertex::Position);
	if (!Counts.IsValid() || !Positions.IsValid())
	{
		OutError = TEXT("a descricao do groom nao tem a contagem de pontos ou as posicoes");
		return false;
	}
	const auto VertexWidths = VertexSet.GetAttributesRef<float>(HairAttribute::Vertex::Width);
	const auto StrandWidths = StrandSet.GetAttributesRef<float>(HairAttribute::Strand::Width);
	const auto RootUVs = StrandSet.GetAttributesRef<FVector2f>(HairAttribute::Strand::RootUV);
	const auto GroupIDs = StrandSet.GetAttributesRef<int>(HairAttribute::Strand::GroupID);
	const auto GroupNames = StrandSet.GetAttributesRef<FName>(HairAttribute::Strand::GroupName);
	const auto Guides = StrandSet.GetAttributesRef<int>(HairAttribute::Strand::Guide);
	const auto StrandIDs = StrandSet.GetAttributesRef<int>(HairAttribute::Strand::ID);
	const auto ClumpIDs = StrandSet.GetAttributesRef<int>(HairAttribute::Strand::ClumpID);
	const auto Colors = VertexSet.GetAttributesRef<FVector3f>(HairAttribute::Vertex::Color);
	const auto Roughness = VertexSet.GetAttributesRef<float>(HairAttribute::Vertex::Roughness);
	const auto AOs = VertexSet.GetAttributesRef<float>(HairAttribute::Vertex::AO);

	// Onde cada fio comeca nos pontos
	TArray<int32> Offsets;
	Offsets.SetNum(NumStrands);
	int64 Total = 0;
	for (int32 Strand = 0; Strand < NumStrands; ++Strand)
	{
		const int32 Count = Counts[FStrandID(Strand)];
		if (Count < 0)
		{
			OutError = FString::Printf(TEXT("o fio %d tem contagem de pontos negativa"), Strand);
			return false;
		}
		Offsets[Strand] = int32(Total);
		Total += Count;
	}
	if (Total != NumVertices)
	{
		OutError = FString::Printf(TEXT("os fios somam %lld pontos, mas a descricao tem %d"), Total, NumVertices);
		return false;
	}

	// Largura por ponto se houver alguma maior que zero; senao a de cada fio; senao o arquivo vai sem largura
	bool bVertexWidths = false;
	if (VertexWidths.IsValid())
	{
		for (int32 Vertex = 0; Vertex < NumVertices && !bVertexWidths; ++Vertex)
		{
			bVertexWidths = VertexWidths[FVertexID(Vertex)] > 0.0f;
		}
	}
	bool bStrandWidths = false;
	if (!bVertexWidths && StrandWidths.IsValid())
	{
		for (int32 Strand = 0; Strand < NumStrands && !bStrandWidths; ++Strand)
		{
			bStrandWidths = StrandWidths[FStrandID(Strand)] > 0.0f;
		}
	}
	OutStats.bWidths = bVertexWidths || bStrandWidths;
	OutStats.bRootUV = RootUVs.IsValid();

	// Um balde por grupo e tipo (fio ou guia), na ordem do grupo
	TArray<FBucket> Buckets;
	for (int32 Strand = 0; Strand < NumStrands; ++Strand)
	{
		if (Counts[FStrandID(Strand)] == 0)
		{
			continue;
		}
		const bool bGuide = Guides.IsValid() && Guides[FStrandID(Strand)] != 0;
		if (bGuide && !Options.bGuides)
		{
			++OutStats.SkippedGuides;
			continue;
		}
		const int32 GroupID = GroupIDs.IsValid() ? GroupIDs[FStrandID(Strand)] : 0;
		FBucket* Bucket = Buckets.FindByPredicate([GroupID, bGuide](const FBucket& Item)
		{
			return Item.GroupID == GroupID && Item.bGuide == bGuide;
		});
		if (!Bucket)
		{
			FBucket& Added = Buckets.AddDefaulted_GetRef();
			Added.GroupID = GroupID;
			Added.bGuide = bGuide;
			if (GroupNames.IsValid())
			{
				const FName GroupName = GroupNames[FStrandID(Strand)];
				if (!GroupName.IsNone())
				{
					Added.GroupName = GroupName.ToString();
				}
			}
			Bucket = &Added;
		}
		Bucket->Strands.Add(Strand);
	}
	if (Buckets.Num() == 0)
	{
		OutError = OutStats.SkippedGuides > 0 ? TEXT("o groom so tem guias: exporte com as guias") : TEXT("o groom nao tem fios com pontos");
		return false;
	}
	Buckets.Sort([](const FBucket& A, const FBucket& B)
	{
		return A.GroupID != B.GroupID ? A.GroupID < B.GroupID : (!A.bGuide && B.bGuide);
	});

	TArray<int32> SeenGroups;
	for (const FBucket& Bucket : Buckets)
	{
		SeenGroups.AddUnique(Bucket.GroupID);

		FCurves& Curves = OutObjects.AddDefaulted_GetRef();
		const FString Base = Bucket.GroupName.IsEmpty() ? FString::Printf(TEXT("grupo_%d"), Bucket.GroupID) : CleanName(Bucket.GroupName);
		Curves.Name = Base + (Bucket.bGuide ? TEXT("_guias") : TEXT("_fios"));
		for (int32 Other = 0; Other < OutObjects.Num() - 1; ++Other)
		{
			if (OutObjects[Other].Name == Curves.Name)
			{
				Curves.Name = FString::Printf(TEXT("%s_%d%s"), *Base, Bucket.GroupID, Bucket.bGuide ? TEXT("_guias") : TEXT("_fios"));
				break;
			}
		}

		const float Scale = FileScale(Options.Target);
		const int32 NumCurves = Bucket.Strands.Num();
		Curves.NumVertices.Reserve(NumCurves);
		TArray<int32> GroupValues;
		TArray<int32> GuideValues;
		TArray<float> UVValues;
		TArray<int32> IDValues;
		TArray<int32> ClumpValues;
		TArray<float> ColorValues;
		TArray<float> RoughnessValues;
		TArray<float> AOValues;
		for (int32 Strand : Bucket.Strands)
		{
			const FStrandID StrandID(Strand);
			const int32 Count = Counts[StrandID];
			Curves.NumVertices.Add(Count);
			GroupValues.Add(Bucket.GroupID);
			if (Bucket.bGuide)
			{
				GuideValues.Add(1);
			}
			if (RootUVs.IsValid())
			{
				const FVector2f UV = RootUVs[StrandID];
				UVValues.Add(UV.X);
				UVValues.Add(UV.Y);
			}
			if (StrandIDs.IsValid())
			{
				IDValues.Add(StrandIDs[StrandID]);
			}
			if (ClumpIDs.IsValid())
			{
				ClumpValues.Add(ClumpIDs[StrandID]);
			}
			for (int32 Point = 0; Point < Count; ++Point)
			{
				const FVertexID VertexID(Offsets[Strand] + Point);
				Curves.Positions.Add(ToFile(Positions[VertexID], Options.Target));
				if (bVertexWidths)
				{
					Curves.Widths.Add(VertexWidths[VertexID] * Scale);
				}
				else if (bStrandWidths)
				{
					Curves.Widths.Add(StrandWidths[StrandID] * Scale);
				}
				if (Colors.IsValid())
				{
					const FVector3f Color = Colors[VertexID];
					ColorValues.Add(Color.X);
					ColorValues.Add(Color.Y);
					ColorValues.Add(Color.Z);
				}
				if (Roughness.IsValid())
				{
					RoughnessValues.Add(Roughness[VertexID]);
				}
				if (AOs.IsValid())
				{
					AOValues.Add(AOs[VertexID]);
				}
			}
		}
		OutStats.Points += Curves.Positions.Num();
		if (Bucket.bGuide)
		{
			OutStats.Guides += NumCurves;
		}
		else
		{
			OutStats.Strands += NumCurves;
		}

		// Os atributos que a importacao de groom da Unreal le (o Blender ignora os que nao conhece)
		Curves.Attributes.Add(IntAttribute(TEXT("groom_version_major"), EScope::Constant, EType::Int16, { SpecMajor }));
		Curves.Attributes.Add(IntAttribute(TEXT("groom_version_minor"), EScope::Constant, EType::Int16, { SpecMinor }));
		Curves.Attributes.Add(IntAttribute(TEXT("groom_group_id"), EScope::Uniform, EType::Int32, MoveTemp(GroupValues)));
		if (Bucket.bGuide)
		{
			Curves.Attributes.Add(IntAttribute(TEXT("groom_guide"), EScope::Uniform, EType::Int32, MoveTemp(GuideValues)));
		}
		if (RootUVs.IsValid())
		{
			Curves.Attributes.Add(FloatAttribute(TEXT("groom_root_uv"), EScope::Uniform, 2, TEXT("vector"), MoveTemp(UVValues)));
		}
		if (StrandIDs.IsValid())
		{
			Curves.Attributes.Add(IntAttribute(TEXT("groom_id"), EScope::Uniform, EType::Int32, MoveTemp(IDValues)));
		}
		if (ClumpIDs.IsValid())
		{
			Curves.Attributes.Add(IntAttribute(TEXT("groom_clumpid"), EScope::Uniform, EType::Int32, MoveTemp(ClumpValues)));
		}
		if (Colors.IsValid())
		{
			Curves.Attributes.Add(FloatAttribute(TEXT("groom_color"), EScope::Vertex, 3, TEXT("rgb"), MoveTemp(ColorValues)));
		}
		if (Roughness.IsValid())
		{
			Curves.Attributes.Add(FloatAttribute(TEXT("groom_roughness"), EScope::Vertex, 1, TEXT(""), MoveTemp(RoughnessValues)));
		}
		if (AOs.IsValid())
		{
			Curves.Attributes.Add(FloatAttribute(TEXT("groom_ao"), EScope::Vertex, 1, TEXT(""), MoveTemp(AOValues)));
		}
	}
	OutStats.Groups = SeenGroups.Num();
	OutStats.Objects = OutObjects.Num();
	return true;
}

bool DeverauxGroomExport::ExportGroom(UGroomAsset& Groom, const FString& Filename, const FOptions& Options, FStats& OutStats,
	FString& OutError)
{
	OutStats = FStats();
	OutError.Reset();
#if WITH_EDITORONLY_DATA
	if (!Groom.CanRebuildFromDescription())
	{
		OutError = TEXT("este groom nao guarda os fios de origem (a descricao do .abc importado), entao nao ha o que exportar");
		return false;
	}
	const FHairDescription Description = Groom.GetHairDescription();
	TArray<DeverauxGroomAlembic::FCurves> Objects;
	if (!BuildCurves(Description, Options, Objects, OutStats, OutError))
	{
		return false;
	}
	DeverauxGroomAlembic::FArchiveInfo Info;
	Info.Application = TEXT("Unreal Engine (Deveraux Groom Export)");
	Info.Description = FString::Printf(TEXT("%s, para %s"), *Groom.GetPathName(),
		Options.Target == ETarget::Blender ? TEXT("o Blender (metros)") : TEXT("a Unreal (cm)"));
	TArray<uint8> Bytes;
	if (!DeverauxGroomAlembic::WriteArchive(Objects, Info, Bytes, OutError))
	{
		return false;
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
	if (!FFileHelper::SaveArrayToFile(Bytes, *Filename))
	{
		OutError = FString::Printf(TEXT("nao consegui gravar %s"), *Filename);
		return false;
	}
	OutStats.Bytes = Bytes.Num();
	return true;
#else
	OutError = TEXT("a exportacao de groom so funciona no editor");
	return false;
#endif
}
