// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimExecutionContext.h"   // FAnimUpdateContext - 노드 함수 인자
#include "Animation/AnimNodeReference.h"
#include "Animation/VBInteractionAnimInterface.h"
#include "Animation/VBInteractionIK.h"
#include "CharacterCommon/VBRagdollComponent.h"
#include "VBEnemyAnimInstance.generated.h"

class UAnimSequenceBase;
class UAnimMontage;
class UBlendSpace;
class UVBEnemyAnimSet;

/**
 * 적 전용 AnimInstance (design .sw/enemy-anim/design.md §5).
 *
 * 무엇: 속도·방향·이동 여부만 계산하고, 클립은 소유 적의 UVBEnemyConfig::AnimSet 에서 읽어 그래프 자산 핀에 넘긴다.
 * 왜 UVBAnimInstance(플레이어/적 공용)와 나누나: 그쪽은 모션매칭·궤적·TIP·트래버설 변수 60여 개를 매 프레임 계산한다.
 *   적은 셋이면 된다. 같은 클래스를 쓰면 적마다 그 계산을 공짜로 치르고, 적에 필요한 변수는 플레이어 것 사이에 묻힌다.
 *   실제로 2026-09-06 까지 감염체·병사가 플레이어 ABP 를 그대로 써서 서 있기·걷기가 플레이어 동작이었다.
 * 가정: 소유 폰은 AVBEnemyBase. 아니면(ABP 에디터 미리보기) AnimSet 이 없어 참조 포즈로 서 있다 - 정상.
 * 부작용: 없음. 캡슐 이동은 CMC 가, 몽타주 루트모션은 ABP 의 RootMotionFromMontagesOnly 가 맡는다.
 */
UCLASS()
class VOWBOUND_API UVBEnemyAnimInstance : public UAnimInstance, public IVBInteractionAnimInterface
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativePostEvaluateAnimation() override;

	// Apply a fixed finisher's capsule correction only after its get-up pose is evaluated.
	void QueueFinisherGetUpHandoff(UAnimMontage* Montage, const FTransform& ActorBefore,
		const FTransform& ActorAfter, const FVector& PelvisBefore);
	void CancelFinisherGetUpHandoff();

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Ragdoll")
	FVBRagdollAnimationState RagdollState;

	UFUNCTION(BlueprintPure, Category="Vowbound|Ragdoll", meta=(BlueprintThreadSafe))
	double GetRagdollElapsedTime() const { return RagdollState.ElapsedSeconds; }

	UFUNCTION(BlueprintPure, Category="Vowbound|Ragdoll", meta=(BlueprintThreadSafe))
	bool IsPoweredRagdollPose() const { return RagdollState.bPowered; }

	UFUNCTION(BlueprintPure, Category="Vowbound|Ragdoll", meta=(BlueprintThreadSafe))
	bool IsNotPoweredRagdollPose() const { return !RagdollState.bPowered; }

	// IVBInteractionAnimInterface — 처형에서 «당하는 쪽» 이 되려면 결과를 들 수 있어야 한다.
	//   「누가 채우고 누가 읽는가」와 UPROPERTY 가 필수인 이유는 그 헤더 한 곳에 있다.
	//   피해자는 기상 Chooser에서도 최종 포즈의 PoseHistory를 사용한다.
	virtual const FPoseSearchBlueprintResult& GetInteractionResult() const override { return InteractionResult; }
	virtual void SetInteractionResult(const FPoseSearchBlueprintResult& InResult) override { InteractionResult = InResult; }

	// 상호작용 손 IK. 피해자도 공격자의 손을 «받는» 쪽으로 제약에 참여한다(제약은 짝의 것이다).
	//   채우는 것은 UVBInteractionIKLibrary 하나뿐이다 - 「왜」는 그 헤더에 있다.
	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly, Category="Vowbound|Finisher")
	FVBInteractionIKRuntime InteractionIK;

	UFUNCTION(BlueprintPure, Category="Vowbound|Finisher", meta=(BlueprintThreadSafe))
	FTransform GetInteractionIKLeftTarget() const { return InteractionIK.Left.Target; }

	UFUNCTION(BlueprintPure, Category="Vowbound|Finisher", meta=(BlueprintThreadSafe))
	FTransform GetInteractionIKRightTarget() const { return InteractionIK.Right.Target; }

	UFUNCTION(BlueprintPure, Category="Vowbound|Finisher", meta=(BlueprintThreadSafe))
	bool IsInteractionIKEnabled() const { return InteractionIK.bEnabled; }

	// === 상호작용 손 IK 의 AnimGraph 접점 ===
	//
	// 왜 함수인가: 애님그래프 핀 바인딩은 변수를 안 받고 함수만 받는다(tool-traps, 2026-07-29).
	//   구조체 멤버 경로(InteractionIK.Left.Alpha)를 핀에 직접 물릴 수 없어서 넷을 낸다.
	// 왜 BlueprintThreadSafe 인가: 핀 바인딩은 애니메이션 워커 스레드에서 불린다. 멤버만 읽는다.
	// TwoBoneIK 의 EffectorLocation 은 FVector 라 위치만 넘긴다 - 회전은 노드의
	//   bTakeRotationFromEffectorSpace 가 든다.
	UFUNCTION(BlueprintPure, Category="Vowbound|Finisher", meta=(BlueprintThreadSafe))
	FVector GetInteractionIKLeftEffector() const { return InteractionIK.Left.Target.GetLocation(); }

	UFUNCTION(BlueprintPure, Category="Vowbound|Finisher", meta=(BlueprintThreadSafe))
	float GetInteractionIKLeftAlpha() const { return InteractionIK.bEnabled ? InteractionIK.Left.Alpha : 0.0f; }

	UFUNCTION(BlueprintPure, Category="Vowbound|Finisher", meta=(BlueprintThreadSafe))
	FVector GetInteractionIKRightEffector() const { return InteractionIK.Right.Target.GetLocation(); }

	UFUNCTION(BlueprintPure, Category="Vowbound|Finisher", meta=(BlueprintThreadSafe))
	float GetInteractionIKRightAlpha() const { return InteractionIK.bEnabled ? InteractionIK.Right.Alpha : 0.0f; }


	// ===== 그래프 자산 핀 바인딩 대상 =====
	// BlueprintThreadSafe 필수 - 핀 바인딩 함수는 애니메이션 워커 스레드에서 호출된다. 멤버만 읽는다.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|Enemy", meta=(BlueprintThreadSafe))
	UAnimSequenceBase* GetIdleSequence() const;

	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|Enemy", meta=(BlueprintThreadSafe))
	UBlendSpace* GetLocomotionBlendSpace() const;

	// 수평 속도 크기(cm/s). 블렌드스페이스 Y.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|Enemy", meta=(BlueprintThreadSafe))
	float GetSpeed() const { return Speed; }

	// 속도가 액터 정면에서 벗어난 각도(-180..180, 오른쪽 양수). 블렌드스페이스 X - 대상을 보며 옆걸음하는 적의 스트레이프 축.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|Enemy", meta=(BlueprintThreadSafe))
	float GetDirection() const { return Direction; }

	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|Enemy", meta=(BlueprintThreadSafe))
	bool IsMoving() const { return bIsMoving; }

	// ===== 노드 함수 (ABP 노드의 OnUpdate 슬롯에 바인딩) =====
	// 왜 핀 바인딩이 아닌가: 2026-09-06 실측 - 함수 핀 바인딩은 bool·BlendSpace 핀엔 값을 넘기고 Sequence·X·Y 핀엔 안 넘겼다
	//   (계측 로그의 값은 정상, 화면은 T 포즈/대기). 원인은 못 가렸다. 노드 함수 + 변수 링크는 GASP 가 쓰는 길이라 그쪽으로 간다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|Enemy", meta=(BlueprintThreadSafe))
	void OnIdleNodeUpdate(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|Enemy", meta=(BlueprintThreadSafe))
	void OnLocomotionNodeUpdate(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

protected:
	// 그래프 VariableGet 링크 대상. AnimSet 에서 초기화 때 복사한다(워커 스레드 게터가 AnimSet 포인터를 안 거치게).
	UPROPERTY(BlueprintReadOnly, Transient, Category="Vowbound|Animation|Enemy")
	TObjectPtr<UAnimSequenceBase> IdleSequence;

	UPROPERTY(BlueprintReadOnly, Transient, Category="Vowbound|Animation|Enemy")
	TObjectPtr<UBlendSpace> LocomotionBlendSpace;

	// 소유 적의 아키타입 클립 묶음. 초기화 때 한 번 캐시한다(적은 도중에 아키타입이 바뀌지 않는다). GC 참조용.
	UPROPERTY(Transient)
	TObjectPtr<const UVBEnemyAnimSet> AnimSet;

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|Enemy")
	float Speed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|Enemy")
	float Direction = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|Enemy")
	bool bIsMoving = false;

	// v1 미사용(적은 안 뜬다). 값만 두어 그래프가 나중에 공중 분기를 열 때 코드를 안 고치게.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|Enemy")
	bool bIsInAir = false;

private:
	TWeakObjectPtr<UAnimMontage> PendingFinisherGetUp;
	FTransform FinisherActorBefore = FTransform::Identity;
	FTransform FinisherActorAfter = FTransform::Identity;
	FVector FinisherPelvisBefore = FVector::ZeroVector;
	bool bFinisherHandoffReady = false;

	// 상호작용 결과. UPROPERTY 가 필수인 이유는 IVBInteractionAnimInterface 헤더에 있다.
	UPROPERTY(Transient)
	FPoseSearchBlueprintResult InteractionResult;

	// AnimSet 값의 사본. 워커 스레드의 게터가 AnimSet 포인터를 거치지 않게 한다.
	float MovingSpeedThreshold = 10.0f;

	// ===== 계측 (콘솔 vb.EnemyAnim.Telemetry 1 일 때만 채우고 찍는다. 기본 0) =====
	// 계측 로그 누적 시간. 게임스레드 전용.
	float TelemetryAccum = 0.0f;

	// 노드 함수 호출 수. «노드 함수가 실제로 도는가» 를 가르기 위해 - 워커 스레드에서 늘고 게임스레드에서 읽는다(대략값이면 충분).
	int32 IdleNodeUpdateCount = 0;
	int32 LocomotionNodeUpdateCount = 0;

	// 노드가 실제로 보는 값. 그래프 밖에서 넣은 값과 노드 안의 값이 다른지 가르기 위해.
	//   2026-09-07 블렌드스페이스 누적 시간이 0 에 박힌 것을 여기서 잡았다(자산의 삼각분할 누락, SystemMap 11 함정 15).
	FVector LocoNodePosition = FVector::ZeroVector;
	FString LocoNodeAsset;
	FString IdleNodeAsset;
	float IdleNodePlayRate = -1.0f;
	float IdleNodeTime = -1.0f;
	float LocoNodePlayRate = -1.0f;
	bool  bLocoNodeLoop = false;
	float LocoNodeTime = -1.0f;
	float LocoNodeDelta = -1.0f;      // 노드 함수가 받는 업데이트 컨텍스트의 DeltaTime - 0 이면 재생이 못 나간다
	float LocoNodeWeight = -1.0f;
};
