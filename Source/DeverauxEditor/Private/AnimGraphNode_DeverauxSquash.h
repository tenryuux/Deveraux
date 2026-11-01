// Projeto Deveraux - no de squash no editor do Animation Blueprint (modulo DeverauxEditor)

#pragma once

#include "AnimGraphNode_Base.h"
#include "Corpo/AnimNode_DeverauxSquash.h"
#include "CoreMinimal.h"

#include "AnimGraphNode_DeverauxSquash.generated.h"

/** O no Deveraux Squash na lista do Animation Blueprint. O trabalho fica no FAnimNode_DeverauxSquash, no modulo Deveraux. */
UCLASS()
class UAnimGraphNode_DeverauxSquash : public UAnimGraphNode_Base
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_DeverauxSquash Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FString GetNodeCategory() const override;
	virtual FLinearColor GetNodeTitleColor() const override;

	/** Avisos na compilacao do Animation Blueprint: pino de pose solto, nenhuma massa, osso que nao existe. */
	virtual void ValidateAnimNodeDuringCompilation(class USkeleton* ForSkeleton, class FCompilerResultsLog& MessageLog) override;
};
