// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Enemy/VBGA_GetUp.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimInstance.h"
#include "Animation/VBRagdollGetUpTypes.h"
#include "Animation/VBEnemyAnimInstance.h"
#include "CharacterCommon/VBRagdollComponent.h"
#include "Chooser.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "BoneIndices.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "Enemy/VBEnemyBase.h"
#include "Data/VBRagdollConfig.h"
#include "Vowbound/Vowbound.h"

namespace
{
	struct FFinisherBodyBasis
	{
		FVector Long;
		FVector Chest;
	};

	bool MakeBodyBasis(const FVector* Positions, FFinisherBodyBasis& OutBasis)
	{
		OutBasis.Long = (Positions[1] - Positions[0]).GetSafeNormal();
		const FVector Lateral = (Positions[3] - Positions[2]).GetSafeNormal();
		OutBasis.Chest = FVector::CrossProduct(Lateral, OutBasis.Long).GetSafeNormal();
		return !OutBasis.Long.IsNearlyZero() && !OutBasis.Chest.IsNearlyZero();
	}

	// Fixed finisher montages have an authored starting direction. Unlike GASP's
	// PoseHistory/Chooser path, their initial pose must be aligned to the fallen body.
	bool MeasureFinisherGetUpYaw(const USkeletalMeshComponent& Mesh, const UAnimMontage& Montage,
		float& OutYaw, FString& OutWhy)
	{
		const FAnimSegment* Segment = Montage.SlotAnimTracks.IsEmpty() ? nullptr
			: Montage.SlotAnimTracks[0].AnimTrack.GetSegmentAtTime(0.0f);
		const UAnimSequence* Clip = Segment ? Cast<UAnimSequence>(Segment->GetAnimReference()) : nullptr;
		const USkeleton* Skeleton = Clip ? Clip->GetSkeleton() : nullptr;
		if (!Clip || !Skeleton || Segment->AnimEndTime <= Segment->AnimStartTime)
		{
			OutWhy = TEXT("기상 시작점에 유효한 시퀀스가 없다");
			return false;
		}

		static const FName BoneNames[] = { TEXT("pelvis"), TEXT("head"), TEXT("clavicle_l"), TEXT("clavicle_r") };
		const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
		const TArray<FTransform>& RefPose = RefSkel.GetRefBonePose();
		const FAnimExtractContext Context(Segment->ConvertTrackPosToAnimPos(0.0f));
		FVector CurrentPositions[4];
		FVector ClipPositions[4];
		for (int32 Bone = 0; Bone < UE_ARRAY_COUNT(BoneNames); ++Bone)
		{
			int32 Index = RefSkel.FindBoneIndex(BoneNames[Bone]);
			if (Index == INDEX_NONE || Mesh.GetBoneIndex(BoneNames[Bone]) == INDEX_NONE)
			{
				OutWhy = FString::Printf(TEXT("기상 방향 측정 본이 없다: %s"), *BoneNames[Bone].ToString());
				return false;
			}
			CurrentPositions[Bone] = Mesh.GetBoneLocation(BoneNames[Bone], EBoneSpaces::WorldSpace);
			FTransform ComponentSpace = FTransform::Identity;
			while (Index != INDEX_NONE)
			{
				// Missing animation tracks retain their reference transform.
				FTransform Local = RefPose[Index];
				Clip->GetBoneTransform(Local, FSkeletonPoseBoneIndex(Index), Context, false);
				ComponentSpace *= Local;
				Index = RefSkel.GetParentIndex(Index);
			}
			ClipPositions[Bone] = ComponentSpace.GetLocation();
		}

		FFinisherBodyBasis CurrentBasis;
		FFinisherBodyBasis ClipBasis;
		if (!MakeBodyBasis(CurrentPositions, CurrentBasis) || !MakeBodyBasis(ClipPositions, ClipBasis))
		{
			OutWhy = TEXT("기상 방향을 정할 수 없는 본 위치");
			return false;
		}
		const FVector ClipLongWorld = Mesh.GetComponentQuat().RotateVector(ClipBasis.Long);
		if (CurrentBasis.Long.SizeSquared2D() <= KINDA_SMALL_NUMBER || ClipLongWorld.SizeSquared2D() <= KINDA_SMALL_NUMBER)
		{
			OutWhy = TEXT("몸의 세로축이 수직이라 수평 기상 방향을 정할 수 없다");
			return false;
		}
		OutYaw = FRotator::NormalizeAxis(FMath::RadiansToDegrees(
			FMath::Atan2(CurrentBasis.Long.Y, CurrentBasis.Long.X) - FMath::Atan2(ClipLongWorld.Y, ClipLongWorld.X)));
		const FVector ClipChestWorld = Mesh.GetComponentQuat().RotateVector(ClipBasis.Chest);
		if ((CurrentBasis.Chest.Z < 0.0) != (ClipChestWorld.Z < 0.0))
		{
			OutWhy = TEXT("쓰러진 자세와 기상 클립의 엎드림/누움이 다르다 - yaw만으로는 맞출 수 없다");
		}
		return FMath::IsFinite(OutYaw);
	}
}

UVBGA_GetUp::UVBGA_GetUp()
{
	// 활성화 배선은 ASC 가 자동 수행한다(UVBGA_Stagger 와 같은 패턴) -
	//   OnGiveAbility 가 AbilityTriggers 를 순회해 등록하므로 별도 구독 코드가 없다.
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = VBGameplayTags::Event_Combat_FinisherSurvived;
	AbilityTriggers.Add(Trigger);

	// 일어나는 동안은 「일어나는 중」이다. 이 태그의 단독 생산자다.
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_GettingUp);

	// 일어나는 동안 공격을 막는다. 일어나자마자 때릴 수 있으면 처형을 버틴 것이
	//   그냥 이득이 되고, 처형을 거는 쪽이 손해를 본다.
	BlockAbilitiesWithTag.AddTag(VBGameplayTags::Ability_Attack);

	// 죽었으면 안 일어난다. 보내는 쪽(UVBGA_Execute)이 이미 IsDead 를 보지만, 그 판정과
	//   실제 활성화 사이에 사망이 확정될 수 있다 - 시체가 일어서는 것은 되돌릴 수 없는 그림이라
	//   막을 수 있는 자리에서 한 번 더 막는다(스태거와 같은 수단).
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Dead);

	// State.Combat.FinisherWindow 는 게이트로 쓰지 않는다. 2026-09-13 에 넣었다가 뺐다.
	//   그 태그의 뜻은 「처형당하는 중」이 아니라 「처형당할 수 있는 상태」다. 버티고 일어난 적은
	//   여전히 처형 가능한 것이 정상이고, 오히려 그때 일어나야 한다.
	//   실측: 튜닝 더미는 창이 99999초라 그 태그를 영구히 들고 있어서 기상이 100% 막혔다 -
	//   이벤트는 5번 나갔는데 받는 쪽 로그가 0줄이었다(막힌 활성화는 아무 말도 안 한다).

	// 연속 처형에서 두 번째부터 안 나오는 것을 막는다. false 면 InstancedPerActor 가 활성 중일 때
	//   재활성화가 조용히 거부된다(스태거·히트리액트와 같은 이유).
	bRetriggerInstancedAbility = true;
}

void UVBGA_GetUp::ExecuteAbility(const FGameplayAbilitySpecHandle     Handle,
                                 const FGameplayAbilityActorInfo*     ActorInfo,
                                 const FGameplayAbilityActivationInfo ActivationInfo,
                                 const FGameplayEventData*            TriggerEventData)
{
	AActor* const Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	USkeletalMeshComponent* const Mesh = ActorInfo ? ActorInfo->SkeletalMeshComponent.Get() : nullptr;
	UAnimInstance* const AnimInstance = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
	const UObject* Selection = TriggerEventData ? TriggerEventData->OptionalObject.Get() : nullptr;
	const UAnimMontage* Montage = Cast<UAnimMontage>(Selection);
	const bool bFixedFinisherMontage = Montage != nullptr;
	double StartTime = 0.0;
	if (const UVBRagdollConfig* Config = Cast<UVBRagdollConfig>(Selection))
	{
		const ACharacter* Character = Cast<ACharacter>(Avatar);
		const UVBRagdollComponent* Ragdoll = Avatar ? Avatar->FindComponentByClass<UVBRagdollComponent>() : nullptr;
		const FAnimNode_PoseSearchHistoryCollector_Base* HistoryNode = AnimInstance
			? UPoseSearchLibrary::FindPoseHistoryNode(TEXT("PoseHistory"), AnimInstance) : nullptr;
		if (Config->RecoverableGetUpChooser && Character && Ragdoll && HistoryNode && HistoryNode->GetPoseHistoryPtr())
		{
			FVBRagdollGetUpInputs Inputs;
			Inputs.MovementMode = Character->GetCharacterMovement()->MovementMode;
			Inputs.RollingGetup = Ragdoll->ShouldPlayRollingGetUp();
			Inputs.PoseHistory = HistoryNode->GetPoseHistoryReference();
			FVBRagdollGetUpOutputs Outputs;
			FChooserEvaluationContext Context(AnimInstance);
			Context.AddStructParam(Inputs);
			Context.AddStructParam(Outputs);
			UChooserTable::EvaluateChooser(Context, Config->RecoverableGetUpChooser,
				FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda([&Montage](UObject* Result)
				{
					Montage = Cast<UAnimMontage>(Result);
					return Montage ? FObjectChooserBase::EIteratorStatus::Stop : FObjectChooserBase::EIteratorStatus::Continue;
				}));
			StartTime = Outputs.MontageStartTime;
			VB_LOG(Log, "GASPGetUp query: actor=%s speedXY=%.1f rolling=%d mode=%d montage=%s start=%.3f",
				*GetNameSafe(Avatar), Ragdoll->GetAnimationState().CenterOfMassVelocity.Size2D(),
				Inputs.RollingGetup, int32(Inputs.MovementMode), *GetNameSafe(Montage), StartTime);
		}
		else
		{
			VB_LOG(Error, "GASPGetUp: missing native chooser or active PoseHistory on %s", *GetNameSafe(Avatar));
		}
	}
	if (Montage && (!Mesh || !Mesh->GetSkeletalMeshAsset() ||
		Montage->GetSkeleton() != Mesh->GetSkeletalMeshAsset()->GetSkeleton() ||
		!FMath::IsFinite(StartTime) || StartTime < 0.0 || StartTime >= Montage->GetPlayLength()))
	{
		VB_LOG(Error, "GASPGetUp: invalid montage skeleton/start time (%s, %.3f)", *GetNameSafe(Montage), StartTime);
		Montage = nullptr;
	}
	if (!Montage)
	{
		if (AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr))
			Enemy->FinishRecoverableGetUp();
		// 비어 있는 것은 결함이 아니다(연출 없는 처형). 다만 조용히 끝나면 「기상이 안 나온다」의
		//   원인이 여기인지 이벤트가 안 왔는지 못 가리므로 한 줄 남긴다.
		VB_LOG(Warning, "%s: 유효한 기상 검색 결과가 없다 (Chooser, PoseHistory, 골격 및 재생 범위를 확인)", *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);
		return;
	}

	UVBEnemyAnimInstance* HandoffAnim = nullptr;
	FTransform HandoffBefore = FTransform::Identity;
	FTransform HandoffAfter = FTransform::Identity;
	FVector HandoffPelvisBefore = FVector::ZeroVector;
	// Only the non-physical, fixed-montage path needs this alignment. Applying it
	// to a GASP pose-matched get-up would rotate the physics handoff a second time.
	if (bFixedFinisherMontage && Avatar && Avatar->HasAuthority() && !Mesh->IsAnySimulatingPhysics())
	{
		float MeasuredYaw = 0.0f;
		FString Why;
		const bool bMeasured = MeasureFinisherGetUpYaw(*Mesh, *Montage, MeasuredYaw, Why);
		const float Trim = TriggerEventData && FMath::IsFinite(TriggerEventData->EventMagnitude)
			? TriggerEventData->EventMagnitude : 0.0f;
		const float YawOffset = FRotator::NormalizeAxis((bMeasured ? MeasuredYaw : 0.0f) + Trim);
		HandoffAnim = Cast<UVBEnemyAnimInstance>(AnimInstance);
		if (HandoffAnim)
		{
			HandoffBefore = Avatar->GetActorTransform();
			HandoffAfter = HandoffBefore;
			HandoffPelvisBefore = Mesh->GetBoneLocation(TEXT("pelvis"), EBoneSpaces::WorldSpace);
			const FTransform Root = Mesh->GetSocketTransform(TEXT("root"), RTS_World);
			const FVector Offset = Root.GetLocation() - Mesh->GetComponentLocation();
			const float RootYaw = FRotator::NormalizeAxis(Root.Rotator().Yaw - Mesh->GetComponentRotation().Yaw);
			HandoffAfter.AddToTranslation(FVector(Offset.X, Offset.Y, 0.0));
			HandoffAfter.SetRotation(FRotator(0.0f, RootYaw + YawOffset, 0.0f).Quaternion() * HandoffBefore.GetRotation());
		}
		else if (!FMath::IsNearlyZero(YawOffset))
		{
			Avatar->AddActorWorldRotation(FRotator(0.0f, YawOffset, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
			Avatar->ForceNetUpdate();
		}
		VB_LOG(Log, "FinisherGetUp alignment: actor=%s montage=%s measured=%d measuredYaw=%.2f trim=%.2f targetYawDelta=%.2f deferred=%d",
			*Avatar->GetName(), *Montage->GetName(), bMeasured ? 1 : 0, MeasuredYaw, Trim, YawOffset, HandoffAnim ? 1 : 0);
		if (!Why.IsEmpty()) VB_LOG(Warning, "FinisherGetUp alignment: %s - %s", *Avatar->GetName(), *Why);
	}

	// bStopAllMontages=true 로 둔다 - 처형 장면의 몽타주가 아직 블렌드아웃 중일 수 있고,
	//   그 위에 겹치면 쓰러진 자세와 일어나는 자세가 섞인다(UVBGA_Stagger 와 같은 이유).
	UAbilityTask_PlayMontageAndWait* MontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, NAME_None, const_cast<UAnimMontage*>(Montage),
			/*Rate=*/1.0f, NAME_None, /*bStopWhenAbilityEnds=*/true,
			/*AnimRootMotionTranslationScale=*/1.0f, /*StartTimeSeconds=*/static_cast<float>(StartTime),
			/*bAllowInterruptAfterBlendOut=*/false);

	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnGetUpEnded);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnGetUpEnded);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnGetUpEnded);
	MontageTask->ReadyForActivation();
	// PlayMontageAndWait can synchronously cancel when the ASC still owns an external
	// montage. In that case OnGetUpEnded has already ended this ability.
	if (!IsActive()) return;
	if (!AnimInstance || !AnimInstance->Montage_IsPlaying(Montage))
	{
		VB_LOG(Error, "%s: 기상 몽타주 재생 상태를 확인하지 못했다 (%s)", *GetName(), *Montage->GetName());
		OnGetUpEnded();
		return;
	}
	if (HandoffAnim)
	{
		HandoffAnim->QueueFinisherGetUpHandoff(const_cast<UAnimMontage*>(Montage), HandoffBefore, HandoffAfter, HandoffPelvisBefore);
	}
	VB_LOG(Log, "GASPGetUp playing: actor=%s montage=%s start=%.3f yaw=%.1f physics=%d source=%s",
		*GetNameSafe(Avatar), *Montage->GetName(), StartTime,
		Avatar ? Avatar->GetActorRotation().Yaw : 0.0f, Mesh && Mesh->IsAnySimulatingPhysics(),
		bFixedFinisherMontage ? TEXT("FinisherMontage") : TEXT("GASPChooser"));
}

void UVBGA_GetUp::OnGetUpEnded()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
	           /*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);
}

void UVBGA_GetUp::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsActive()) return;
	if (UVBEnemyAnimInstance* EnemyAnim = ActorInfo ? Cast<UVBEnemyAnimInstance>(ActorInfo->GetAnimInstance()) : nullptr)
	{
		EnemyAnim->CancelFinisherGetUpHandoff();
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
	if (AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr))
		Enemy->FinishRecoverableGetUp();
}
