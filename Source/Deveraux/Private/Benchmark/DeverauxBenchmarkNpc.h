// Projeto Deveraux - NPC do benchmark de densidade, o spike 3 da Fase 0 (modulo Deveraux)

#pragma once

#include "AIController.h"
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Math/RandomStream.h"
#include "TimerManager.h"

#include "DeverauxBenchmarkNpc.generated.h"

class UStaticMeshComponent;

/**
 * NPC de teste: um Character com a malha e a animacao de Project Settings > Deveraux Benchmark, andando a
 * esmo numa area. Mede quanto custa um NPC encarnado (movimento, animacao e sombra); o cerebro de verdade,
 * com rotina e StateTree, vem na Fase 1. Sem malha configurada, vira um cilindro e so o movimento conta.
 */
UCLASS(NotPlaceable, Transient)
class ADeverauxBenchmarkNpc : public ACharacter
{
	GENERATED_BODY()

public:
	ADeverauxBenchmarkNpc();

	/**
	 * Prepara o NPC entre o SpawnActorDeferred e o FinishSpawning: malha, animacao, velocidade e a area onde anda.
	 * A otimizacao da animacao so pode ser escolhida aqui, porque a Unreal a prepara quando a malha e registrada.
	 */
	void Setup(const FVector& InAreaCenter, float InAreaRadius, int32 InSeed, bool bInOptimized);

	const FVector& GetAreaCenter() const { return AreaCenter; }
	float GetAreaRadius() const { return AreaRadius; }
	int32 GetSeed() const { return Seed; }
	bool IsOptimized() const { return bOptimized; }

	/** Tem malha de personagem. Sem ela, o NPC e um cilindro. */
	bool HasCharacterMesh() const { return bHasCharacterMesh; }

private:
	/** Corpo provisorio, para quando o Project Settings nao tem malha. */
	UPROPERTY(VisibleAnywhere, Category = "Benchmark")
	TObjectPtr<UStaticMeshComponent> Placeholder;

	FVector AreaCenter = FVector::ZeroVector;
	float AreaRadius = 1000.0f;
	int32 Seed = 0;
	bool bOptimized = false;
	bool bHasCharacterMesh = false;
};

/**
 * Leva o NPC a um ponto sorteado da area, espera um pouco e sorteia outro. Com NavMesh no mapa, o caminho vem
 * dele; sem NavMesh, anda em linha reta. Se o mapa nem tiver sistema de navegacao, o NPC anda por conta propria.
 */
UCLASS(NotPlaceable, Transient)
class ADeverauxBenchmarkNpcController : public AAIController
{
	GENERATED_BODY()

public:
	ADeverauxBenchmarkNpcController();

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;

	/** O pedido de caminho foi recusado, e o NPC anda em linha reta por conta propria. */
	bool IsSteeringDirectly() const { return bSteerDirectly; }

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

private:
	void MoveToRandomPoint();
	void StartSteering(const FVector& Target);
	void SchedulePause(float MinSeconds, float MaxSeconds);
	FVector PickPoint(const ADeverauxBenchmarkNpc& Npc);

	FRandomStream Random;
	FTimerHandle PauseTimer;
	FVector SteerTarget = FVector::ZeroVector;
	float SteerSecondsLeft = 0.0f;
	bool bSteerDirectly = false;
	bool bSteering = false;
};
