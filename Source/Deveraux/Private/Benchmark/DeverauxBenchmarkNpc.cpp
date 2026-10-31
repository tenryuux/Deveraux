// Projeto Deveraux - NPC do benchmark de densidade, o spike 3 da Fase 0 (modulo Deveraux)

#include "Benchmark/DeverauxBenchmarkNpc.h"

#include "Animation/AnimInstance.h"
#include "Benchmark/DeverauxBenchmarkSettings.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "UObject/ConstructorHelpers.h"

namespace DeverauxBenchmarkNpcPrivate
{
	/** Capsula do manequim da Unreal, em cm. */
	constexpr float CapsuleRadius = 42.0f;
	constexpr float CapsuleHalfHeight = 96.0f;

	/** Lado do cilindro basico da engine, em cm. */
	constexpr float CylinderMeshSize = 100.0f;

	/** Perto disto do ponto, o NPC chegou. */
	constexpr float AcceptanceRadius = 60.0f;

	/** Pausa entre um ponto e o proximo, em segundos. */
	constexpr float MinPauseSeconds = 0.5f;
	constexpr float MaxPauseSeconds = 2.5f;

	/** A primeira saida e espalhada, para os NPCs nao andarem todos juntos. A Unreal ignora um timer de 0 s. */
	constexpr float MinFirstDelaySeconds = 0.05f;
	constexpr float MaxFirstDelaySeconds = 1.0f;

	/** Andando por conta propria, desiste do ponto depois disto (preso em outro NPC, por exemplo). */
	constexpr float MaxSteerSeconds = 20.0f;
}

ADeverauxBenchmarkNpc::ADeverauxBenchmarkNpc()
{
	using namespace DeverauxBenchmarkNpcPrivate;

	AIControllerClass = ADeverauxBenchmarkNpcController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// Vira para onde anda, como um pedestre
	bUseControllerRotationYaw = false;
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.0, 360.0, 0.0);

	GetCapsuleComponent()->InitCapsuleSize(CapsuleRadius, CapsuleHalfHeight);

	Placeholder = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cilindro"));
	Placeholder->SetupAttachment(GetCapsuleComponent());
	Placeholder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Placeholder->SetRelativeScale3D(FVector(2.0f * CapsuleRadius / CylinderMeshSize, 2.0f * CapsuleRadius / CylinderMeshSize,
		2.0f * CapsuleHalfHeight / CylinderMeshSize));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		Placeholder->SetStaticMesh(CylinderMesh.Object);
	}
}

void ADeverauxBenchmarkNpc::Setup(const FVector& InAreaCenter, float InAreaRadius, int32 InSeed, bool bInOptimized)
{
	AreaCenter = InAreaCenter;
	AreaRadius = InAreaRadius;
	Seed = InSeed;
	bOptimized = bInOptimized;

	const UDeverauxBenchmarkSettings* Settings = GetDefault<UDeverauxBenchmarkSettings>();
	GetCharacterMovement()->MaxWalkSpeed = Settings->NpcWalkSpeed;

	USkeletalMeshComponent* MeshComponent = GetMesh();
	USkeletalMesh* MeshAsset = Settings->NpcMesh.LoadSynchronous();
	bHasCharacterMesh = MeshComponent && MeshAsset;
	if (bHasCharacterMesh)
	{
		MeshComponent->SetSkeletalMeshAsset(MeshAsset);
		MeshComponent->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, Settings->NpcMeshOffsetZ), FRotator(0.0f, Settings->NpcMeshYaw, 0.0f));
		if (UClass* AnimClass = Settings->NpcAnimClass.LoadSynchronous())
		{
			MeshComponent->SetAnimInstanceClass(AnimClass);
		}

		// O que um jogo faz com a multidao: animacao em menos quadros quando longe e parada quando ninguem ve
		MeshComponent->bEnableUpdateRateOptimizations = bOptimized;
		MeshComponent->VisibilityBasedAnimTickOption = bOptimized
			? EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered
			: EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
	Placeholder->SetVisibility(!bHasCharacterMesh);
}

ADeverauxBenchmarkNpcController::ADeverauxBenchmarkNpcController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ADeverauxBenchmarkNpcController::OnPossess(APawn* InPawn)
{
	using namespace DeverauxBenchmarkNpcPrivate;

	Super::OnPossess(InPawn);

	const ADeverauxBenchmarkNpc* Npc = Cast<ADeverauxBenchmarkNpc>(InPawn);
	Random.Initialize(Npc ? Npc->GetSeed() : 0);
	bSteering = false;
	SchedulePause(MinFirstDelaySeconds, MaxFirstDelaySeconds);
}

void ADeverauxBenchmarkNpcController::OnUnPossess()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PauseTimer);
	}
	bSteering = false;

	Super::OnUnPossess();
}

void ADeverauxBenchmarkNpcController::Tick(float DeltaSeconds)
{
	using namespace DeverauxBenchmarkNpcPrivate;

	Super::Tick(DeltaSeconds);

	if (!bSteering)
	{
		return;
	}
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		bSteering = false;
		return;
	}

	FVector ToTarget = SteerTarget - ControlledPawn->GetActorLocation();
	ToTarget.Z = 0.0;
	SteerSecondsLeft -= DeltaSeconds;
	if (ToTarget.SizeSquared() <= FMath::Square(AcceptanceRadius) || SteerSecondsLeft <= 0.0f)
	{
		bSteering = false;
		SchedulePause(MinPauseSeconds, MaxPauseSeconds);
		return;
	}
	ControlledPawn->AddMovementInput(ToTarget.GetSafeNormal());
}

void ADeverauxBenchmarkNpcController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	using namespace DeverauxBenchmarkNpcPrivate;

	Super::OnMoveCompleted(RequestID, Result);

	// Um pedido trocado ou cancelado (o NPC saindo do mundo, por exemplo) nao agenda o proximo
	if (!Result.IsInterrupted() && GetPawn())
	{
		SchedulePause(MinPauseSeconds, MaxPauseSeconds);
	}
}

void ADeverauxBenchmarkNpcController::MoveToRandomPoint()
{
	using namespace DeverauxBenchmarkNpcPrivate;

	const ADeverauxBenchmarkNpc* Npc = Cast<ADeverauxBenchmarkNpc>(GetPawn());
	if (!Npc)
	{
		return;
	}

	FVector Target = PickPoint(*Npc);
	if (bSteerDirectly)
	{
		StartSteering(Target);
		return;
	}

	// Com NavMesh, um ponto que da para alcancar e o caminho ate ele; sem NavMesh, linha reta ate o ponto sorteado
	bool bUsePathfinding = false;
	if (const UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld()))
	{
		FNavLocation NavLocation;
		if (NavSystem->GetRandomReachablePointInRadius(Npc->GetAreaCenter(), Npc->GetAreaRadius(), NavLocation, nullptr, nullptr))
		{
			Target = NavLocation.Location;
			bUsePathfinding = true;
		}
	}

	const EPathFollowingRequestResult::Type Request = MoveToLocation(Target, AcceptanceRadius, true, bUsePathfinding, false, false,
		TSubclassOf<UNavigationQueryFilter>(), true);
	if (Request == EPathFollowingRequestResult::Failed)
	{
		// O mapa nao aceitou o pedido (sem sistema de navegacao): daqui em diante o NPC anda sozinho
		bSteerDirectly = true;
		StartSteering(Target);
	}
	else if (Request == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		SchedulePause(MinPauseSeconds, MaxPauseSeconds);
	}
}

void ADeverauxBenchmarkNpcController::StartSteering(const FVector& Target)
{
	// Andando sozinho, a proxima saida so e agendada na chegada. O pedido recusado ja avisou o fim (OnMoveCompleted)
	// e agendou uma: ela sai, ou o NPC trocaria de destino no meio do caminho
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PauseTimer);
	}
	SteerTarget = Target;
	SteerSecondsLeft = DeverauxBenchmarkNpcPrivate::MaxSteerSeconds;
	bSteering = true;
}

void ADeverauxBenchmarkNpcController::SchedulePause(float MinSeconds, float MaxSeconds)
{
	if (UWorld* World = GetWorld())
	{
		const float Seconds = FMath::Max(Random.FRandRange(MinSeconds, MaxSeconds), DeverauxBenchmarkNpcPrivate::MinFirstDelaySeconds);
		World->GetTimerManager().SetTimer(PauseTimer, this, &ADeverauxBenchmarkNpcController::MoveToRandomPoint, Seconds, false);
	}
}

FVector ADeverauxBenchmarkNpcController::PickPoint(const ADeverauxBenchmarkNpc& Npc)
{
	// Uniforme no disco: a raiz quadrada evita juntar todo mundo no centro
	const float Angle = Random.FRandRange(0.0f, UE_TWO_PI);
	const float Distance = Npc.GetAreaRadius() * FMath::Sqrt(Random.FRand());
	return Npc.GetAreaCenter() + FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.0f);
}
