// Projeto Deveraux - no de contato dos ossos do Animation Blueprint (modulo Deveraux)

#include "Corpo/AnimNode_DeverauxContactBones.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "BonePose.h"
#include "Corpo/DeverauxBodyMath.h"
#include "Corpo/DeverauxContactComponent.h"
#include "GameFramework/Actor.h"

namespace AnimNodeDeverauxContactBonesPrivate
{
	/** Voltas por cadeia: a ponta que gira chega perto da superficie a cada volta. */
	constexpr int32 NumPasses = 4;
}

FAnimNode_DeverauxContactBones::FAnimNode_DeverauxContactBones()
{
	// As cadeias do corpo de teste, como no passo de ossos do Blender: a ponta do seio fica no meio dele, a uns 5 cm da
	// pele; a do gluteo, na parte mais saliente, perto da pele
	const auto AddChain = [this](const TCHAR* Root, const TCHAR* Tip, EDeverauxContactMass Mass, float Radius)
	{
		FDeverauxContactBoneChain Chain;
		Chain.RootBone.BoneName = FName(Root);
		Chain.TipBone.BoneName = FName(Tip);
		Chain.Mass = Mass;
		Chain.Radius = Radius;
		Chains.Add(Chain);
	};
	AddChain(TEXT("jig_seio_l"), TEXT("jig_seio_01_l"), EDeverauxContactMass::BreastLeft, 5.0f);
	AddChain(TEXT("jig_seio_r"), TEXT("jig_seio_01_r"), EDeverauxContactMass::BreastRight, 5.0f);
	AddChain(TEXT("jig_gluteo_l"), TEXT("jig_gluteo_01_l"), EDeverauxContactMass::GluteLeft, 2.0f);
	AddChain(TEXT("jig_gluteo_r"), TEXT("jig_gluteo_01_r"), EDeverauxContactMass::GluteRight, 2.0f);
}

void FAnimNode_DeverauxContactBones::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);

	Source.Initialize(Context);
	LastPushed = 0;
}

void FAnimNode_DeverauxContactBones::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Source.CacheBones(Context);

	const FBoneContainer& RequiredBones = Context.AnimInstanceProxy->GetRequiredBones();
	for (FDeverauxContactBoneChain& Chain : Chains)
	{
		Chain.RootBone.Initialize(RequiredBones);
		Chain.TipBone.Initialize(RequiredBones);
	}
}

void FAnimNode_DeverauxContactBones::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	// Pinos ligados a variaveis (bEnabled) chegam aqui
	GetEvaluateGraphExposedInputs().Execute(Context);

	Source.Update(Context);
}

void FAnimNode_DeverauxContactBones::PreUpdate(const UAnimInstance* InAnimInstance)
{
	// No game thread: copia o que o componente mandou ao deformer neste quadro
	Values.Reset();
	bHasComponent = false;
	const AActor* Owner = InAnimInstance ? InAnimInstance->GetOwningActor() : nullptr;
	if (const UDeverauxContactComponent* Contact = Owner ? Owner->FindComponentByClass<UDeverauxContactComponent>() : nullptr)
	{
		Values = Contact->GetLastValues();
		bHasComponent = true;
	}
}

void FAnimNode_DeverauxContactBones::Evaluate_AnyThread(FPoseContext& Output)
{
	Source.Evaluate(Output);
	LastPushed = 0;
	if (!bEnabled || Chains.Num() == 0 || Values.Num() < DeverauxBody::ContactLayout::Num)
	{
		return;
	}

	// A pose no espaco da malha, feita uma vez; de novo so depois que uma cadeia mexeu nela (para cadeia dentro de cadeia)
	FCSPose<FCompactPose> ComponentSpace;
	bool bStale = true;
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	for (const FDeverauxContactBoneChain& Chain : Chains)
	{
		if (!Chain.RootBone.IsValidToEvaluate(Bones) || !Chain.TipBone.IsValidToEvaluate(Bones))
		{
			continue;
		}
		if (bStale)
		{
			ComponentSpace.InitPose(Output.Pose);
			bStale = false;
		}
		const FCompactPoseBoneIndex RootIndex = Chain.RootBone.GetCompactPoseIndex(Bones);
		const FCompactPoseBoneIndex TipIndex = Chain.TipBone.GetCompactPoseIndex(Bones);
		const FCompactPoseBoneIndex ParentIndex = Output.Pose.GetParentBoneIndex(RootIndex);
		const bool bTipOnRoot = Output.Pose.GetParentBoneIndex(TipIndex).GetInt() == RootIndex.GetInt();
		const FTransform RootTransform = ComponentSpace.GetComponentSpaceTransform(RootIndex);
		const FVector Root = RootTransform.GetLocation();
		const FVector StartTip = ComponentSpace.GetComponentSpaceTransform(TipIndex).GetLocation();
		const double StartLength = (StartTip - Root).Size();
		if (StartLength <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		// A ponta gira em volta da raiz e vai para onde as contas a deixam; algumas voltas, porque girar pode levar a
		// ponta para dentro de outra coisa (ou deixar perto da superficie, quando ela nao pode esticar)
		FQuat Turn = FQuat::Identity;
		FVector Tip = StartTip;
		double Length = StartLength;
		bool bPushed = false;
		for (int32 Pass = 0; Pass < AnimNodeDeverauxContactBonesPrivate::NumPasses; ++Pass)
		{
			const FVector Resolved = DeverauxBody::ResolveContactPoint(Values, Tip, static_cast<int32>(Chain.Mass), Chain.Radius);
			const FVector OldDirection = (Tip - Root).GetSafeNormal();
			const FVector NewDirection = (Resolved - Root).GetSafeNormal();
			if ((Resolved - Tip).SizeSquared() < 1.0e-4 || OldDirection.IsZero() || NewDirection.IsZero())
			{
				break;
			}
			Turn = FQuat::FindBetweenNormals(OldDirection, NewDirection) * Turn;
			// Empurrada de frente, a massa encurta (a carne apertada), nunca mais que o tamanho que veio do jiggle. So
			// com a ponta filha da raiz; senao, a cadeia so gira
			if (bTipOnRoot)
			{
				Length = FMath::Min((Resolved - Root).Size(), StartLength);
			}
			Tip = Root + NewDirection * Length;
			bPushed = true;
		}
		if (!bPushed)
		{
			continue;
		}

		// Do espaco da malha para o do pai, que e o que a pose guarda
		const FQuat ParentRotation = ParentIndex.IsValid() ? ComponentSpace.GetComponentSpaceTransform(ParentIndex).GetRotation() : FQuat::Identity;
		FQuat Local = ParentRotation.Inverse() * (Turn * RootTransform.GetRotation());
		Local.Normalize();
		Output.Pose[RootIndex].SetRotation(Local);
		if (Length < StartLength)
		{
			FTransform& TipLocal = Output.Pose[TipIndex];
			TipLocal.SetTranslation(TipLocal.GetTranslation() * (Length / StartLength));
		}
		bStale = true;
		++LastPushed;
	}
}

void FAnimNode_DeverauxContactBones::GatherDebugData(FNodeDebugData& DebugData)
{
	DebugData.AddDebugItem(FString::Printf(TEXT("%s (%d cadeias, %d empurradas%s%s)"), *DebugData.GetNodeName(this), Chains.Num(), LastPushed,
		bEnabled ? TEXT("") : TEXT(", desligado"), bHasComponent ? TEXT("") : TEXT(", sem o componente Deveraux Contact no ator")));
	Source.GatherDebugData(DebugData);
}
