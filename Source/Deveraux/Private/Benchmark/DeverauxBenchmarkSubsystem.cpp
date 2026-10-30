// Projeto Deveraux - benchmark: mede o jogo sempre na mesma cena (modulo Deveraux)

#include "Benchmark/DeverauxBenchmarkSubsystem.h"

#include "Benchmark/DeverauxBenchmarkNpc.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Ceu/DeverauxSkySettings.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Math/RandomStream.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Relogio/DeverauxClockSubsystem.h"
#include "UnrealClient.h"

#if !UE_BUILD_SHIPPING

DEFINE_LOG_CATEGORY_STATIC(LogDeverauxBenchmark, Log, All);
CSV_DEFINE_CATEGORY(DeverauxBenchmark, true);

namespace DeverauxBenchmarkSubsystemPrivate
{
	constexpr int32 ProgressMessageKey = 7200;
	constexpr float ProgressMessageSeconds = 0.5f;
	constexpr float ErrorMessageSeconds = 10.0f;
	constexpr float ReportMessageSeconds = 600.0f;

	/** O crepusculo do benchmark: sol 3 graus abaixo do horizonte, com sol e lua acesos. */
	constexpr double TwilightSunElevationDegrees = -3.0;
	constexpr double FallbackTwilightHour = 18.0;

	constexpr double QuickWarmupSeconds = 2.0;
	constexpr double QuickMeasureSeconds = 3.0;

	/** Rodado na linha de comando, o benchmark espera o mapa e o jogador aparecerem ate isto. */
	constexpr double MaxWaitForWorldSeconds = 60.0;

	constexpr int32 SpawnSeed = 1234;
	constexpr int32 SpawnSeedStride = 7919;
	constexpr int32 MaxLooseNpcs = 500;
	constexpr double CentimetersPerMeter = 100.0;
	constexpr double GroundTraceUp = 5000.0;
	constexpr double GroundTraceDown = 50000.0;
	constexpr double SpawnLift = 5.0;

	struct FVariableOverride
	{
		const TCHAR* Name;
		const TCHAR* Value;
	};

	/** Sem VSync e sem limite de fps enquanto mede: so assim aparece a folga acima de 60. */
	const FVariableOverride OverriddenVariables[] = {
		{ TEXT("r.VSync"), TEXT("0") },
		{ TEXT("t.MaxFPS"), TEXT("0") },
	};

	/** O relatorio anota estes, para comparar uma rodada com outra. */
	const TCHAR* const ReportedVariables[] = {
		TEXT("r.ScreenPercentage"),
		TEXT("r.AntiAliasingMethod"),
		TEXT("r.DynamicRes.OperationMode"),
		TEXT("r.DynamicGlobalIlluminationMethod"),
		TEXT("r.Lumen.HardwareRayTracing"),
		TEXT("sg.ResolutionQuality"),
		TEXT("sg.ShadowQuality"),
		TEXT("sg.GlobalIlluminationQuality"),
		TEXT("sg.ReflectionQuality"),
		TEXT("sg.PostProcessQuality"),
		TEXT("sg.EffectsQuality"),
	};

	/** Na 5.8 o tempo da GPU do stat unit e um numero; em versoes antigas era uma lista, um por GPU. */
	template <typename T>
	float FirstValue(const T& Value)
	{
		return static_cast<float>(Value);
	}

	template <typename T, size_t N>
	float FirstValue(const T (&Values)[N])
	{
		return static_cast<float>(Values[0]);
	}

	FString VariableText(const TCHAR* Name)
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable ? Variable->GetString() : FString(TEXT("?"));
	}

	bool IsStatUnitOn(const UGameViewportClient& Viewport)
	{
		return Viewport.IsStatEnabled(TEXT("Unit")) || Viewport.IsStatEnabled(TEXT("STAT_Unit"));
	}

	void ShowMessage(int32 Key, float Seconds, const FColor& Color, const FString& Text)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(Key, Seconds, Color, Text);
		}
	}

	/** Relatorio na tela, de cima para baixo, com a ultima linha (o resultado) em verde ou vermelho. */
	void ShowReport(const TArray<FString>& Lines, bool bPassed)
	{
		if (!GEngine)
		{
			return;
		}
		GEngine->RemoveOnScreenDebugMessage(ProgressMessageKey);
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			const bool bLast = Index == Lines.Num() - 1;
			const FColor Color = bLast ? (bPassed ? FColor::Green : FColor::Red) : FColor::White;
			GEngine->AddOnScreenDebugMessage(-1, ReportMessageSeconds, Color, Lines[Index], false);
		}
	}

	void DestroyNpc(ADeverauxBenchmarkNpc& Npc)
	{
		if (AController* Controller = Npc.GetController())
		{
			Controller->Destroy();
		}
		Npc.Destroy();
	}
}

void UDeverauxBenchmarkSubsystem::Deinitialize()
{
	// O mapa esta saindo (fim do Play, troca de mapa) e leva os atores; console variables e o relogio valem
	// para o jogo inteiro e voltam agora
	if (IsRunning())
	{
		RestoreClock();
		RestoreGlobals();
		Phase = EDeverauxBenchmarkPhase::Idle;
	}
	Npcs.Reset();

	Super::Deinitialize();
}

TStatId UDeverauxBenchmarkSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDeverauxBenchmarkSubsystem, STATGROUP_Tickables);
}

bool UDeverauxBenchmarkSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UDeverauxBenchmarkSubsystem::Start(const FDeverauxBenchmarkOptions& InOptions, FString& OutError)
{
	using namespace DeverauxBenchmarkSubsystemPrivate;

	if (IsRunning())
	{
		OutError = TEXT("O benchmark ja esta rodando. Deveraux.Benchmark.Parar cancela.");
		return false;
	}
	const UDeverauxBenchmarkSettings* Settings = GetDefault<UDeverauxBenchmarkSettings>();
	if (Settings->Steps.Num() == 0)
	{
		OutError = TEXT("Nenhum passo em Project Settings > Deveraux Benchmark > Passos.");
		return false;
	}

	Options = InOptions;
	Steps = Settings->Steps;
	WarmupSeconds = Options.bQuick ? QuickWarmupSeconds : Settings->WarmupSeconds;
	MeasureSeconds = Options.bQuick ? QuickMeasureSeconds : Settings->MeasureSeconds;
	TargetFps = Settings->TargetFps;
	MinimumFps = FMath::Min(Settings->MinimumFps, Settings->TargetFps);
	StepIndex = 0;
	Samples.Reset();
	Results.Reset();
	ReportPath.Reset();
	bStatUnitMissing = false;
	bNpcMeshMissing = false;

	// Os NPCs soltos do Deveraux.Benchmark.Npcs saem; o benchmark poe os dele
	DestroyNpcs();

	// O resto comeca no proximo quadro, com o mapa e o jogador prontos
	Phase = EDeverauxBenchmarkPhase::WaitingForWorld;
	WaitEndSeconds = FPlatformTime::Seconds() + MaxWaitForWorldSeconds;
	return true;
}

void UDeverauxBenchmarkSubsystem::Stop()
{
	using namespace DeverauxBenchmarkSubsystemPrivate;

	if (!IsRunning())
	{
		return;
	}
	DestroyNpcs();
	RestoreView();
	RestoreClock();
	RestoreGlobals();
	Phase = EDeverauxBenchmarkPhase::Idle;
	if (GEngine)
	{
		GEngine->RemoveOnScreenDebugMessage(ProgressMessageKey);
	}
	UE_LOG(LogDeverauxBenchmark, Display, TEXT("Benchmark parado no passo %d de %d; nada foi gravado."), StepIndex + 1, Steps.Num());
}

bool UDeverauxBenchmarkSubsystem::SetLooseNpcs(int32 Count, bool bOptimized, FString& OutError)
{
	using namespace DeverauxBenchmarkSubsystemPrivate;

	if (IsRunning())
	{
		OutError = TEXT("O benchmark esta rodando: espere ele terminar ou use Deveraux.Benchmark.Parar.");
		return false;
	}
	const UWorld* World = GetWorld();
	const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	if (!Controller)
	{
		OutError = TEXT("Sem jogador: use o comando com o jogo rodando.");
		return false;
	}

	// Uma area nova na frente da camera quando ainda nao ha NPCs soltos; com eles, a area fica onde esta
	if (Count > 0 && Npcs.Num() == 0)
	{
		const UDeverauxBenchmarkSettings* Settings = GetDefault<UDeverauxBenchmarkSettings>();
		FVector ViewLocation = FVector::ZeroVector;
		FRotator ViewRotation = FRotator::ZeroRotator;
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
		CrowdRadius = Settings->CrowdRadiusMeters * CentimetersPerMeter;
		CrowdCenter = ViewLocation + FRotator(0.0, ViewRotation.Yaw, 0.0).Vector() * (Settings->CrowdDistanceMeters * CentimetersPerMeter);
		TraceGround(CrowdCenter, CrowdCenter);
	}
	SetNpcCount(FMath::Clamp(Count, 0, MaxLooseNpcs), bOptimized);
	return true;
}

void UDeverauxBenchmarkSubsystem::Tick(float DeltaTime)
{
	using namespace DeverauxBenchmarkSubsystemPrivate;

	if (Phase == EDeverauxBenchmarkPhase::Idle)
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();
	if (Phase == EDeverauxBenchmarkPhase::WaitingForWorld)
	{
		FString Error;
		if (!BeginRun(Error) && Now >= WaitEndSeconds)
		{
			Phase = EDeverauxBenchmarkPhase::Idle;
			UE_LOG(LogDeverauxBenchmark, Warning, TEXT("Benchmark cancelado: %s"), *Error);
			ShowMessage(ProgressMessageKey, ErrorMessageSeconds, FColor::Red, FString::Printf(TEXT("Benchmark cancelado: %s"), *Error));
		}
		return;
	}

	if (Phase == EDeverauxBenchmarkPhase::Measure)
	{
		TakeSample();
	}
	if (Now < PhaseEndSeconds)
	{
		ShowProgress(PhaseEndSeconds - Now);
		return;
	}
	if (Phase == EDeverauxBenchmarkPhase::Warmup)
	{
		Phase = EDeverauxBenchmarkPhase::Measure;
		Samples.Reset();
		PhaseEndSeconds = Now + MeasureSeconds;
		ShowProgress(MeasureSeconds);
		return;
	}
	FinishStep();
}

bool UDeverauxBenchmarkSubsystem::BeginRun(FString& OutError)
{
	using namespace DeverauxBenchmarkSubsystemPrivate;

	// Primeiro so confere; nada muda ate tudo estar pronto
	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay())
	{
		OutError = TEXT("o mapa nao comecou.");
		return false;
	}
	APlayerController* Controller = World->GetFirstPlayerController();
	UGameViewportClient* Viewport = World->GetGameViewport();
	if (!Controller || !Viewport)
	{
		OutError = TEXT("sem jogador ou sem janela de jogo.");
		return false;
	}

	const UDeverauxBenchmarkSettings* Settings = GetDefault<UDeverauxBenchmarkSettings>();

	// A cena: o primeiro Player Start do mapa; sem ele, onde o jogador esta
	FVector Origin = FVector::ZeroVector;
	double Yaw = 0.0;
	bCameraOnPlayerStart = false;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Origin = It->GetActorLocation();
		Yaw = It->GetActorRotation().Yaw;
		bCameraOnPlayerStart = true;
		break;
	}
	if (!bCameraOnPlayerStart)
	{
		if (const APawn* Pawn = Controller->GetPawn())
		{
			Origin = Pawn->GetActorLocation();
			Yaw = Pawn->GetActorRotation().Yaw;
		}
	}
	FVector Ground = Origin;
	TraceGround(Origin, Ground);

	CameraLocation = Ground + FVector(0.0, 0.0, Settings->CameraHeight);
	CameraYaw = Yaw;
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;
	ACameraActor* NewCamera = World->SpawnActor<ACameraActor>(CameraLocation, FRotator(Settings->CameraPitch, Yaw, 0.0), SpawnParameters);
	if (!NewCamera)
	{
		OutError = TEXT("nao deu para criar a camera.");
		return false;
	}
	if (UCameraComponent* CameraComponent = NewCamera->GetCameraComponent())
	{
		CameraComponent->SetFieldOfView(Settings->CameraFieldOfView);
		CameraComponent->SetConstraintAspectRatio(false);
	}
	Camera = NewCamera;

	CrowdRadius = Settings->CrowdRadiusMeters * CentimetersPerMeter;
	CrowdCenter = Ground + FRotator(0.0, Yaw, 0.0).Vector() * (Settings->CrowdDistanceMeters * CentimetersPerMeter);
	TraceGround(CrowdCenter, CrowdCenter);
	bNpcsOptimized = false;

	PlayerController = Controller;
	SavedViewTarget = Controller->GetViewTarget();
	Controller->SetViewTarget(NewCamera);

	// O jogador sai de cena: a camera fica no lugar dele e ninguem anda ate o fim
	if (APawn* Pawn = Controller->GetPawn())
	{
		HiddenPawn = Pawn;
		bSavedPawnHidden = Pawn->IsHidden();
		Pawn->SetActorHiddenInGame(true);
		Pawn->DisableInput(Controller);
	}

	if (const UDeverauxClockSubsystem* Clock = FindClock())
	{
		bSavedClockPaused = Clock->IsClockPaused();
		bPausedClock = true;
	}

	OverrideGlobals();
	bEnabledStatUnit = !IsStatUnitOn(*Viewport);
	if (bEnabledStatUnit)
	{
		Controller->ConsoleCommand(TEXT("stat unit"), false);
	}
	if (Options.bCsvProfile && GEngine)
	{
		GEngine->Exec(World, TEXT("CsvProfile Start"), *GLog);
		bStartedCsvProfile = true;
	}
	if (GEngine)
	{
		GEngine->ClearOnScreenDebugMessages();
	}

	UE_LOG(LogDeverauxBenchmark, Display, TEXT("Benchmark comecou: %d passos, %.0f s aquecendo e %.0f s medindo em cada."),
		Steps.Num(), WarmupSeconds, MeasureSeconds);
	BeginStep();
	return true;
}

void UDeverauxBenchmarkSubsystem::BeginStep()
{
	using namespace DeverauxBenchmarkSubsystemPrivate;

	const FDeverauxBenchmarkStep& Step = Steps[StepIndex];
	double Hour = FMath::Clamp(static_cast<double>(Step.Hour), 0.0, 24.0);
	if (Step.bTwilight)
	{
		// Sem crepusculo (dia ou noite sem fim, perto dos polos), fica as 18:00
		Hour = FallbackTwilightHour;
		DeverauxBenchmark::FindEveningSunHour(GetDefault<UDeverauxSkySettings>()->MakeSunPathParams(), TwilightSunElevationDegrees, Hour);
	}

	// Minuto inteiro, como o relogio mostra; o relogio para nessa hora ate o passo acabar
	CurrentHour = FMath::RoundToInt(static_cast<float>(Hour * 60.0)) / 60.0;
	if (UDeverauxClockSubsystem* Clock = FindClock())
	{
		Clock->GetClock().AdvanceToTimeOfDay(FTimespan::FromMinutes(CurrentHour * 60.0));
		Clock->SetClockPaused(true);
	}

	SetNpcCount(Step.NpcCount, Step.bOptimizedNpcs);
	Samples.Reset();
	Phase = EDeverauxBenchmarkPhase::Warmup;
	PhaseEndSeconds = FPlatformTime::Seconds() + WarmupSeconds;
	CSV_EVENT(DeverauxBenchmark, TEXT("Passo %d"), StepIndex + 1);
	ShowProgress(WarmupSeconds);
}

void UDeverauxBenchmarkSubsystem::FinishStep()
{
	const FDeverauxBenchmarkStep& Step = Steps[StepIndex];
	FDeverauxStepResult& Result = Results.AddDefaulted_GetRef();
	Result.Hour = CurrentHour;
	Result.bTwilight = Step.bTwilight;
	Result.NpcCount = Npcs.Num();
	Result.bOptimizedNpcs = Step.bOptimizedNpcs;
	Result.bCountsForResult = Step.bCountsForResult;
	DeverauxBenchmark::Summarize(Samples, Result);
	UE_LOG(LogDeverauxBenchmark, Display, TEXT("%s"), *DeverauxBenchmark::BuildTable(Results, TargetFps, MinimumFps).Last());

	++StepIndex;
	if (StepIndex < Steps.Num())
	{
		BeginStep();
	}
	else
	{
		FinishRun();
	}
}

void UDeverauxBenchmarkSubsystem::FinishRun()
{
	using namespace DeverauxBenchmarkSubsystemPrivate;

	// O contexto le as console variables e os NPCs antes de tudo voltar ao normal
	const FString Context = DescribeContext();
	DestroyNpcs();
	RestoreView();
	RestoreClock();
	RestoreGlobals();
	Phase = EDeverauxBenchmarkPhase::Idle;

	bool bPassed = true;
	const TArray<FString> Table = DeverauxBenchmark::BuildTable(Results, TargetFps, MinimumFps);
	const TArray<FString> Summary = DeverauxBenchmark::BuildSummary(Results, TargetFps, MinimumFps, bPassed);

	FString Report = Context;
	for (const FString& Line : Table)
	{
		Report += Line + LINE_TERMINATOR;
	}
	Report += LINE_TERMINATOR;
	for (const FString& Line : Summary)
	{
		Report += Line + LINE_TERMINATOR;
	}

	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Benchmark");
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString BaseName = FString::Printf(TEXT("Benchmark-%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
	const FString TextPath = FPaths::ConvertRelativePathToFull(Directory / (BaseName + TEXT(".txt")));
	const FString CsvPath = FPaths::ConvertRelativePathToFull(Directory / (BaseName + TEXT(".csv")));
	const bool bSaved = FFileHelper::SaveStringToFile(Report, *TextPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
		&& FFileHelper::SaveStringToFile(DeverauxBenchmark::BuildCsv(Results, TargetFps, MinimumFps), *CsvPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	ReportPath = bSaved ? TextPath : FString();

	UE_LOG(LogDeverauxBenchmark, Display, TEXT("Benchmark terminado.%s%s"), LINE_TERMINATOR, *Report);
	TArray<FString> Screen;
	Screen.Add(bSaved
		? FString::Printf(TEXT("Benchmark terminado. Relatorio: %s"), *TextPath)
		: FString(TEXT("Benchmark terminado, mas nao deu para gravar o relatorio em Saved/Benchmark.")));
	Screen.Append(Table);
	Screen.Append(Summary);
	ShowReport(Screen, bPassed);

	if (Options.bQuitWhenDone)
	{
		if (GIsEditor)
		{
			UE_LOG(LogDeverauxBenchmark, Display, TEXT("Benchmark: a opcao sair so fecha o jogo empacotado, nao o editor."));
		}
		else
		{
			FPlatformMisc::RequestExitWithStatus(false, static_cast<uint8>(bPassed ? 0 : 1), TEXT("Deveraux.Benchmark"));
		}
	}
}

void UDeverauxBenchmarkSubsystem::TakeSample()
{
	using namespace DeverauxBenchmarkSubsystemPrivate;

	const UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	const FStatUnitData* StatUnit = Viewport ? Viewport->GetStatUnitData() : nullptr;

	FDeverauxFrameSample Sample;
	if (StatUnit && FirstValue(StatUnit->RawFrameTime) > 0.0f)
	{
		Sample.FrameMs = FirstValue(StatUnit->RawFrameTime);
		Sample.GameThreadMs = FirstValue(StatUnit->RawGameThreadTime);
		Sample.RenderThreadMs = FirstValue(StatUnit->RawRenderThreadTime);
		Sample.RhiThreadMs = FirstValue(StatUnit->RawRHITTime);
		Sample.GpuMs = FirstValue(StatUnit->RawGPUFrameTime);
	}
	else
	{
		// Sem stat unit, so o tempo do quadro, pelo relogio da engine
		bStatUnitMissing = true;
		Sample.FrameMs = static_cast<float>(FApp::GetDeltaTime() * 1000.0);
	}
	Samples.Add(Sample);
}

void UDeverauxBenchmarkSubsystem::ShowProgress(double SecondsLeft) const
{
	using namespace DeverauxBenchmarkSubsystemPrivate;

	if (!Steps.IsValidIndex(StepIndex))
	{
		return;
	}
	const FDeverauxBenchmarkStep& Step = Steps[StepIndex];
	FDeverauxStepResult Preview;
	Preview.Hour = CurrentHour;
	Preview.bTwilight = Step.bTwilight;
	Preview.NpcCount = Npcs.Num();
	Preview.bOptimizedNpcs = Step.bOptimizedNpcs;
	ShowMessage(ProgressMessageKey, ProgressMessageSeconds, FColor::Cyan, FString::Printf(TEXT("Benchmark %d/%d: %s | %s, %d s | Deveraux.Benchmark.Parar cancela"),
		StepIndex + 1, Steps.Num(), *Preview.Describe(), Phase == EDeverauxBenchmarkPhase::Warmup ? TEXT("aquecendo") : TEXT("medindo"),
		FMath::CeilToInt(static_cast<float>(SecondsLeft))));
}

void UDeverauxBenchmarkSubsystem::SetNpcCount(int32 Count, bool bOptimized)
{
	using namespace DeverauxBenchmarkSubsystemPrivate;

	Npcs.RemoveAll([](const TWeakObjectPtr<ADeverauxBenchmarkNpc>& Npc) { return !Npc.IsValid(); });

	// A otimizacao da animacao so vale para quem nasce com ela: trocar refaz todos
	if (bOptimized != bNpcsOptimized)
	{
		DestroyNpcs();
		bNpcsOptimized = bOptimized;
	}
	while (Npcs.Num() > Count)
	{
		if (ADeverauxBenchmarkNpc* Npc = Npcs.Last().Get())
		{
			DestroyNpc(*Npc);
		}
		Npcs.Pop();
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const bool bWantsMesh = !GetDefault<UDeverauxBenchmarkSettings>()->NpcMesh.IsNull();
	for (int32 Index = Npcs.Num(); Index < Count; ++Index)
	{
		const FTransform Transform = NpcSpawnTransform(Index);
		ADeverauxBenchmarkNpc* Npc = World->SpawnActorDeferred<ADeverauxBenchmarkNpc>(ADeverauxBenchmarkNpc::StaticClass(), Transform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!Npc)
		{
			break;
		}
		Npc->Setup(CrowdCenter, static_cast<float>(CrowdRadius), SpawnSeed + Index, bOptimized);
		Npc->FinishSpawning(Transform);
		if (bWantsMesh && !Npc->HasCharacterMesh())
		{
			bNpcMeshMissing = true;
		}
		Npcs.Add(Npc);
	}
}

void UDeverauxBenchmarkSubsystem::DestroyNpcs()
{
	for (const TWeakObjectPtr<ADeverauxBenchmarkNpc>& Weak : Npcs)
	{
		if (ADeverauxBenchmarkNpc* Npc = Weak.Get())
		{
			DeverauxBenchmarkSubsystemPrivate::DestroyNpc(*Npc);
		}
	}
	Npcs.Reset();
}

FTransform UDeverauxBenchmarkSubsystem::NpcSpawnTransform(int32 Index) const
{
	using namespace DeverauxBenchmarkSubsystemPrivate;

	// O mesmo lugar para o mesmo numero de NPC: rodadas diferentes medem a mesma cena
	FRandomStream Random(SpawnSeed + Index * SpawnSeedStride);
	const float Angle = Random.FRandRange(0.0f, UE_TWO_PI);
	const double Distance = CrowdRadius * FMath::Sqrt(Random.FRand());
	FVector Location = CrowdCenter + FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.0);
	FVector Ground = Location;
	if (TraceGround(Location, Ground))
	{
		Location = Ground;
	}
	Location.Z += GetDefault<ADeverauxBenchmarkNpc>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + SpawnLift;
	return FTransform(FRotator(0.0, Random.FRandRange(-180.0f, 180.0f), 0.0), Location);
}

bool UDeverauxBenchmarkSubsystem::TraceGround(const FVector& Location, FVector& OutGround) const
{
	using namespace DeverauxBenchmarkSubsystemPrivate;

	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// So o que e cenario (WorldStatic): NPCs e o jogador nao contam como chao
	FHitResult Hit;
	const FCollisionQueryParams Params(FName(TEXT("DeverauxBenchmarkChao")), false);
	if (World->LineTraceSingleByObjectType(Hit, Location + FVector(0.0, 0.0, GroundTraceUp), Location - FVector(0.0, 0.0, GroundTraceDown),
		FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		OutGround = Hit.ImpactPoint;
		return true;
	}
	return false;
}

void UDeverauxBenchmarkSubsystem::OverrideGlobals()
{
	using namespace DeverauxBenchmarkSubsystemPrivate;

	SavedVariables.Reset();
	for (const FVariableOverride& Override : OverriddenVariables)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Override.Name))
		{
			SavedVariables.Add(TPair<FString, FString>(FString(Override.Name), Variable->GetString()));
			Variable->Set(Override.Value, ECVF_SetByConsole);
		}
	}
	if (GEngine)
	{
		bSavedSmoothFrameRate = GEngine->bSmoothFrameRate != 0;
		GEngine->bSmoothFrameRate = false;
	}
	bOverrodeGlobals = true;
}

void UDeverauxBenchmarkSubsystem::RestoreGlobals()
{
	if (!bOverrodeGlobals)
	{
		return;
	}
	for (const TPair<FString, FString>& Saved : SavedVariables)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Saved.Key))
		{
			Variable->Set(*Saved.Value, ECVF_SetByConsole);
		}
	}
	SavedVariables.Reset();
	if (GEngine)
	{
		GEngine->bSmoothFrameRate = bSavedSmoothFrameRate;
		if (bStartedCsvProfile)
		{
			GEngine->Exec(GetWorld(), TEXT("CsvProfile Stop"), *GLog);
		}
	}
	bStartedCsvProfile = false;
	bOverrodeGlobals = false;
}

void UDeverauxBenchmarkSubsystem::RestoreView()
{
	using namespace DeverauxBenchmarkSubsystemPrivate;

	APlayerController* Controller = PlayerController.Get();
	ACameraActor* CameraActor = Camera.Get();
	if (Controller)
	{
		AActor* Target = SavedViewTarget.Get();
		if (!Target || Target == CameraActor)
		{
			Target = Controller->GetPawn();
		}
		Controller->SetViewTarget(Target ? Target : static_cast<AActor*>(Controller));

		// O stat unit so sai se foi o benchmark que ligou e se ainda esta ligado
		const UWorld* World = GetWorld();
		UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
		if (bEnabledStatUnit && Viewport && IsStatUnitOn(*Viewport))
		{
			Controller->ConsoleCommand(TEXT("stat unit"), false);
		}
	}
	bEnabledStatUnit = false;

	if (CameraActor)
	{
		CameraActor->Destroy();
	}
	Camera.Reset();

	if (APawn* Pawn = HiddenPawn.Get())
	{
		Pawn->SetActorHiddenInGame(bSavedPawnHidden);
		if (Controller)
		{
			Pawn->EnableInput(Controller);
		}
	}
	HiddenPawn.Reset();
	PlayerController.Reset();
	SavedViewTarget.Reset();
}

void UDeverauxBenchmarkSubsystem::RestoreClock()
{
	if (bPausedClock)
	{
		if (UDeverauxClockSubsystem* Clock = FindClock())
		{
			Clock->SetClockPaused(bSavedClockPaused);
		}
		bPausedClock = false;
	}
}

UDeverauxClockSubsystem* UDeverauxBenchmarkSubsystem::FindClock() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UDeverauxClockSubsystem>() : nullptr;
}

FString UDeverauxBenchmarkSubsystem::DescribeContext() const
{
	using namespace DeverauxBenchmarkSubsystemPrivate;

	const UWorld* World = GetWorld();
	const UDeverauxBenchmarkSettings* Settings = GetDefault<UDeverauxBenchmarkSettings>();
	TArray<FString> Lines;

	Lines.Add(FString::Printf(TEXT("Deveraux benchmark, %s"), *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"))));
	Lines.Add(FString::Printf(TEXT("Mapa %s | %s, %s | Unreal %s"), World ? *World->GetMapName() : TEXT("?"),
		GIsEditor ? TEXT("no editor") : TEXT("jogo empacotado"), LexToString(FApp::GetBuildConfiguration()), *FEngineVersion::Current().ToString()));
	Lines.Add(FString::Printf(TEXT("CPU %s (%d threads) | GPU %s | RAM %u GB"), *FPlatformMisc::GetCPUBrand().TrimStartAndEnd(),
		FPlatformMisc::NumberOfCoresIncludingHyperthreads(), *FPlatformMisc::GetPrimaryGPUBrand(), FPlatformMemory::GetConstants().TotalPhysicalGB));

	FVector2D ViewportSize(0.0, 0.0);
	if (const UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr)
	{
		Viewport->GetViewportSize(ViewportSize);
	}
	FString Variables;
	for (const TCHAR* Name : ReportedVariables)
	{
		Variables += FString::Printf(TEXT(" | %s %s"), Name, *VariableText(Name));
	}
	Lines.Add(FString::Printf(TEXT("Tela %.0fx%.0f%s"), ViewportSize.X, ViewportSize.Y, *Variables));

	FString Overrides;
	for (const TPair<FString, FString>& Saved : SavedVariables)
	{
		Overrides += FString::Printf(TEXT("%s%s %s (era %s)"), Overrides.IsEmpty() ? TEXT("") : TEXT(", "), *Saved.Key, *VariableText(*Saved.Key), *Saved.Value);
	}
	if (!Overrides.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("Durante a medida: %s"), *Overrides));
	}

	Lines.Add(FString::Printf(TEXT("Meta %.0f fps, minimo %.0f fps | %.0f s aquecendo e %.0f s medindo em cada passo%s"), TargetFps, MinimumFps,
		WarmupSeconds, MeasureSeconds, Options.bQuick ? TEXT(" (rapido: so para conferir, nao vale como medida)") : TEXT("")));
	Lines.Add(FString::Printf(TEXT("Camera %s em (%.0f, %.0f, %.0f), olhando para %.0f graus | multidao a %.0f m, raio de %.0f m"),
		bCameraOnPlayerStart ? TEXT("no Player Start") : TEXT("onde o jogador estava"), CameraLocation.X, CameraLocation.Y, CameraLocation.Z,
		CameraYaw, Settings->CrowdDistanceMeters, Settings->CrowdRadiusMeters));

	if (Settings->NpcMesh.IsNull())
	{
		Lines.Add(TEXT("NPCs: cilindros, sem malha em Project Settings > Deveraux Benchmark (so o movimento conta)"));
	}
	else
	{
		Lines.Add(FString::Printf(TEXT("NPCs: %s com %s"), *Settings->NpcMesh.GetAssetName(),
			Settings->NpcAnimClass.IsNull() ? TEXT("nenhuma animacao") : *Settings->NpcAnimClass.GetAssetName()));
	}
	if (bNpcMeshMissing)
	{
		Lines.Add(TEXT("Aviso: a malha dos NPCs nao carregou e eles viraram cilindros. No jogo empacotado, a pasta dela precisa estar em Additional Asset Directories to Cook."));
	}
	for (const TWeakObjectPtr<ADeverauxBenchmarkNpc>& Weak : Npcs)
	{
		const ADeverauxBenchmarkNpc* Npc = Weak.Get();
		const ADeverauxBenchmarkNpcController* NpcController = Npc ? Cast<ADeverauxBenchmarkNpcController>(Npc->GetController()) : nullptr;
		if (NpcController && NpcController->IsSteeringDirectly())
		{
			Lines.Add(TEXT("Aviso: o mapa nao tem sistema de navegacao, e os NPCs andaram em linha reta por conta propria."));
			break;
		}
	}
	if (bStatUnitMissing)
	{
		Lines.Add(TEXT("Aviso: o stat unit nao respondeu; so o tempo do quadro foi medido."));
	}
	if (GIsEditor)
	{
		Lines.Add(TEXT("Aviso: no editor os tempos saem maiores que no jogo. Para valer, rode no jogo empacotado."));
	}

	return FString::Join(Lines, LINE_TERMINATOR) + LINE_TERMINATOR + LINE_TERMINATOR;
}

// Comandos de console
namespace DeverauxBenchmarkSubsystemPrivate
{
	UDeverauxBenchmarkSubsystem* FindBenchmark(UWorld* World, FOutputDevice& Ar)
	{
		UDeverauxBenchmarkSubsystem* Benchmark = World ? World->GetSubsystem<UDeverauxBenchmarkSubsystem>() : nullptr;
		if (!Benchmark)
		{
			Ar.Log(TEXT("Benchmark indisponivel: use o comando com o jogo rodando (Play ou jogo empacotado)."));
		}
		return Benchmark;
	}

	bool HasOption(const TArray<FString>& Args, const TCHAR* Option)
	{
		for (const FString& Arg : Args)
		{
			if (Arg.Equals(Option, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	void ReportError(const FString& Error, FOutputDevice& Ar)
	{
		Ar.Log(*Error);
		ShowMessage(ProgressMessageKey, ErrorMessageSeconds, FColor::Red, Error);
	}

	void StartCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UDeverauxBenchmarkSubsystem* Benchmark = FindBenchmark(World, Ar);
		if (!Benchmark)
		{
			return;
		}
		FDeverauxBenchmarkOptions CommandOptions;
		CommandOptions.bQuitWhenDone = HasOption(Args, TEXT("sair"));
		CommandOptions.bQuick = HasOption(Args, TEXT("rapido"));
		CommandOptions.bCsvProfile = HasOption(Args, TEXT("csv"));
		FString Error;
		if (Benchmark->Start(CommandOptions, Error))
		{
			Ar.Log(TEXT("Benchmark comecando. Nao mexa no jogo ate o fim; Deveraux.Benchmark.Parar cancela."));
		}
		else
		{
			ReportError(Error, Ar);
		}
	}

	void StopCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UDeverauxBenchmarkSubsystem* Benchmark = FindBenchmark(World, Ar);
		if (!Benchmark)
		{
			return;
		}
		if (!Benchmark->IsRunning())
		{
			Ar.Log(TEXT("Nenhum benchmark rodando."));
			return;
		}
		Benchmark->Stop();
		Ar.Log(TEXT("Benchmark parado; nada foi gravado."));
	}

	void NpcsCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		UDeverauxBenchmarkSubsystem* Benchmark = FindBenchmark(World, Ar);
		if (!Benchmark)
		{
			return;
		}
		if (Args.Num() == 0 || !Args[0].IsNumeric())
		{
			Ar.Log(TEXT("Uso: Deveraux.Benchmark.Npcs <quantos> [otimizado]. 0 tira todos."));
			return;
		}
		FString Error;
		if (!Benchmark->SetLooseNpcs(FCString::Atoi(*Args[0]), HasOption(Args, TEXT("otimizado")), Error))
		{
			ReportError(Error, Ar);
			return;
		}
		Ar.Logf(TEXT("%d NPCs andando na frente da camera."), Benchmark->GetNpcCount());
	}
}

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GDeverauxBenchmarkCommand(
	TEXT("Deveraux.Benchmark"),
	TEXT("Mede o jogo na mesma cena, passo a passo (Project Settings > Deveraux Benchmark). Opcoes: rapido (so confere), csv (grava o CSV da Unreal), sair (fecha o jogo no fim)."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&DeverauxBenchmarkSubsystemPrivate::StartCommand),
	ECVF_Default);

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GDeverauxBenchmarkPararCommand(
	TEXT("Deveraux.Benchmark.Parar"),
	TEXT("Para o benchmark no meio e volta tudo como estava, sem gravar relatorio."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&DeverauxBenchmarkSubsystemPrivate::StopCommand),
	ECVF_Default);

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GDeverauxBenchmarkNpcsCommand(
	TEXT("Deveraux.Benchmark.Npcs"),
	TEXT("Deveraux.Benchmark.Npcs <n> [otimizado]: poe n NPCs andando na frente da camera, sem medir nada. 0 tira todos."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&DeverauxBenchmarkSubsystemPrivate::NpcsCommand),
	ECVF_Default);

#else // UE_BUILD_SHIPPING

// Na versao final o benchmark nao existe: o subsistema nem e criado

void UDeverauxBenchmarkSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

TStatId UDeverauxBenchmarkSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDeverauxBenchmarkSubsystem, STATGROUP_Tickables);
}

bool UDeverauxBenchmarkSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return false;
}

void UDeverauxBenchmarkSubsystem::Tick(float DeltaTime)
{
}

bool UDeverauxBenchmarkSubsystem::Start(const FDeverauxBenchmarkOptions& InOptions, FString& OutError)
{
	OutError = TEXT("O benchmark nao existe na versao final (Shipping).");
	return false;
}

void UDeverauxBenchmarkSubsystem::Stop()
{
}

bool UDeverauxBenchmarkSubsystem::SetLooseNpcs(int32 Count, bool bOptimized, FString& OutError)
{
	OutError = TEXT("O benchmark nao existe na versao final (Shipping).");
	return false;
}

#endif // UE_BUILD_SHIPPING
