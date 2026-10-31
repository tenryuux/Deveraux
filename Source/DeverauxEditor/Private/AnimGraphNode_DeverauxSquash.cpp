// Projeto Deveraux - no de squash no editor do Animation Blueprint (modulo DeverauxEditor)

#include "AnimGraphNode_DeverauxSquash.h"

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

#undef LOCTEXT_NAMESPACE
