// Projeto Deveraux - morphs de gravidade do corpo (modulo Deveraux)

#include "Corpo/DeverauxGravityMorphComponent.h"

#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Corpo/DeverauxBodyMath.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogDeverauxGravidade, Log, All);

UDeverauxGravityMorphComponent::UDeverauxGravityMorphComponent()
{
	// Depois da animacao do quadro, para ler o tronco ja na pose nova
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;

	Morphs.Add(FDeverauxGravityMorph::Make(TEXT("GravCostas"), FVector(0.0, -1.0, 0.0)));
	Morphs.Add(FDeverauxGravityMorph::Make(TEXT("GravFrente"), FVector(0.0, 1.0, 0.0)));
	Morphs.Add(FDeverauxGravityMorph::Make(TEXT("GravLadoE"), FVector(1.0, 0.0, 0.0)));
	Morphs.Add(FDeverauxGravityMorph::Make(TEXT("GravLadoD"), FVector(-1.0, 0.0, 0.0)));
}

void UDeverauxGravityMorphComponent::BeginPlay()
{
	Super::BeginPlay();

	if (USkeletalMeshComponent* MeshComponent = FindMesh())
	{
		AddTickPrerequisiteComponent(MeshComponent);
	}
}

void UDeverauxGravityMorphComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bGravityEnabled)
	{
		return;
	}
	USkeletalMeshComponent* MeshComponent = Mesh.IsValid() ? Mesh.Get() : FindMesh();
	if (!MeshComponent || !CacheBone(*MeshComponent))
	{
		return;
	}
	const TArray<FTransform>& Transforms = MeshComponent->GetComponentSpaceTransforms();
	if (!Transforms.IsValidIndex(BoneIndex))
	{
		return;
	}

	LastGravity = DeverauxBody::GravityInRestFrame(MeshComponent->GetComponentQuat(), Transforms[BoneIndex].GetRotation(), BoneRefRotation);
	Weights.SetNum(Morphs.Num());
	for (int32 Index = 0; Index < Morphs.Num(); ++Index)
	{
		const FDeverauxGravityMorph& Morph = Morphs[Index];
		const float Target = DeverauxBody::GravityMorphWeight(LastGravity, Morph.Direction) * Morph.MaxWeight;

		// No primeiro quadro, ja no peso certo: a personagem pode nascer deitada
		Weights[Index] = bHasWeights ? DeverauxBody::SmoothTowards(Weights[Index], Target, DeltaTime, ResponseSeconds) : Target;
		MeshComponent->SetMorphTarget(Morph.MorphTarget, Weights[Index], false);
	}
	bHasWeights = true;
}

void UDeverauxGravityMorphComponent::SetGravityMorphsEnabled(bool bInEnabled)
{
	if (bGravityEnabled == bInEnabled)
	{
		return;
	}
	bGravityEnabled = bInEnabled;
	if (!bGravityEnabled)
	{
		ClearMorphs();
	}
}

USkeletalMeshComponent* UDeverauxGravityMorphComponent::FindMesh()
{
	// O ator pode ter mais de uma Skeletal Mesh (cabelo, roupa): o corpo e a que tem mais shape keys de gravidade
	USkeletalMeshComponent* Best = nullptr;
	int32 BestCount = -1;
	if (const AActor* OwnerActor = GetOwner())
	{
		TArray<USkeletalMeshComponent*> Candidates;
		OwnerActor->GetComponents(Candidates);
		for (USkeletalMeshComponent* Candidate : Candidates)
		{
			const int32 Count = CountMorphTargets(Candidate->GetSkeletalMeshAsset());
			if (Count > BestCount)
			{
				Best = Candidate;
				BestCount = Count;
			}
		}
	}
	Mesh = Best;
	return Best;
}

int32 UDeverauxGravityMorphComponent::CountMorphTargets(const USkeletalMesh* Asset) const
{
	int32 Count = 0;
	if (Asset)
	{
		for (const FDeverauxGravityMorph& Morph : Morphs)
		{
			Count += Asset->FindMorphTarget(Morph.MorphTarget) ? 1 : 0;
		}
	}
	return Count;
}

void UDeverauxGravityMorphComponent::CheckMorphTargets(const USkeletalMesh* Asset)
{
	// Sem a shape key, SetMorphTarget nao faz nada nem avisa: avisa aqui, uma vez por malha
	MissingMorphTargets.Reset();
	if (!Asset)
	{
		return;
	}
	FString Names;
	for (const FDeverauxGravityMorph& Morph : Morphs)
	{
		if (!Asset->FindMorphTarget(Morph.MorphTarget))
		{
			MissingMorphTargets.Add(Morph.MorphTarget);
			if (!Names.IsEmpty())
			{
				Names += TEXT(", ");
			}
			Names += Morph.MorphTarget.ToString();
		}
	}
	if (MissingMorphTargets.Num() > 0)
	{
		UE_LOG(LogDeverauxGravidade, Warning, TEXT("%s: a malha %s nao tem as shape keys %s. Confira o Import Morph Targets na importacao e os nomes em Morphs."),
			*GetNameSafe(GetOwner()), *GetNameSafe(Asset), *Names);
	}
}

bool UDeverauxGravityMorphComponent::CacheBone(const USkeletalMeshComponent& MeshComponent)
{
	// O osso ou a malha podem mudar com o jogo rodando: refaz o cache quando um dos dois muda
	const USkeletalMesh* Asset = MeshComponent.GetSkeletalMeshAsset();
	if (CachedBone == Bone && BoneIndex != INDEX_NONE && CachedAsset.Get() == Asset)
	{
		return true;
	}
	if (CachedAsset.Get() != Asset)
	{
		CheckMorphTargets(Asset);
	}
	CachedAsset = Asset;
	BoneIndex = INDEX_NONE;
	const int32 NewIndex = Asset ? MeshComponent.GetBoneIndex(Bone) : INDEX_NONE;
	if (NewIndex == INDEX_NONE)
	{
		if (!bBoneMissing || CachedBone != Bone)
		{
			UE_LOG(LogDeverauxGravidade, Warning, TEXT("%s: a malha nao tem o osso %s; os morphs de gravidade ficam parados."),
				*GetNameSafe(GetOwner()), *Bone.ToString());
		}
		CachedBone = Bone;
		bBoneMissing = true;
		return false;
	}
	CachedBone = Bone;
	BoneIndex = NewIndex;
	BoneRefRotation = FAnimationRuntime::GetComponentSpaceTransformRefPose(Asset->GetRefSkeleton(), BoneIndex).GetRotation();
	bBoneMissing = false;
	return true;
}

void UDeverauxGravityMorphComponent::ClearMorphs()
{
	if (USkeletalMeshComponent* MeshComponent = Mesh.Get())
	{
		for (const FDeverauxGravityMorph& Morph : Morphs)
		{
			MeshComponent->SetMorphTarget(Morph.MorphTarget, 0.0f, true);
		}
	}
	Weights.Reset();
	bHasWeights = false;
}

// Comando para conferir as esculturas: mostra a gravidade no espaco da malha em pe e o peso de cada shape key
#if !UE_BUILD_SHIPPING
namespace DeverauxGravityMorphPrivate
{
	void GravityCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		int32 Count = 0;
		for (TObjectIterator<UDeverauxGravityMorphComponent> It; It; ++It)
		{
			const UDeverauxGravityMorphComponent* Component = *It;
			if (Component->GetWorld() != World || Component->HasAnyFlags(RF_ClassDefaultObject))
			{
				continue;
			}
			++Count;
			const FVector& Gravity = Component->GetLastGravity();
			FString Weights;
			for (int32 Index = 0; Index < Component->Morphs.Num(); ++Index)
			{
				const float Weight = Component->GetWeights().IsValidIndex(Index) ? Component->GetWeights()[Index] : 0.0f;
				Weights += FString::Printf(TEXT(" | %s %.2f"), *Component->Morphs[Index].MorphTarget.ToString(), Weight);
			}
			FString Missing;
			for (const FName& Name : Component->GetMissingMorphTargets())
			{
				Missing += Missing.IsEmpty() ? TEXT(" | sem as shape keys ") : TEXT(", ");
				Missing += Name.ToString();
			}
			Ar.Logf(TEXT("%s: gravidade (%.2f, %.2f, %.2f) no espaco da malha em pe%s%s%s"), *GetNameSafe(Component->GetOwner()),
				Gravity.X, Gravity.Y, Gravity.Z, *Weights, *Missing,
				Component->IsBoneMissing() ? TEXT(" | sem o osso do tronco") : (Component->AreGravityMorphsEnabled() ? TEXT("") : TEXT(" | desligado")));
		}
		if (Count == 0)
		{
			Ar.Log(TEXT("Nenhum componente de morphs de gravidade neste mundo."));
		}
	}
}

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GDeverauxCorpoGravidadeCommand(
	TEXT("Deveraux.Corpo.Gravidade"),
	TEXT("Mostra, para cada corpo, para onde a gravidade puxa no espaco da malha em pe e o peso de cada shape key de gravidade."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&DeverauxGravityMorphPrivate::GravityCommand),
	ECVF_Default);
#endif // !UE_BUILD_SHIPPING
