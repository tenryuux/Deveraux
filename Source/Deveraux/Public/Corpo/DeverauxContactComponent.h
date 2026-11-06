// Projeto Deveraux - contato do corpo para o deformer e para os ossos (modulo Deveraux)

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

#include "DeverauxContactComponent.generated.h"

class AActor;
class UBoxComponent;
class UCapsuleComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class USphereComponent;

/** Uma massa de um par: uma capsula em volta do eixo entre dois ossos, que acompanha a pose. */
USTRUCT(BlueprintType)
struct DEVERAUX_API FDeverauxContactMass
{
	GENERATED_BODY()

	/** Comeco do eixo da massa: a raiz da cadeia de jiggle (seio e gluteo) ou o quadril (coxa). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato")
	FName StartBone;

	/** Fim do eixo: a ponta da cadeia de jiggle (seio e gluteo) ou o joelho (coxa). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato")
	FName EndBone;

	/** Raio da massa em volta do eixo, em cm: mais ou menos metade da largura dela. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato", meta = (ClampMin = "0"))
	float Radius = 10.0f;

	static FDeverauxContactMass Make(FName InStartBone, FName InEndBone, float InRadius)
	{
		FDeverauxContactMass Mass;
		Mass.StartBone = InStartBone;
		Mass.EndBone = InEndBone;
		Mass.Radius = InRadius;
		return Mass;
	}
};

/** Um par de massas, a esquerda e a direita, que nao se atravessam e achatam uma contra a outra quando apertadas. */
USTRUCT(BlueprintType)
struct DEVERAUX_API FDeverauxContactPair
{
	GENERATED_BODY()

	/** Desligado, as duas massas podem se atravessar; o resto do contato continua. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato")
	FDeverauxContactMass Left;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato")
	FDeverauxContactMass Right;

	/**
	 * Shape key opcional do par apertado (a carne que sobe e espalha quando as duas massas se apertam), com o peso
	 * pelo aperto, de 0 a 1. Sem ela na malha, nada acontece.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato")
	FName SqueezeMorph;

	static FDeverauxContactPair Make(const FDeverauxContactMass& InLeft, const FDeverauxContactMass& InRight, FName InSqueezeMorph)
	{
		FDeverauxContactPair Pair;
		Pair.Left = InLeft;
		Pair.Right = InRight;
		Pair.SqueezeMorph = InSqueezeMorph;
		return Pair;
	}
};

/** Ossos do proprio corpo que viram contato, pelos corpos do Physics Asset da malha, e as partes que eles empurram. */
USTRUCT(BlueprintType)
struct DEVERAUX_API FDeverauxBodyContactRule
{
	GENERATED_BODY()

	/** Os corpos presos em ossos cujo nome comeca assim (upperarm, hand, calf...), sem diferenca de maiuscula. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato")
	FString BonePrefix;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato")
	bool bBreasts = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato")
	bool bGlutes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato")
	bool bThighs = true;

	/** O resto da carne macia, como a barriga. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato")
	bool bOther = true;

	/** Bits das massas (DeverauxBody::ContactRegion::MassBit) que a regra empurra: os dois lados de cada parte marcada. */
	int32 GetMassBits() const;

	static FDeverauxBodyContactRule Make(const FString& InBonePrefix, bool bInBreasts, bool bInGlutes, bool bInThighs, bool bInOther)
	{
		FDeverauxBodyContactRule Rule;
		Rule.BonePrefix = InBonePrefix;
		Rule.bBreasts = bInBreasts;
		Rule.bGlutes = bInGlutes;
		Rule.bThighs = bInThighs;
		Rule.bOther = bInOther;
		return Rule;
	}
};

/** Uma capsula presa num osso de uma Skeletal Mesh, com as pontas no espaco do osso. */
struct FDeverauxBoneCapsule
{
	TWeakObjectPtr<const USkeletalMeshComponent> Mesh;
	int32 BoneIndex = INDEX_NONE;
	FName BoneName;
	FVector LocalA = FVector::ZeroVector;
	FVector LocalB = FVector::ZeroVector;
	float Radius = 0.0f;
	/** As massas que a capsula empurra (DeverauxBody::ContactRegion::MassBit). */
	int32 MassBits = 0;
	/** Do proprio corpo: as pontas na pose de referencia, no espaco da malha. A carne que ja entra nelas parada fica. */
	bool bHasRest = false;
	FVector RestA = FVector::ZeroVector;
	FVector RestB = FVector::ZeroVector;
};

/**
 * Contato do corpo. A cada quadro junta tudo em que a carne macia encosta e manda ao Deformer Graph da malha numa
 * variavel so, Contatos (array de Vector 4, no layout de DeverauxBody::ContactLayout); o no Deveraux Contato dos Ossos le
 * o mesmo array. Entram: o plano entre as duas massas de cada par (seios, gluteos e coxas), os ossos do proprio corpo
 * pelas regras (bracos, maos, pernas e o tronco embaixo dos seios, pelo Physics Asset), as formas dos atores com
 * ContactTag (esferas, capsulas, caixas e os corpos do Physics Asset das Skeletal Meshes deles, como o parceiro) e o chao
 * embaixo de cada massa.
 * O kernel fica em Private/Corpo/ContatoKernel.hlsl. Vai no ator do personagem e usa a Skeletal Mesh dele que tem deformer.
 */
UCLASS(ClassGroup = (Deveraux), meta = (BlueprintSpawnableComponent))
class DEVERAUX_API UDeverauxContactComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDeverauxContactComponent();

	/** Nome da variavel do grafo: um array de Vector 4. */
	static const FName VariableName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato|Pares")
	FDeverauxContactPair Breasts;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato|Pares")
	FDeverauxContactPair Glutes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato|Pares")
	FDeverauxContactPair Thighs;

	/**
	 * Ossos do proprio corpo que empurram a carne, pelos corpos do Physics Asset da malha. A carne que ja encosta num
	 * deles na pose de referencia (o seio no braco, a coxa na panturrilha, no joelho) fica onde esta; so o que entra alem
	 * disso sai, como quando ela se ajoelha e a panturrilha aperta a coxa.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato|Corpo")
	TArray<FDeverauxBodyContactRule> BodyRules;

	/**
	 * Atores com esta tag entram como contato, empurrando toda a carne macia: cada Sphere, Capsule e Box Component, e os
	 * corpos do Physics Asset das Skeletal Meshes deles (o parceiro). Num Pawn, a capsula raiz (a do movimento) fica de fora.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato|Formas")
	FName ContactTag = TEXT("SpikeContato");

	/** Forma mais longe que isto das massas fica de fora, em cm. Ficam as mais perto: 16 capsulas e 2 caixas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato|Formas", meta = (ClampMin = "0"))
	float MaxDistance = 300.0f;

	/** De quanto em quanto tempo procura de novo os atores com a tag, em segundos. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato|Formas", meta = (ClampMin = "0.1"))
	float RefreshSeconds = 1.0f;

	/**
	 * Procura, a cada quadro, o chao embaixo de cada massa (tambem assento e cama). Os atores com ContactTag ficam de
	 * fora da procura: eles ja entram pelas formas.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato|Chao")
	bool bFindFloor = true;

	/**
	 * Canal da procura do chao: conta o que bloqueia este canal. No padrao, Visibility: o chao, os moveis e as paredes;
	 * gatilhos (Trigger Box e afins) e personagens ficam de fora.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato|Chao")
	TEnumAsByte<ECollisionChannel> FloorChannel = ECC_Visibility;

	/** Ate onde procura o chao para baixo, em cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato|Chao", meta = (ClampMin = "0"))
	float FloorSearchDistance = 200.0f;

	/** Quanto a carne em volta do contato estufa com a massa toda apertada, em cm. 0: nao estufa. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato|Carne", meta = (ClampMin = "0"))
	float BulgeCm = 1.5f;

	/** Ate que distancia do contato a carne estufa, em cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato|Carne", meta = (ClampMin = "0"))
	float BulgeBandCm = 6.0f;

	/** Faixa do meio do corpo (o fundo do decote e do sulco dos gluteos) em que o plano do par nao empurra, em cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato|Carne", meta = (ClampMin = "0"))
	float MidlineCm = 1.5f;

	/** Segundos para a shape key de aperto cobrir 63% do caminho ate o valor novo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contato|Carne", meta = (ClampMin = "0"))
	float SqueezeResponseSeconds = 0.1f;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Procura de novo, agora, os atores com a tag e os corpos do Physics Asset. */
	UFUNCTION(BlueprintCallable, Category = "Contato")
	void RefreshContacts();

	/** O array mandado no ultimo quadro, no layout de DeverauxBody::ContactLayout. */
	const TArray<FVector4>& GetLastValues() const { return LastValues; }

	/** O deformer aceitou a variavel Contatos no ultimo quadro. false: a malha sem deformer, ou o grafo sem a variavel. */
	bool IsAccepted() const { return bAccepted; }

	/** Aperto da massa (0 a 5: seio E, seio D, gluteo E, gluteo D, coxa E, coxa D), de 0 a 1. */
	float GetCompression(int32 Mass) const;

	/** Peso da shape key de aperto do par (0 seios, 1 gluteos, 2 coxas), de 0 a 1. */
	float GetSqueeze(int32 Pair) const;

	/** Massas com os dois ossos na malha. */
	int32 GetActiveMasses() const;

	/** O plano do par esta ligado. */
	bool IsPairActive(int32 Pair) const;

	/** Capsulas achadas na ultima procura: do proprio corpo e dos atores com a tag. */
	int32 GetBodyCapsules() const { return BodyCapsules.Num(); }
	int32 GetOtherCapsules() const;

	/** Caixas com a tag achadas na ultima procura. */
	int32 GetFoundBoxes() const { return Boxes.Num(); }

	/** Chaos achados no ultimo quadro, de 0 a 7 (um por massa e um para o resto da carne). */
	int32 GetFloorsFound() const { return FloorsFound; }

	/** Nome da massa, para o log e o comando. */
	static FString GetMassName(int32 Mass);

private:
	USkeletalMeshComponent* FindMesh();
	const FDeverauxContactPair& GetPair(int32 Pair) const;
	const FDeverauxContactMass& GetMass(int32 Mass) const;
	void CacheMesh(const USkeletalMeshComponent& MeshComponent);
	void BuildBodyCapsules(const USkeletalMeshComponent& MeshComponent);
	void BuildValues(const USkeletalMeshComponent& MeshComponent, float DeltaTime);
	bool SendValues(USkeletalMeshComponent& MeshComponent) const;

	TWeakObjectPtr<USkeletalMeshComponent> Mesh;
	TWeakObjectPtr<const USkeletalMesh> CachedAsset;
	TArray<FDeverauxBoneCapsule> BodyCapsules;
	TArray<FDeverauxBoneCapsule> OtherBodyCapsules;
	TArray<TWeakObjectPtr<USphereComponent>> Spheres;
	TArray<TWeakObjectPtr<UCapsuleComponent>> Capsules;
	TArray<TWeakObjectPtr<UBoxComponent>> Boxes;
	/** Os atores com a tag, que a procura do chao ignora. */
	TArray<TWeakObjectPtr<const AActor>> TaggedActors;
	TArray<FVector4> LastValues;

	/** Ossos de cada massa na malha (comeco e fim), INDEX_NONE quando falta. */
	int32 MassBones[6][2];
	/** O eixo de cada massa na pose de referencia. */
	FVector RestMassA[6];
	FVector RestMassB[6];
	/** Na pose de referencia: a direcao da massa esquerda para a direita e quanto as duas ja se encostam, por par. */
	FVector RestPairNormals[3];
	float RestPairOverlaps[3];
	float LeftSign = 1.0f;
	float Compression[6];
	float Squeeze[3];
	float SecondsToRefresh = 0.0f;
	int32 FloorsFound = 0;
	bool bAccepted = false;
	bool bHasSqueeze = false;
};
