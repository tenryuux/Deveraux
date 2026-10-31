// Projeto Deveraux - contato do corpo para o deformer (modulo Deveraux)

#include "Corpo/DeverauxContactComponent.h"

#include "Animation/MeshDeformerInstance.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Corpo/DeverauxBodyMath.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace DeverauxContactPrivate
{
	const FName SetVector4FunctionName(TEXT("SetVector4Variable"));

	/** Parametros do Set Variable (Vector 4) da instancia do deformer, no mesmo formato que a Unreal gera para a funcao. */
	struct FSetVector4Params
	{
		FName InVariableName;
		FVector4 InValue;
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
		if (Source && Source->FindFunction(SetVector4FunctionName))
		{
			return Source;
		}
		if (Instance->FindFunction(SetVector4FunctionName))
		{
			return Instance;
		}
		// Reserva: o campo DefaultInstance do gerenciador
		if (const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Instance->GetClass(), TEXT("DefaultInstance")))
		{
			UObject* Inner = Property->GetObjectPropertyValue_InContainer(Instance);
			if (Inner && Inner->FindFunction(SetVector4FunctionName))
			{
				return Inner;
			}
		}
		return nullptr;
	}

	/** true se o grafo tem a variavel, do tipo Vector 4. */
	bool SetVector4Variable(UObject& VariableOwner, FName Name, const FVector4& Value)
	{
		// Os parametros da funcao precisam estar como os daqui; se a Unreal mudar a funcao, nada e chamado.
		// ParmsSize vai ate o fim do ultimo parametro, sem a sobra do fim da struct: pode ser menor que ela.
		UFunction* Function = VariableOwner.FindFunction(SetVector4FunctionName);
		if (!Function || Function->NumParms != 3 || static_cast<SIZE_T>(Function->ParmsSize) > sizeof(FSetVector4Params)
			|| static_cast<SIZE_T>(Function->ReturnValueOffset) != STRUCT_OFFSET(FSetVector4Params, ReturnValue))
		{
			return false;
		}
		FSetVector4Params Params;
		Params.InVariableName = Name;
		Params.InValue = Value;
		VariableOwner.ProcessEvent(Function, &Params);
		return Params.ReturnValue;
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
}

UDeverauxContactComponent::UDeverauxContactComponent()
{
	// Depois que o corpo e as maos se moveram no quadro
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UDeverauxContactComponent::BeginPlay()
{
	Super::BeginPlay();

	FindMesh();
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
		AcceptedVariables = 0;
		return;
	}
	BuildValues(*MeshComponent);
	AcceptedVariables = SendValues(*MeshComponent);
}

void UDeverauxContactComponent::RefreshContacts()
{
	SecondsToRefresh = RefreshSeconds;
	Spheres.Reset();
	Boxes.Reset();

	TArray<AActor*> Actors;
	UGameplayStatics::GetAllActorsWithTag(this, ContactTag, Actors);
	TArray<USphereComponent*> ActorSpheres;
	TArray<UBoxComponent*> ActorBoxes;
	for (const AActor* Actor : Actors)
	{
		// O corpo nao encosta em si mesmo
		if (!Actor || Actor == GetOwner())
		{
			continue;
		}
		Actor->GetComponents(ActorSpheres);
		for (USphereComponent* Sphere : ActorSpheres)
		{
			Spheres.Add(Sphere);
		}
		Actor->GetComponents(ActorBoxes);
		for (UBoxComponent* Box : ActorBoxes)
		{
			Boxes.Add(Box);
		}
	}
}

FName UDeverauxContactComponent::GetVariableName(int32 Index)
{
	static const FName Names[NumVariables] = {
		FName(TEXT("Esfera0")), FName(TEXT("Esfera1")),
		FName(TEXT("Caixa0Centro")), FName(TEXT("Caixa0EixoX")), FName(TEXT("Caixa0EixoY")), FName(TEXT("Caixa0EixoZ")),
		FName(TEXT("Caixa1Centro")), FName(TEXT("Caixa1EixoX")), FName(TEXT("Caixa1EixoY")), FName(TEXT("Caixa1EixoZ")),
	};
	return Index >= 0 && Index < NumVariables ? Names[Index] : NAME_None;
}

USkeletalMeshComponent* UDeverauxContactComponent::FindMesh()
{
	const AActor* Actor = GetOwner();
	USkeletalMeshComponent* MeshComponent = Actor ? Actor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	Mesh = MeshComponent;
	return MeshComponent;
}

void UDeverauxContactComponent::BuildValues(const USkeletalMeshComponent& MeshComponent)
{
	using namespace DeverauxContactPrivate;

	// Desligado: raio 0 e caixa com W 0 no centro, que o kernel ignora
	LastValues.Init(FVector4(0.0, 0.0, 0.0, 0.0), NumVariables);
	const FTransform& MeshToWorld = MeshComponent.GetComponentTransform();
	const FVector BodyCenter = MeshComponent.Bounds.Origin;

	// Os mais perto do corpo primeiro
	TArray<TPair<double, const USphereComponent*>> NearSpheres;
	for (const TWeakObjectPtr<USphereComponent>& Weak : Spheres)
	{
		if (const USphereComponent* Sphere = Weak.Get())
		{
			const double Distance = FMath::Max((Sphere->GetComponentLocation() - BodyCenter).Size() - Sphere->GetScaledSphereRadius(), 0.0);
			if (Distance <= MaxDistance)
			{
				NearSpheres.Emplace(Distance, Sphere);
			}
		}
	}
	NearSpheres.Sort([](const TPair<double, const USphereComponent*>& A, const TPair<double, const USphereComponent*>& B) { return A.Key < B.Key; });
	for (int32 Index = 0; Index < FMath::Min(NearSpheres.Num(), MaxSpheres); ++Index)
	{
		const USphereComponent& Sphere = *NearSpheres[Index].Value;
		LastValues[Index] = DeverauxBody::PackSphere(MeshToWorld, Sphere.GetComponentLocation(), Sphere.GetScaledSphereRadius());
	}

	TArray<TPair<double, const UBoxComponent*>> NearBoxes;
	for (const TWeakObjectPtr<UBoxComponent>& Weak : Boxes)
	{
		if (const UBoxComponent* Box = Weak.Get())
		{
			const double Distance = DistanceToBox(*Box, BodyCenter);
			if (Distance <= MaxDistance)
			{
				NearBoxes.Emplace(Distance, Box);
			}
		}
	}
	NearBoxes.Sort([](const TPair<double, const UBoxComponent*>& A, const TPair<double, const UBoxComponent*>& B) { return A.Key < B.Key; });
	for (int32 Index = 0; Index < FMath::Min(NearBoxes.Num(), MaxBoxes); ++Index)
	{
		const UBoxComponent& Box = *NearBoxes[Index].Value;
		FVector4 Packed[4];
		DeverauxBody::PackBox(MeshToWorld, Box.GetComponentTransform(), Box.GetScaledBoxExtent(), Packed);
		for (int32 Part = 0; Part < 4; ++Part)
		{
			LastValues[MaxSpheres + Index * 4 + Part] = Packed[Part];
		}
	}
}

int32 UDeverauxContactComponent::SendValues(USkeletalMeshComponent& MeshComponent) const
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

	int32 Accepted = 0;
	for (int32 OwnerIndex = 0; OwnerIndex < VariableOwners.Num(); ++OwnerIndex)
	{
		for (int32 Index = 0; Index < NumVariables; ++Index)
		{
			if (SetVector4Variable(*VariableOwners[OwnerIndex], GetVariableName(Index), LastValues[Index]) && OwnerIndex == 0)
			{
				++Accepted;
			}
		}
	}
	return Accepted;
}

// Comando para montar o grafo: mostra o que cada corpo achou e quantas variaveis o deformer aceitou
#if !UE_BUILD_SHIPPING
namespace DeverauxContactPrivate
{
	void ContactCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		int32 Count = 0;
		for (TObjectIterator<UDeverauxContactComponent> It; It; ++It)
		{
			const UDeverauxContactComponent* Component = *It;
			if (Component->GetWorld() != World || Component->HasAnyFlags(RF_ClassDefaultObject))
			{
				continue;
			}
			++Count;
			Ar.Logf(TEXT("%s: %d esferas e %d caixas com a tag %s | o deformer aceitou %d de %d variaveis%s"), *GetNameSafe(Component->GetOwner()),
				Component->GetFoundSpheres(), Component->GetFoundBoxes(), *Component->ContactTag.ToString(), Component->GetAcceptedVariables(),
				UDeverauxContactComponent::NumVariables,
				Component->GetAcceptedVariables() == 0 ? TEXT(" (a malha sem deformer, ou o grafo sem essas variaveis do tipo Vector 4)") : TEXT(""));
			const TArray<FVector4>& Values = Component->GetLastValues();
			for (int32 Index = 0; Index < Values.Num(); ++Index)
			{
				Ar.Logf(TEXT("  %s = (%.1f, %.1f, %.1f, %.2f)"), *UDeverauxContactComponent::GetVariableName(Index).ToString(),
					Values[Index].X, Values[Index].Y, Values[Index].Z, Values[Index].W);
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
	TEXT("Mostra, para cada corpo, as esferas e caixas de contato achadas, o que foi mandado ao deformer e quantas variaveis ele aceitou."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&DeverauxContactPrivate::ContactCommand),
	ECVF_Default);
#endif // !UE_BUILD_SHIPPING
