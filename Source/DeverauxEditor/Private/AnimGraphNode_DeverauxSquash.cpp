// Projeto Deveraux - no de squash no editor do Animation Blueprint (modulo DeverauxEditor)

#include "AnimGraphNode_DeverauxSquash.h"

#include "Animation/Skeleton.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/CompilerResultsLog.h"

#define LOCTEXT_NAMESPACE "DeverauxSquash"

FText UAnimGraphNode_DeverauxSquash::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Title", "Deveraux Squash");
}

FText UAnimGraphNode_DeverauxSquash::GetTooltipText() const
{
	return LOCTEXT("Tooltip", "Achata cada massa no sentido em que o jiggle balancou, com o mesmo volume. Animated: a pose de antes do jiggle (o Slot, por um Cached Pose). Simulated: a saida do Kawaii ou do Control Rig Dynamics.");
}

FString UAnimGraphNode_DeverauxSquash::GetNodeCategory() const
{
	return TEXT("Deveraux");
}

FLinearColor UAnimGraphNode_DeverauxSquash::GetNodeTitleColor() const
{
	return FLinearColor(0.75f, 0.35f, 0.45f);
}

void UAnimGraphNode_DeverauxSquash::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

	// Pino de pose solto vira a pose de referencia: com o Animated solto, o balanco e a animacao inteira e o squash
	// fica no maximo o tempo todo; com o Simulated solto, o corpo para na pose de referencia
	const UEdGraphPin* AnimatedPin = FindPin(GET_MEMBER_NAME_CHECKED(FAnimNode_DeverauxSquash, Animated));
	if (AnimatedPin && AnimatedPin->LinkedTo.Num() == 0)
	{
		MessageLog.Warning(TEXT("@@: o pino Animated esta solto. Ligue nele a pose de antes do jiggle (um Use Cached Pose do Slot); solto, o squash fica no maximo o tempo todo."), this);
	}
	const UEdGraphPin* SimulatedPin = FindPin(GET_MEMBER_NAME_CHECKED(FAnimNode_DeverauxSquash, Simulated));
	if (SimulatedPin && SimulatedPin->LinkedTo.Num() == 0)
	{
		MessageLog.Warning(TEXT("@@: o pino Simulated esta solto. Ligue nele a saida do jiggle; solto, o corpo fica na pose de referencia."), this);
	}
	if (Node.Masses.Num() == 0)
	{
		MessageLog.Warning(TEXT("@@: nenhuma massa, o no nao faz nada. Em Masses, uma linha por massa, com o Scale Bone e o Tip Bone."), this);
	}

	// Os ossos de cada massa, no esqueleto do Animation Blueprint
	if (!ForSkeleton || ForSkeleton->HasAnyFlags(RF_NeedPostLoad))
	{
		return;
	}
	const FReferenceSkeleton& RefSkeleton = ForSkeleton->GetReferenceSkeleton();
	for (int32 Index = 0; Index < Node.Masses.Num(); ++Index)
	{
		const FDeverauxSquashMass& Mass = Node.Masses[Index];
		const int32 ScaleIndex = RefSkeleton.FindBoneIndex(Mass.ScaleBone.BoneName);
		const int32 TipIndex = RefSkeleton.FindBoneIndex(Mass.TipBone.BoneName);
		if (ScaleIndex == INDEX_NONE || TipIndex == INDEX_NONE)
		{
			const FString Missing = ScaleIndex != INDEX_NONE ? Mass.TipBone.BoneName.ToString()
				: (TipIndex != INDEX_NONE ? Mass.ScaleBone.BoneName.ToString()
					: FString::Printf(TEXT("%s e %s"), *Mass.ScaleBone.BoneName.ToString(), *Mass.TipBone.BoneName.ToString()));
			MessageLog.Warning(*FString::Printf(TEXT("@@: massa %d: o esqueleto nao tem %s. Ela fica de fora."), Index + 1, *Missing), this);
		}
		else if (!RefSkeleton.BoneIsChildOf(TipIndex, ScaleIndex))
		{
			MessageLog.Warning(*FString::Printf(TEXT("@@: massa %d: o Tip Bone (%s) precisa ficar abaixo do Scale Bone (%s) no esqueleto."),
				Index + 1, *Mass.TipBone.BoneName.ToString(), *Mass.ScaleBone.BoneName.ToString()), this);
		}
	}
}

#undef LOCTEXT_NAMESPACE
