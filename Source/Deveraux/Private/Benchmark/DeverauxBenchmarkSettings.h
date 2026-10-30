// Projeto Deveraux - configuracao do benchmark (Project Settings > Game > Deveraux Benchmark)

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPtr.h"

#include "DeverauxBenchmarkSettings.generated.h"

class UAnimInstance;
class USkeletalMesh;

/** Um passo do benchmark: a hora do ceu e quantos NPCs andam na frente da camera. */
USTRUCT()
struct FDeverauxBenchmarkStep
{
	GENERATED_BODY()

	/** Hora do dia, de 0 a 24 (18,5 = 18:30). Ignorada no crepusculo. */
	UPROPERTY(EditAnywhere, Category = "Passo", meta = (ClampMin = "0", ClampMax = "24", EditCondition = "!bTwilight"))
	float Hour = 12.0f;

	/** Fim da tarde, com o sol 3 graus abaixo do horizonte: sol e lua acesos, a hora mais cara para o ceu. */
	UPROPERTY(EditAnywhere, Category = "Passo")
	bool bTwilight = false;

	UPROPERTY(EditAnywhere, Category = "Passo", meta = (ClampMin = "0", ClampMax = "500"))
	int32 NpcCount = 0;

	/** NPCs com as otimizacoes de multidao da Unreal: animacao em menos quadros quando longe (URO) e parada quando ninguem ve. */
	UPROPERTY(EditAnywhere, Category = "Passo")
	bool bOptimizedNpcs = false;

	/** Conta para o resultado: se este passo ficar abaixo do minimo de fps, o benchmark reprova. */
	UPROPERTY(EditAnywhere, Category = "Passo")
	bool bCountsForResult = true;

	static FDeverauxBenchmarkStep Make(float InHour, bool bInTwilight, int32 InNpcCount, bool bInOptimizedNpcs, bool bInCountsForResult)
	{
		FDeverauxBenchmarkStep Step;
		Step.Hour = InHour;
		Step.bTwilight = bInTwilight;
		Step.NpcCount = InNpcCount;
		Step.bOptimizedNpcs = bInOptimizedNpcs;
		Step.bCountsForResult = bInCountsForResult;
		return Step;
	}
};

/**
 * Benchmark do Projeto Deveraux: a mesma cena sempre, para comparar builds e fechar os gates do roadmap.
 * Rode com Deveraux.Benchmark no console do jogo. Editado em Project Settings, fica no Config/DefaultGame.ini.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Deveraux Benchmark"))
class UDeverauxBenchmarkSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDeverauxBenchmarkSettings()
	{
		// Ceu sozinho em tres horas, depois a multidao subindo ao meio-dia e, por ultimo, o caso tipico
		// (40 NPCs) na hora mais cara do ceu. De 60 NPCs para cima so informa: e o que o spike de densidade mede
		using FStep = FDeverauxBenchmarkStep;
		Steps.Add(FStep::Make(12.0f, false, 0, false, true));
		Steps.Add(FStep::Make(12.0f, true, 0, false, true));
		Steps.Add(FStep::Make(0.0f, false, 0, false, true));
		Steps.Add(FStep::Make(12.0f, false, 10, false, true));
		Steps.Add(FStep::Make(12.0f, false, 20, false, true));
		Steps.Add(FStep::Make(12.0f, false, 40, false, true));
		Steps.Add(FStep::Make(12.0f, false, 60, false, false));
		Steps.Add(FStep::Make(12.0f, false, 80, false, false));
		Steps.Add(FStep::Make(12.0f, false, 100, false, false));
		Steps.Add(FStep::Make(12.0f, false, 100, true, false));
		Steps.Add(FStep::Make(12.0f, true, 40, false, true));
	}

	/** Meta de quadros por segundo. Na meta ou acima, o passo fica "ok". */
	UPROPERTY(Config, EditAnywhere, Category = "Meta", meta = (ClampMin = "10", ClampMax = "240"))
	float TargetFps = 60.0f;

	/** Minimo aceito. Entre ele e a meta, o passo e "toleravel"; abaixo, "estouro". */
	UPROPERTY(Config, EditAnywhere, Category = "Meta", meta = (ClampMin = "10", ClampMax = "240"))
	float MinimumFps = 55.0f;

	/** Segundos de espera depois de mudar de passo, para sombras, exposicao e shaders assentarem. */
	UPROPERTY(Config, EditAnywhere, Category = "Tempo", meta = (ClampMin = "1", ClampMax = "60"))
	float WarmupSeconds = 4.0f;

	/** Segundos medindo cada passo. */
	UPROPERTY(Config, EditAnywhere, Category = "Tempo", meta = (ClampMin = "2", ClampMax = "120"))
	float MeasureSeconds = 8.0f;

	/** Os passos, em ordem. O relogio so anda para frente, entao cada hora e a proxima vez que ela chega. */
	UPROPERTY(Config, EditAnywhere, Category = "Passos")
	TArray<FDeverauxBenchmarkStep> Steps;

	/** Altura da camera acima do chao, em cm, no primeiro Player Start do mapa. */
	UPROPERTY(Config, EditAnywhere, Category = "Camera", meta = (ClampMin = "50", ClampMax = "10000"))
	float CameraHeight = 170.0f;

	/** Inclinacao da camera, em graus (negativo = para baixo). 0 poe o horizonte no meio da tela. */
	UPROPERTY(Config, EditAnywhere, Category = "Camera", meta = (ClampMin = "-89", ClampMax = "89"))
	float CameraPitch = -8.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Camera", meta = (ClampMin = "30", ClampMax = "120"))
	float CameraFieldOfView = 90.0f;

	/** Distancia da camera ate o centro da multidao, em m, na direcao do Player Start. */
	UPROPERTY(Config, EditAnywhere, Category = "NPCs", meta = (ClampMin = "2", ClampMax = "500"))
	float CrowdDistanceMeters = 15.0f;

	/** Raio da area onde os NPCs andam, em m. */
	UPROPERTY(Config, EditAnywhere, Category = "NPCs", meta = (ClampMin = "1", ClampMax = "500"))
	float CrowdRadiusMeters = 10.0f;

	/**
	 * Malha dos NPCs, por exemplo o SKM_Manny ou o SKM_Quinn do pacote Third Person. Vazio: um cilindro, que
	 * so mede o movimento. No jogo empacotado, a pasta dela precisa estar em Additional Asset Directories to Cook.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "NPCs")
	TSoftObjectPtr<USkeletalMesh> NpcMesh;

	/** Animation Blueprint da malha, por exemplo o ABP_Manny. Vazio: a malha fica parada na pose inicial. */
	UPROPERTY(Config, EditAnywhere, Category = "NPCs")
	TSoftClassPtr<UAnimInstance> NpcAnimClass;

	/** Altura da malha dentro da capsula, em cm. O pe do manequim fica no chao com -96. */
	UPROPERTY(Config, EditAnywhere, Category = "NPCs", meta = (ClampMin = "-300", ClampMax = "300"))
	float NpcMeshOffsetZ = -96.0f;

	/** Giro da malha, em graus. O manequim nasce olhando para o lado e precisa de -90 para andar de frente. */
	UPROPERTY(Config, EditAnywhere, Category = "NPCs", meta = (ClampMin = "-180", ClampMax = "180"))
	float NpcMeshYaw = -90.0f;

	/** Velocidade de caminhada, em cm/s (150 = passo de pedestre). */
	UPROPERTY(Config, EditAnywhere, Category = "NPCs", meta = (ClampMin = "10", ClampMax = "1000"))
	float NpcWalkSpeed = 150.0f;
};
