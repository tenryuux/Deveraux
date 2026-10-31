// Projeto Deveraux - contato do corpo para o deformer (modulo Deveraux)

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "DeverauxContactComponent.generated.h"

class UBoxComponent;
class USkeletalMeshComponent;
class USphereComponent;

/**
 * Contato do corpo: a cada quadro, manda ao Deformer Graph da malha as esferas (maos) e as caixas (chao, assento)
 * dos atores com a tag ContactTag, no espaco da malha, e o grafo empurra para fora a carne que entra nelas.
 * Variaveis do grafo, todas Vector 4: Esfera0 e Esfera1 (centro e raio, raio 0 = desligada) e, para cada caixa
 * N (0 e 1), CaixaNCentro (W 0 = desligada), CaixaNEixoX, CaixaNEixoY e CaixaNEixoZ (eixo e meia largura).
 * O kernel que usa isso fica em Private/Corpo/ContatoKernel.hlsl.
 */
UCLASS(ClassGroup = (Deveraux), meta = (BlueprintSpawnableComponent))
class DEVERAUX_API UDeverauxContactComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxSpheres = 2;
	static constexpr int32 MaxBoxes = 2;

	/** Variaveis do grafo, na ordem de GetLastValues: as esferas e depois quatro por caixa. */
	static constexpr int32 NumVariables = MaxSpheres + MaxBoxes * 4;

	UDeverauxContactComponent();

	/** Atores com esta tag entram como contato: cada Sphere Component vira uma mao; cada Box Component, chao ou assento. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato")
	FName ContactTag = TEXT("SpikeContato");

	/** Contato mais longe que isto do centro do corpo fica de fora, em cm. Ficam os mais perto, ate 2 esferas e 2 caixas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato", meta = (ClampMin = "0"))
	float MaxDistance = 300.0f;

	/** De quanto em quanto tempo procura de novo os atores com a tag, em segundos (uma mao que nasceu depois, por exemplo). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato", meta = (ClampMin = "0.1"))
	float RefreshSeconds = 1.0f;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Procura de novo, agora, os atores com a tag. */
	UFUNCTION(BlueprintCallable, Category = "Contato")
	void RefreshContacts();

	/** Nome de cada variavel do grafo, na ordem de GetLastValues. */
	static FName GetVariableName(int32 Index);

	/** O que foi mandado no ultimo quadro, na ordem das variaveis. */
	const TArray<FVector4>& GetLastValues() const { return LastValues; }

	/** Quantas variaveis o deformer aceitou no ultimo quadro. 0: a malha sem deformer, ou o grafo com outros nomes. */
	int32 GetAcceptedVariables() const { return AcceptedVariables; }

	/** Esferas e caixas com a tag encontradas na ultima procura, perto ou nao. */
	int32 GetFoundSpheres() const { return Spheres.Num(); }
	int32 GetFoundBoxes() const { return Boxes.Num(); }

private:
	USkeletalMeshComponent* FindMesh();
	void BuildValues(const USkeletalMeshComponent& MeshComponent);
	int32 SendValues(USkeletalMeshComponent& MeshComponent) const;

	TWeakObjectPtr<USkeletalMeshComponent> Mesh;
	TArray<TWeakObjectPtr<USphereComponent>> Spheres;
	TArray<TWeakObjectPtr<UBoxComponent>> Boxes;
	TArray<FVector4> LastValues;
	float SecondsToRefresh = 0.0f;
	int32 AcceptedVariables = 0;
};
