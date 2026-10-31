// Projeto Deveraux - no de squash do Animation Blueprint (modulo Deveraux)

#include "Corpo/AnimNode_DeverauxSquash.h"

#include "Animation/AnimInstanceProxy.h"
#include "BonePose.h"
#include "Corpo/DeverauxBodyMath.h"

void FAnimNode_DeverauxSquash::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);

	Animated.Initialize(Context);
	Simulated.Initialize(Context);
	SettledOffsets.Reset();
	bHasSettled = false;
}

void FAnimNode_DeverauxSquash::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Animated.CacheBones(Context);
	Simulated.CacheBones(Context);

	const FBoneContainer& RequiredBones = Context.AnimInstanceProxy->GetRequiredBones();
	for (FDeverauxSquashMass& Mass : Masses)
	{
		Mass.ScaleBone.Initialize(RequiredBones);
		Mass.TipBone.Initialize(RequiredBones);
	}
}

void FAnimNode_DeverauxSquash::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	// Pinos ligados a variaveis (bEnabled, no spike) chegam aqui
	GetEvaluateGraphExposedInputs().Execute(Context);

	Animated.Update(Context);
	Simulated.Update(Context);
	LastDeltaSeconds = Context.GetDeltaTime();
}

void FAnimNode_DeverauxSquash::Evaluate_AnyThread(FPoseContext& Output)
{
	Simulated.Evaluate(Output);
	if (!bEnabled || Masses.Num() == 0)
	{
		// Ao religar, o balanco parado comeca de novo, sem um salto
		bHasSettled = false;
		return;
	}

	FPoseContext AnimatedPose(Output);
	Animated.Evaluate(AnimatedPose);

	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	FCSPose<FCompactPose> SimulatedSpace;
	SimulatedSpace.InitPose(Output.Pose);
	FCSPose<FCompactPose> AnimatedSpace;
	AnimatedSpace.InitPose(AnimatedPose.Pose);

	// Lista de massas mudou (editada com o jogo rodando): o balanco parado comeca de novo
	if (SettledOffsets.Num() != Masses.Num())
	{
		SettledOffsets.SetNumZeroed(Masses.Num());
		bHasSettled = false;
	}
	for (int32 Index = 0; Index < Masses.Num(); ++Index)
	{
		const FDeverauxSquashMass& Mass = Masses[Index];
		if (!Mass.ScaleBone.IsValidToEvaluate(Bones) || !Mass.TipBone.IsValidToEvaluate(Bones))
		{
			continue;
		}
		const FCompactPoseBoneIndex ScaleIndex = Mass.ScaleBone.GetCompactPoseIndex(Bones);
		const FCompactPoseBoneIndex TipIndex = Mass.TipBone.GetCompactPoseIndex(Bones);

		// O balanco e o quanto a ponta simulada saiu da animada, menos a parte parada (peso de quem esta deitada)
		const FVector Offset = SimulatedSpace.GetComponentSpaceTransform(TipIndex).GetLocation()
			- AnimatedSpace.GetComponentSpaceTransform(TipIndex).GetLocation();
		FVector& Settled = SettledOffsets[Index];
		Settled = bHasSettled ? DeverauxBody::SmoothTowards(Settled, Offset, LastDeltaSeconds, SettleSeconds) : Offset;

		// Nos eixos do osso que escala, como ele esta depois do jiggle
		const FQuat ScaleRotation = SimulatedSpace.GetComponentSpaceTransform(ScaleIndex).GetRotation();
		const FVector Scale = DeverauxBody::SquashScale(ScaleRotation.UnrotateVector(Offset - Settled), StrengthPerCm, MaxChange);
		FTransform& Local = Output.Pose[ScaleIndex];
		Local.SetScale3D(Local.GetScale3D() * Scale);
	}
	bHasSettled = true;
}

void FAnimNode_DeverauxSquash::GatherDebugData(FNodeDebugData& DebugData)
{
	DebugData.AddDebugItem(FString::Printf(TEXT("%s (%d massas%s)"), *DebugData.GetNodeName(this), Masses.Num(), bEnabled ? TEXT("") : TEXT(", desligado")));
	Simulated.GatherDebugData(DebugData.BranchFlow(1.0f));
	Animated.GatherDebugData(DebugData.BranchFlow(0.0f));
}
