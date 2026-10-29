// Projeto Deveraux - relogio do jogo enquanto o jogo roda (modulo Deveraux)

#pragma once

#include "CoreMinimal.h"
#include "DeverauxGameClock.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"

#include "DeverauxClockSubsystem.generated.h"

/** Disparado a cada minuto de jogo, com o total de minutos desde o dia 1 as 00:00. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDeverauxOnGameMinute, int64, TotalMinutes);

/**
 * Dono do relogio do jogo enquanto o jogo roda. Vive na GameInstance, entao sobrevive a troca de mapa.
 * Anda com o tempo do mundo: para quando o jogo e pausado e acompanha a dilatacao de tempo (slomo).
 *
 * Em C++: GetGameInstance()->GetSubsystem<UDeverauxClockSubsystem>()->GetClock().
 * Em Blueprint: no "Get DeverauxClockSubsystem" e as funcoes da categoria Deveraux | Relogio.
 */
UCLASS()
class DEVERAUX_API UDeverauxClockSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickable() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;
	virtual TStatId GetStatId() const override;

	/** O relogio, para os outros sistemas em C++. */
	FDeverauxGameClock& GetClock() { return Clock; }
	const FDeverauxGameClock& GetClock() const { return Clock; }

	/** Dia de jogo, comecando em 1. */
	UFUNCTION(BlueprintPure, Category = "Deveraux|Relogio")
	int32 GetDay() const { return Clock.GetDay(); }

	/** Hora do dia, de 0 a 23. */
	UFUNCTION(BlueprintPure, Category = "Deveraux|Relogio")
	int32 GetHour() const { return Clock.GetHour(); }

	/** Minuto da hora, de 0 a 59. */
	UFUNCTION(BlueprintPure, Category = "Deveraux|Relogio")
	int32 GetMinute() const { return Clock.GetMinute(); }

	/** Minutos de jogo desde o dia 1 as 00:00. */
	UFUNCTION(BlueprintPure, Category = "Deveraux|Relogio")
	int64 GetTotalMinutes() const { return Clock.GetTotalMinutes(); }

	/** Fracao do dia, de 0 (meia-noite) ate quase 1. Para girar o sol. */
	UFUNCTION(BlueprintPure, Category = "Deveraux|Relogio")
	double GetTimeOfDayFraction() const { return Clock.GetTimeOfDayFraction(); }

	/** "Dia 1, 08:00", para depuracao. O texto traduzido do jogo vem depois, na interface. */
	UFUNCTION(BlueprintPure, Category = "Deveraux|Relogio")
	FString GetClockDebugText() const { return Clock.ToDebugString(); }

	UFUNCTION(BlueprintPure, Category = "Deveraux|Relogio")
	bool IsClockPaused() const { return Clock.IsPaused(); }

	/** Pausa so o relogio; o mundo continua rodando. A batalha usa isto e avanca o tempo por acao. */
	UFUNCTION(BlueprintCallable, Category = "Deveraux|Relogio")
	void SetClockPaused(bool bPaused) { Clock.SetPaused(bPaused); }

	/** Avanca minutos de jogo de uma vez, mesmo com o relogio pausado. */
	UFUNCTION(BlueprintCallable, Category = "Deveraux|Relogio")
	void AdvanceMinutes(int32 Minutes) { Clock.AdvanceGameMinutes(Minutes); }

	/** Disparado a cada minuto de jogo. */
	UPROPERTY(BlueprintAssignable, Category = "Deveraux|Relogio")
	FDeverauxOnGameMinute OnGameMinute;

private:
	void HandleMinutePassed(int64 TotalMinutes);

	FDeverauxGameClock Clock;
	bool bClockInitialized = false;
};
