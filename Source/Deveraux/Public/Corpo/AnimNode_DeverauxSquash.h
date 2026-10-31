// Projeto Deveraux - no de squash do Animation Blueprint (modulo Deveraux)

#pragma once

#include "Animation/AnimNodeBase.h"
#include "BoneContainer.h"
#include "CoreMinimal.h"

#include "AnimNode_DeverauxSquash.generated.h"

/** Uma massa com squash: o osso que achata e alarga, e a ponta da cadeia de jiggle, cujo balanco move o squash. */
USTRUCT(BlueprintType)
struct DEVERAUX_API FDeverauxSquashMass
{
	GENERATED_BODY()

	/** Osso que achata e alarga, normalmente o primeiro da massa: a escala dele leva a massa inteira. */
	UPROPERTY(EditAnywhere, Category = "Squash")
	FBoneReference ScaleBone;

	/** Ultimo osso da cadeia de jiggle. A distancia entre ele animado e ele simulado e o balanco. */
	UPROPERTY(EditAnywhere, Category = "Squash")
	FBoneReference TipBone;
};

/**
 * Squash do jiggle: compara a pose de antes do jiggle com a de depois e achata cada massa no sentido em que ela
 * balancou, alargando nos outros dois, com o mesmo volume. So reage ao movimento: o peso parado, de quem esta
 * deitada, e coisa dos morphs de gravidade. Serve para o Kawaii e para o Control Rig Dynamics.
 *
 * No Animation Blueprint: Animated recebe a pose do Slot por um Cached Pose; Simulated, a saida do jiggle.
 */
USTRUCT(BlueprintInternalUseOnly)
struct DEVERAUX_API FAnimNode_DeverauxSquash : public FAnimNode_Base
{
	GENERATED_BODY()

	/** Pose de antes do jiggle: a do Slot, por um Cached Pose. */
	UPROPERTY(EditAnywhere, Category = Links)
	FPoseLink Animated;

	/** Pose de depois do jiggle. E a que sai do no, com o squash por cima. */
	UPROPERTY(EditAnywhere, Category = Links)
	FPoseLink Simulated;

	/** Desligado, a pose simulada passa direto, sem custo. No spike, ligado a variavel SpikeSquash. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinShownByDefault))
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, Category = Settings)
	TArray<FDeverauxSquashMass> Masses;

	/** Quanto a massa achata por cm de balanco: 0,02 = 2% por cm. Negativo estica em vez de achatar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinHiddenByDefault))
	float StrengthPerCm = 0.02f;

	/** Limite da mudanca: 0,3 = no maximo 30% mais fina ou mais larga em cada eixo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (ClampMin = "0", ClampMax = "0.9", PinHiddenByDefault))
	float MaxChange = 0.3f;

	/** Segundos para um balanco parado deixar de contar (63% dele). Menor reage mais rapido e some mais rapido. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (ClampMin = "0.01", PinHiddenByDefault))
	float SettleSeconds = 0.3f;

	// FAnimNode_Base
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;

private:
	/** Balanco parado de cada massa, que o squash ignora. */
	TArray<FVector> SettledOffsets;
	float LastDeltaSeconds = 0.0f;
	bool bHasSettled = false;
};
