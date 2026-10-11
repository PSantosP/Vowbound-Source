// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBEnemyAnimInstance.h"
#include "Animation/VBInteractionIK.h"

#include "Animation/AnimSequenceBase.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBEnemyAnimSet.h"
#include "Data/VBEnemyConfig.h"
#include "Enemy/VBEnemyBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Vowbound/Vowbound.h"
#include "SequencePlayerLibrary.h"
#include "BlendSpacePlayerLibrary.h"

// 적 애니 계측. 0 = 조용(기본). 1 = 적마다 2초 간격으로 [EnemyAnim] 두 줄 - 그래프 밖의 값(속도·방향·이동)과
//   노드 안의 값(노드가 보는 자산·좌표·재생 속도·누적 시간·델타·가중치)을 나란히 찍는다.
// 왜 남겨 두나: 2026-09-07 «이동 중 대기 자세» 의 원인(블렌드스페이스 삼각분할 누락)을 가른 것이 이 두 줄이었다 -
//   그래프 핀 값은 정적으로 읽을 길이 없어 이 로그가 유일한 대조 수단이다. 콘솔 `vb.EnemyAnim.Telemetry 1`.
static TAutoConsoleVariable<int32> CVarEnemyAnimTelemetry(
	TEXT("vb.EnemyAnim.Telemetry"),
	0,
	TEXT("Enemy AnimInstance telemetry: 0=off, 1=log graph inputs and asset-player state every 2s per enemy."),
	ECVF_Default
);

void UVBEnemyAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	CancelFinisherGetUpHandoff();

	AnimSet = nullptr;
	const AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(TryGetPawnOwner());
	if (!Enemy)
	{
		// ABP 에디터 미리보기·CDO 에는 폰이 없다. 경고할 일이 아니다.
		return;
	}

	// GetEnemyConfig() 는 미할당이면 CDO 를 돌려주므로 null 이 아니다. AnimSet 은 CDO 에서 비어 있다.
	AnimSet = Enemy->GetEnemyConfig()->AnimSet;
	if (!AnimSet)
	{
		// 참조 포즈로 서 있게 된다 - 눈에 보이는 실패지만 원인은 여기 한 줄뿐이라 소리내어 남긴다.
		VB_LOG(Warning, "%s: EnemyConfig 에 AnimSet 이 없다 -> 클립 없이 참조 포즈. DA_EnemyAnimSet_* 을 할당하라",
		       *GetNameSafe(Enemy));
		return;
	}

	MovingSpeedThreshold = AnimSet->MovingSpeedThreshold;
	IdleSequence         = AnimSet->Idle;
	LocomotionBlendSpace = AnimSet->Locomotion;

}

void UVBEnemyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	// UE updates montage evaluation data before NativeUpdateAnimation. A handoff
	// queued later by an end event must wait for this next update and evaluation.
	bFinisherHandoffReady = PendingFinisherGetUp.IsValid() && Montage_IsPlaying(PendingFinisherGetUp.Get());

	const APawn* Pawn = TryGetPawnOwner();
	if (!Pawn)
	{
		return;
	}

	// 상호작용 손 IK. 상호작용 중이 아니면 꺼진 상태로 돌아오므로 매 프레임 불러도 된다.
	//   게임 스레드여야 한다 - 몽타주 상태를 본다.
	UVBInteractionIKLibrary::Update(this, GetInteractionResult(), DeltaSeconds, InteractionIK);
	// 보폭 배율의 홈은 AVBEnemyBase 다(GetStrideScale 의 규약). 폰이 적이 아니면(미리보기) 1.
	const AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(Pawn);
	const UVBRagdollComponent* Ragdoll = Enemy ? Enemy->FindComponentByClass<UVBRagdollComponent>() : nullptr;
	RagdollState = Ragdoll ? Ragdoll->GetAnimationState() : FVBRagdollAnimationState();
	const float StrideScale   = Enemy ? Enemy->GetStrideScale() : 1.0f;

	const FVector Velocity    = Pawn->GetVelocity();
	const FVector Velocity2D  = FVector(Velocity.X, Velocity.Y, 0.0);
	const float   GroundSpeed = static_cast<float>(Velocity2D.Size());

	// 블렌드스페이스가 아는 것은 «애니 고유 속도» 뿐인데, 메시가 크면 같은 클립이 그리는 보폭도
	//   그만큼 커진다(배율 1.4 인 보스는 보폭이 1.4배). 그래서 지면 속도를 배율로 나눠야 다리와
	//   실제 이동이 맞는다. 배율 1 인 적은 나누기 1 이라 종전과 완전히 같은 값이다.
	// 왜 여기가 홈인가: 배율은 BP 의 성질이고 블렌드스페이스도 EnemyConfig 도 그것을 모른다.
	//   아는 것은 메시 컴포넌트를 쥔 이 클래스뿐이다. 자산 쪽에 배율을 반영한 숫자를 적으면
	//   배율을 바꿀 때마다 자산을 함께 고쳐야 하고, 둘이 어긋나도 아무도 말해 주지 않는다.
	Speed = GroundSpeed / StrideScale;

	// 이동 여부는 «지면에서 실제로 움직이나» 라 배율과 무관하다. 나눈 값으로 재면 큰 적일수록
	//   문턱이 높아져 같은 떨림이 적마다 다르게 걸러진다.
	bIsMoving = GroundSpeed > MovingSpeedThreshold;

	// 방향 = 속도 벡터를 액터 정면 기준으로 돌린 각도. 적은 대상을 보며 움직이므로(SetFocus) 정면과 속도가 갈린다.
	//   엔진 UAnimInstance::CalculateDirection 은 5.0 부터 폐기(UKismetAnimationLibrary 로 이동)라 같은 식을 직접 쓴다 -
	//   AnimGraphRuntime 모듈 의존을 이 한 줄 때문에 늘리지 않는다.
	if (bIsMoving)
	{
		const FVector Forward = Pawn->GetActorForwardVector();
		const FVector Right   = Pawn->GetActorRightVector();
		const FVector Dir     = Velocity2D.GetSafeNormal();
		const double  ForwardDot = FVector::DotProduct(Forward, Dir);
		const double  RightDot   = FVector::DotProduct(Right, Dir);
		Direction = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(RightDot, ForwardDot)));
	}
	// 멈추면 마지막 방향을 유지한다 - 0 으로 튀면 정지 직전 프레임에 블렌드스페이스가 앞걸음으로 스냅한다.

	if (const ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		const UCharacterMovementComponent* Move = Character->GetCharacterMovement();
		bIsInAir = Move && Move->IsFalling();
	}

	// 계측(2초 간격, vb.EnemyAnim.Telemetry). 그래프 핀 값은 정적으로 읽을 길이 없어(tool-traps 2026-07-29) 이 값과 PIE 관측을
	//   대조하는 것이 «바인딩이 값을 못 받는다» 와 «값이 0 이다» 를 가르는 유일한 길이다. 값이 여기 찍히는데 화면이 대기면 전자다.
	if (CVarEnemyAnimTelemetry.GetValueOnGameThread() == 0)
	{
		return;
	}
	TelemetryAccum += DeltaSeconds;
	if (TelemetryAccum >= 2.0f)
	{
		TelemetryAccum = 0.0f;
		VB_LOG(Log, "[EnemyAnim] %s speed=%.0f dir=%.0f moving=%d idle=%s loco=%s montage=%s nodeFn(idle/loco)=%d/%d nodeSees idle=%s bs=%s pos=(%.0f,%.0f)",
		       *GetNameSafe(Pawn), Speed, Direction, bIsMoving ? 1 : 0,
		       *GetNameSafe(GetIdleSequence()), *GetNameSafe(GetLocomotionBlendSpace()),
		       *GetNameSafe(GetCurrentActiveMontage()), IdleNodeUpdateCount, LocomotionNodeUpdateCount,
		       *IdleNodeAsset, *LocoNodeAsset, LocoNodePosition.X, LocoNodePosition.Y);
		VB_LOG(Log, "[EnemyAnim]   idleNode rate=%.2f time=%.2f | locoNode rate=%.2f loop=%d time=%.2f delta=%.4f weight=%.2f",
		       IdleNodePlayRate, IdleNodeTime, LocoNodePlayRate, bLocoNodeLoop ? 1 : 0, LocoNodeTime, LocoNodeDelta, LocoNodeWeight);
	}
}

void UVBEnemyAnimInstance::QueueFinisherGetUpHandoff(UAnimMontage* Montage, const FTransform& ActorBefore,
	const FTransform& ActorAfter, const FVector& PelvisBefore)
{
	PendingFinisherGetUp = Montage;
	FinisherActorBefore = ActorBefore;
	FinisherActorAfter = ActorAfter;
	FinisherPelvisBefore = PelvisBefore;
	bFinisherHandoffReady = false;
}

void UVBEnemyAnimInstance::CancelFinisherGetUpHandoff()
{
	PendingFinisherGetUp.Reset();
	bFinisherHandoffReady = false;
}

void UVBEnemyAnimInstance::NativePostEvaluateAnimation()
{
	Super::NativePostEvaluateAnimation();
	if (!bFinisherHandoffReady) return;
	AActor* Actor = GetOwningActor();
	USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	const bool bCanApply = Actor && Actor->HasAuthority() && Mesh && !Mesh->IsAnySimulatingPhysics() &&
		PendingFinisherGetUp.IsValid() && Montage_IsPlaying(PendingFinisherGetUp.Get());
	CancelFinisherGetUpHandoff();
	if (!bCanApply) return;

	// Preserve any root motion consumed since the request, in the corrected frame.
	const FTransform CorrectedActor = Actor->GetActorTransform().GetRelativeTransform(FinisherActorBefore) * FinisherActorAfter;
	const FVector BeforeLocation = Actor->GetActorLocation();
	Actor->SetActorLocationAndRotation(CorrectedActor.GetLocation(), CorrectedActor.GetRotation(), false, nullptr,
		ETeleportType::TeleportPhysics);
	Actor->ForceNetUpdate();

	// PostEvaluate runs before the component-space buffer swap. Read the newly
	// evaluated buffer, not GetBoneLocation's previous displayed pose.
	const int32 PelvisIndex = Mesh->GetBoneIndex(TEXT("pelvis"));
	const TArray<FTransform>& NewPose = Mesh->GetEditableComponentSpaceTransforms();
	if (NewPose.IsValidIndex(PelvisIndex))
	{
		const FVector PelvisAfter = Mesh->GetComponentTransform().TransformPosition(NewPose[PelvisIndex].GetLocation());
		VB_LOG(Log, "FinisherGetUp handoff: actor=%s frame=%llu capsuleStepCm=%.2f pelvisStepCm=%.2f",
			*Actor->GetName(), GFrameCounter, FVector::Distance(BeforeLocation, Actor->GetActorLocation()),
			FVector::Distance(FinisherPelvisBefore, PelvisAfter));
	}
}

UAnimSequenceBase* UVBEnemyAnimInstance::GetIdleSequence() const
{
	return IdleSequence;
}

UBlendSpace* UVBEnemyAnimInstance::GetLocomotionBlendSpace() const
{
	return LocomotionBlendSpace;
}

void UVBEnemyAnimInstance::OnIdleNodeUpdate(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	// 매 업데이트 같은 자산을 다시 넣는다 - 라이브러리가 같은 값이면 아무 일도 안 하고, 아키타입은 도중에 안 바뀐다.
	EAnimNodeReferenceConversionResult Result;
	const FSequencePlayerReference Player = USequencePlayerLibrary::ConvertToSequencePlayer(Node, Result);
	if (Result == EAnimNodeReferenceConversionResult::Succeeded)
	{
		USequencePlayerLibrary::SetSequence(Player, IdleSequence);
		if (CVarEnemyAnimTelemetry.GetValueOnAnyThread() == 0)
		{
			return;
		}
		++IdleNodeUpdateCount;
		UAnimSequenceBase* NodeSequence = nullptr;
		USequencePlayerLibrary::GetSequence(Player, NodeSequence);
		IdleNodeAsset = GetNameSafe(NodeSequence);
		IdleNodePlayRate = USequencePlayerLibrary::GetPlayRate(Player);
		IdleNodeTime     = USequencePlayerLibrary::GetAccumulatedTime(Player);
	}
}

void UVBEnemyAnimInstance::OnLocomotionNodeUpdate(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	EAnimNodeReferenceConversionResult Result;
	const FBlendSpacePlayerReference Player = UBlendSpacePlayerLibrary::ConvertToBlendSpacePlayer(Node, Result);
	if (Result == EAnimNodeReferenceConversionResult::Succeeded)
	{
		UBlendSpacePlayerLibrary::SetBlendSpace(Player, LocomotionBlendSpace);
		if (CVarEnemyAnimTelemetry.GetValueOnAnyThread() == 0)
		{
			return;
		}
		++LocomotionNodeUpdateCount;
		LocoNodeAsset    = GetNameSafe(UBlendSpacePlayerLibrary::GetBlendSpace(Player));
		LocoNodePosition = UBlendSpacePlayerLibrary::GetPosition(Player);
		LocoNodePlayRate = UBlendSpacePlayerLibrary::GetPlayRate(Player);
		bLocoNodeLoop    = UBlendSpacePlayerLibrary::GetLoop(Player);
		LocoNodeTime     = UBlendSpacePlayerLibrary::GetAccumulatedTime(Player);
		if (const FAnimationUpdateContext* Ctx = Context.GetContext())
		{
			LocoNodeDelta  = Ctx->GetDeltaTime();
			LocoNodeWeight = Ctx->GetFinalBlendWeight();
		}
	}
}
