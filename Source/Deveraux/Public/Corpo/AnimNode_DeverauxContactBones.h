// Projeto Deveraux - no de contato dos ossos do Animation Blueprint (modulo Deveraux)

#pragma once

#include "Animation/AnimNodeBase.h"
#include "BoneContainer.h"
#include "CoreMinimal.h"

#include "AnimNode_DeverauxContactBones.generated.h"

/** As seis massas do contato, na ordem do componente Deveraux Contact: indice = par * 2 + lado. */
UENUM(BlueprintType)
enum class EDeverauxContactMass : uint8
{
	BreastLeft UMETA(DisplayName = "Seio esquerdo"),
	BreastRight UMETA(DisplayName = "Seio direito"),
	GluteLeft UMETA(DisplayName = "Gluteo esquerdo"),
	GluteRight UMETA(DisplayName = "Gluteo direito"),
	ThighLeft UMETA(DisplayName = "Coxa esquerda"),
	ThighRight UMETA(DisplayName = "Coxa direita"),
};

/** Uma cadeia de jiggle com contato: a ponta nao entra no que a massa encosta, e a raiz gira para leva-la para fora. */
USTRUCT(BlueprintType)
struct DEVERAUX_API FDeverauxContactBoneChain
{
	GENERATED_BODY()

	/** Osso que gira: a raiz da cadeia de jiggle. */
	UPROPERTY(EditAnywhere, Category = "Contato")
	FBoneReference RootBone;

	/** Ultimo osso da cadeia, o que fica no miolo da massa. */
	UPROPERTY(EditAnywhere, Category = "Contato")
	FBoneReference TipBone;

	/** Distancia da ponta ate a pele, em cm: a ponta para antes, e a pele encosta. */
	UPROPERTY(EditAnywhere, Category = "Contato", meta = (ClampMin = "0"))
	float Radius = 5.0f;

	/** De qual massa e a cadeia: decide o que a empurra e de que lado do par ela fica. */
	UPROPERTY(EditAnywhere, Category = "Contato")
	EDeverauxContactMass Mass = EDeverauxContactMass::BreastLeft;
};

/**
 * Contato dos ossos de jiggle: depois do Kawaii ou do Control Rig Dynamics, tira a ponta de cada cadeia de dentro do
 * que a massa encosta (capsulas, caixas, o chao e o outro lado do par), girando a raiz; empurrada de frente, a massa
 * encurta (a ponta chega mais perto da raiz, nunca mais longe). Le o mesmo array que o componente Deveraux Contact do
 * ator manda ao deformer, entao o osso e a pele param no mesmo lugar.
 *
 * No Animation Blueprint: entre o jiggle e o Deveraux Squash, que achata a massa empurrada.
 */
USTRUCT(BlueprintInternalUseOnly)
struct DEVERAUX_API FAnimNode_DeverauxContactBones : public FAnimNode_Base
{
	GENERATED_BODY()

	/** A pose depois do jiggle. */
	UPROPERTY(EditAnywhere, Category = Links)
	FPoseLink Source;

	/** Desligado, a pose passa direto, sem custo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinShownByDefault))
	bool bEnabled = true;

	/** Uma linha por cadeia de jiggle. O padrao e o do corpo de teste: as quatro cadeias jig_ de seios e gluteos. */
	UPROPERTY(EditAnywhere, Category = Settings)
	TArray<FDeverauxContactBoneChain> Chains;

	FAnimNode_DeverauxContactBones();

	// FAnimNode_Base
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual bool HasPreUpdate() const override { return true; }
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;

	/** Quantas pontas o no tirou de dentro de alguma coisa na ultima pose. */
	int32 GetLastPushed() const { return LastPushed; }

	/** O ator tem o componente Deveraux Contact (visto na ultima atualizacao). */
	bool HasContactComponent() const { return bHasComponent; }

private:
	/** Copia do array do componente, feita no game thread. */
	TArray<FVector4> Values;
	int32 LastPushed = 0;
	bool bHasComponent = false;
};
