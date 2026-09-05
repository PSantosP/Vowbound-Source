// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/VBAnimationTypes.h"
#include "Character/VBWeaponStateTypes.h"
#include "Curves/CurveFloat.h"
#include "CharacterTrajectoryComponent.h"
#include "AnimationWarpingTypes.h"
#include "BoneControllers/AnimNode_OrientationWarping.h" // EOrientationWarpingSpace enum (RootBoneTransform/ComponentTransform/CustomTransform)
#include "GameplayTagContainer.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "PoseSearch/PoseSearchTrajectoryLibrary.h"
#include "PoseSearch/PoseSearchTrajectoryTypes.h"
#include "BoneControllers/AnimNode_OffsetRootBone.h"
#include "VBAnimInstance.generated.h"

class UPoseSearchDatabase;

class ACharacter;
class AVBCharacter;
class UCharacterMovementComponent;
class UVBTurnInPlaceConfig;
struct FVBTurnInPlaceEntry;

// FIND-029 무기별 TIP (2026-06-07): 무기(EVBWeaponType) 키 하나당 제자리 턴 연기 4종 묶음.
// 클립 컨트랙트: root-static(회전은 pelvis 베이크) + TurnYaw 커브(실측 진행 0→90/180) —
// 커브가 없으면 ConsumeTurnInPlaceCurve 소비 불가로 BeginTurnInPlace 가드가 재생을 거부한다.
USTRUCT(BlueprintType)
struct FVBTIPMontageSet
{
	GENERATED_BODY()

	// EditDefaultsOnly + BlueprintReadOnly: BP 가시성이 있어야 python 래퍼에 init 파라미터가
	// 생겨 MCP 데이터 배선이 가능 (WeaponSummonData 선례 — EditDefaultsOnly 단독은 init 0개).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|TurnInplace")
	TObjectPtr<UAnimMontage> TurnLeft90;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|TurnInplace")
	TObjectPtr<UAnimMontage> TurnRight90;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|TurnInplace")
	TObjectPtr<UAnimMontage> Turn180;

	// 크라우치는 임계(140°) 특성상 180 단일 — L/R 분화 시 슬롯 추가
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|TurnInplace")
	TObjectPtr<UAnimMontage> CrouchTurn180;
};

/**
 * 플레이어/적 공용 AnimInstance
 * ACharacter 기반으로 공통변수(쏙도, 이동 상태)를 계산
 * AVBCharacter일 경우에만 추가 변수(전투모드, 스프린트)를 업데이트
 */
UCLASS()
class VOWBOUND_API UVBAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativePostEvaluateAnimation() override;

	// 트래버설 front-ledge world transform (TryTraversalAction Step5.2 가 C++ 에서 직접 설정).
	// PSC_Traversal_Pos/Head 채널은 AnimInstance 를 GASP BP 인터페이스 BPI_InteractionTransform 으로 캐스트해
	//   GetInteractionTransform 으로 이 값을 읽는다(PSC_Traversal_Pos::BP_GetWorldPosition graph 검증). C++ 는 BP
	//   인터페이스를 구현 못 하므로 ABP_VBCharacter(BP child)가 BPI 를 구현하고 이 변수를 반환/저장해야 채널 cast 성공.
	//   (구현 안 되면 cast 실패 → attach 쿼리=0 → 음수축(-X/-Y) normal 접근 시 MotionMatch selDB=0.)
	//   BlueprintReadWrite: C++ 가 set, ABP BPI::Get 이 read (+필요시 BPI::Set 이 write).
	UPROPERTY(BlueprintReadWrite, Category="Vowbound|Animation|MotionMatching")
	FTransform InteractionTransform = FTransform::Identity;

	// ========== GASP Pin Binding Helpers ==========
	// OffsetRootBone 노드는 TranslationMode/RotationMode/TranslationHalflife 속성이
	// meta=(FoldProperty, PinHiddenByDefault) 로 선언되어 런타임 setter 가 없다.
	// GASP 는 AnimGraph 의 property pin binding 으로 이 속성을 매 프레임 pure 함수로 구동.
	// 아래 BlueprintPure 헬퍼들은 ABP 의 set_anim_node_pin_binding 대상.

	// GASP IsMoving — Velocity 유효성(HasVelocity) + 예측 궤적(FutureVelocity) 모두 비영.
	// OffsetRootBone TranslationMode 분기 및 ABP MMState 전환 조건으로도 사용.
	// BlueprintThreadSafe 필수 — AnimGraph property pin binding 은 animation worker thread 에서 호출됨.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	bool IsMoving() const;

	// OffsetRootBone.TranslationMode pin binding — GASP 로직:
	// DefaultSlot 재생 중이면 Release(오프셋 흘려보냄), 아니면 Grounded+IsMoving 시 Interpolate,
	// 그 외(정지/공중) Release.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	EOffsetRootBoneMode GetOffsetRootTranslationMode() const;

	// OffsetRootBone.RotationMode pin binding — GASP 로직:
	// DefaultSlot 재생 중이면 Release, 아니면 Accumulate(회전 누적).
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	EOffsetRootBoneMode GetOffsetRootRotationMode() const;

	// OffsetRootBone.TranslationHalflife pin binding — GASP 로직:
	// Idle/Stopping 은 0.05(빠른 복구), Moving 은 0.2(부드러운 추종).
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	float GetOffsetRootTranslationHalfLife() const;

	// ========== GASP MotionMatching 상태 판정 Helpers ==========

	// GASP IsStarting — 이동 시작 순간(이전 프레임 Idle, 이번 프레임 Moving). MM Starts DB 선택에 사용.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	bool IsStarting() const;

	// GASP IsPivoting — Acceleration 과 Velocity 의 각도 차이가 큼 (급격한 방향 전환). MM Pivots DB 선택.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	bool IsPivoting() const;

	// GASP ShouldTurnInPlace — 정지 상태에서 Desired Controller Yaw 변화가 큼. TIP DB 선택.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	bool ShouldTurnInPlace() const;

	// GASP ShouldSpinTransition — 정지 → 이동 시작 시 현재 mesh 방향과 목표 방향 차이가 큼. SpinTransition 선택.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	bool ShouldSpinTransition() const;

	// GASP EnableSteering — OffsetRootBone Accumulate 모드 + Moving + 충분한 속도일 때 Steering 활성.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	bool EnableSteering() const;

	// GASP Get_OrientationWarpingWarpingSpace — OW 노드의 WarpingSpace pin 바인딩.
	// OffsetRootBoneEnabled=true → RootBoneTransform, false → ComponentTransform.
	// MM 의 BlendStack 내부 OrientationWarping 노드가 이 함수로 매 프레임 space 결정.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	EOrientationWarpingSpace GetOrientationWarpingWarpingSpace() const;

	// GASP CalculateRelativeAccelerationAmount — 캐릭터 local 좌표계 [-1..1] 가속도 벡터.
	// 가속 중이면 MaxAcceleration, 감속 중이면 MaxBrakingDeceleration 으로 정규화.
	// Get_LeanAmount 가 이 값의 Y(lateral) 컴포넌트를 BlendSpace X 입력으로 사용.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	FVector CalculateRelativeAccelerationAmount() const;

	// GASP Get_LeanAmount — Lean Additive BlendSpace(BS1D_Additive_Lean_Run) 의 X 입력.
	// 공식: X = RelAccel.Y * MapRangeClamped(Speed2D, [0..MaxRunSpeed], [0..1]); Y = 0.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	FVector2D GetLeanAmount() const;

	// BlendSpacePlayer.X pin binding 용 float 분리 헬퍼 — GetLeanAmount().X.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	float GetLeanAmountX() const;

	// GASP Get_AOValue — AimOffset BlendSpace(BS_Neutral_AO_Stand) 의 X,Y 입력.
	// RootTransform 과 Character BaseAimRotation 사이 delta (Yaw, Pitch).
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|AimOffset", meta=(BlueprintThreadSafe))
	FVector2D GetAOValue() const;

	// GASP Enable_AO — AimOffset 활성 조건. Apply Mesh Space Additive 의 Alpha 입력.
	// Strafe 모드 + 카메라-root yaw delta ≤ 90° + DefaultSlot 몽타주 비활성.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|AimOffset", meta=(BlueprintThreadSafe))
	bool EnableAO() const;

	// BlendSpacePlayer.X/Y pin binding 전용 — BlendSpacePlayer pin 이 float 두 개라 FVector2D 쪼개는 헬퍼.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|AimOffset", meta=(BlueprintThreadSafe))
	float GetAOValueX() const;

	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|AimOffset", meta=(BlueprintThreadSafe))
	float GetAOValueY() const;

	// Enable_AO 의 yaw 임계치 — GASP CDO 검증 결과 115° (이전 추정 90° → 정정 2026-04-27).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|AimOffset")
	float EnableAOYawThreshold = 115.0f;

	// GetSlotLocalWeight('DefaultSlot') 기준 Enable_AO 차단 임계치 — GASP 0.5 일치.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|AimOffset")
	float EnableAOSlotWeightThreshold = 0.5f;

	// GASP Get_MMBlendTime — MotionMatching 노드의 Blend Time 동적 제어.
	// 이동 전환 급격하면 짧게, 부드러우면 길게.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	float GetMMBlendTime() const;

	// GASP Get_MMInterruptMode — MotionMatching SetDatabasesToSearch 의 InterruptMode.
	// 기본 DoNotInterrupt, 특수 상황(Landing/Pivot 등)에서 InterruptOnDatabaseChange.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	EPoseSearchInterruptMode GetMMInterruptMode() const;

	// GASP JustLanded_Light / Heavy — LandVelocity 로부터 판정. Light/Heavy Lands PSD 선택.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	bool JustLandedLight() const;

	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	bool JustLandedHeavy() const;

	// GASP JustTraversed — Traversal 직후 프레임 감지. FromTraversal PSD 선택.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	bool JustTraversed() const;

	// GASP TimeToLand — InAir chooser 의 Far/일반 Jump 분기 입력.
	// Velocity.Z 기반 단순 kinematic 추정: 하강 속도로 LandPredictionHeight 만큼 떨어지는 데 걸리는 시간.
	// Grounded → 0, 상승 중 → 999, 하강 중 → LandPredictionHeight / |Vz| (clamp 999).
	// 라인트레이스 기반 ground 거리 캐시는 이미 있다 — DistanceToGround(UpdateEssentialValues 가 공중일 때 매 프레임 채운다).
	// 이 함수가 그것을 쓰지 않고 LandPredictionHeight 상수로 재계산하는 것은 현재 상태의 서술이지 미완이 아니다.
	// 연결하려면 FEEL 회귀 검증이 따라오므로 별건이다.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	float TimeToLand() const;

	// GASP multi-DB search pool — 모든 가능한 PSD 를 한 번에 SetDatabasesToSearch 에 전달.
	// MM 이 cost 가장 낮은 anim 을 DB 경계 무시하고 자유 선택 → Idle→Run_Start 자연 매칭.
	// CDO 의 ActivePoseSearchDatabases array 그대로 반환 (null 제거).
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	TArray<UPoseSearchDatabase*> GetAllActivePoseSearchDBs() const;

	// PoseSearchHistoryCollector.TransformTrajectory pin 의 property binding target.
	// Why: pin binding 은 BP function 호출이라 BlueprintReadOnly 변수 직접 binding 불가 → 명시 getter 필요.
	// 호출은 매 frame animation worker thread → BlueprintThreadSafe 필수.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	FTransformTrajectory GetTrajectory() const { return Trajectory; }

	// GASP PostSelection 헬퍼 — Update_MotionMatching_PostSelection 내부에서 호출.
	// ConvertToMotionMatchingNode + GetMotionMatchingSearchResult + Break struct 체인을 BP 에서 할 수 없어
	// (BreakStruct 가 MCP add_node 미지원) C++ 단일 함수로 wrap. 원본 로직 동일.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	void CacheMMSelectedDatabase(UPARAM(ref) const FAnimNodeReference& Node);

	// GASP GetDesiredFacing 의 cpp internal impl (FTransform 버전) — BP `GetDesiredFacing` 함수와 이름 충돌
	// 회피 위해 `GetDesiredFacingTransform` 으로 rename. BP 함수가 GASP 등가 (Quat return).
	// cpp 외부에서 직접 호출 안 함; GetDesiredFacingOrientation 의 internal helper 로만 사용.
	// Trajectory 의 미래 샘플(기본 0.4s 후)에서 Position + Facing 읽어 Transform 반환.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	FTransform GetDesiredFacingTransform() const;

	// Steering.TargetOrientation pin binding 용 Quat 전용 헬퍼 — GetDesiredFacingTransform().GetRotation().
	// Steering.TargetOrientation 이 FQuat 타입이라 FTransform→FQuat 중간 변환 회피.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|MotionMatching", meta=(BlueprintThreadSafe))
	FQuat GetDesiredFacingOrientation() const;

	// GetDesiredFacing 이 참조할 미래 샘플 시간 (초). GASP default ~0.4s.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float DesiredFacingLookaheadSeconds = 0.4f;

	// GASP GenerateTrajectory 이식 — 정지/이동 상태별 궤적 생성 파라미터. 값의 홈은 ABP Class Defaults 고
	// 코드는 UpdateMotionMatching 에서 읽기만 한다. 저작값 Moving=0/0/0, Idle=0/100/0
	// (RotateTowardsMovementSpeed / MaxControllerYawRate / BendVelocityTowardsAcceleration).
	// 엔진 디폴트 10/70/0 으로 되돌리지 말 것 — 2026-05-11 H23: Moving 이 10/70 이면 공중 마우스 yaw 가
	// 궤적을 70deg/s 로 돌려 lateral noise 를 넣고, MM 이 F/B Start 미러 변형 사이를 매 프레임 진동시킨다
	// (다리 흔들림 증상). 0 이면 궤적이 액터 공간에 고정된다. Idle 의 100 은 빠른 카메라 스핀 대응.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching|Trajectory")
	FPoseSearchTrajectoryData TrajectoryGenerationData_Idle;

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching|Trajectory")
	FPoseSearchTrajectoryData TrajectoryGenerationData_Moving;

	// GASP multi-DB search pool — Editor 에서 9 PSDs (Idles, Walk Starts/Loops/Stops, Run Starts/Loops/Stops/Pivots, TurnInPlace) 채움.
	// Update_MotionMatching 함수의 SetDatabasesToSearch 입력. CHT 단일 결과 + 단일 DB 한계 우회.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	TArray<TObjectPtr<UPoseSearchDatabase>> ActivePoseSearchDatabases;

	// GASP master CHT — GetAllActivePoseSearchDBs() 가 매 frame chooser 를 평가해 매칭되는 PSD array 를 반환.
	// Editor 에서 CHT_VB_PoseSearchDatabases (master) 로 설정. 평가 결과는 chooser internal 의 InAir/Stand/Walk/Run row 에서 동적으로 결정 — InAir 점프 anim, Idle/Walk/Run land anim 등 자동 분기.
	// Why: 이전 hardcoded index pick(0-8) 방식은 jump/land PSD 를 절대 못 고름 — chooser 가 정통 GASP 패턴.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	TObjectPtr<class UChooserTable> MasterChooserTable;

	// 궤적 샘플링 파라미터(History/Prediction 간격·개수)는 여기 없다. 값은 UpdateTrajectory 의
	// PoseSearchGenerateTransformTrajectory 호출 인자 리터럴이 유일한 홈이다 — 이유는 그 호출부 주석 참조.

	// GASP GenerateTrajectory 의 last-update yaw 추적 필드 — PoseSearchGenerateTransformTrajectory 가 ref 로 수정.
	// PreviousDesiredControllerYaw 와 별도 — 엔진 API 용 internal state.
	float DesiredControllerYawLastUpdate = 0.0f;

protected:

	// FIND-029 무기별 TIP 몽타주 맵 (2026-06-07) — 구 TIP_A/B_* 8슬롯(평상/전투 세트) 은퇴.
	// 왜 무기 키인가: VB TIP는 SM 경로(상태3=무기 든 전투, 또는 크라우치) 전용이라(bUsesStateMachinePath
	// 게이트) 현재 무기가 곧 알맞은 연기 세트다. user FEEL "적절한 애니메이션인가?" = 무기 불문
	// 카타나 턴이 나오던 문제의 구조적 해소. None=빈손 크라우치용(비크라우치 빈손은 MM 경로라 미발동).
	// 미등록 키(Magic 등)는 SelectTIPMontage가 nullptr → BeginTurnInPlace blend-out 폴백이 흡수.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|TurnInplace")
	TMap<EVBWeaponType, FVBTIPMontageSet> TIPMontageSets;

	// TIP 몽타주 판별용 모음 (NativeInitializeAnimation에서 TIPMontageSets 평탄화로 채워짐).
	// 하드코드 == 체인 대신 Contains로 판별 — EndTurnInPlace가 타 몽타주(무기 전환 연기 등)를
	// 오발 정지하지 않기 위한 멤버십 체크.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimMontage>> TIPMontages;


	// RotateRootBone 노드에 전달할 Yaw 역회전량
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	float RootYawOffset = 0.0f;

	// 무장 게이트 미러 — armed 로코모션 분기 전용 RotateRootBone 입력.
	// 왜: RootYawOffset 은 비무장 MM 중에도 누적되는데(TIP 어큐뮬레이터 무조건 갱신),
	// armed 분기 재연결 시 비무장 경로가 RotateRootBone 을 경유하므로 그대로 쓰면 MM 포즈가
	// 이중 회전한다. 비무장(LocomotionStateIndex==0)에선 0 을 보장하는 미러로 노드를 구동.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	float ArmedRootYawOffset = 0.0f;

	// RotateRootBone.Yaw 핀 바인딩용 (애님 워커 스레드에서 매 프레임 호출 — §13 GASP 헬퍼 패턴)
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation", meta=(BlueprintThreadSafe))
	float GetArmedRootYawOffset() const { return ArmedRootYawOffset; }

	// DEBT-006 Phase A: 무장 '이동 중' 턴-래그 (캡슐 요 델타 흡수-감쇠 스프링).
	// 왜: 무장 분기는 velocity 구동 인플레이스 8-way BS 라 카메라 턴이 통짜 회전으로 보임 —
	//   요 델타를 흡수했다가 감쇠시켜 OrientationWarping(Manual)에 공급하면 하체가 진행 방향을
	//   잠깐 유지하고 상체가 먼저 도는 끌림을 연기한다. idle 턴은 TIP 전담이므로 이동 중에만 누적.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	float ArmedTurnLagYaw = 0.0f;

	// OrientationWarping.OrientationAngle 핀 바인딩용 (ThreadSafe — 위 게터와 동일 패턴)
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation", meta=(BlueprintThreadSafe))
	float GetArmedTurnLagYaw() const { return ArmedTurnLagYaw; }

	// 현재 모드 
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	EVBRootYawOffsetMode RootYawOffsetMode = EVBRootYawOffsetMode::BlendOut;
	
	// CHT의 핵심 분기 조건 Idle/Moving/Stopping에 따라 다른 PSD 선택
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	EVBMMState MMState = EVBMMState::Idle;

	// GASP MovementState_LastFrame 대응 — 프레임간 전환 감지에 사용 (ABP의 Get_MMBlendTime 등 패턴).
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	EVBMMState MMState_LastFrame = EVBMMState::Idle;

	// GASP MovementMode/RotationMode/Stance enum 대응. ABP의 switch 분기에 사용.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	EVBMovementMode MovementMode = EVBMovementMode::Grounded;

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	EVBMovementMode MovementMode_LastFrame = EVBMovementMode::Grounded;

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	EVBRotationMode RotationMode = EVBRotationMode::Strafe;

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	EVBRotationMode RotationMode_LastFrame = EVBRotationMode::Strafe;

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	EVBStance Stance = EVBStance::Stand;

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	EVBStance Stance_LastFrame = EVBStance::Stand;
	
	// Run/Sprint 분기. Stopping 시에도 마지막 Gait를 유지해야 한다.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	EVBGait Gait = EVBGait::Run;
	
	// 이동 변수(ACharacter 공통 - 플레이어/적 모두 동작)
	
	// 전진(+)/후진(-) 속도 성분 - BlendSpace Y축
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	float ForwardSpeed;
	
	// 우(+)/좌(-) 속도 성분 - BlendSpace X축
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	float RightSpeed;
	
	// 지면 이동 속도(XY평면) - ShouldMove 판정용
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	float GroundSpeed;

	// GASP Speed2D — Velocity.Size2D와 동일. GroundSpeed와 중복이지만 GASP 네이밍 유지로 BP 이식 편의.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	float Speed2D = 0.0f;

	// GASP Velocity — 현재 프레임 Character 속도 (world space). UpdateEssentialValues 에서 매 tick 갱신.
	// IsPivoting / ShouldSpinTransition 등 GASP helper 가 직접 참조.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	FVector Velocity = FVector::ZeroVector;

	// GASP CharacterTransform — Character->GetActorTransform(). CalculateRelativeAccelerationAmount / AimOffset 등이 참조.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	FTransform CharacterTransform = FTransform::Identity;

	// GASP RootTransform — Mesh 의 "root" 본 world transform. AimOffset (Get_AOValue) 가 참조.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	FTransform RootTransform = FTransform::Identity;

	// GASP Velocity_LastFrame — 이전 프레임 속도. VelocityAcceleration 계산용.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	FVector Velocity_LastFrame = FVector::ZeroVector;

	// GASP VelocityAcceleration — 프레임 당 속도 변화량(Acceleration과 다름. 실제 delta-v).
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	FVector VelocityAcceleration = FVector::ZeroVector;

	// GASP LastNonZeroVelocity — 마지막으로 0이 아니었던 속도. Stopping 시 방향 참조용.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	FVector LastNonZeroVelocity = FVector::ForwardVector;

	// GASP HasVelocity / HasAcceleration — 매 tick 계산된 bool.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	bool HasVelocity = false;

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	bool HasAcceleration = false;

	// GASP Acceleration — CMC의 GetCurrentAcceleration.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	FVector Acceleration = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	float AccelerationAmount = 0.0f;

	// GASP Gait_LastFrame — Walk↔Run 전환 감지.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	EVBGait Gait_LastFrame = EVBGait::Run;

	// K 확장 후속 (2026-06-10): 무기 스왑 = MM 풀 교체 감지용. GetMMInterruptMode 의 기존 4종
	// (MovementMode/MMState/Gait/Stance) 은 정속 주행 중 무기 스왑(K↔F 등 MM↔MM)에서 아무것도 안 변해
	// DoNotInterrupt → 이전 무기 풀의 continuing pose 가 잔존("달리면서 슬롯키 눌러도 안 바뀜").
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	EVBWeaponType MMWeaponType_LastFrame = EVBWeaponType::None;
	// 풀 교체 interrupt 는 보폭 수렴 후 1회 발화한다 (user 트레이스 06-10). 스왑 프레임 즉시 발화하면
	// 비무장500→무장350 감속 과도기에 첫 검색이 옛 속도 정합 클립(Fighter_Run_Root 549)을 골랐다가
	// 수렴 후 jog 로 재선택 — 두 번 갈아타는 블립. 래치해 뒀다가 |Speed2D−MaxWalkSpeed| 수렴(또는 저속/
	// 타임아웃) 시 발화하면 보폭 동일 스왑(K↔F)은 즉시, 발도/납도는 이전 풀이 감속을 자연 연기 후 1회 전환.
	bool  bPendingPoolInterrupt = false;   // 래치 (game thread 전용)
	float PoolInterruptPendingTime = 0.0f; // 래치 경과(초) — 타임아웃 보장
	// 발도↔납도 풀 스왑 감지 (FIND-030, user FEEL 07-05 "걸으면서 Tab 하면 바로 안 바뀜"). BigSword 는
	// 상태2(납도)=Sheathed 풀 / 상태3(발도)=Stand 풀 로 CHT 가 갈리지만 두 상태 모두 MMWeaponType=BigSword 라
	// 위 MMWeaponType 래치가 안 걸린다 → continuing pose 잔존으로 전환 지연. 전 프레임 sheathed 여부와 비교해
	// 변화 시 즉시 interrupt (보폭 동일 무장 gait 스왑이라 정착 대기 불필요).
	bool  bMMPoolSheathed_LastFrame = false; // 게임 스레드 전용
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	bool  bFirePoolInterrupt = false;      // 이번 프레임 발화 — GetMMInterruptMode(worker) 소비

	// Sprint 풀 합류 자격 (발도/스왑 블립 루트 코즈 픽스, 2026-06-11 로그 실증). 발도 순간 보폭 게이트는
	// 즉시 350 으로 떨어지지만 실속도는 관성 감쇠(500→443→394, 3~4프레임) — 그 구간이 Speed2D 단독 게이트
	// (>=420)를 통과시켜 Sprint 풀(Run_Root 등 고속 클립)이 2프레임 합류→탈락. continuing DB 탈락은 interrupt
	// 모드와 무관하게 강제 재검색이라 위 래치로 못 막음 → jog→sprint클립→jog 이중 스냅 = 블립.
	// 자격 = 의도(bIsSprinting) || 감속 히스테리시스(자격 유지 중 Speed2D>=SprintPoolJoinSpeed) —
	// 속도는 전환 순간 거짓말을 하지만 입력 의도는 즉시 진실. sprint 해제 감속 핸드오프는 히스테리시스가 보존.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	bool bSprintPoolEligible = false;

	// Sprint 풀 합류 속도 하한. 이 값(500)은 3곳이 반드시 합의해야 하는 단일 계약값이다(FIND-045 P2 C부록, 07-25):
	//   ① 여기(C++ SprintPoolJoinSpeed) → bSprintPoolEligible 히스테리시스 하한
	//   ② CHT_VB_PoseSearchDatabases_Fighter_Dense 의 Sprint 합류 행 FloatRange Min 셀
	//   ③ CHT_VB_PoseSearchDatabases_Katana_Dense 의 Sprint 합류 행 FloatRange Min 셀
	//   (BigSword Dense 는 sprint 없음=순항 캡이라 제외. 무기 Dense CHT 추가 시 그 셀도 이 목록에 편입할 것.)
	// MMSprintGateSpeed 가 ①의 자격을 ②③ 컬럼 입력으로 나른다 → 세 값이 어긋나면 히스테리시스 창과 풀 게이트가
	// 불일치해 발도/스왑 감쇠 구간에 sprint 클립이 2프레임 합류→탈락 = 블립/스케이트 재발(06-11 라운드3 이력).
	// chooser 셀은 에셋이라 C++ 이 못 읽는다 → 자동 단일화 불가. 이 주석이 유일한 드리프트 방어이니 한 곳 바꾸면 3곳 다 바꿀 것.
	// 500 = Run 순항(430)보다 위·Sprint 1단(585)보다 아래 — 순항이 히스테리시스에 영구 걸리는 것 방지 (구 420은 430 순항에서 파탄).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float SprintPoolJoinSpeed = 500.0f;

	// 풀 교체 interrupt 수렴 판정 - 현재 속도가 CMC 목표 최대속도에 이만큼(cm/s) 이내로 붙으면 보폭이 수렴한 것으로 본다.
	// 왜 기다리는가: 무장 전환 직후 속도가 감쇠하는 과도기에 발화하면 MM 이 옛 속도에 맞는 클립을 골랐다가
	//   수렴 후 다시 고른다 - 한 번의 스왑이 화면에서 두 번 갈아타는 블립이 된다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float PoolInterruptSpeedSettleTolerance = 40.0f;

	// 위 수렴 판정의 두 번째 통로 - 거의 정지(cm/s)면 목표속도와의 차이와 무관하게 수렴으로 본다.
	// 왜 별도 통로인가: 정지 중에는 목표 최대속도가 여전히 크게 남아 있어 허용오차만으로는 영영 수렴하지 않는다.
	// ArmedTurnLagMinSpeed 와 숫자가 같지만 다른 값이다. 한쪽을 튜닝했다고 다른 쪽이 따라가면 안 된다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float PoolInterruptNearStopSpeed = 10.0f;

	// 수렴을 못 본 채 이 시간(초)이 지나면 강제로 발화한다. 안전장치다.
	// 없으면 목표속도가 계속 흔들리는 상황에서 래치가 안 풀려 무기를 바꿔도 풀이 갈리지 않는다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float PoolInterruptTimeoutSeconds = 0.7f;

	// Dense CHT Sprint 합류 행 전용 게이트 값 (자격 없으면 0 = 행 차단, 자격 있으면 Speed2D).
	// 별도 컬럼인 이유: 기존 Speed2D 컬럼은 Idle 행들이 Max=20/999 로 실사용 중이라 입력을 갈아끼우면
	// 비자격(0) 프레임마다 Idle 행이 이동 중에도 매칭된다. 신규 컬럼의 비-Sprint 행 셀은 반드시
	// bNoMin/bNoMax=True 명시 — 빈 셀 () 은 [0,0] 으로 파싱되는 기존 트랩 (2026-06-09).
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	float MMSprintGateSpeed = 0.0f;
	
	// 공중 상태(점프/낙하)
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	bool IsFalling;
	
	// 이동 중 여부 (GroundSpeed > 3.0f) AI는 Acceleration이 작동하지 않아서 가속 입력 제거
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	bool ShouldMove;
	
	// 앉기 상태(IsCrouched - 플레이어/적 공통)
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	bool bIsCrouching;
	
	// ========== 플레이어 전용 변수 ==========
	
	// 전투모드 — WSC->IsInCombat() 미러 (DEC-006: 상태2·3 모두 true).
	// "TIP A/B 세트 선택에 사용" 이라던 서술은 거짓이 됐다(2026-08-17 정정): 구 A/B(평상/전투) 세트는 FIND-029
	//   무기별 리모델로 은퇴했고(VBAnimInstance.cpp:1037) SelectTIPMontage 는 CurrentWeaponType 으로 키를 잡는다.
	//   현재 C++ 소비처는 0이고 대입(:262)만 남아 있다. BlueprintReadOnly 라 ABP 가 읽을 여지는 있으나
	//   그 여부는 미확인 - 제거하려면 ABP 그래프 판독이 선행돼야 한다.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	bool bIsCombatMode;

	// 스프린트 상태
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	bool bIsSprinting;

	// 캐릭터 카메라 yaw 차이 - TurnInPlace 트리거용	
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	float AimYaw;
	
	// Tip 에셋 선택 (Standing)
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	EVBTurnInPlaceType TurnType;
	
	// Tip 에셋 선택 (Crouch)
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	EVBTurnInPlaceType CrouchTurnType;
	
	// TIP이 트리거 되어야 하는지
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	bool bShouldTurnInPlace;

	// 메시가 캡슐보다 얼마나 덜 돌았는가(도). MM 경로 TIP 게이트의 입력이다.
	// RootYawOffset 과의 차이: 저쪽은 우리가 누적·감쇠로 '추정'한 장부값이고, 이쪽은 OffsetRootBone
	//   노드의 SimulatedRotation 과 컴포넌트 회전의 차이를 매 프레임 '측정'한 값이다. 클립 루트모션이
	//   메시를 돌리면 줄고 캡슐이 돌면 늘어 노드가 이미 루프를 닫아 준다 - 그래서 MM 경로에는
	//   소비자(몽타주 TurnYaw 커브)가 없어도 된다. FIND-029, 설계 .sw/armed-tip-rotation/design.md.
	// 값은 노드 평가 뒤에 갱신되므로 한 프레임 지연이다(게이트 판정에는 무해).
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	float MeasuredRootYawOffset = 0.0f;
	
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	float DistanceToGround;
	
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	bool bIsLockedOn = false;
	
	// Orientation Warping 입력
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	float LocomotionAngle = 0.0f;

	// 이전 frame LocomotionAngle — direction change 감지로 MM Force interrupt 트리거. NativeUpdateAnimation 말미에서 cache.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	float PreviousLocomotionAngle = 0.0f;

	// MM PoseSearchHistoryCollector에 공급할 trajectory. VBCharacter.CharacterTrajectoryComponent에서 매 tick 복사.
	// ABP AnimGraph에서 이 변수를 PoseSearchHistoryCollector.TransformTrajectory 핀에 연결해야 MM이 미래 궤적 기반 매칭 수행.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	FTransformTrajectory Trajectory;

	// GASP HandleTransformTrajectoryWorldCollisions 결과 — TimeToLand 필드 보유.
	// CHT_VB_PoseSearchDatabases_Dense:InAir col 0 ("Time to Land") 의 binding target.
	// 이전 Vowbound TimeToLand UFUNCTION (Vz 부호 의존, 상승 999/하강 H/|Vz|) 은
	// 점프 정점에서 임계 1.2 fluctuation → InAir chooser PSD pool oscillation 유발.
	// HandleTransformTrajectoryWorldCollisions 는 trajectory 미래 샘플을 trace 로 ground impact 시점 계산 →
	// 상승/하강 부호 무관 smooth 한 TimeToLand 반환 → row 1 (Jumps_Far) 안정 매칭.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	FPoseSearchTrajectory_WorldCollisionResults TrajectoryCollision;

	// Trajectory 마지막 샘플의 속도. GASP의 IsMoving()이 Velocity != 0 && FutureVelocity != 0 판별용.
	// OffsetRootBone / MM 전환 판단에 사용.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	FVector FutureVelocity = FVector::ZeroVector;

	// OffsetRootBone 노드 활성화 토글 — BP AnimGraph에서 노드의 Alpha/Enable에 바인딩.
	// 전환·디버깅 용도. 기본 true (GASP 스타일 활성).
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	bool bOffsetRootBoneEnabled = true;

	// GASP MMDatabaseLOD — Master CHT 의 Dense/Sparse 분기 입력. CVar `vb.MM.LOD` 가 매 frame 갱신.
	// 0=Dense (모든 PSD), 1=Sparse (Loops/Pivots/Starts/Stops 만). CHT 가 BR 컬럼으로 사용.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	int32 MMDatabaseLOD = 0;
	
	// PlayRate 조절
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	float WantedPlayRate = 1.0f;

	// GASP CurrentSelectedDatabase — PostSelection 에서 MotionMatchingSearchResult 로 캐시되는 현재 선택 DB.
	// EventGraph 에서 이 DB 의 tags 를 읽어 CurrentDatabaseTags 에 저장 (thread safety 우회).
	UPROPERTY(BlueprintReadWrite, Category="Vowbound|Animation|MotionMatching")
	TObjectPtr<const UPoseSearchDatabase> CurrentSelectedDatabase = nullptr;

	// GASP CurrentDatabaseTags — CurrentSelectedDatabase->Tags (TArray<FName>) 를 CacheMMSelectedDatabase 에서 복사.
	// IsStarting / ShouldSpinTransition 이 이 태그를 검사 — FName.Contains("Pivots"), "SpinTransition" 존재 여부.
	// UPoseSearchDatabase::Tags 타입이 엔진상 TArray<FName> 이므로 FName 배열 그대로 사용 (원본 동일).
	UPROPERTY(BlueprintReadWrite, Category="Vowbound|Animation|MotionMatching")
	TArray<FName> CurrentDatabaseTags;

	// GASP IsStarting 의 Velocity→FutureVelocity delta bias — GASP CDO 검증 결과 100 cm/s (정정 2026-04-27).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float IsStartingVelocityBias = 100.0f;

	// GASP IsPivoting 의 Strafe/Free 모드별 각도 임계치 — GASP CDO 검증 결과:
	// Strafe=40° (이전 추정 45°), Free(OrientToMovement)=60° (이전 추정 90°). 정정 2026-04-27.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float IsPivotingStrafeThreshold = 40.0f;

	// GASP 는 Free 60°. VB 는 맨틀 종료 직후 회전 시 루트모션 과속(속도 ~578 > MaxWalkSpeed 500)으로 회전
	// 반경이 커져 0.5s 예측창의 pivDelta 가 ~48° 까지만 올라 60° 문턱을 못 넘음 → IsPivoting 미발화 → chooser 가
	// Run_Pivots 대신 FromTraversal(Hurdle, cost 1.6)로 흘러 종료 컷. (GASP RewindDebugger 2026-06-04 대조:
	// GASP 는 같은 회전을 Run_Pivots/Run_Turn 으로 cost 0.1~0.35 에 매칭. GASP 는 과속 없어 pivDelta≥60° 자연 발화.)
	// VB 의 과속이 사라지기 전까지 40° 로 낮춰 회전이 Run_Pivots 를 켜게 한다(behavior 는 GASP 일치, 값만 보정).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float IsPivotingFreeThreshold = 40.0f;

	// GASP ShouldTurnInPlace 의 root/capsule yaw delta 임계치 (원본 md 기준 50°).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float ShouldTurnInPlaceYawDelta = 50.0f;

	// GASP ShouldSpinTransition 의 각도 임계치 + 속도 임계치 (원본 md §3.2 기준 130°).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float ShouldSpinTransitionYawDelta = 130.0f;

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float ShouldSpinTransitionMinSpeed = 150.0f;

	// GASP RootTransform yaw 보정 — mesh 가 world -Y forward 인 관례라 +90° 보정.
	// Vowbound mesh 가 다른 orientation 쓰면 이 값 0 으로.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float RootTransformYawCorrection = 90.0f;

	// GASP HeavyLandSpeedThreshold — 낙하 수직 속도가 이 값 이상이면 JustLandedHeavy.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float HeavyLandSpeedThreshold = 700.0f;

public:
	// GASP JustLanded / LandVelocity — VBCharacter::Landed 콜백이 채워 넣음. JustLanded_* helper 가 소비.
	// public: C++ 에서 VBCharacter 가 직접 쓰기 접근 필요 — BlueprintReadWrite 만으로는 C++ 비접근.
	UPROPERTY(BlueprintReadWrite, Category="Vowbound|Animation|MotionMatching")
	bool JustLanded = false;

	UPROPERTY(BlueprintReadWrite, Category="Vowbound|Animation|MotionMatching")
	FVector LandVelocity = FVector::ZeroVector;

	// GASP JustLanded grace duration — CBP_SandboxCharacter::OnLanded 의 Retriggerable Delay 값 (0.3s).
	// JustLanded 가 이 시간 동안 true 유지 → Lands PSD 가 nested chooser 의 후보로 살아있어
	// Land anim 이 즉시 다른 anim 으로 jump 하지 않고 자연 진행.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching", meta=(ClampMin="0.0"))
	float JustLandedGraceDuration = 0.3f;

	// JustLanded 가 false 로 자동 clear 될 절대 시각 (World->GetTimeSeconds() 기준).
	// VBCharacter::Landed 가 매번 갱신 (retriggerable). NativePostEvaluateAnimation 에서 시각 비교.
	UPROPERTY(Transient, BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	float JustLandedClearTime = 0.0f;

protected:

	// bDoingTraversalAction 은 2026-08-09 삭제됐다. C++ 이 읽지도 쓰지도 않았고 자산 참조도 0이었다.
	// 트래버설 진행 여부의 정본은 UVBTraversalComponent::IsDoingTraversal() 이다 - 같은 이름의 필드가
	// 두 클래스에 있어서, ABP 에서 판정하려는 사람이 죽은 쪽을 잡을 위험이 실재했다.

	UPROPERTY(BlueprintReadWrite, Category="Vowbound|Animation|MotionMatching")
	bool bJustTraversedFlag = false;

	// GASP PreviousDesiredControllerYaw — 이전 프레임 controller yaw. TIP 트리거용 delta 계산.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation|MotionMatching")
	float PreviousDesiredControllerYaw = 0.0f;

	// NOTE: 이전에 내가 추정으로 만든 Pivot/Spin/TIP 임계치는 GASP 원본 로직으로 재작성됨.
	// 새 임계치는 IsPivotingStrafeThreshold/IsPivotingFreeThreshold/ShouldSpinTransition*/ShouldTurnInPlaceYawDelta 로 분리.

	// GASP Get_MMBlendTime 원본 4분기 상수값 — CDO 검증 결과 (정정 2026-04-27):
	//   Default(Grounded↔Grounded): 0.4s (이전 추정 0.3)
	//   FastLand(Grounded←InAir): 0.2s (이전 추정 0.1, 너무 짧음)
	//   VeryShortJump(InAir+Vel.Z>thr): 0.15s (이전 추정 0.05)
	//   InAirDefault(InAir 안정): 0.5s (이전엔 Default 와 같이 묶음 — GASP 분리)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float MMDefaultBlendTime = 0.4f;

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float MMFastLandBlendTime = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float MMVeryShortJumpBlendTime = 0.15f;

	// InAir 안정 상태(점프 직후 Vel.Z 큰 순간 지나면)에서 사용. GASP 0.5s.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float MMInAirDefaultBlendTime = 0.5f;

	// GASP JumpSpeedThreshold — Velocity.Z 가 이 값 이상이면 "점프 시작" 판정.
	// CDO 검증 결과 100 cm/s (이전 500은 거의 트리거 안 됨, 정정 2026-04-27).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float MMJumpSpeedThreshold = 100.0f;

	// GASP OffsetRoot translation halflife — MMState 별 분리 노출.
	// GASP CDO 정렬 (2026-05-16): Idle=0.10, Moving=0.20.
	// EVBMMState 이 Idle/Moving 만 사용하므로 Stopping/Starting 별 값 제거 (cleanup).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float OffsetRootTranslationHalfLife_Idle = 0.10f;

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float OffsetRootTranslationHalfLife_Moving = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float OffsetRootTranslationHalfLife_Default = 0.1f;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float MinPlayRate = 0.8f;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float MaxPlayRate = 1.5f;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float RunAnimRootMotionSpeed = 500.0f;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float WalkAnimRootMotionSpeed = 200.0f;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	FRuntimeFloatCurve SpeedToPlayRateCurve;
	
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	EVBLocomotionState LocomotionState = EVBLocomotionState::Unarmed;
	
	// ABP Blend Poses by int에서 직접 사용.
	// DEC-007 Phase A: enum 직캐스트(0~2)에 crouch 전용 핀 3 이 추가된 4핀 인덱스 공간 —
	// 0=Unarmed MM / 1=상태2·3 MM(Fighter/Katana/BigSword) / 2=상태3 SM / 3=crouch SM (MM에 crouch 포즈 없음).
	// pin2(index 2)는 죽은 코드가 아니다: 상태3 인데 MMWeaponType==None 인 경우 = 비-MM 전투무기 fallback.
	//   현재 유일 도달 케이스 = Magic 무기(Armed MM 미구축, EVBWeaponType::Magic). F/K/B 는 index 1(MM)로 감.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	int32 LocomotionStateIndex = 0;

	// DEC-007 Phase A: SM 경로 사용 여부 (pin2=상태3 SM / pin3=crouch SM) — 게이트 3곳의 단일 진실원.
	// MM 재사용 핀(0·1)은 MM 전용 게이트(OffsetRootBone Accumulate + MM TIP PSD)를 타고,
	// SM 경로만 RotateRootBone+VB TIP 흡수를 쓴다. 게임 스레드 기록 / 워커 읽기 (기존 멤버 패턴).
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	bool bUsesStateMachinePath = false;
	
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	EVBWeaponType CurrentWeaponType = EVBWeaponType::None;

	// DEC-007 P4: CHT WeaponType 분기(MasterChooserTable) 바인딩 전용 — MM 노드가 검색할 무기 풀을 가른다.
	// 값의 출처는 무기 데이터다(2026-08-14 DEBT-010): UVBWeaponStateComponent::GetMMPoolForState 가
	//   WeaponDataMap 의 ArmedMMPool/SheathedMMPool 을 읽어 돌려주며, 여기서 무기 이름을 나열하지 않는다.
	//   None = 무기 없는 기본 풀. 평상·전용 풀이 없는 무기·미등록 무기가 전부 None 이다.
	// CurrentWeaponType(선택무기 기억 — TIP 몽타주/재발도용) 과 의미 분리.
	// AVBCharacter::UpdateMaxSpeed 가 같은 값으로 속도 프로파일도 고른다 — 클립과 보폭의 출처를 하나로 묶어
	//   워프비가 윈도우를 벗어나지 않게 한다.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	EVBWeaponType MMWeaponType = EVBWeaponType::None;

	// 무기별 BlendListByInt 의 ActiveChildIndex 입력. 값의 의미는 아래 WeaponPoseSlots 가 정한다.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	int32 WeaponTypeIndex = 0;

	// DEC-008: crouch BS 포즈 인덱스. 값의 의미는 아래 CrouchPoseSlots 가 정한다.
	// ABP ArmedCombatLocomotion 의 CrouchMoving/CrouchIdle BlendListByInt ActiveChildIndex 입력.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	int32 CrouchPoseIndex = 0;

	// ABP 의 무기별 BlendListByInt 핀 순서를 선언한다. 인덱스가 곧 핀 번호이고 배열 길이가 곧 핀 개수다.
	//
	// 왜 enum 을 그대로 핀 번호로 쓰지 않는가: EVBWeaponType 은 append-only 라 콘텐츠 없는 선언도 영구히
	//   남는다. enum 직결이면 그 빈자리마다 핀을 만들어야 하고, 만들지 않으면 엔진이 마지막 자식으로
	//   클램프해 새 무기가 조용히 다른 무기의 포즈를 재생한다(FAnimNode_BlendListByInt::GetActiveChildIndex).
	//   핀은 저작된 포즈가 있을 때만 생기므로 핀 순서의 홈은 enum 이 아니라 그 포즈를 담은 자산이다.
	//
	// 왜 여기(ABP CDO)인가: 핀을 늘리는 편집과 이 배열을 늘리는 편집이 같은 자산, 같은 세션에서
	//   일어난다. 두 편집이 한 작업이 되면 어긋날 자리가 없어진다. 또한 배열 길이와 실제 핀 개수를
	//   정적으로 대조할 수 있어, 불일치가 PIE 가 아니라 리드백에서 잡힌다.
	//
	// 2026-08-19 이전에는 이 배열이 없고 enum 값이 곧 핀 번호였다. None(0) 이 자기 콘텐츠가 없어
	//   핀0 을 카타나에 물렸고, 그 결과 핀0·1 이 같은 노드를 가리키는 팬아웃이 8곳에 생겼다
	//   (한 노드가 한 프레임에 두 번 평가돼 애니가 2배속이 되고, UE5.8 2-pose 고속경로가 에디터를 죽인다).
	//   조밀 인덱스는 그 남는 핀 자체를 없애 팬아웃이 생길 자리를 지운다.
	//
	// 저작 규칙: 핀을 추가할 때만 행을 추가한다. 배열에 없는 무기는 인덱스 0 + 경고로 떨어진다 —
	//   클램프처럼 조용히 틀리지 않는다.
	//   예외는 None 하나다. 이 로스터는 조밀해서 None 항목이 없지만 평상 상태가 곧 None 이므로
	//   결함이 아니다 — ResolveWeaponPoseIndex 가 조회 전에 0 으로 돌려보내고 경고도 내지 않는다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|PoseContract")
	TArray<EVBWeaponType> WeaponPoseSlots;

	// crouch 계열의 핀 공간은 위와 다르다. 여기서는 인덱스 0 이 자기 콘텐츠를 가진다
	//   (납도 공용 ver_A) — 무기별 포즈를 빌려 쓰는 자리가 아니므로 None 항목이 정당하다.
	// ArmedExplorationLocomotion 의 crouch 는 이 배열이 아니라 WeaponPoseSlots 를 쓴다(납도 공용이
	//   카타나 포즈와 같아 별도 슬롯이 필요 없다).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|PoseContract")
	TArray<EVBWeaponType> CrouchPoseSlots;

	// 무기 -> BlendListByInt 핀 번호. 미등록은 0 + 최초 1회 경고.
	// 매 프레임 캐릭터마다 호출되지만 로스터가 3~4개라 선형 탐색 비용이 잡음 수준이다(캐시 불필요).
	//   캐시로 옮길 시점: 로스터가 16~20개를 넘거나, GT 프로파일에서 이 함수가 잡음 위로 올라올 때.
	//   그때는 NativeInitializeAnimation 에서 TMap<EVBWeaponType,int32> 를 한 번 만들어 쓴다.
	int32 ResolveWeaponPoseIndex(EVBWeaponType InWeaponType);
	int32 ResolveCrouchPoseIndex(EVBWeaponType InWeaponType);

	// 같은 무기로 매 프레임 경고가 쏟아지지 않게 마지막으로 경고한 무기를 기억한다. 게임 스레드 전용.
	EVBWeaponType LastWarnedPoseSlotWeapon = EVBWeaponType::None;
	EVBWeaponType LastWarnedCrouchSlotWeapon = EVBWeaponType::None;

	// 사망 여부 - State.Dead 태그를 매 tick 미러링한다.
	// ABP 가 사망 시 두 노드를 무력화하는 데 쓴다. 이 두 노드는 살아있는 로코모션 전용이고
	// 사망 포즈에는 능동적으로 해롭다.
	//   OffsetRootBone: 메시와 캡슐의 위치차를 흡수한다(Interpolate, maxTranslationError=30).
	//     사망 몽타주의 루트모션이 캡슐을 끌고 가도 그 델타를 오프셋으로 먹어 화면에 안 나타난다.
	//   LegIK: Alpha 가 상수 1 이라 누워 있는 포즈의 발까지 지면에 맞추려 해 다리가 뒤틀린다.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	bool bIsDead = false;

	// 사망 시 OffsetRootBone 의 Translation/Rotation 모드로 넣을 값.
	// Release = 누적된 오프셋을 놓아 메시가 캡슐에 곧바로 붙는다.
	// 평상시에는 각각 Interpolate / Accumulate 를 유지한다(노드 기본값과 동일).
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	EOffsetRootBoneMode OffsetRootTranslationMode = EOffsetRootBoneMode::Interpolate;

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	EOffsetRootBoneMode OffsetRootRotationMode = EOffsetRootBoneMode::Accumulate;

	// 사망 시 0 - LegIK Alpha 핀에 물린다.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Animation")
	float LegIKAlpha = 1.0f;

	// AnimGraph 핀 바인딩은 변수가 아닌 함수만 받는다. enum 2종은 UHT 가 접근자를 자동 생성해 주지만
	// float 은 생성되지 않아 직접 둔다. 값 계산은 UpdateDeathState 에서 끝나고 여기서는 읽기만 한다.
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation")
	float GetLegIKAlpha() const;


private:
	// State.Dead 를 미러링해 bIsDead / OffsetRoot 모드 2종 / LegIKAlpha 를 갱신한다.
	void UpdateDeathState(const APawn* OwnerPawn);

	// GASP UpdateEssentialValues 대응 — 매 tick 속도/가속/trajectory 파생 변수 갱신.
	// NativeUpdateAnimation 시작 부분에서 호출.
	void UpdateEssentialValues(float DeltaSeconds, class ACharacter* Character, class UCharacterMovementComponent* CMC);

	// R-006 추출(2026-07-05, Design_VBAnimInstance_Refactor.md): NativeUpdateAnimation 인라인 phase 분할.
	// 동작 보존(byte-identical 로직 relocate) — 각 헬퍼는 this 멤버를 조작하며 호출 순서·불변식 유지가 계약.
	void UpdateTrajectory(float DeltaSeconds, class ACharacter* Character);            // GASP 궤적 예측+충돌 → Trajectory/FutureVelocity/TrajectoryCollision
	void UpdateLocomotionAngle(float DeltaSeconds, const class ACharacter* Character); // Velocity 방향 보간 → LocomotionAngle (OW 입력)
	void UpdatePlayRate();                                                             // 속도/Gait → WantedPlayRate (SpeedRemappingCurve)
	void UpdateMovementStateAndGait(bool bHasMovementInput, bool bSprintActive, EVBGait CharacterGait); // → MMState/Gait/LastMovingGait
	void UpdateTurnLag(float DeltaSeconds, float ActorYawDelta);                       // DEBT-006 무장 이동 턴-래그 스프링 → ArmedTurnLagYaw
	// FEEL-critical (FIND-030). R-007 풀 교체 interrupt 2 트리거(수렴 래치 + 발도↔납도 즉시) 통합.
	//   동작 보존 필수 — 변경 후 BigSword 발도/납도 FEEL 재확인할 것.
	void EvaluatePoolInterrupt(float DeltaSeconds, class UCharacterMovementComponent* CMC, EVBLocomotionState RawState); // → bFirePoolInterrupt

	// GASP UpdateStates 대응 — MMState/MovementMode/RotationMode/Stance/Gait 전환 + _LastFrame 캐시.
	// UpdateEssentialValues 이후 호출.
	void UpdateStates();

	// AimYaw로부터 TurnType 계산
	// AimYaw: 캐릭터<->카메라 각도 차이
	// 반환: TIP 타입(None / TurnLeft90 / TurnRight90 / Turn180)
	EVBTurnInPlaceType CalculateTurnType(float InAimYaw) const;
	
	// CMC 회전 모드 진실 캐시 (FIND-019 U1) — 게임스레드(NativeUpdateAnimation)에서 기록,
	// ShouldTurnInPlace(워커스레드)가 읽는다. RotationMode enum 은 MM/CHT 선택 품질 때문에
	// 비무장=OrientToMovement 를 유지하므로(UpdateStates 주석), "캡슐이 실제로 카메라를 추종 중인가"는
	// 이 값이 진실원이다. enum 으로 판정하면 idle TIP 가 영구 불발(이원화 버그).
	bool bCMCStrafeRotation = false;

	// 몽타주 경로가 제자리 회전을 소유하는가 — 게임스레드(UpdateTurnInPlaceState)에서 기록,
	// ShouldTurnInPlace()(워커스레드)가 읽는다. 그 함수가 CHT 의 ShouldTurnInPlace 열에 묶인
	// 열쇠라, 소유권을 거기서 밝히지 않으면 MM TIP 풀이 몽타주와 나란히 열린다(2026-08-30 실증).
	bool bTurnProfileOwnsTIP = false;

	// «아직 돌아야 할 회전량» - 정규화하지 않은 부호 있는 각 (2026-09-04, Codex 설계).
	// AimYaw 는 -180..180 으로 정규화된 최단 경로라 오른쪽으로 200 도를 돌리면 -160 으로 읽혀 왼쪽 클립이 나간다
	//   (2026-09-04 user PIE 로그 5건). 종전에는 «감은 양» 을 따로 세고 AimYaw 의 부호와 조합해 방향을 추측했는데,
	//   그 추측의 경계(먼 길 195)에서 입력 1도 차이로 방향이 뒤집혔다.
	// 이 값은 매 프레임 AimYaw 의 변화량을 정규화해 더한다 - 회전 중에도. 캡슐이 도는 만큼 자연히 줄고,
	//   잔여 위에 되감으면 그냥 덧셈이다. 요청의 방향과 크기가 이 값 하나에서 나온다. 중립에서 AimYaw 로 다시 맞춘다(표류 방지).
	float UnwrappedRemainingYaw = 0.0f;
	float PrevAimYawForUnwrap   = 0.0f;


	// 이전 프레임 Actor Yaw 저장
	float ActorYawLastFrame = 0.0f;
	bool bHasInitializedActorYawLastFrame = false;
	float PreviousTurnYawCurve = 0.0f;
	bool bPostTIPBlendOut = false;
	
	
	void UpdateRootYawOffset(float DeltaSeconds, float ActorYawDelta);
	// OffsetRootBone 노드에서 메시-캡슐 회전 오프셋을 읽어 MeasuredRootYawOffset 을 갱신한다.
	// 노드는 ABP 의 AnimNodeProperties 를 타입으로 훑어 한 번만 찾고 캐시한다(이름/인덱스에 의존하지 않아
	//   ABP 를 재배선해도 안 깨진다). 노드가 없으면 조용히 0 을 유지 - 그 경우 MM TIP 은 발동하지 않는다.
	void UpdateMeasuredRootYawOffset();
	// 위 함수가 처음 한 번만 해석해 캐시한다. UObject 가 아니라 GC 대상이 아니며, 노드 실체는 이
	// AnimInstance 인스턴스 안에 있어 수명이 같다. 해석 실패도 캐시해 매 프레임 재탐색을 막는다.
	struct FAnimNode_OffsetRootBone* CachedOffsetRootBoneNode = nullptr;
	bool bOffsetRootBoneNodeResolved = false;


	void UpdateTurnInPlaceState();
	void BeginTurnInPlace();
	void ConsumeTurnInPlaceCurve();
	void EndTurnInPlace(bool bInterrupted);

	// FIND-029 (2026-06-07): TIP 몽타주(또는 세그먼트 소스 시퀀스)에 TurnYaw 커브가 실재하는지.
	// 커브 없는 몽타주로 TIP에 진입하면 ConsumeTurnInPlaceCurve가 소비할 값이 없어
	// RootYawOffset이 TIPExitThreshold 아래로 못 내려감 → bShouldTurnInPlace 영구 Hold
	// → 누적 정지 → 메시가 캡슐(카메라)을 1:1 추종하는 stuck 버그.
	bool MontageHasTurnYawCurve(const UAnimMontage* Montage) const;

	// FIND-029 안전망: TIP 몽타주 종료(자연/캔슬 공통) 시 상태 강제 정리.
	// TIP는 DefaultSlot(무기 전환 연기와 공유)에서 캔슬될 수 있어 커브-소비 탈출만으로는 부족.
	void OnTIPMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// 커브 부재 경고 1회 가드 (틱 단위 재발동 경로라 스팸 방지)
	bool bLoggedTIPCurveMissing = false;

	// FIND-105 루트모션 TIP (2026-08-28). 프로필이 있는 (무기 x 자세) 조합에서만 이 경로가 회전을 만든다.
	// 회전을 «몸» 에만 가진 옛 클립으로 회전을 만들려던 장부 기구(오프셋 누적 + 커브 소비)는 여기서
	// 쓰지 않는다 - 그 기구가 다섯 번 어긋난 이유가 «몽타주가 끝나면 몸의 회전이 사라진다» 였다.
	// 루트가 도는 클립은 그 회전을 캡슐에 남기므로 사라질 것이 없다.
	void UpdateRootMotionTurnInPlace(const UVBTurnInPlaceConfig& InConfig);

	// 지금 돌고 있는 루트모션 회전 몽타주. 신원을 들고 있어야 «다른 몽타주가 끼어든 것» 과 구별된다.
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveRootMotionTurnMontage;

	// 못 낸 요청을 소리내기 위한 1회 표시. 사유별로 나눈다 - 한 비트로 두면 한 번 뜬 뒤
	// 다른 무기·방향·각도의 누락이 전부 조용해진다. 조용한 것은 «문제 없음» 으로 읽히므로
	// 그 침묵이 곧 거짓말이 된다 (2026-08-28 Codex 교차검토에서 지적, 파일에서 확인).
	bool bLoggedTurnNoProfile = false; // 이 조합이 아직 새 경로 위에 없다
	bool bLoggedTurnRequestUnservable = false; // 방향·각도에 맞는 행이 없다
	bool bLoggedTurnMontagePlayFailed = false; // 행은 찾았는데 Montage_Play 가 0 을 냈다

	// 낼 수 없는 요청을 만나면 그 회전 에피소드 동안 캡슐 회전으로 되돌린다.
	// 옛 장부 몽타주로 폴백하지 않는다 - 한 자세 안에 회전 주체가 둘이 되면 다음 사람이
	// 어느 쪽을 따라야 할지 알 수 없다. 그리고 매 프레임 재시도하지 않도록 걸쇠로 둔다 -
	// 재무장 임계 아래로 내려오거나 자세·무기가 바뀔 때 풀린다.
	bool bTurnFallbackUntilNeutral = false;

	// 몽타주 경로가 회전을 소유하는 동안 옛 장부가 «그 회전» 을 쌓는다. UpdateRootYawOffset 은 경로와
	//   무관하게 매 프레임 돌고, 소유 중에는 bShouldTurnInPlace 도 bPostTIPBlendOut 도 거짓이라
	//   「안 움직임 -> Accelerate」 가지로 떨어지기 때문이다. 그렇게 쌓인 값은 소유권이 옛 경로로
	//   돌아가는 순간(전투 타이머 만료로 저절로 납도되는 때가 그렇다) 회전 요청으로 읽혀
	//   user 가 조작하지 않은 «유령 회전» 을 만든다. 2026-08-29 user PIE 로그가 그 순간을 잡았다 -
	//   `aim=-0.0` 인데 `measured=-32.8` 로 게이트가 열렸다.
	// 계측 (FIND-029) - 「루트모션이 캡슐에 실제로 얼마를 전달했나」를 재기 위해 시작 시점을 기억한다.
	//   정적으로는 잴 수 없다(디버그 함수의 delta 는 병진만, AnimMontage 는 회전 추출을 파이썬에 안 연다).
	//   회전이 끝나는 프레임에 «요청 / 실제 캡슐 회전» 을 나란히 찍으면 전달 여부가 숫자로 갈린다.
	float TurnStartCapsuleYaw = 0.0f;
	float TurnRequestedYaw    = 0.0f;

	// 커밋 대기 중 지금까지 본 요구 각도(부호 있음). 카메라가 멎으면 이 값으로 클립을 고른다.
	float PendingTurnRequestYaw = 0.0f;

	bool bRootMotionTurnOwnedLastFrame   = false;   // 소유권이 넘어온 프레임을 잡기 위한 직전 상태
	bool bSuppressLegacyTIPUntilNeutral  = false;   // 인계 직후: 남은 찌꺼기로는 회전을 열지 않는다

	// 걸쇠를 «조합이 바뀌었을 때» 도 풀기 위한 직전 조합. 무기를 바꿨는데 이전 실패가 남아 있으면
	//   새 프로필이 멀쩡한데도 회전을 안 한다(2026-08-29 Codex 지적 - 주석은 그렇게 푼다고 적어놓고
	//   구현은 이동·낙하·중립 복귀에서만 풀고 있었다).
	EVBWeaponType LastTurnPoseWeapon = EVBWeaponType::None;
	bool bLastTurnCrouching = false;

public:
	// 지금 이 회전을 TIP 가 소유하고 있나. 캐릭터의 회전율 결정이 이 답을 같이 쓴다 -
	// 판정이 두 곳에 있으면 «애니는 도는데 캡슐도 도는» 이중 회전이 다시 생긴다.
	// 몽타주가 도는 동안에도 참이어야 한다: 루트모션이 캡슐을 돌려 요구각이 줄어들면
	// 요청 해석만으로는 회전 도중에 소유권이 풀린다.
	bool IsRootMotionTurnActive() const;

private:

	// 현재 전투모드 + TurnType에 맞는 Montage 선택
	UAnimMontage* SelectTIPMontage(EVBTurnInPlaceType InTurnType) const;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation")
	FName TurnYawCurveName = TEXT("TurnYaw");
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation")
	float TIPExitThreshold = 15.0f;

	// TIP Montage 중단(interrupted) 시 블렌드 아웃 시간
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation", meta=(ClampMin="0.0"))
	float TIPBlendOutTime_Interrupted = 0.15f;

	// TIP Montage 정상 종료 시 블렌드 아웃 시간
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation", meta=(ClampMin="0.0"))
	float TIPBlendOutTime_Normal = 0.25f;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation")
	float TIPThreshold = 80.0f;		// TIP 트리거 각도
	
	// MM 경로 전용 TIP 임계값. 위 TIPThreshold/TIPExitThreshold 와 **다른 양**을 재므로 값을 공유하면 안 된다.
	//   SM 경로  = RootYawOffset — 우리가 누적하는 장부값. 실측 피크 108~179도.
	//   MM 경로  = MeasuredRootYawOffset — OffsetRootBone 의 메시-캡슐 차이. 실측 피크 30~71도.
	// 2026-08-18 에 SM 값(80/15)을 그대로 MM 에 썼다가 게이트가 거의 안 열렸다(로그 tip=0, 에피소드 0.02~0.32초).
	// 범위가 다른 이유: 노드의 RotationHalfLife 가 0.100 이라 실측값은 0.1초 반감기로 감쇠한다 →
	//   누적 부채가 아니라 **현재 회전 속도의 대리 지표**다(캡슐 125deg/s 이면 약 18도, 빠른 플릭이면 50~70도).
	// 그래서 20 은 "실제 회전이면 반드시 넘는" 값이고(실측 최소 피크 30), 8 은 회전이 잦아들면 0.3초 안에
	//   떨어지는 값이다. 게이트가 열려 있는 시간 = 실제로 돌린 시간 + 약 0.3초 꼬리.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation", meta=(ClampMin="0.0"))
	float TIPThresholdMM = 20.0f;

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation", meta=(ClampMin="0.0"))
	float TIPExitThresholdMM = 8.0f;

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation")
	float Turn180Threshold = 130.0f;		// TIP 트리거 각도
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation")
	float CrouchTIPThreshold = 140.0f;		// TIP 트리거 각도
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation")
	float RootYawOffsetClamp = 180.0f;		// 최대 축적량
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation")
	float BlendOutSpeed_Moving = 10.0f;		// 이동 시 복원 속도
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation")
	float BlendOutSpeed_TIP = 3.0f;			// TIP 후 복원 속도

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation")
	float LandPredictionHeight = 150.0f;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation")
	float MoveThreshold = 3.0f;

	EVBGait LastMovingGait = EVBGait::Run;

	// GaitThreshold 삭제 (Step 13-2 Phase 2-0a, 2026-04-20) — Gait는 입력(bIsSprinting)이 직접 결정.

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float LocomotionAngleInterpSpeed = 15.0f;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|MotionMatching")
	float LocomotionAngleMinSpeed = 50.0f;

	// DEBT-006: 턴-래그 흡수 비율 — 캡슐 요 델타 중 하체가 '늦는' 비율 (0=연기 없음, 1=풀 래그)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|ArmedLocomotion")
	float ArmedTurnLagAbsorb = 0.6f;

	// DEBT-006: 턴-래그 감쇠 속도 (FInterpTo, 클수록 빨리 따라잡음)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|ArmedLocomotion")
	float ArmedTurnLagDecaySpeed = 6.0f;

	// DEBT-006: 턴-래그 한계각(도) — OW 워프 한계와 균형 (과하면 하체 비틀림)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|ArmedLocomotion")
	float ArmedTurnLagMaxYaw = 35.0f;

	// DEBT-006: 턴-래그가 동작하는 최소 지상속도(cm/s). 이 미만에서는 흡수하지 않는다.
	// 왜 하한이 필요한가: 턴-래그는 '이동 중' 하체가 늦게 따라오는 연기이고 제자리 회전은 TIP(RootYawOffset) 전담이다.
	//   하한이 없으면 정지 직전 잔속도 구간에서 두 시스템이 같은 요를 동시에 건드려 발이 미끄러진다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Animation|ArmedLocomotion")
	float ArmedTurnLagMinSpeed = 10.0f;

};
