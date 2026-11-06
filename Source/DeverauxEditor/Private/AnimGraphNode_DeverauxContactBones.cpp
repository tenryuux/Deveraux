// Projeto Deveraux - no de contato dos ossos no editor do Animation Blueprint (modulo DeverauxEditor)

#include "AnimGraphNode_DeverauxContactBones.h"

#include "Animation/Skeleton.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/CompilerResultsLog.h"

#define LOCTEXT_NAMESPACE "DeverauxContactBones"

FText UAnimGraphNode_DeverauxContactBones::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Title", "Deveraux Contato dos Ossos");
}

FText UAnimGraphNode_DeverauxContactBones::GetTooltipText() const
{
	return LOCTEXT("Tooltip", "Tira a ponta de cada cadeia de jiggle de dentro do que a massa encosta (o outro lado do par, os bracos, o parceiro, objetos e o chao), girando a raiz. Vai depois do Kawaii ou do Control Rig Dynamics e antes do Deveraux Squash. Precisa do componente Deveraux Contact no ator.");
}

FString UAnimGraphNode_DeverauxContactBones::GetNodeCategory() const
{
	return TEXT("Deveraux");
}

FLinearColor UAnimGraphNode_DeverauxContactBones::GetNodeTitleColor() const
{
	return FLinearColor(0.35f, 0.55f, 0.75f);
}

void UAnimGraphNode_DeverauxContactBones::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

	// Pino de pose solto vira a pose de referencia: o corpo para nela
	const UEdGraphPin* SourcePin = FindPin(GET_MEMBER_NAME_CHECKED(FAnimNode_DeverauxContactBones, Source));
	if (SourcePin && SourcePin->LinkedTo.Num() == 0)
	{
		MessageLog.Warning(TEXT("@@: o pino Source esta solto. Ligue nele a saida do jiggle; solto, o corpo fica na pose de referencia."), this);
	}
	if (Node.Chains.Num() == 0)
	{
		MessageLog.Warning(TEXT("@@: nenhuma cadeia, o no nao faz nada. Em Chains, uma linha por cadeia de jiggle, com o Root Bone, o Tip Bone e a massa."), this);
	}

	// Os ossos de cada cadeia, no esqueleto do Animation Blueprint
	if (!ForSkeleton || ForSkeleton->HasAnyFlags(RF_NeedPostLoad))
	{
		return;
	}
	const FReferenceSkeleton& RefSkeleton = ForSkeleton->GetReferenceSkeleton();
	for (int32 Index = 0; Index < Node.Chains.Num(); ++Index)
	{
		const FDeverauxContactBoneChain& Chain = Node.Chains[Index];
		const int32 RootIndex = RefSkeleton.FindBoneIndex(Chain.RootBone.BoneName);
		const int32 TipIndex = RefSkeleton.FindBoneIndex(Chain.TipBone.BoneName);
		if (RootIndex == INDEX_NONE || TipIndex == INDEX_NONE)
		{
			const FString Missing = RootIndex != INDEX_NONE ? Chain.TipBone.BoneName.ToString()
				: (TipIndex != INDEX_NONE ? Chain.RootBone.BoneName.ToString()
					: FString::Printf(TEXT("%s e %s"), *Chain.RootBone.BoneName.ToString(), *Chain.TipBone.BoneName.ToString()));
			MessageLog.Warning(*FString::Printf(TEXT("@@: cadeia %d: o esqueleto nao tem %s. Ela fica de fora."), Index + 1, *Missing), this);
		}
		else if (!RefSkeleton.BoneIsChildOf(TipIndex, RootIndex))
		{
			MessageLog.Warning(*FString::Printf(TEXT("@@: cadeia %d: o Tip Bone (%s) precisa ficar abaixo do Root Bone (%s) no esqueleto."),
				Index + 1, *Chain.TipBone.BoneName.ToString(), *Chain.RootBone.BoneName.ToString()), this);
		}
	}
}

#undef LOCTEXT_NAMESPACE
