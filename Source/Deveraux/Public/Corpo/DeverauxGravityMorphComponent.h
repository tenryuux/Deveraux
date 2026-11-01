// Projeto Deveraux - morphs de gravidade do corpo (modulo Deveraux)

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "DeverauxGravityMorphComponent.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;

/** Um morph de gravidade: a shape key e para onde a gravidade puxa quando ela fica no maximo. */
USTRUCT(BlueprintType)
struct DEVERAUX_API FDeverauxGravityMorph
{
	GENERATED_BODY()

	/** Nome da shape key, como ficou na importacao. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gravidade")
	FName MorphTarget;

	/**
	 * Para onde a gravidade puxa, no espaco da malha em pe, quando a shape key fica no maximo. O comando
	 * Deveraux.Corpo.Gravidade mostra a direcao de agora, para conferir em cada pose.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gravidade")
	FVector Direction = FVector(0.0, -1.0, 0.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gravidade", meta = (ClampMin = "0", ClampMax = "1"))
	float MaxWeight = 1.0f;

	static FDeverauxGravityMorph Make(FName InMorphTarget, const FVector& InDirection)
	{
		FDeverauxGravityMorph Morph;
		Morph.MorphTarget = InMorphTarget;
		Morph.Direction = InDirection;
		return Morph;
	}
};

/**
 * Morphs de gravidade: le a inclinacao do tronco e da o peso de cada shape key, para as massas se acomodarem para o
 * lado que a gravidade puxa (deitada de costas, de brucos ou de lado). Em pe, todas ficam em zero: a escultura
 * em pe ja tem a gravidade normal. Vai no ator do personagem e usa a Skeletal Mesh dele com mais dessas shape keys
 * (o ator pode ter outras, como cabelo e roupa).
 */
UCLASS(ClassGroup = (Deveraux), meta = (BlueprintSpawnableComponent))
class DEVERAUX_API UDeverauxGravityMorphComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDeverauxGravityMorphComponent();

	/** Osso do tronco que diz a inclinacao do corpo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gravidade")
	FName Bone = TEXT("spine_03");

	/**
	 * As shape keys. O padrao e o do corpo de teste, com a malha olhando para +Y como o manequim da Unreal:
	 * costas em -Y, frente em +Y, lado esquerdo em +X e direito em -X.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gravidade")
	TArray<FDeverauxGravityMorph> Morphs;

	/** Segundos que a carne leva para se acomodar quando o corpo gira (63% do caminho). 0: na hora. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gravidade", meta = (ClampMin = "0", ClampMax = "2"))
	float ResponseSeconds = 0.15f;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Desligado, as shape keys voltam a zero e nada e calculado. */
	UFUNCTION(BlueprintCallable, Category = "Gravidade")
	void SetGravityMorphsEnabled(bool bInEnabled);

	UFUNCTION(BlueprintPure, Category = "Gravidade")
	bool AreGravityMorphsEnabled() const { return bGravityEnabled; }

	/** A gravidade no espaco da malha em pe, no ultimo quadro. */
	const FVector& GetLastGravity() const { return LastGravity; }

	/** Os pesos de agora, na ordem de Morphs. */
	const TArray<float>& GetWeights() const { return Weights; }

	/** O osso nao existe na malha: nada e calculado. */
	bool IsBoneMissing() const { return bBoneMissing; }

	/** Shape keys de Morphs que a malha nao tem (Import Morph Targets desligado, ou nome diferente). */
	const TArray<FName>& GetMissingMorphTargets() const { return MissingMorphTargets; }

private:
	USkeletalMeshComponent* FindMesh();
	int32 CountMorphTargets(const USkeletalMesh* Asset) const;
	void CheckMorphTargets(const USkeletalMesh* Asset);
	bool CacheBone(const USkeletalMeshComponent& MeshComponent);
	void ClearMorphs();

	TWeakObjectPtr<USkeletalMeshComponent> Mesh;
	TWeakObjectPtr<const USkeletalMesh> CachedAsset;
	TArray<FName> MissingMorphTargets;
	TArray<float> Weights;
	FVector LastGravity = FVector(0.0, 0.0, -1.0);
	FQuat BoneRefRotation = FQuat::Identity;
	FName CachedBone;
	int32 BoneIndex = INDEX_NONE;
	bool bGravityEnabled = true;
	bool bHasWeights = false;
	bool bBoneMissing = false;
};
