// Projeto Deveraux - no de contato dos ossos no editor do Animation Blueprint (modulo DeverauxEditor)

#pragma once

#include "AnimGraphNode_Base.h"
#include "Corpo/AnimNode_DeverauxContactBones.h"
#include "CoreMinimal.h"

#include "AnimGraphNode_DeverauxContactBones.generated.h"

/** O no Deveraux Contato dos Ossos na lista do Animation Blueprint. O trabalho fica no FAnimNode_DeverauxContactBones. */
UCLASS()
class UAnimGraphNode_DeverauxContactBones : public UAnimGraphNode_Base
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_DeverauxContactBones Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FString GetNodeCategory() const override;
	virtual FLinearColor GetNodeTitleColor() const override;

	/** Avisos na compilacao do Animation Blueprint: pino de pose solto, nenhuma cadeia, osso que nao existe. */
	virtual void ValidateAnimNodeDuringCompilation(class USkeleton* ForSkeleton, class FCompilerResultsLog& MessageLog) override;
};
