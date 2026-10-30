// Projeto Deveraux - benchmark: mede o jogo sempre na mesma cena (modulo Deveraux)

#pragma once

#include "Benchmark/DeverauxBenchmarkSettings.h"
#include "Benchmark/DeverauxBenchmarkStats.h"
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "DeverauxBenchmarkSubsystem.generated.h"

class AActor;
class ACameraActor;
class ADeverauxBenchmarkNpc;
class APawn;
class APlayerController;
class UDeverauxClockSubsystem;

/** Opcoes do comando Deveraux.Benchmark. */
struct FDeverauxBenchmarkOptions
{
	/** Fecha o jogo no fim, com codigo 0 se passou e 1 se nao: para rodar sozinho, por linha de comando. */
	bool bQuitWhenDone = false;

	/** Passos curtos, para conferir se tudo funciona antes da medida de verdade. */
	bool bQuick = false;

	/** Grava tambem o CSV da Unreal (CsvProfile), com cada passo marcado, em Saved/Profiling/CSV. */
	bool bCsvProfile = false;
};

enum class EDeverauxBenchmarkPhase : uint8
{
	Idle,
	WaitingForWorld,
	Warmup,
	Measure
};

/**
 * Benchmark da Fase 0: camera fixa no Player Start, relogio parado em horas fixas e NPCs andando na frente,
 * passo a passo, como Project Settings > Deveraux Benchmark manda. Mede o que o stat unit mede (quadro, game
 * thread, render thread, RHI e GPU), mostra na tela e grava em Saved/Benchmark. Comando: Deveraux.Benchmark.
 * Na versao final (Shipping) nao existe.
 */
UCLASS()
class UDeverauxBenchmarkSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Comeca o benchmark. false, com o motivo em OutError, se nao da para comecar agora. */
	bool Start(const FDeverauxBenchmarkOptions& InOptions, FString& OutError);

	/** Para no meio, volta tudo como estava e nao grava relatorio. */
	void Stop();

	bool IsRunning() const { return Phase != EDeverauxBenchmarkPhase::Idle; }

	/** Poe NPCs andando na frente da camera do jogador, sem benchmark, para olhar com calma. 0 tira todos. */
	bool SetLooseNpcs(int32 Count, bool bOptimized, FString& OutError);

	/** NPCs vivos. Um que caiu do mundo (Kill Z) ou foi destruido por outro motivo nao conta. */
	int32 GetNpcCount() const;

	const TArray<FDeverauxStepResult>& GetResults() const { return Results; }

	/** Arquivo do ultimo relatorio gravado, vazio se nenhum. */
	const FString& GetReportPath() const { return ReportPath; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	bool BeginRun(FString& OutError);
	void BeginStep();
	void FinishStep();
	void FinishRun();
	void TakeSample();
	void ShowProgress(double SecondsLeft) const;

	void SetNpcCount(int32 Count, bool bOptimized);
	void DestroyNpcs();
	FTransform NpcSpawnTransform(int32 Index) const;

	/** Chao debaixo do ponto, num raio que sai de AboveCm acima dele (e de bem mais alto, se esse nao achar). false sem chao. */
	bool TraceGround(const FVector& Location, double AboveCm, FVector& OutGround) const;

	void OverrideGlobals();
	void RestoreGlobals();
	void RestoreView();
	void RestoreClock();
	UDeverauxClockSubsystem* FindClock() const;

	/** O que pode fazer a medida nao valer, uma linha "Aviso: ..." cada, para o relatorio e para a tela. */
	TArray<FString> CollectWarnings() const;

	/** Cabecalho do relatorio: maquina, ajustes, cena, ceu e os avisos no fim. */
	FString DescribeContext(const TArray<FString>& Warnings) const;

	EDeverauxBenchmarkPhase Phase = EDeverauxBenchmarkPhase::Idle;
	FDeverauxBenchmarkOptions Options;
	TArray<FDeverauxBenchmarkStep> Steps;
	double WarmupSeconds = 0.0;
	double MeasureSeconds = 0.0;
	double TargetFps = 60.0;
	double MinimumFps = 55.0;
	int32 StepIndex = 0;
	double PhaseEndSeconds = 0.0;
	double WaitEndSeconds = 0.0;
	double CurrentHour = 0.0;
	TArray<FDeverauxFrameSample> Samples;
	TArray<FDeverauxStepResult> Results;
	bool bStatUnitMissing = false;
	FString ReportPath;

	// O que o benchmark mudou, para devolver no fim
	TArray<TPair<FString, FString>> SavedVariables;
	bool bOverrodeGlobals = false;
	bool bSavedSmoothFrameRate = false;
	bool bEnabledStatUnit = false;
	bool bStartedCsvProfile = false;
	bool bPausedClock = false;
	bool bSavedClockPaused = false;
	bool bSavedPawnHidden = false;
	bool bSavedPawnInput = false;
	bool bSunTestWasOn = false;
	TWeakObjectPtr<APlayerController> PlayerController;
	TWeakObjectPtr<AActor> SavedViewTarget;
	TWeakObjectPtr<APawn> HiddenPawn;
	TWeakObjectPtr<ACameraActor> Camera;
	FVector CameraLocation = FVector::ZeroVector;
	double CameraYaw = 0.0;
	bool bCameraOnPlayerStart = false;

	// NPCs andando na frente da camera
	TArray<TWeakObjectPtr<ADeverauxBenchmarkNpc>> Npcs;
	bool bNpcsOptimized = false;
	bool bNpcMeshMissing = false;
	FVector CrowdCenter = FVector::ZeroVector;
	double CrowdRadius = 1000.0;
};
