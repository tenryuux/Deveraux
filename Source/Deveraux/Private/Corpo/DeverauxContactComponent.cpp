// Projeto Deveraux - contato do corpo para o deformer e para os ossos (modulo Deveraux)

#include "Corpo/DeverauxContactComponent.h"

#include "Animation/MeshDeformerInstance.h"
#include "AnimationRuntime.h"
#include "CollisionQueryParams.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Corpo/DeverauxBodyMath.h"
#include "Engine/HitResult.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogDeverauxContato, Log, All);

const FName UDeverauxContactComponent::VariableName(TEXT("Contatos"));

namespace DeverauxContactPrivate
{
	const FName SetArrayFunctionName(TEXT("SetVector4ArrayVariable"));

	/** Parametros do Set Variable (array de Vector 4) da instancia do deformer, no formato que a Unreal gera para a funcao. */
	struct FSetVector4ArrayParams
	{
		FName InVariableName;
		TArray<FVector4> InValue;
		bool ReturnValue = false;
	};

	/**
	 * Quem guarda as variaveis do grafo. Na 5.8, a malha guarda um gerenciador de instancias, e a instancia do
	 * deformer escolhido vem de GetInstanceForSourceDeformer. A funcao que muda a variavel e achada pela reflexao
	 * da Unreal, sem depender do plugin do Deformer Graph para compilar.
	 */
	UObject* FindVariableOwner(UMeshDeformerInstance* Instance)
	{
		if (!Instance)
		{
			return nullptr;
		}
		UMeshDeformerInstance* Source = Instance->GetInstanceForSourceDeformer();
		if (Source && Source->FindFunction(SetArrayFunctionName))
		{
			return Source;
		}
		if (Instance->FindFunction(SetArrayFunctionName))
		{
			return Instance;
		}
		// Reserva: o campo DefaultInstance do gerenciador
		if (const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Instance->GetClass(), TEXT("DefaultInstance")))
		{
			UObject* Inner = Property->GetObjectPropertyValue_InContainer(Instance);
			if (Inner && Inner->FindFunction(SetArrayFunctionName))
			{
				return Inner;
			}
		}
		return nullptr;
	}

	/** true se o grafo tem a variavel, do tipo array de Vector 4. */
	bool SetVector4ArrayVariable(UObject& VariableOwner, FName Name, const TArray<FVector4>& Value)
	{
		// Os parametros da funcao precisam estar como os daqui; se a Unreal mudar a funcao, nada e chamado.
		// ParmsSize vai ate o fim do ultimo parametro, sem a sobra do fim da struct: pode ser menor que ela.
		UFunction* Function = VariableOwner.FindFunction(SetArrayFunctionName);
		if (!Function || Function->NumParms != 3 || static_cast<SIZE_T>(Function->ParmsSize) > sizeof(FSetVector4ArrayParams)
			|| static_cast<SIZE_T>(Function->ReturnValueOffset) != STRUCT_OFFSET(FSetVector4ArrayParams, ReturnValue))
		{
			return false;
		}
		FSetVector4ArrayParams Params;
		Params.InVariableName = Name;
		Params.InValue = Value;
		VariableOwner.ProcessEvent(Function, &Params);
		return Params.ReturnValue;
	}

	/** A malha tem deformer em algum LOD. */
	bool HasDeformer(const USkeletalMeshComponent& MeshComponent)
	{
		for (int32 Lod = 0; Lod < MeshComponent.GetNumLODs(); ++Lod)
		{
			if (MeshComponent.GetMeshDeformerInstanceForLOD(Lod))
			{
				return true;
			}
		}
		return false;
	}

	/** Distancia do ponto ate a caixa; 0 dentro dela. */
	double DistanceToBox(const UBoxComponent& Box, const FVector& Point)
	{
		const FTransform& BoxToWorld = Box.GetComponentTransform();
		const FVector Local = BoxToWorld.GetRotation().UnrotateVector(Point - BoxToWorld.GetLocation());
		const FVector Extent = Box.GetScaledBoxExtent();
		const FVector Outside(FMath::Max(FMath::Abs(Local.X) - Extent.X, 0.0), FMath::Max(FMath::Abs(Local.Y) - Extent.Y, 0.0),
			FMath::Max(FMath::Abs(Local.Z) - Extent.Z, 0.0));
		return Outside.Size();
	}

	/** Bits das regioes que o osso empurra pelas regras; 0 sem regra. */
	int32 RuleBits(const FName& BoneName, const TArray<FDeverauxBodyContactRule>& Rules)
	{
		const FString Name = BoneName.ToString();
		int32 Bits = 0;
		for (const FDeverauxBodyContactRule& Rule : Rules)
		{
			// Prefixo vazio pegaria o corpo inteiro, coluna e quadril juntos
			if (!Rule.BonePrefix.IsEmpty() && Name.StartsWith(Rule.BonePrefix, ESearchCase::IgnoreCase))
			{
				Bits |= Rule.GetMassBits();
			}
		}
		return Bits;
	}

	/**
	 * Os corpos do Physics Asset da malha como capsulas presas nos ossos: capsulas e esferas; caixas e convexos ficam de
	 * fora. Rules nulo: todos os ossos, empurrando toda a carne (o parceiro); senao, so os ossos das regras.
	 */
	void AddPhysicsCapsules(const USkeletalMeshComponent& Source, const TArray<FDeverauxBodyContactRule>* Rules,
		TArray<FDeverauxBoneCapsule>& Out)
	{
		const UPhysicsAsset* Physics = Source.GetPhysicsAsset();
		if (!Physics)
		{
			return;
		}
		for (const USkeletalBodySetup* Body : Physics->SkeletalBodySetups)
		{
			if (!Body)
			{
				continue;
			}
			const int32 BoneIndex = Source.GetBoneIndex(Body->BoneName);
			const int32 Bits = Rules ? RuleBits(Body->BoneName, *Rules) : DeverauxBody::ContactRegion::AllBits;
			if (BoneIndex == INDEX_NONE || Bits == 0)
			{
				continue;
			}
			FDeverauxBoneCapsule Capsule;
			Capsule.Mesh = &Source;
			Capsule.BoneIndex = BoneIndex;
			Capsule.BoneName = Body->BoneName;
			Capsule.MassBits = Bits;
			// Capsula da Unreal: em pe no eixo Z do elemento, com Length sem as duas meias esferas
			for (const FKSphylElem& Sphyl : Body->AggGeom.SphylElems)
			{
				const FVector Axis = Sphyl.Rotation.Quaternion().GetAxisZ() * (Sphyl.Length * 0.5);
				Capsule.LocalA = Sphyl.Center + Axis;
				Capsule.LocalB = Sphyl.Center - Axis;
				Capsule.Radius = Sphyl.Radius;
				Out.Add(Capsule);
			}
			for (const FKTaperedCapsuleElem& Tapered : Body->AggGeom.TaperedCapsuleElems)
			{
				const FVector Axis = Tapered.Rotation.Quaternion().GetAxisZ() * (Tapered.Length * 0.5);
				Capsule.LocalA = Tapered.Center + Axis;
				Capsule.LocalB = Tapered.Center - Axis;
				Capsule.Radius = FMath::Max(Tapered.Radius0, Tapered.Radius1);
				Out.Add(Capsule);
			}
			for (const FKSphereElem& Sphere : Body->AggGeom.SphereElems)
			{
				Capsule.LocalA = Sphere.Center;
				Capsule.LocalB = Sphere.Center;
				Capsule.Radius = Sphere.Radius;
				Out.Add(Capsule);
			}
		}
	}

	/** A capsula agora, no espaco da malha de destino. false: a malha dela sumiu ou o osso esta fora da pose. */
	bool CapsuleInMesh(const FDeverauxBoneCapsule& Capsule, const FTransform& MeshToWorld, double MeshScale, FVector& OutA,
		FVector& OutB, float& OutRadius)
	{
		const USkeletalMeshComponent* Source = Capsule.Mesh.Get();
		if (!Source)
		{
			return false;
		}
		const TArray<FTransform>& Pose = Source->GetComponentSpaceTransforms();
		if (!Pose.IsValidIndex(Capsule.BoneIndex))
		{
			return false;
		}
		const FTransform BoneToWorld = Pose[Capsule.BoneIndex] * Source->GetComponentTransform();
		OutA = MeshToWorld.InverseTransformPosition(BoneToWorld.TransformPosition(Capsule.LocalA));
		OutB = MeshToWorld.InverseTransformPosition(BoneToWorld.TransformPosition(Capsule.LocalB));
		OutRadius = static_cast<float>(Capsule.Radius * BoneToWorld.GetMaximumAxisScale() / MeshScale);
		return true;
	}

	/** Uma capsula candidata a ir para o deformer. */
	struct FCandidate
	{
		FVector A;
		FVector B;
		float Radius = 0.0f;
		int32 Bits = 0;
		double Score = 0.0;
	};
}

int32 FDeverauxBodyContactRule::GetMassBits() const
{
	using namespace DeverauxBody;

	return (bBreasts ? ContactRegion::RegionBits(ContactRegion::Seios) : 0) | (bGlutes ? ContactRegion::RegionBits(ContactRegion::Gluteos) : 0)
		| (bThighs ? ContactRegion::RegionBits(ContactRegion::Coxas) : 0) | (bOther ? ContactRegion::RegionBits(ContactRegion::Outras) : 0);
}

UDeverauxContactComponent::UDeverauxContactComponent()
{
	// Depois da animacao do quadro: os ossos das massas e dos bracos ja na pose nova
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;

	// Padrao do corpo de teste: os ossos jig_ do Blender e as coxas da MetaHuman
	Breasts = FDeverauxContactPair::Make(FDeverauxContactMass::Make(TEXT("jig_seio_l"), TEXT("jig_seio_01_l"), 10.0f),
		FDeverauxContactMass::Make(TEXT("jig_seio_r"), TEXT("jig_seio_01_r"), 10.0f), TEXT("ApertoSeios"));
	Glutes = FDeverauxContactPair::Make(FDeverauxContactMass::Make(TEXT("jig_gluteo_l"), TEXT("jig_gluteo_01_l"), 12.0f),
		FDeverauxContactMass::Make(TEXT("jig_gluteo_r"), TEXT("jig_gluteo_01_r"), 12.0f), TEXT("ApertoGluteos"));
	Thighs = FDeverauxContactPair::Make(FDeverauxContactMass::Make(TEXT("thigh_l"), TEXT("calf_l"), 9.0f),
		FDeverauxContactMass::Make(TEXT("thigh_r"), TEXT("calf_r"), 9.0f), TEXT("ApertoCoxas"));

	// Bracos, maos e pernas empurram tudo; a coxa so empurra os seios (de brucos ou inclinada para a frente)
	BodyRules.Add(FDeverauxBodyContactRule::Make(TEXT("upperarm"), true, true, true, true));
	BodyRules.Add(FDeverauxBodyContactRule::Make(TEXT("lowerarm"), true, true, true, true));
	BodyRules.Add(FDeverauxBodyContactRule::Make(TEXT("hand"), true, true, true, true));
	BodyRules.Add(FDeverauxBodyContactRule::Make(TEXT("calf"), true, true, true, true));
	BodyRules.Add(FDeverauxBodyContactRule::Make(TEXT("foot"), true, true, true, true));
	BodyRules.Add(FDeverauxBodyContactRule::Make(TEXT("ball"), true, true, true, true));
	BodyRules.Add(FDeverauxBodyContactRule::Make(TEXT("thigh"), true, false, false, false));

	for (int32 Mass = 0; Mass < DeverauxBody::ContactRegion::NumMasses; ++Mass)
	{
		MassBones[Mass][0] = INDEX_NONE;
		MassBones[Mass][1] = INDEX_NONE;
		Compression[Mass] = 0.0f;
	}
	for (int32 Pair = 0; Pair < DeverauxBody::ContactRegion::NumPairs; ++Pair)
	{
		RestPairNormals[Pair] = FVector(1.0, 0.0, 0.0);
		RestPairOverlaps[Pair] = 0.0f;
		Squeeze[Pair] = 0.0f;
	}
}

void UDeverauxContactComponent::BeginPlay()
{
	Super::BeginPlay();

	if (USkeletalMeshComponent* MeshComponent = FindMesh())
	{
		AddTickPrerequisiteComponent(MeshComponent);
	}
	RefreshContacts();
}

void UDeverauxContactComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	SecondsToRefresh -= DeltaTime;
	if (SecondsToRefresh <= 0.0f)
	{
		RefreshContacts();
	}
	USkeletalMeshComponent* MeshComponent = Mesh.IsValid() ? Mesh.Get() : FindMesh();
	if (!MeshComponent)
	{
		bAccepted = false;
		return;
	}
	if (MeshComponent->GetSkeletalMeshAsset() != CachedAsset.Get())
	{
		CacheMesh(*MeshComponent);
	}
	BuildValues(*MeshComponent, DeltaTime);
	bAccepted = SendValues(*MeshComponent);

	// Shape keys de aperto: so as que a malha tem
	const USkeletalMesh* Asset = MeshComponent->GetSkeletalMeshAsset();
	for (int32 Pair = 0; Pair < DeverauxBody::ContactRegion::NumPairs; ++Pair)
	{
		const FName& Morph = GetPair(Pair).SqueezeMorph;
		if (Asset && Morph != NAME_None && Asset->FindMorphTarget(Morph))
		{
			MeshComponent->SetMorphTarget(Morph, Squeeze[Pair], false);
		}
	}
}

void UDeverauxContactComponent::RefreshContacts()
{
	SecondsToRefresh = RefreshSeconds;
	Spheres.Reset();
	Capsules.Reset();
	Boxes.Reset();
	OtherBodyCapsules.Reset();
	TaggedActors.Reset();

	// A malha sem deformer (ou ainda sem): procura de novo, o deformer pode ter chegado depois
	const USkeletalMeshComponent* Current = Mesh.Get();
	if (!Current || !DeverauxContactPrivate::HasDeformer(*Current))
	{
		Current = FindMesh();
	}
	if (Current && Current->GetSkeletalMeshAsset() == CachedAsset.Get())
	{
		// O Physics Asset pode ter mudado com o jogo rodando. Os avisos ja sairam quando a malha chegou
		BuildBodyCapsules(*Current, false);
	}

	TArray<AActor*> Actors;
	UGameplayStatics::GetAllActorsWithTag(this, ContactTag, Actors);
	TArray<USphereComponent*> ActorSpheres;
	TArray<UCapsuleComponent*> ActorCapsules;
	TArray<UBoxComponent*> ActorBoxes;
	TArray<USkeletalMeshComponent*> ActorMeshes;
	for (const AActor* Actor : Actors)
	{
		// O corpo nao encosta em si mesmo por aqui: os ossos dele entram pelas regras
		if (!Actor || Actor == GetOwner())
		{
			continue;
		}
		TaggedActors.Add(Actor);
		Actor->GetComponents(ActorSpheres);
		for (USphereComponent* Sphere : ActorSpheres)
		{
			Spheres.Add(Sphere);
		}
		// Num Pawn, a capsula raiz e a do movimento, bem maior que o corpo
		const APawn* Pawn = Cast<APawn>(Actor);
		Actor->GetComponents(ActorCapsules);
		for (UCapsuleComponent* Capsule : ActorCapsules)
		{
			if (!Pawn || Capsule != Pawn->GetRootComponent())
			{
				Capsules.Add(Capsule);
			}
		}
		Actor->GetComponents(ActorBoxes);
		for (UBoxComponent* Box : ActorBoxes)
		{
			Boxes.Add(Box);
		}
		Actor->GetComponents(ActorMeshes);
		for (const USkeletalMeshComponent* ActorMesh : ActorMeshes)
		{
			DeverauxContactPrivate::AddPhysicsCapsules(*ActorMesh, nullptr, OtherBodyCapsules);
		}
	}
}

int32 UDeverauxContactComponent::GetOtherCapsules() const
{
	return OtherBodyCapsules.Num() + Spheres.Num() + Capsules.Num();
}

float UDeverauxContactComponent::GetCompression(int32 Mass) const
{
	return Mass >= 0 && Mass < DeverauxBody::ContactRegion::NumMasses ? Compression[Mass] : 0.0f;
}

float UDeverauxContactComponent::GetSqueeze(int32 Pair) const
{
	return Pair >= 0 && Pair < DeverauxBody::ContactRegion::NumPairs ? Squeeze[Pair] : 0.0f;
}

int32 UDeverauxContactComponent::GetActiveMasses() const
{
	int32 Count = 0;
	for (int32 Mass = 0; Mass < DeverauxBody::ContactRegion::NumMasses; ++Mass)
	{
		Count += LastValues.IsValidIndex(DeverauxBody::ContactLayout::Masses + Mass * 2)
			&& LastValues[DeverauxBody::ContactLayout::Masses + Mass * 2].W > 0.0 ? 1 : 0;
	}
	return Count;
}

bool UDeverauxContactComponent::IsPairActive(int32 Pair) const
{
	const int32 Index = DeverauxBody::ContactLayout::Pairs + Pair;
	if (Pair < 0 || Pair >= DeverauxBody::ContactRegion::NumPairs || !LastValues.IsValidIndex(Index))
	{
		return false;
	}
	const FVector4& Plane = LastValues[Index];
	return Plane.X * Plane.X + Plane.Y * Plane.Y + Plane.Z * Plane.Z > 0.25;
}

FString UDeverauxContactComponent::GetMassName(int32 Mass)
{
	static const TCHAR* Names[DeverauxBody::ContactRegion::NumMasses] = {
		TEXT("seio esquerdo"), TEXT("seio direito"), TEXT("gluteo esquerdo"), TEXT("gluteo direito"), TEXT("coxa esquerda"), TEXT("coxa direita") };
	return Mass >= 0 && Mass < DeverauxBody::ContactRegion::NumMasses ? FString(Names[Mass]) : FString(TEXT("resto da carne"));
}

USkeletalMeshComponent* UDeverauxContactComponent::FindMesh()
{
	// O ator pode ter mais de uma Skeletal Mesh (cabelo, roupa): o corpo e a que tem o deformer; sem nenhuma, a primeira
	USkeletalMeshComponent* Found = nullptr;
	if (const AActor* OwnerActor = GetOwner())
	{
		TArray<USkeletalMeshComponent*> Candidates;
		OwnerActor->GetComponents(Candidates);
		for (USkeletalMeshComponent* Candidate : Candidates)
		{
			if (DeverauxContactPrivate::HasDeformer(*Candidate))
			{
				Found = Candidate;
				break;
			}
			Found = Found ? Found : Candidate;
		}
	}
	Mesh = Found;
	return Found;
}

const FDeverauxContactPair& UDeverauxContactComponent::GetPair(int32 Pair) const
{
	return Pair == DeverauxBody::ContactRegion::Seios ? Breasts : (Pair == DeverauxBody::ContactRegion::Gluteos ? Glutes : Thighs);
}

const FDeverauxContactMass& UDeverauxContactComponent::GetMass(int32 Mass) const
{
	const FDeverauxContactPair& Pair = GetPair(Mass / 2);
	return Mass % 2 == 0 ? Pair.Left : Pair.Right;
}

void UDeverauxContactComponent::CacheMesh(const USkeletalMeshComponent& MeshComponent)
{
	using namespace DeverauxBody;

	const USkeletalMesh* Asset = MeshComponent.GetSkeletalMeshAsset();
	CachedAsset = Asset;
	FVector RestA[ContactRegion::NumMasses];
	FVector RestB[ContactRegion::NumMasses];
	bool bRest[ContactRegion::NumMasses];
	for (int32 Mass = 0; Mass < ContactRegion::NumMasses; ++Mass)
	{
		const FDeverauxContactMass& Config = GetMass(Mass);
		MassBones[Mass][0] = Asset && Config.StartBone != NAME_None ? MeshComponent.GetBoneIndex(Config.StartBone) : INDEX_NONE;
		MassBones[Mass][1] = Asset && Config.EndBone != NAME_None ? MeshComponent.GetBoneIndex(Config.EndBone) : INDEX_NONE;
		bRest[Mass] = MassBones[Mass][0] != INDEX_NONE && MassBones[Mass][1] != INDEX_NONE;
		if (Asset && !bRest[Mass])
		{
			UE_LOG(LogDeverauxContato, Warning, TEXT("%s: a malha %s nao tem os ossos %s e %s: a massa %s fica sem contato."), *GetNameSafe(GetOwner()),
				*GetNameSafe(Asset), *Config.StartBone.ToString(), *Config.EndBone.ToString(), *GetMassName(Mass));
		}
		RestA[Mass] = bRest[Mass] ? FAnimationRuntime::GetComponentSpaceTransformRefPose(Asset->GetRefSkeleton(), MassBones[Mass][0]).GetLocation() : FVector::ZeroVector;
		RestB[Mass] = bRest[Mass] ? FAnimationRuntime::GetComponentSpaceTransformRefPose(Asset->GetRefSkeleton(), MassBones[Mass][1]).GetLocation() : FVector::ZeroVector;
	}

	// O lado esquerdo do corpo, pelo X das massas esquerdas na pose de referencia (no manequim da Unreal, +X)
	double LeftX = 0.0;
	for (int32 Pair = 0; Pair < ContactRegion::NumPairs; ++Pair)
	{
		const int32 Left = Pair * 2;
		if (bRest[Left])
		{
			LeftX += (RestA[Left].X + RestB[Left].X) * 0.5;
		}
	}
	LeftSign = LeftX >= 0.0 ? 1.0f : -1.0f;

	for (int32 Pair = 0; Pair < ContactRegion::NumPairs; ++Pair)
	{
		const int32 Left = Pair * 2;
		const int32 Right = Pair * 2 + 1;
		RestPairNormals[Pair] = FVector(LeftSign < 0.0f ? 1.0 : -1.0, 0.0, 0.0);
		RestPairOverlaps[Pair] = 0.0f;
		if (!bRest[Left] || !bRest[Right])
		{
			continue;
		}
		const FVector LeftCenter = (RestA[Left] + RestB[Left]) * 0.5;
		const FVector RightCenter = (RestA[Right] + RestB[Right]) * 0.5;
		if ((LeftCenter.X - RightCenter.X) * LeftSign <= 0.0)
		{
			UE_LOG(LogDeverauxContato, Warning, TEXT("%s: na pose de referencia, a massa %s nao fica do lado esquerdo da massa %s. Confira os ossos delas."),
				*GetNameSafe(GetOwner()), *GetMassName(Left), *GetMassName(Right));
		}
		const FVector Across = (RightCenter - LeftCenter).GetSafeNormal();
		if (!Across.IsZero())
		{
			RestPairNormals[Pair] = Across;
		}
		float Overlap = 0.0f;
		PairPlane(RestA[Left], RestB[Left], GetMass(Left).Radius, RestA[Right], RestB[Right], GetMass(Right).Radius, RestPairNormals[Pair], Overlap);
		RestPairOverlaps[Pair] = FMath::Max(Overlap, 0.0f);
	}

	BuildBodyCapsules(MeshComponent, true);
	bHasSqueeze = false;
}

void UDeverauxContactComponent::BuildBodyCapsules(const USkeletalMeshComponent& MeshComponent, bool bWarn)
{
	using namespace DeverauxBody;

	BodyCapsules.Reset();
	DeverauxContactPrivate::AddPhysicsCapsules(MeshComponent, &BodyRules, BodyCapsules);

	// Um osso que ja entra fundo numa massa na pose de referencia achataria a carne parada: ele nao empurra essa
	// massa (a panturrilha e a coxa do mesmo lado, que se tocam no joelho), so as outras
	const USkeletalMesh* Asset = MeshComponent.GetSkeletalMeshAsset();
	if (!Asset)
	{
		return;
	}
	for (FDeverauxBoneCapsule& Capsule : BodyCapsules)
	{
		const FTransform BoneRest = FAnimationRuntime::GetComponentSpaceTransformRefPose(Asset->GetRefSkeleton(), Capsule.BoneIndex);
		const FVector A = BoneRest.TransformPosition(Capsule.LocalA);
		const FVector B = BoneRest.TransformPosition(Capsule.LocalB);
		for (int32 Mass = 0; Mass < ContactRegion::NumMasses; ++Mass)
		{
			const FDeverauxContactMass& Config = GetMass(Mass);
			if (MassBones[Mass][0] == INDEX_NONE || MassBones[Mass][1] == INDEX_NONE || (Capsule.MassBits & ContactRegion::MassBit(Mass)) == 0)
			{
				continue;
			}
			const FVector MassA = FAnimationRuntime::GetComponentSpaceTransformRefPose(Asset->GetRefSkeleton(), MassBones[Mass][0]).GetLocation();
			const FVector MassB = FAnimationRuntime::GetComponentSpaceTransformRefPose(Asset->GetRefSkeleton(), MassBones[Mass][1]).GetLocation();
			if (MassDepthInCapsule(MassA, MassB, Config.Radius, A, B, Capsule.Radius) > Config.Radius * 0.5f)
			{
				Capsule.MassBits &= ~ContactRegion::MassBit(Mass);
				if (bWarn)
				{
					UE_LOG(LogDeverauxContato, Log, TEXT("%s: o corpo do osso %s ja entra na massa %s na pose de referencia, entao nao empurra essa massa. Se devia empurrar, diminua o corpo no Physics Asset ou o raio da massa."),
						*GetNameSafe(GetOwner()), *Capsule.BoneName.ToString(), *GetMassName(Mass));
				}
			}
		}
	}
}

void UDeverauxContactComponent::BuildValues(const USkeletalMeshComponent& MeshComponent, float DeltaTime)
{
	using namespace DeverauxContactPrivate;
	using namespace DeverauxBody;

	LastValues.Init(FVector4(0.0, 0.0, 0.0, 0.0), ContactLayout::Num);
	const FTransform& MeshToWorld = MeshComponent.GetComponentTransform();
	const double MeshScale = FMath::Max(MeshToWorld.GetMaximumAxisScale(), UE_KINDA_SMALL_NUMBER);
	const TArray<FTransform>& Pose = MeshComponent.GetComponentSpaceTransforms();
	const FVector BodyCenter = MeshToWorld.InverseTransformPosition(MeshComponent.Bounds.Origin);

	// Massas: o eixo de cada uma agora, no espaco da malha
	FVector MassA[ContactRegion::NumMasses];
	FVector MassB[ContactRegion::NumMasses];
	float MassRadius[ContactRegion::NumMasses];
	for (int32 Mass = 0; Mass < ContactRegion::NumMasses; ++Mass)
	{
		const int32 Start = MassBones[Mass][0];
		const int32 End = MassBones[Mass][1];
		const bool bOn = Pose.IsValidIndex(Start) && Pose.IsValidIndex(End) && GetMass(Mass).Radius > 0.0f;
		MassA[Mass] = bOn ? Pose[Start].GetLocation() : FVector::ZeroVector;
		MassB[Mass] = bOn ? Pose[End].GetLocation() : FVector::ZeroVector;
		MassRadius[Mass] = bOn ? GetMass(Mass).Radius : 0.0f;
		if (bOn)
		{
			LastValues[ContactLayout::Masses + Mass * 2] = FVector4(MassA[Mass], MassRadius[Mass]);
			LastValues[ContactLayout::Masses + Mass * 2 + 1] = FVector4(MassB[Mass], 0.0);
		}
	}

	// Pares: o plano entre as duas massas e quanto elas se apertam alem do que ja se encostam paradas
	float PairDepth[ContactRegion::NumPairs] = { 0.0f, 0.0f, 0.0f };
	for (int32 Pair = 0; Pair < ContactRegion::NumPairs; ++Pair)
	{
		const int32 Left = Pair * 2;
		const int32 Right = Pair * 2 + 1;
		if (!GetPair(Pair).bEnabled || MassRadius[Left] <= 0.0f || MassRadius[Right] <= 0.0f)
		{
			continue;
		}
		float Overlap = 0.0f;
		LastValues[ContactLayout::Pairs + Pair] = PairPlane(MassA[Left], MassB[Left], MassRadius[Left], MassA[Right], MassB[Right], MassRadius[Right],
			RestPairNormals[Pair], Overlap);
		PairDepth[Pair] = FMath::Max(Overlap - RestPairOverlaps[Pair], 0.0f);
	}

	// Capsulas candidatas, no espaco da malha, pela distancia ate a massa mais perto entre as que ela empurra (a coxa,
	// que so empurra os seios, conta pela distancia ate eles); sem nenhuma dessas, pela distancia ate o centro do corpo
	TArray<FCandidate> Candidates;
	const auto Consider = [&](const FVector& A, const FVector& B, float Radius, int32 Bits)
	{
		double Score = UE_BIG_NUMBER;
		bool bAnyMass = false;
		for (int32 Mass = 0; Mass < ContactRegion::NumMasses; ++Mass)
		{
			if (MassRadius[Mass] > 0.0f && (Bits & ContactRegion::MassBit(Mass)) != 0)
			{
				FVector OnMass;
				FVector OnCapsule;
				ClosestPointsBetweenSegments(MassA[Mass], MassB[Mass], A, B, OnMass, OnCapsule);
				Score = FMath::Min(Score, (OnCapsule - OnMass).Size() - Radius - MassRadius[Mass]);
				bAnyMass = true;
			}
		}
		if (!bAnyMass)
		{
			Score = (ClosestPointOnSegment(BodyCenter, A, B) - BodyCenter).Size() - Radius;
		}
		if (Score <= MaxDistance / MeshScale)
		{
			FCandidate& Candidate = Candidates[Candidates.AddDefaulted()];
			Candidate.A = A;
			Candidate.B = B;
			Candidate.Radius = Radius;
			Candidate.Bits = Bits;
			Candidate.Score = Score;
		}
	};
	FVector A;
	FVector B;
	float Radius = 0.0f;
	for (const FDeverauxBoneCapsule& Capsule : BodyCapsules)
	{
		if (CapsuleInMesh(Capsule, MeshToWorld, MeshScale, A, B, Radius))
		{
			Consider(A, B, Radius, Capsule.MassBits);
		}
	}
	for (const FDeverauxBoneCapsule& Capsule : OtherBodyCapsules)
	{
		if (CapsuleInMesh(Capsule, MeshToWorld, MeshScale, A, B, Radius))
		{
			Consider(A, B, Radius, Capsule.MassBits);
		}
	}
	for (const TWeakObjectPtr<USphereComponent>& Weak : Spheres)
	{
		if (const USphereComponent* Sphere = Weak.Get())
		{
			const FVector Center = MeshToWorld.InverseTransformPosition(Sphere->GetComponentLocation());
			Consider(Center, Center, static_cast<float>(Sphere->GetScaledSphereRadius() / MeshScale), ContactRegion::AllBits);
		}
	}
	for (const TWeakObjectPtr<UCapsuleComponent>& Weak : Capsules)
	{
		if (const UCapsuleComponent* Capsule = Weak.Get())
		{
			// Capsule Component: em pe no Z dele, com a meia altura contando as meias esferas
			const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
			const FVector Axis = Capsule->GetComponentQuat().GetAxisZ() * FMath::Max(Capsule->GetScaledCapsuleHalfHeight() - CapsuleRadius, 0.0f);
			const FVector Center = Capsule->GetComponentLocation();
			Consider(MeshToWorld.InverseTransformPosition(Center + Axis), MeshToWorld.InverseTransformPosition(Center - Axis),
				static_cast<float>(CapsuleRadius / MeshScale), ContactRegion::AllBits);
		}
	}
	Candidates.Sort([](const FCandidate& Left, const FCandidate& Right) { return Left.Score < Right.Score; });
	const int32 NumCapsules = FMath::Min(Candidates.Num(), ContactLayout::MaxCapsules);
	for (int32 Index = 0; Index < NumCapsules; ++Index)
	{
		const FCandidate& Candidate = Candidates[Index];
		LastValues[ContactLayout::Capsules + Index * 2] = FVector4(Candidate.A, Candidate.Radius);
		LastValues[ContactLayout::Capsules + Index * 2 + 1] = FVector4(Candidate.B, Candidate.Bits);
	}

	// Caixas: as mais perto do centro do corpo
	const FVector BodyCenterWorld = MeshComponent.Bounds.Origin;
	TArray<TPair<double, const UBoxComponent*>> NearBoxes;
	for (const TWeakObjectPtr<UBoxComponent>& Weak : Boxes)
	{
		if (const UBoxComponent* Box = Weak.Get())
		{
			const double Distance = DistanceToBox(*Box, BodyCenterWorld);
			if (Distance <= MaxDistance)
			{
				NearBoxes.Emplace(Distance, Box);
			}
		}
	}
	NearBoxes.Sort([](const TPair<double, const UBoxComponent*>& Left, const TPair<double, const UBoxComponent*>& Right) { return Left.Key < Right.Key; });
	const int32 NumBoxes = FMath::Min(NearBoxes.Num(), ContactLayout::MaxBoxes);
	for (int32 Index = 0; Index < NumBoxes; ++Index)
	{
		const UBoxComponent& Box = *NearBoxes[Index].Value;
		FVector4 Packed[4];
		PackBox(MeshToWorld, Box.GetComponentTransform(), Box.GetScaledBoxExtent(), BodyCenterWorld, Packed);
		for (int32 Part = 0; Part < 4; ++Part)
		{
			LastValues[ContactLayout::Boxes + Index * 4 + Part] = Packed[Part];
		}
	}

	// Chao embaixo de cada massa e do resto da carne: o que for World Static ou Dynamic, olhando para cima
	FloorsFound = 0;
	UWorld* World = GetWorld();
	if (bFindFloor && World)
	{
		FCollisionObjectQueryParams Objects(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		// Os atores com a tag ficam de fora: uma mao ou um objeto embaixo da massa viraria um chao sem fim
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DeverauxContatoChao), false, GetOwner());
		for (const TWeakObjectPtr<const AActor>& Tagged : TaggedActors)
		{
			if (const AActor* Actor = Tagged.Get())
			{
				Params.AddIgnoredActor(Actor);
			}
		}
		for (int32 Floor = 0; Floor < ContactLayout::NumFloors; ++Floor)
		{
			const bool bMass = Floor < ContactRegion::NumMasses;
			if (bMass && MassRadius[Floor] <= 0.0f)
			{
				continue;
			}
			// Do alto da massa para baixo: a massa deitada no chao ainda acha o chao embaixo dela
			const FVector Center = MeshToWorld.TransformPosition(bMass ? (MassA[Floor] + MassB[Floor]) * 0.5 : BodyCenter);
			const FVector Start = Center + FVector(0.0, 0.0, (bMass ? MassRadius[Floor] * MeshScale : 0.0) + 5.0);
			const FVector End = Center - FVector(0.0, 0.0, FloorSearchDistance);
			FHitResult Hit;
			if (World->LineTraceSingleByObjectType(Hit, Start, End, Objects, Params) && Hit.ImpactNormal.Z > 0.2)
			{
				const FVector Normal = MeshToWorld.InverseTransformVectorNoScale(Hit.ImpactNormal).GetSafeNormal();
				LastValues[ContactLayout::Floors + Floor] = FVector4(Normal, FVector::DotProduct(Normal, MeshToWorld.InverseTransformPosition(Hit.ImpactPoint)));
				++FloorsFound;
			}
		}
	}

	// Aperto de cada massa: quanto ela entra no que encosta, sobre o raio dela
	for (int32 Mass = 0; Mass < ContactRegion::NumMasses; ++Mass)
	{
		Compression[Mass] = 0.0f;
		if (MassRadius[Mass] <= 0.0f)
		{
			continue;
		}
		float Depth = PairDepth[Mass / 2] * 0.5f;
		const int32 Bit = ContactRegion::MassBit(Mass);
		for (int32 Index = 0; Index < NumCapsules; ++Index)
		{
			if ((Candidates[Index].Bits & Bit) != 0)
			{
				Depth = FMath::Max(Depth, MassDepthInCapsule(MassA[Mass], MassB[Mass], MassRadius[Mass], Candidates[Index].A, Candidates[Index].B,
					Candidates[Index].Radius));
			}
		}
		for (int32 Index = 0; Index < NumBoxes; ++Index)
		{
			Depth = FMath::Max(Depth, MassDepthInBox(MassA[Mass], MassB[Mass], MassRadius[Mass], &LastValues[ContactLayout::Boxes + Index * 4]));
		}
		Depth = FMath::Max(Depth, MassDepthInPlane(MassA[Mass], MassB[Mass], MassRadius[Mass], LastValues[ContactLayout::Floors + Mass]));
		Compression[Mass] = FMath::Clamp(Depth / MassRadius[Mass], 0.0f, 1.0f);
	}
	LastValues[ContactLayout::Compression] = FVector4(Compression[0], Compression[1], Compression[2], Compression[3]);
	LastValues[ContactLayout::Compression + 1] = FVector4(Compression[4], Compression[5], 0.0, 0.0);

	// Shape key de aperto: so o aperto de uma massa contra a outra, suavizado
	for (int32 Pair = 0; Pair < ContactRegion::NumPairs; ++Pair)
	{
		const float Smaller = FMath::Min(MassRadius[Pair * 2], MassRadius[Pair * 2 + 1]);
		const float Target = Smaller > 0.0f ? FMath::Clamp(PairDepth[Pair] / Smaller, 0.0f, 1.0f) : 0.0f;
		Squeeze[Pair] = bHasSqueeze ? SmoothTowards(Squeeze[Pair], Target, DeltaTime, SqueezeResponseSeconds) : Target;
	}
	bHasSqueeze = true;

	LastValues[ContactLayout::Header] = FVector4(NumCapsules, NumBoxes, BulgeCm / MeshScale, BulgeBandCm / MeshScale);
	LastValues[ContactLayout::Settings] = FVector4(LeftSign, MidlineCm / MeshScale, 0.0, 0.0);
}

bool UDeverauxContactComponent::SendValues(USkeletalMeshComponent& MeshComponent) const
{
	using namespace DeverauxContactPrivate;

	// Uma instancia por LOD com deformer; varios LODs podem dividir a mesma
	TArray<UObject*, TInlineAllocator<4>> VariableOwners;
	for (int32 Lod = 0; Lod < MeshComponent.GetNumLODs(); ++Lod)
	{
		if (UObject* VariableOwner = FindVariableOwner(MeshComponent.GetMeshDeformerInstanceForLOD(Lod)))
		{
			VariableOwners.AddUnique(VariableOwner);
		}
	}

	bool bFirstAccepted = false;
	for (int32 OwnerIndex = 0; OwnerIndex < VariableOwners.Num(); ++OwnerIndex)
	{
		const bool bSent = SetVector4ArrayVariable(*VariableOwners[OwnerIndex], VariableName, LastValues);
		bFirstAccepted = bFirstAccepted || (OwnerIndex == 0 && bSent);
	}
	return bFirstAccepted;
}

// Comando para montar e conferir: o que cada corpo achou, o aperto de cada massa e se o deformer aceitou a variavel
#if !UE_BUILD_SHIPPING
namespace DeverauxContactPrivate
{
	void ContactCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		using namespace DeverauxBody;

		int32 Count = 0;
		for (TObjectIterator<UDeverauxContactComponent> It; It; ++It)
		{
			const UDeverauxContactComponent* Component = *It;
			if (Component->GetWorld() != World || Component->HasAnyFlags(RF_ClassDefaultObject))
			{
				continue;
			}
			++Count;
			const TArray<FVector4>& Values = Component->GetLastValues();
			const int32 Sent = Values.IsValidIndex(ContactLayout::Header) ? static_cast<int32>(Values[ContactLayout::Header].X + 0.5) : 0;
			const int32 SentBoxes = Values.IsValidIndex(ContactLayout::Header) ? static_cast<int32>(Values[ContactLayout::Header].Y + 0.5) : 0;
			Ar.Logf(TEXT("%s: deformer %s | massas %d de 6 | capsulas: %d do corpo, %d com a tag %s, %d mandadas | caixas %d | chaos %d de 7"),
				*GetNameSafe(Component->GetOwner()),
				Component->IsAccepted() ? TEXT("aceitou a variavel Contatos") : TEXT("SEM a variavel Contatos (a malha sem deformer, ou o grafo sem a variavel array de Vector 4)"),
				Component->GetActiveMasses(), Component->GetBodyCapsules(), Component->GetOtherCapsules(), *Component->ContactTag.ToString(), Sent,
				SentBoxes, Component->GetFloorsFound());
			static const TCHAR* PairNames[ContactRegion::NumPairs] = { TEXT("seios"), TEXT("gluteos"), TEXT("coxas") };
			for (int32 Pair = 0; Pair < ContactRegion::NumPairs; ++Pair)
			{
				Ar.Logf(TEXT("  %s: plano %s | aperto %.2f e %.2f | shape key %.2f"), PairNames[Pair],
					Component->IsPairActive(Pair) ? TEXT("ligado") : TEXT("desligado"), Component->GetCompression(Pair * 2),
					Component->GetCompression(Pair * 2 + 1), Component->GetSqueeze(Pair));
			}
		}
		if (Count == 0)
		{
			Ar.Log(TEXT("Nenhum componente de contato neste mundo."));
		}
	}
}

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GDeverauxCorpoContatoCommand(
	TEXT("Deveraux.Corpo.Contato"),
	TEXT("Mostra, para cada corpo, o que o contato achou (massas, capsulas, caixas e chaos), o aperto de cada massa e se o deformer aceitou a variavel Contatos."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&DeverauxContactPrivate::ContactCommand),
	ECVF_Default);
#endif // !UE_BUILD_SHIPPING
