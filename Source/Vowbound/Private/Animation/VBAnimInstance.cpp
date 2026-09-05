// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "Animation/VBAnimInstance.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBCharacter.h"
#include "Data/VBTurnInPlaceConfig.h"
#include "Animation/VBMotionWarpingNames.h"
#include "MotionWarpingComponent.h"
#include "Character/VBTargetLockComponent.h"
#include "Character/VBWeaponStateComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PoseSearch/MotionMatchingAnimNodeLibrary.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchResult.h"
#include "ChooserFunctionLibrary.h"
#include "Vowbound/Vowbound.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "Animation/AnimClassInterface.h"
#include "BoneControllers/AnimNode_OffsetRootBone.h"

// GASP `a.AnimNode.MotionMatching.LOD` 의 Vowbound 대응 — Master CHT 의 Dense/Sparse 분기 입력.
// 0 = Dense (모든 anim, 가까운 거리 또는 hi-fidelity LOD), 1 = Sparse (필수만, 원거리 또는 lo LOD).
// console 에서 `vb.AnimNode.MotionMatching.LOD 1` 로 toggle 가능. UpdateEssentialValues 가 매 frame 읽음.
static TAutoConsoleVariable<int32> CVarMMDatabaseLOD(
	TEXT("vb.AnimNode.MotionMatching.LOD"),
	0,
	TEXT("MMDatabase LOD index used by Master CHT (0=Dense, 1=Sparse)."),
	ECVF_Default
);

// TIP 몽타주가 사는 슬롯. 16종 전부 이 슬롯이다(2026-08-27 통일).
static const FName VB_TIPSlotName(TEXT("TurnInPlace"));

namespace
{
	// LocomotionStateIndex 가 넣는 값은 ABP AnimGraph 의 Blend Poses by int 핀 번호다.
	// 데이터화할 수 없다 - 핀 인덱스는 자산 구조 자체이고, 여기서 할 수 있는 건 이름을 붙이는 것뿐이다.
	// ABP 에서 핀 순서를 바꾸면 이 enum 도 같이 고쳐야 한다(무음 오배선이 되는 지점).
	// 0/2 가 static_cast<int32>(EVBLocomotionState) 와 일치하는 것은 우연이 아니라 계약이다.
	// UENUM 으로 승격하지 않는다 - BP 노출은 새 계약을 만들고, 이건 순수 C++ 가독성 장치다.
	enum class EVBLocomotionPin : int32
	{
		UnarmedMM = 0,   // 비무장 MM
		CombatMM  = 1,   // 전투 MM (stand)
		ArmedSM   = 2,   // 상태3 비-Fighter 스테이트머신
		CrouchSM  = 3,   // crouch BS/SM (DEC-008)
	};
}

namespace VBTIPSlot
{
	// 제자리 회전 몽타주가 도는 슬롯 이름. `AM_TIP_RM_*` 의 저작 슬롯명과 같아야 한다.
	// 두 곳에서 쓴다 - 턴랙 스프링의 흡수 차단, 그리고 OffsetRootBone 의 회전 모드 판정.
	// 한 곳에만 두는 이유: 이름이 갈리면 한쪽만 발화해 「반만 고쳐진」 상태가 된다.
	static const FName Name(TEXT("TurnInPlace"));
}

void UVBAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	RootYawOffset      = 0.0f;
	bShouldTurnInPlace = false;
	bPostTIPBlendOut   = false;

	// TIP 몽타주 판별 배열 구축 — TIPMontageSets 평탄화 (FIND-029 무기별 리모델).
	// EndTurnInPlace가 멤버십 체크로 타 몽타주(무기 전환 연기 등) 오발 정지를 막는 데 쓴다.
	TIPMontages.Reset();
	for (const TPair<EVBWeaponType, FVBTIPMontageSet>& Pair : TIPMontageSets)
	{
		for (UAnimMontage* TIPMontage : { Pair.Value.TurnLeft90.Get(), Pair.Value.TurnRight90.Get(),
		                                  Pair.Value.Turn180.Get(), Pair.Value.CrouchTurn180.Get() })
		{
			if (TIPMontage)
			{
				TIPMontages.AddUnique(TIPMontage);
			}
		}
	}

	// TrajectoryGenerationData_Moving/_Idle 은 여기서 덮어쓰지 않는다. ABP Class Defaults 가 홈이고,
	// 초기화 때 코드가 덮어쓰면 에디터 필드가 바꿔도 아무 일 없는 죽은 저작 지점이 된다.
	// 값과 그 근거는 VBAnimInstance.h 선언부 주석 참조.

	// ActivePoseSearchDatabases 는 ABP CDO (EditDefaultsOnly UPROPERTY) 에서 designer 가 채움.
	// 이전엔 cpp 에 9 PSD path 하드코드 fallback 있었으나 Data-Driven 원칙 위반으로 제거 (2026-05-03).
	// CDO 가 비어있으면 MM 결과 없음 (Anim=?, T-pose) — 명시적 실패가 silent fallback 보다 안전.
}

void UVBAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	APawn* Pawn = TryGetPawnOwner();
	if (!Pawn)
	{
		return;
	}

	ACharacter* Character = Cast<ACharacter>(Pawn);
	if (!Character)
	{
		return;
	}

	UCharacterMovementComponent* CMC = Character->GetCharacterMovement();
	if (!CMC)
	{
		return;
	}

	// 이 호출은 반드시 UpdateTurnInPlaceState(아래 :315) 보다 앞이어야 한다 - 그쪽 사망 게이트가
	// 여기서 갱신한 bIsDead 를 재사용한다(ASC 를 같은 프레임에 두 번 조회하지 않기 위함).
	UpdateDeathState(Pawn);

	// _LastFrame shift — frame 시작점에서 직전 프레임의 값을 _LastFrame 으로 캐시.
	// UpdateStates 말미에서 하면 같은 frame 내 inline 갱신값이 _LastFrame 에 들어가버려
	// Worker thread 의 GetMMInterruptMode 가 항상 (current == _LastFrame) → DoNotInterrupt 반환.
	// 결과: MM 이 실제로 interrupt 되지 않아 Run pose 가 1~2초 잔존하는 시각적 버그.
	// shift 를 frame 진입 시점으로 옮겨 직전 프레임 값을 보존 → state 전환 정확히 검출.
	MMState_LastFrame      = MMState;
	MovementMode_LastFrame = MovementMode;
	RotationMode_LastFrame = RotationMode;
	Stance_LastFrame       = Stance;
	Gait_LastFrame         = Gait;
	MMWeaponType_LastFrame = MMWeaponType; // 무기 스왑(MM 풀 교체) 감지 — 아래 래치 로직 소비
	bFirePoolInterrupt = false; // 1프레임 펄스 — 발화 프레임의 worker(GetMMInterruptMode)만 소비
	// Direction change 감지 — frame 진입 시점에 prev 보존. GetMMInterruptMode 가 frame delta 비교.
	PreviousLocomotionAngle = LocomotionAngle;

	// GASP UpdateEssentialValues / UpdateStates 이식 — ABP BP 함수들이 기대하는 변수 세트 선계산.
	// (Speed2D, Velocity_LastFrame, VelocityAcceleration, LastNonZeroVelocity, HasVelocity/Acceleration)
	UpdateEssentialValues(DeltaSeconds, Character, CMC);

	// 공통 변수
	// ForwardSpeed / RightSpeed: Velocity를 로컬 방향으로 분해.
	// 멤버 Velocity 는 아래 UpdateEssentialValues 에서 저장되므로 이 블록은 local 변수명 충돌 피하기 위해
	// CurrentVel 로 명명.
	const FVector CurrentVel    = Character->GetVelocity();
	const FVector ForwardVector = Character->GetActorForwardVector();
	const FVector RightVector   = Character->GetActorRightVector();

	ForwardSpeed = FVector::DotProduct(CurrentVel, ForwardVector);
	RightSpeed   = FVector::DotProduct(CurrentVel, RightVector);

	// GroundSpeed: XY 평면 속도 크기
	GroundSpeed = CurrentVel.Size2D();

	// IsFalling: 공중 상태
	IsFalling = CMC->IsFalling();

	// CMC 회전 모드 진실 캐시 (FIND-019 U1) — ShouldTurnInPlace 게이트의 입력.
	// WHY: 캡슐은 WantsToStrafe 기본 true 로 카메라를 추종하지만 RotationMode enum 은 비무장에서
	// 의도적으로 OrientToMovement 를 유지(UpdateStates — MM Run_Turn 선택 품질). 게이트가 enum 을 보면
	// 두 정의가 갈라져 idle TIP 가 영구 불발 → "캡슐이 실제로 도는가"는 CMC 상태로 판정한다.
	bCMCStrafeRotation = CMC->bUseControllerDesiredRotation;

	if (IsFalling)
	{
		FHitResult    Hit;
		const FVector Start = Character->GetActorLocation();
		const FVector End   = Start - FVector(0.0f, 0.0f, LandPredictionHeight);

		FCollisionQueryParams Params;
		Params.AddIgnoredActor(Character);

		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			DistanceToGround = Hit.Distance;
		}
		else
		{
			DistanceToGround = LandPredictionHeight;
		}
	}
	else
	{
		DistanceToGround = 0.0f;
	}
	
	// ShouldMove: 실제 이동 중인지
	ShouldMove = GroundSpeed > MoveThreshold;

	// Size2D를 사용하는 이유는 Z축을 제외한 이동 입력만 확인하기 위해서
	bool bHasMovementInput = CMC->GetCurrentAcceleration().Size2D() > KINDA_SMALL_NUMBER;

	// VBCharacter 캐시 (Sprint 플래그 + 플레이어가 선택한 Gait 조회 + Trajectory 공급)
	AVBCharacter* VBCharLocal = Cast<AVBCharacter>(Character);
	const bool bSprintActive = VBCharLocal && VBCharLocal->GetIsSprinting();
	const EVBGait CharacterGait = VBCharLocal ? VBCharLocal->GetGait() : EVBGait::Run;

	// R-006: GASP 궤적 예측 + WorldCollisions + FutureVelocity → Trajectory/FutureVelocity/TrajectoryCollision.
	UpdateTrajectory(DeltaSeconds, Character);

	// R-006: IsMoving 판정 → MMState/Gait/LastMovingGait (Sprint 홀드 = Run 강제 Run_Loops 풀 라우팅).
	UpdateMovementStateAndGait(bHasMovementInput, bSprintActive, CharacterGait);

	// R-006: Velocity 방향 → LocomotionAngle 보간 (OW 입력).
	UpdateLocomotionAngle(DeltaSeconds, Character);
	
	// R-006: 속도/Gait → WantedPlayRate (SpeedRemappingCurve).
	UpdatePlayRate();
	
	// bIsCrouching: ACharacter 내장 변수
	bIsCrouching = Character->bIsCrouched;

	// 플레이어 전용 변수


	if (AVBCharacter* VBChar = Cast<AVBCharacter>(Character))
	{
		bIsSprinting = VBChar->GetIsSprinting();

		// Sprint 풀 합류 자격 — 의도(홀드) 즉시 ON / 해제 후엔 감속이 SprintPoolJoinSpeed 아래로
		// 떨어질 때까지 유지(핸드오프 보존). 발도/스왑 감쇠 구간(의도 없음·자격 이력 없음)은 차단.
		bSprintPoolEligible = bIsSprinting || (bSprintPoolEligible && Speed2D >= SprintPoolJoinSpeed);
		MMSprintGateSpeed   = bSprintPoolEligible ? Speed2D : 0.0f; // Dense CHT Sprint 행 전용 컬럼 입력

		if (UVBWeaponStateComponent* WSC = VBChar->GetWeaponStateComponent())
		{
			// 파쿠르 임시수납 시에는 Unarmed로 표시
			LocomotionState = WSC->GetEffectiveLocomotionState();

			// raw WSC 상태 캡처 — 아래 crouch 리다이렉트가 평상-crouch 의 enum 을 ArmedExploration 으로
			// 올리므로, 전투 MM 판정(상태2 포함)은 오염 전의 WSC 진실로만 한다.
			const EVBLocomotionState RawState = LocomotionState;

			// 평상 crouch 는 MM 에 포즈가 없어 SM 전용 핀(pin3)으로 보낸다.
			// crouch 시 enum 을 ArmedExploration 으로 올리던 구 리다이렉트는 유지 — LocomotionState 기반
			// 게이트들(TIP 등)이 crouch 에서 SM 시절과 동일하게 동작해야 한다.
			if (bIsCrouching && LocomotionState == EVBLocomotionState::Unarmed)
			{
				LocomotionState = EVBLocomotionState::ArmedExploration;
			}
			CurrentWeaponType = WSC->GetCurrentWeaponType();
			WeaponTypeIndex   = ResolveWeaponPoseIndex(CurrentWeaponType);

			// 전투 MM 풀 선택 - 어떤 풀을 쓰느냐는 그 무기가 어떤 클립을 가졌느냐의 문제라 홈이 무기
			//   데이터다(WeaponDataMap 의 ArmedMMPool/SheathedMMPool). 무기를 추가해도 여기는 고치지 않는다.
			// MMWeaponType 은 CurrentWeaponType(선택 무기 기억)과 분리된다 - 전용 풀이 없는 무기와 평상
			//   상태는 None 으로 떨어져 무기 없는 기본 풀을 탄다. 풀 구성(무기별 클립·gait·정규화)의
			//   정본은 Docs/SystemMap/05_Animation.md.
			// RawState 로 묻는 이유: crouch 리다이렉트가 LocomotionState 를 ArmedExploration 으로 올려
			//   오염시키므로, 그 전에 잡아 둔 WSC 진실이라야 납도 중에 발도 풀이 뽑히지 않는다.
			MMWeaponType = WSC->GetMMPoolForState(RawState);
			const bool bCombatMM = (MMWeaponType != EVBWeaponType::None);

			// R-007: 풀 교체 interrupt — 수렴 래치(무기 스왑) + 발도↔납도 즉시 래치(FIND-030) 통합. → bFirePoolInterrupt.
			EvaluatePoolInterrupt(DeltaSeconds, CMC, RawState);

			// stride-warp 는 엔진-네이티브다 (DEC-007 P5, 2026-06-10 확정). MM 노드 PlayRate 인터벌 {0.85, 1.5} 가
			// 쿼리속도/클립속도 비율(GetEstimatedSpeedRatio)로 재생속도를 자동 워프한다 — jog 350/238=1.47,
			// sprint 950/636=1.49 PIE 실측. 여기서 WantedPlayRate 를 C++ 로 덮지 않는 이유: 이 값은 트래버설
			// 몽타주 재생속도(VBTraversalComponent::ServerPerformTraversal)로도 소비돼 무장 워프가 새면 안 된다.

			LocomotionStateIndex = static_cast<int32>(LocomotionState);
			if (bIsCrouching)
			{
				// DEC-008: 전 crouch → SM/BS(pin3). crouch 콘텐츠는 8-way 루프+Idle뿐(start/stop/pivot=0)이라
				// MM이 과도한 도구(저속 재선택 톱니파) — BS 연속보간이 적합. 전투 crouch도 MM 대신 여기로(MM crouch 은퇴).
				LocomotionStateIndex = static_cast<int32>(EVBLocomotionPin::CrouchSM);
			}
			else if (bCombatMM)
			{
				// MM 경로 (전투 MM — stand. crouch 는 위 분기에서 SM 로 라우팅)
				LocomotionStateIndex = static_cast<int32>(EVBLocomotionPin::CombatMM);
			}
			// crouch BS 포즈 인덱스 (DEC-008): 발도(전투)=무기별 BS(1 Katana/2 BigSword/3 Fighter) / 수납(평상·경계)=0=ver_A 공용 BS.
			// RawState(crouch 리다이렉트 전 WSC 진실)로 판정 — 수납 상태인데 칼든 자세(ver_B)가 재생되던 잠복버그 차단.
			CrouchPoseIndex = (RawState == EVBLocomotionState::ArmedCombat)
				                  ? ResolveCrouchPoseIndex(CurrentWeaponType)
				                  : 0;
			// SM 경로 = 상태3 비-Fighter SM(pin2) 또는 crouch SM(pin3). MM 핀(0·1)과 흡수/연기 게이트가 갈린다.
			bUsesStateMachinePath = (LocomotionStateIndex >= static_cast<int32>(EVBLocomotionPin::ArmedSM));
			// SM 경로 게이트 미러 — SM 경로에서만 RotateRootBone 회전 적용 (MM 경로는 MM 내장 Steering/OW 가 연기)
			//
			// 도는 동안: 장부가 Hold 로 멈춰 있고 커브 소비도 없으므로 이 미러는 저절로 고정된다.
			// 끝난 뒤: 몸이 놓는 만큼만 뿌리가 받아야 한다. 몽타주는 블렌드아웃으로 0.25초에 걸쳐
			// 빠지는데 뿌리가 한 번에 받으면 그 사이가 어긋나고, 반대로 뿌리만 한 프레임 늦으면
			// 그 한 프레임에 캐릭터가 원래 방향으로 보인다 - user 가 본 «깜빡» 이 그것이다.
			// 그래서 두 속도를 같은 것에 묶는다: 남은 슬롯 가중치가 곧 «몸이 아직 들고 있는 비율» 이다.
			if (!bUsesStateMachinePath)
			{
				ArmedRootYawOffset = 0.0f;
			}
			else if (bShouldTurnInPlace)
			{
				// 도는 동안: 장부는 Hold 로 멈춰 있으니 이 미러도 그대로 고정된다.
				ArmedRootYawOffset = RootYawOffset;
			}
			else if (bPostTIPBlendOut)
			{
				// 인계 구간. 몸이 아직 들고 있는 비율만큼만 뿌리가 받는다.
				const float TIPSlotWeight = GetSlotMontageLocalWeight(VB_TIPSlotName);
				ArmedRootYawOffset = RootYawOffset * TIPSlotWeight;

				if (TIPSlotWeight <= KINDA_SMALL_NUMBER)
				{
					// 몸이 다 놓았으면 인계도 끝이다 - 장부를 여기서 끝낸다.
					// 남겨두면 BlendOut 이 그 각(약 140도)을 천천히 되돌려, 화면에는 «원점으로 튀었다가
					// 스르르륵 다시 도는» 두 번째 회전으로 보인다(2026-08-28 user 관찰).
					RootYawOffset = 0.0f;
					ArmedRootYawOffset = 0.0f;
					bPostTIPBlendOut = false;
				}
			}
			else
			{
				ArmedRootYawOffset = RootYawOffset;
			}
			// DEC-006: IsInCombat = 상태2·3 — TIP B(전투) 세트가 전투 전체(무기Off 포함)에 적용된다.
			// 상태2에서 전투 TIP이 어색하면 WSC->IsWeaponSummoned() 기준으로 한 줄 조정 (FEEL 튜닝 후보).
			bIsCombatMode = WSC->IsInCombat();
		}

		if (VBChar->GetTargetLockComponent())
		{
			bIsLockedOn = VBChar->GetTargetLockComponent()->IsLockedOn();
		}
		else
		{
			bIsLockedOn = false;
		}

		// AimYaw: Controller 기반 (플레이어 카메라)
		if (AController* Controller = VBChar->GetController())
		{
			const FRotator ControlRotation = Controller->GetControlRotation();
			const FRotator ActorRotation   = Character->GetActorRotation();
			const FRotator DeltaRotation   = (ControlRotation - ActorRotation).GetNormalized();
			AimYaw                         = DeltaRotation.Yaw;

			// RootYawOffset 관련 추가
			// 1. 이번 프레임 Actor Yaw
			const float ActorYawThisFrame = ActorRotation.Yaw;

			// 2. 첫 유효 프레임에는 delta를 만들지 않고 기준값만 잡는다
			float ActorYawDelta = 0.0f;

			if (!bHasInitializedActorYawLastFrame)
			{
				ActorYawLastFrame                = ActorYawThisFrame;
				bHasInitializedActorYawLastFrame = true;
			}
			else
			{
				ActorYawDelta     = FRotator::NormalizeAxis(ActorYawThisFrame - ActorYawLastFrame);
				ActorYawLastFrame = ActorYawThisFrame;
			}

			// 3. 모드에 따라 RootYawOffset 업데이트
			UpdateRootYawOffset(DeltaSeconds, ActorYawDelta);

			// R-006: DEBT-006 무장 이동 턴-래그 스프링(감쇠→흡수 순서 고정) → ArmedTurnLagYaw.
			UpdateTurnLag(DeltaSeconds, ActorYawDelta);
		}
	}

	// TIP 게이트보다 먼저 - 게이트가 이 값을 읽는다(MM 경로).
	UpdateMeasuredRootYawOffset();
	UpdateTurnInPlaceState();

	// GASP UpdateStates 이식 — enum 세트 전환 + _LastFrame 캐시.
	// NativeUpdateAnimation 말미에서 호출해야 MMState/Gait가 확정된 뒤 shift 됨.
	UpdateStates();
}

// R-006 추출 (2026-07-05): NativeUpdateAnimation 인라인 phase 를 동작 보존 relocate. 로직·순서 원본 동일.
void UVBAnimInstance::UpdateTrajectory(float DeltaSeconds, ACharacter* Character)
{
	// GASP GenerateTrajectory 이식 — GroundSpeed 기반 Idle/Moving TrajectoryData 선택 →
	// PoseSearchGenerateTransformTrajectory 로 미래 궤적 예측. 결과는 Trajectory(UPROPERTY) 에 저장 →
	// AnimGraph 의 PoseSearchHistoryCollector.TransformTrajectory / Steering.GetDesiredFacing 입력.
	// GASP ABP_SandboxCharacter::GenerateTrajectory exact (GroundSpeed > 0 → Moving, else Idle).
	const FPoseSearchTrajectoryData& TrajData =
		(GroundSpeed > 0.0f) ? TrajectoryGenerationData_Moving : TrajectoryGenerationData_Idle;

	FTransformTrajectory TempTrajectory = Trajectory;
	// 샘플링 파라미터는 GASP 원본 인자 그대로다 — History 간격 -1(엔진 기본 대체 센티널)/개수 30,
	// Prediction 간격 0.1s/개수 15 = 미래 1.5초 커버.
	//
	// 이 값들을 UPROPERTY 로 빼지 말 것. 2026-08-09 까지 헤더에 저작 지점 4개(0.04/10/0.4/8)가 있었는데
	// 코드가 한 번도 읽지 않아 값이 발산했고, 헤더 주석은 "3.2초 미래 커버"라는 거짓 상태를 설명하고 있었다.
	// MM 궤적을 디버깅하는 사람이 그 주석을 믿고 3.2초를 가정한 채 원인을 못 찾는 구조였다.
	// 여기 리터럴이 유일한 진실이다. 값을 바꾸면 MM 로코모션 FEEL 이 전면 변하므로 튜닝은 별건이다.
	UPoseSearchTrajectoryLibrary::PoseSearchGenerateTransformTrajectory(
		this, TrajData, DeltaSeconds,
		/*InOut*/ TempTrajectory,
		/*InOut*/ DesiredControllerYawLastUpdate,
		/*Out*/ Trajectory,
		/*HistorySamplingInterval*/ -1.0f,
		/*TrajectoryHistoryCount*/ 30,
		/*PredictionSamplingInterval*/ 0.1f,
		/*TrajectoryPredictionCount*/ 15);

	// GASP HandleTrajectoryWorldCollisions exact: FloorCollisionsOffset=0.01(1mm — 0.0 은 trace MISS →
	// Z stick 실패 → FV.Z -3000 누적, PIE 실측) / MaxObstacleHeight=150(GASP, default 10000 보다 보수적).
	{
		FTransformTrajectory CollidedTrajectory;
		TArray<AActor*> ActorsToIgnore;
		if (Character) { ActorsToIgnore.Add(Character); }
		UPoseSearchTrajectoryLibrary::HandleTransformTrajectoryWorldCollisions(
			this, this,
			/*In*/ Trajectory,
			/*bApplyGravity*/ true,
			/*FloorCollisionsOffset*/ 0.01f,
			/*Out*/ CollidedTrajectory,
			/*Out*/ TrajectoryCollision,
			/*TraceChannel*/ ETraceTypeQuery::TraceTypeQuery1,
			/*bTraceComplex*/ false,
			ActorsToIgnore,
			/*DrawDebugType*/ EDrawDebugTrace::None,
			/*bIgnoreSelf*/ true,
			/*MaxObstacleHeight*/ 150.0f);
		Trajectory = CollidedTrajectory;
	}

	// FutureVelocity — GASP exact: (sample@0.5 - sample@0.4)/0.1. 가까운 미래 diff 라 falling 누적 최소화.
	UPoseSearchTrajectoryLibrary::GetTransformTrajectoryVelocity(
		Trajectory, /*Time1*/ 0.4f, /*Time2*/ 0.5f, /*Out*/ FutureVelocity, /*bExtrapolate*/ false);
}

void UVBAnimInstance::UpdateLocomotionAngle(float DeltaSeconds, const ACharacter* Character)
{
	float TargetAngle = 0.0f;

	if (ShouldMove && GroundSpeed > LocomotionAngleMinSpeed)
	{
		// 충분한 속도가 있을때만 Velocity 방향을 신뢰한다.
		const FRotator VelocityRotation = Velocity.ToOrientationRotator();
		const FRotator ActorRotation = Character->GetActorRotation();
		const FRotator DeltaRot = (VelocityRotation - ActorRotation).GetNormalized();
		TargetAngle = DeltaRot.Yaw;
	}

	const float AngleDiff = FRotator::NormalizeAxis(TargetAngle - LocomotionAngle);
	const float InterpAlpha = FMath::Min(LocomotionAngleInterpSpeed * DeltaSeconds, 1.0f);
	LocomotionAngle = FRotator::NormalizeAxis(LocomotionAngle + AngleDiff * InterpAlpha);
}

void UVBAnimInstance::UpdatePlayRate()
{
	if (ShouldMove && GroundSpeed > MoveThreshold)
	{
		// Phase 2-1 SpeedRemappingCurve. 1차: 곡선(FRuntimeFloatCurve) 설정 시 속도→PlayRate.
		// 2차: 곡선 미설정 시 Gait 레퍼런스 속도 비율 폴백.
		const FRichCurve* Curve = SpeedToPlayRateCurve.GetRichCurveConst();

		if (Curve && Curve->GetNumKeys() > 0)
		{
			WantedPlayRate = FMath::Clamp(Curve->Eval(GroundSpeed), MinPlayRate, MaxPlayRate);
		}
		else
		{
			// Walk Gait → WalkAnimRootMotionSpeed(200) / Run·Sprint Gait → RunAnimRootMotionSpeed(500, Sprint 는 Run PSD 재사용).
			const float ReferenceSpeed = (Gait == EVBGait::Walk)
				? WalkAnimRootMotionSpeed
				: RunAnimRootMotionSpeed;

			if (ReferenceSpeed > KINDA_SMALL_NUMBER)
			{
				WantedPlayRate = FMath::Clamp(GroundSpeed / ReferenceSpeed, MinPlayRate, MaxPlayRate);
			}
			else
			{
				WantedPlayRate = 1.0f;
			}
		}
	}
	else
	{
		WantedPlayRate = 1.0f;
	}
}

void UVBAnimInstance::UpdateMovementStateAndGait(bool bHasMovementInput, bool bSprintActive, EVBGait CharacterGait)
{
	// GASP ABP_SandboxCharacter::IsMoving() 정확 이식: Not Equal(Vector) ErrorTolerance=0.1(default, 2026-05-26 실측 원복).
	const bool bIsMovingGASP =
		!Velocity.IsNearlyZero(0.1f) && !FutureVelocity.IsNearlyZero(0.1f);

	if (bIsMovingGASP)
	{
		MMState = EVBMMState::Moving;
		// Sprint(홀드) = 전방 가속 램프(Run549→Sprint636): Gait 를 Run 으로 강제해 Run_Loops 풀(jog238·run549·
		// sprint636 속도연속체)로 라우팅 → MM 이 캡슐 속도로 자연선택. 이 가드 없으면 Walk 토글 sprint 시
		// Walk_Loops(167) 풀로 오라우팅돼 저속/빈 매칭 (DEC-007 gait 사다리 Option A, 2026-06-09).
		if (bHasMovementInput)
		{
			Gait = bSprintActive ? EVBGait::Run : CharacterGait;
			LastMovingGait = Gait;
		}
		else
		{
			Gait = LastMovingGait;
		}
	}
	else
	{
		MMState = EVBMMState::Idle;
		// H53: 정지 순간 Gait 를 Run 강제 reset 하면 Walk-to-stop 시 CHT "Stand Idles" Walk row(Gait=Walk+Speed2D
		// 20~999) 미매칭 → Walk_Stops PSD 영구 진입 실패. LastMovingGait 유지로 stop anim 정상 매칭.
		Gait = LastMovingGait;
	}
}

void UVBAnimInstance::UpdateTurnLag(float DeltaSeconds, float ActorYawDelta)
{
	// DEBT-006 Phase A: 무장 '이동 중' 턴-래그 스프링 — 요 델타를 흡수했다가 감쇠. idle 턴은 TIP(RootYawOffset)
	// 전담이라 이동 중에만 흡수; 비무장(LocomotionStateIndex==0)은 0 보장. 부호: 캡슐 +요면 하체는 -로 '늦게'.
	// 순서 고정 필수: 감쇠(이전 누적분) → 흡수(이번 프레임). 역순이면 큰 dt 에서 FInterpTo 스텝(dt×speed≥1)이
	//   이번 흡수분을 같은 프레임에 전량 소거 → 저fps 연기 소멸(2026-06-07 PIE 실측 버그).
	ArmedTurnLagYaw = FMath::FInterpTo(ArmedTurnLagYaw, 0.0f, DeltaSeconds, ArmedTurnLagDecaySpeed);

	// 제자리 회전 몽타주가 도는 동안에는 흡수하지 않는다 (2026-08-31).
	// 위 주석의 설계 의도가 「idle 턴은 TIP 전담이라 이동 중에만 흡수」인데, 그 판정을 GroundSpeed 로 한다.
	//   그런데 회전 몽타주의 루트모션이 바로 그 속도를 만든다 - 같은 착각으로 2026-08-29 에 몽타주가
	//   자기가 만든 속도에 스스로 끊긴 적이 있다. 여기서도 그 속도가 문턱을 넘으면 회전 내내 요 델타를
	//   흡수해 하체가 최대 35도까지 감기고, 회전이 끝나 속도가 떨어지면 그것이 풀리며 다리가 훽 돈다
	//   (user: 「다 돌았다 싶었을 때 막 돌아」).
	// 슬롯 «가중치» 로 보는 이유: 블렌드아웃 구간에도 포즈가 남아 있어 그때 흡수가 되살아나면 안 된다.
	//   감쇠는 계속 돈다 - 이미 감긴 값은 풀려야 하고, 막는 것은 새로 감는 것뿐이다.
	const bool bTurnMontageHoldsPose =
		GetSlotMontageLocalWeight(VBTIPSlot::Name) > KINDA_SMALL_NUMBER;
	if (!bTurnMontageHoldsPose && LocomotionStateIndex > 0 && GroundSpeed > ArmedTurnLagMinSpeed)
	{
		ArmedTurnLagYaw = FMath::Clamp(
			ArmedTurnLagYaw - ActorYawDelta * ArmedTurnLagAbsorb,
			-ArmedTurnLagMaxYaw, ArmedTurnLagMaxYaw);
	}
	else if (LocomotionStateIndex == 0)
	{
		ArmedTurnLagYaw = 0.0f;
	}
}

// R-007 추출 (2026-07-05): 풀 교체 interrupt 2 트리거 통합. NativeUpdateAnimation WSC 블록에서 호출.
void UVBAnimInstance::EvaluatePoolInterrupt(float DeltaSeconds, UCharacterMovementComponent* CMC, EVBLocomotionState RawState)
{
	// 풀 교체 interrupt — 두 트리거를 한 곳에 통합(R-007). 둘 다 bFirePoolInterrupt(1프레임 펄스)를 발화하지만
	//   타이밍 의미가 다르다. 로직은 통합 전과 동일(co-locate).
	//
	// [트리거 A — 수렴 래치] MMWeaponType 스왑(무기별 MM 풀 교체) 시. 즉시 발화하면 보폭 과도기(비무장500→무장
	//   350 감속 중) 쿼리가 옛 속도 정합 클립을 골랐다가 수렴 후 재선택 = 두 번 갈아탐(블립). → 래치해 뒀다가
	//   보폭 수렴(±40)·거의정지(<10)·0.7s 타임아웃 시 1회 발화. 보폭 동일 스왑(K↔F)은 조건 즉시 충족.
	if (MMWeaponType != MMWeaponType_LastFrame)
	{
		bPendingPoolInterrupt = true;
		PoolInterruptPendingTime = 0.0f;
	}
	if (bPendingPoolInterrupt)
	{
		PoolInterruptPendingTime += DeltaSeconds;
		const float TargetMax = CMC ? CMC->GetMaxSpeed() : 0.0f;
		const bool bSpeedSettled = (FMath::Abs(Speed2D - TargetMax) < PoolInterruptSpeedSettleTolerance) || (Speed2D < PoolInterruptNearStopSpeed);
		if (bSpeedSettled || PoolInterruptPendingTime > PoolInterruptTimeoutSeconds)
		{
			bFirePoolInterrupt = true;
			bPendingPoolInterrupt = false;
		}
	}

	// [트리거 B — 발도↔납도 즉시 래치 (FIND-030)] 납도·발도가 같은 MM 풀을 쓰는 무기(대검처럼 자기 풀 안에서
	//   CHT 가 상태로 갈리는 경우)는 트리거 A 가 안 걸린다 → CHT 가 Sheathed↔Stand 를 갈아도 continuing pose
	//   잔존으로 "걸으며 Tab 해도 안 바뀜". 보폭 동일(무장 gait) 스왑이라 정착 대기 불필요 → 즉시 발화.
	//   무기 이름으로 좁히지 않는 이유: 아래 MMWeaponType 동일 조건이 이미 "풀이 안 바뀐 경우"만 남긴다.
	//   풀이 바뀌는 무기는 트리거 A 가 처리하고 그 조건에서 걸러지므로 이중 발화가 없다.
	const bool bMMPoolSheathed = (MMWeaponType != EVBWeaponType::None
	                              && RawState == EVBLocomotionState::ArmedExploration);
	if (bMMPoolSheathed != bMMPoolSheathed_LastFrame && MMWeaponType == MMWeaponType_LastFrame)
	{
		bFirePoolInterrupt = true;
		bPendingPoolInterrupt = false;
	}
	bMMPoolSheathed_LastFrame = bMMPoolSheathed;
}

void UVBAnimInstance::NativePostEvaluateAnimation()
{
	Super::NativePostEvaluateAnimation();

	// JustLanded grace 처리 (2026-05-15 H49) — GASP CBP_SandboxCharacter OnLanded 의
	// "Retriggerable Delay 0.3s" 패턴 이식. VBCharacter::Landed 가 JustLandedClearTime 을
	// (현재 시각 + JustLandedGraceDuration) 으로 세팅. 이번 frame 시각이 clear time 을 지나면 false.
	// 이전 (즉시 clear): 1프레임만 true → 다음 frame Lands PSD nested 후보에서 빠짐 → Land anim
	// 10ms 만에 Run_Loop/Idle 로 jump 하던 증상 원인.
	// NativePostEvaluate 시점에서 평가하는 이유: 이번 frame 의 chooser/MM 평가가 끝난 *다음* 에 clear
	// → 같은 frame 내 race 없음.
	if (JustLanded)
	{
		const UWorld* World = GetWorld();
		if (World && World->GetTimeSeconds() >= JustLandedClearTime)
		{
			JustLanded = false;
		}
	}
}

EVBTurnInPlaceType UVBAnimInstance::CalculateTurnType(float InAimYaw) const
{
	const float AbsAimYaw = FMath::Abs(InAimYaw);

	if (AbsAimYaw > Turn180Threshold)
	{
		// 130도 초과: 180도 회전
		return EVBTurnInPlaceType::Turn180;
	}

	// 45도~130도: 좌/우 90도 회전
	return (InAimYaw > 0.0f)
		       ? EVBTurnInPlaceType::TurnRight90
		       : EVBTurnInPlaceType::TurnLeft90;
}

void UVBAnimInstance::UpdateEssentialValues(float DeltaSeconds, ACharacter* Character, UCharacterMovementComponent* CMC)
{
	// GASP UpdateEssentialValues 로직 이식.
	// 목적: 매 tick 속도/가속/상태 파생 변수를 선계산해 BP 함수/AnimGraph가 바로 읽을 수 있게.

	// GASP MMDatabaseLOD — Master CHT 의 Dense/Sparse 분기 입력. console `vb.AnimNode.MotionMatching.LOD` 가 매 frame 갱신.
	// CVar 가 file scope 에 register 됨. EvaluateChooser(CHT_VB_PoseSearchDatabases) 의 FloatRange column 이 이 값을 읽어 분기.
	MMDatabaseLOD = CVarMMDatabaseLOD.GetValueOnAnyThread();

	// GASP Transform 캐싱 — AimOffset / Lean / ShouldTurnInPlace / ShouldSpinTransition 등의 입력.
	CharacterTransform = Character->GetActorTransform();
	if (USkeletalMeshComponent* Mesh = Character->GetMesh())
	{
		// Mesh 의 root bone transform (world space). GASP 는 여기에 Yaw +90° 보정을 추가 —
		// mesh 가 -Y forward 관례라 world forward 와 맞추려면 보정이 필요.
		// 보정 상수는 RootTransformYawCorrection UPROPERTY 로 노출 (프로젝트 mesh orientation 에 맞춰 조정 가능).
		static const FName RootBoneName(TEXT("root"));
		const int32 BoneIdx = Mesh->GetBoneIndex(RootBoneName);
		FTransform RawRoot = (BoneIdx != INDEX_NONE)
			? Mesh->GetSocketTransform(RootBoneName, RTS_World)
			: Mesh->GetComponentTransform();

		if (!FMath::IsNearlyZero(RootTransformYawCorrection))
		{
			FRotator Rot = RawRoot.Rotator();
			Rot.Yaw = FRotator::NormalizeAxis(Rot.Yaw + RootTransformYawCorrection);
			RawRoot.SetRotation(Rot.Quaternion());
		}
		RootTransform = RawRoot;
	}

	// GASP PreviousDesiredControllerYaw — ShouldTurnInPlace 가 컨트롤러 회전 변화량 판단에 사용.
	// 다음 tick 에 전 프레임 값이 남도록 shift 는 함수 말미.
	const FVector NewVelocity = Character->GetVelocity();
	Velocity = NewVelocity; // GASP helper (IsPivoting/ShouldSpinTransition 등) 가 직접 참조.

	// VelocityAcceleration: 프레임간 delta-v (진짜 가속 = d(v)/dt).
	// CMC.GetCurrentAcceleration()은 "입력된 가속" (플레이어 입력/AI 목표)이므로 별개.
	if (DeltaSeconds > KINDA_SMALL_NUMBER)
	{
		VelocityAcceleration = (NewVelocity - Velocity_LastFrame) / DeltaSeconds;
	}
	else
	{
		VelocityAcceleration = FVector::ZeroVector;
	}

	Speed2D     = NewVelocity.Size2D();
	HasVelocity = !NewVelocity.IsNearlyZero(1.0f); // 1cm/s 미만은 정지로 간주
	if (HasVelocity)
	{
		LastNonZeroVelocity = NewVelocity;
	}

	Acceleration       = CMC->GetCurrentAcceleration();
	AccelerationAmount = Acceleration.Size();
	HasAcceleration    = !Acceleration.IsNearlyZero();

	// 다음 tick을 위해 현재 속도 캐시 (함수 말미에 shift).
	Velocity_LastFrame = NewVelocity;

	// GASP PreviousDesiredControllerYaw — 다음 tick 에서 ShouldTurnInPlace 판정 위해 현재 controller yaw 캐시.
	// NativeUpdateAnimation 은 game thread 이므로 GetControlRotation 호출 안전.
	if (const AController* Controller = Character->GetController())
	{
		PreviousDesiredControllerYaw = Controller->GetControlRotation().Yaw;
	}
}

void UVBAnimInstance::UpdateStates()
{
	// _LastFrame shift 는 NativeUpdateAnimation 시작점에서 이미 처리됨 (프레임 진입 시 직전 값 캐시).
	// 여기서 다시 shift 하면 같은 frame 내 갱신값이 _LastFrame 으로 들어가 Worker thread 비교가 깨짐.

	// MovementMode — IsFalling을 enum화.
	MovementMode = IsFalling ? EVBMovementMode::InAir : EVBMovementMode::Grounded;

	// Stance — bIsCrouching을 enum화.
	Stance = bIsCrouching ? EVBStance::Crouch : EVBStance::Stand;

	// RotationMode 는 LockedOn / ArmedCombat 두 입력만 본다.
	// WantsToStrafe 는 의도적으로 연결하지 않는다 - 기각된 안이다(2026-08-09 재확인).
	//   캡슐은 WantsToStrafe 기본 true 로 카메라를 추종하지만, 이 enum 은 비무장에서 OrientToMovement 를
	//   유지해야 MM 이 Run_Turn 을 고른다. Strafe 로 두면 Run_Shuffle 이 선택돼 트래버설 후 회전이 끊긴다.
	//   "캡슐이 실제로 도는가"의 진실원은 이 enum 이 아니라 bCMCStrafeRotation 이다(UpdateEssentialValues 주석).
	//   여기에 WantsToStrafe 를 연결하면 idle TIP 영구 불발 회귀를 재현한다. 다시 제안하지 말 것.
	const bool bLockedOn    = bIsLockedOn;
	const bool bArmedCombat = (LocomotionState == EVBLocomotionState::ArmedCombat);
	if (bLockedOn)
	{
		RotationMode = EVBRotationMode::OrientToTarget;
	}
	else if (bArmedCombat)
	{
		RotationMode = EVBRotationMode::Strafe;
	}
	else
	{
		// 기본은 OrientToMovement(Free) = GASP sandbox 기본 회전모드. 캐릭터 CMC(WantsToStrafe=false→
		// bOrientRotationToMovement=true)와 정렬: 회전 시 이동방향으로 몸 회전 → MM이 Run_Turn 애니 선택
		// (이전 Strafe 기본은 Run_Shuffle 선택 → 트래버설 후 회전 컷의 원인). Strafe는 LockedOn/ArmedCombat 시만.
		RotationMode = EVBRotationMode::OrientToMovement;
	}

	// MMState, Gait는 NativeUpdateAnimation 본문에서 이미 갱신됨. 여기선 _LastFrame shift만 보장.
}

void UVBAnimInstance::UpdateRootYawOffset(float DeltaSeconds, float ActorYawDelta)
{
	// 1. 모드 결정
	// 몽타주 경로가 회전을 소유하는 동안, 그리고 인계 직후 찌꺼기를 방전하는 동안에는 «비우는 쪽» 으로만
	//   돈다. 소유 중에 Accelerate 로 떨어지면 옛 장부가 몽타주 자신의 회전을 쌓고, 그 값이 소유권이
	//   돌아가는 순간 유령 회전 요청이 된다(2026-08-29 실측: aim 0 인데 measured -32.8 로 게이트 열림).
	// 조건은 «소유권» 이지 «지금 회전 중인가» 가 아니다 (2026-08-30 정정).
	// 종전에는 `IsRootMotionTurnActive()` 를 썼는데, 그 함수가 «낼 수 있는 요청이 있는가»(90도 이상)로
	//   바뀌면서 aim 이 작은 구간에서 거짓이 됐다. 그러면 이 장부가 캡슐 회전을 그대로 빨아들이고,
	//   `RotateRootBone` 이 그 값을 메시에 반대로 걸어 화면상 회전이 정확히 0 이 된다.
	//   실측(user PIE): 캡슐이 180 -> -81.6 으로 98.4 도 돌았는데 장부가 -98.4 로 같이 쌓여 몸이 안 돌았다.
	// 두 질문을 한 함수로 답한 것이 잘못이었다 - «캡슐을 잠글까» 는 각도, «옛 장부를 멈출까» 는 소유권이다.
	// 한 프레임 낡을 수 있다(이 함수가 UpdateTurnInPlaceState 보다 먼저 돈다). 무기를 뽑는 그 한 프레임의
	//   누적은 회전율 x dt 라 몇 도 수준이고, 소유권 판정의 홈을 둘로 만드는 값이 훨씬 비싸다.
	if (bTurnProfileOwnsTIP || bSuppressLegacyTIPUntilNeutral)
	{
		RootYawOffsetMode = EVBRootYawOffsetMode::BlendOut;
	}
	else if (bShouldTurnInPlace)
	{
		// 몽타주 경로(SM)는 Hold - 오프셋을 얼려 두고 몽타주의 TurnYaw 커브가 정확히 소비한다.
		//
		// MM 경로에는 소비할 커브가 없다. 종전에는 그래서 무조건 BlendOut 을 걸었는데, 그러면 풀이
		//   열려 있는 시간이 조작이 아니라 감쇠율로 정해진다 - 80도에서 TIPExitThreshold 까지
		//   BlendOutSpeed_TIP(3.0) 로 약 0.56초. 무장 회전 클립은 0.67~0.83초라 매번 중간에 잘리고,
		//   재개방 때 BlockTransition 진입창(앞 0.167초) 때문에 반드시 처음부터 다시 걸렸다.
		//   2026-08-17 트레이스 실측: 열림 에피소드 6회/6.41초, 지속 중앙 0.458 최대 0.518초로 단 한 번도
		//   클립 길이에 도달하지 못함. DB 전환 13회(초당 2회)가 그 사이클이고, user FEEL 의
		//   "회전이 여러 번 시작된다"가 이것이다. 카메라 속도와 무관했던 이유도 같다.
		//
		// MM 경로는 이 값을 더 이상 쓰지 않는다 - 게이트가 MeasuredRootYawOffset(OffsetRootBone 실측)을 읽는다.
		//   그래도 BlendOut 으로 0 에 수렴시키는 것은 경로가 SM 으로 바뀔 때(무기 교체·crouch 진입)
		//   낡은 누적값이 튀어나오지 않게 하기 위함이다.
		RootYawOffsetMode = bUsesStateMachinePath
			                    ? EVBRootYawOffsetMode::Hold
			                    : EVBRootYawOffsetMode::BlendOut;
	}
	else if (bPostTIPBlendOut)
	{
		// SM(몽타주) 경로에서 몽타주가 아직 빠지는 중이면 Hold 다. 이 구간의 인계는 미러 쪽에서
		// «장부 x 남은 가중치» 로 만들고 있는데, 여기서 장부까지 같이 줄이면 두 항이 동시에 줄어
		// 두 배로 빠진다. 가중치가 0 이 된 뒤에 비로소 장부를 0 으로 수렴시킨다.
		const bool bTIPMontageStillBlending =
			bUsesStateMachinePath && GetSlotMontageLocalWeight(VB_TIPSlotName) > KINDA_SMALL_NUMBER;
		RootYawOffsetMode = bTIPMontageStillBlending
			                    ? EVBRootYawOffsetMode::Hold
			                    : EVBRootYawOffsetMode::BlendOut;
	}
	else if (!ShouldMove && !IsFalling)
	{
		// Idle 상태 -> 축적
		RootYawOffsetMode = EVBRootYawOffsetMode::Accelerate;
	}
	else
	{
		//  이동 중 -> 빠른 BlendOut
		RootYawOffsetMode = EVBRootYawOffsetMode::BlendOut;
	}

	// 모드 별 처리
	switch (RootYawOffsetMode)
	{
		case EVBRootYawOffsetMode::Accelerate:
			// Actor 회전의 반대 방향으로 누적
			RootYawOffset = FRotator::NormalizeAxis(RootYawOffset - ActorYawDelta);

			// 과도한 역회전 방지
			RootYawOffset = FMath::Clamp(RootYawOffset, -RootYawOffsetClamp, RootYawOffsetClamp);
			break;
		case EVBRootYawOffsetMode::BlendOut:
			{
				// 이동 시 RootYawOffset -> 0 보간
				// TIP 진행 중(MM 경로)과 TIP 이탈 직후는 같은 성격의 정리라 같은 노브를 쓴다.
				// 이동으로 인한 해소만 빠른 쪽(BlendOutSpeed_Moving)이다.
				const float Speed = (bPostTIPBlendOut || bShouldTurnInPlace) ? BlendOutSpeed_TIP : BlendOutSpeed_Moving;
				RootYawOffset     = FMath::FInterpTo(RootYawOffset, 0.0f, DeltaSeconds, Speed);

				// 거의 0이면 스냅 (미세 진동 방지)
				if (FMath::Abs(RootYawOffset) < 0.1f)
				{
					RootYawOffset    = 0.0f;
					bPostTIPBlendOut = false;
				}
			}
			break;
		case EVBRootYawOffsetMode::Hold:
			break;
	}
}

// 사망 상태를 ABP 로 넘긴다. 사망 포즈에 해로운 두 노드(OffsetRootBone / LegIK)를 끄기 위한 것이다.
// 자세한 이유는 헤더의 bIsDead 주석 참조.
void UVBAnimInstance::UpdateDeathState(const APawn* OwnerPawn)
{
	// 종전에는 여기서 IAbilitySystemInterface 캐스트로 ASC 를 직접 얻었다 - 획득 방법이 프로젝트에 네 갈래로
	//   갈려 있던 것의 한 갈래였다. 엔진 정본 경로로 통일한다(인터페이스 우선, 없으면 컴포넌트 탐색).
	//   우리 폰은 전부 IAbilitySystemInterface 를 구현하므로 결과는 같고, 폴백이 붙어 범위만 넓어진다.
	const bool bDeadNow = UVBAbilitySystemStatics::IsActorDead(OwnerPawn);

	bIsDead = bDeadNow;
	// Release 는 누적 오프셋을 즉시 놓는다. 살아있을 때 값은 노드 기본값과 같아야 로코모션이 안 바뀐다.
	OffsetRootTranslationMode = bDeadNow ? EOffsetRootBoneMode::Release : EOffsetRootBoneMode::Interpolate;
	OffsetRootRotationMode    = bDeadNow ? EOffsetRootBoneMode::Release : EOffsetRootBoneMode::Accumulate;
	LegIKAlpha                = bDeadNow ? 0.0f : 1.0f;
}

// 메시가 캡슐보다 얼마나 덜 돌았는가를 '측정'한다. 종전 MM 경로는 이 값을 RootYawOffset 에
//   누적·감쇠로 추정했고, 그래서 풀이 열려 있는 시간을 애니메이션이 아니라 감쇠율이 정했다
//   (2026-08-17 실측: 열림 지속 최대 0.518초 < 클립 0.67~0.83초 = 모든 회전이 중간에 잘림).
// OffsetRootBone 노드가 유지하는 SimulatedRotation 은 클립 루트모션이 메시를 돌리면 줄고 캡슐이
//   돌면 느는 값이라, 노드가 이미 루프를 닫아 준다. 헤더 주석이 그 정의를 못박는다:
//   "Offset = ComponentTransform - SimulatedTransform".
void UVBAnimInstance::UpdateMeasuredRootYawOffset()
{
	if (!bOffsetRootBoneNodeResolved)
	{
		bOffsetRootBoneNodeResolved = true;

		// 이름이나 인덱스가 아니라 타입으로 찾는다 - ABP 를 재배선해도 안 깨진다.
		if (IAnimClassInterface* AnimClass = IAnimClassInterface::GetFromClass(GetClass()))
		{
			for (const FStructProperty* NodeProperty : AnimClass->GetAnimNodeProperties())
			{
				if (NodeProperty && NodeProperty->Struct
					&& NodeProperty->Struct->IsChildOf(FAnimNode_OffsetRootBone::StaticStruct()))
				{
					CachedOffsetRootBoneNode =
						NodeProperty->ContainerPtrToValuePtr<FAnimNode_OffsetRootBone>(this);
					break;
				}
			}
		}

		if (!CachedOffsetRootBoneNode)
		{
			// 노드가 없으면 MM TIP 은 발동하지 않는다. 조용히 죽으면 원인 추적이 불가능하므로 한 번 남긴다.
			VB_LOG(Warning, "[TIP] OffsetRootBone node not found - MM path turn-in-place disabled");
		}

		// 해석한 첫 프레임은 값을 믿지 않는다. 노드의 Reset 이 SimulatedRotation 을 컴포넌트 회전으로
		//   시딩하지만(FAnimNode_OffsetRootBone::Reset), 그 Reset 전에 읽으면 오프셋이 -ComponentYaw 로
		//   잡혀 스폰 직후 TIP 이 오발동한다.
		MeasuredRootYawOffset = 0.0f;
		return;
	}

	if (!CachedOffsetRootBoneNode)
	{
		MeasuredRootYawOffset = 0.0f;
		return;
	}

	const USkeletalMeshComponent* MeshComp = GetOwningComponent();
	if (!MeshComp)
	{
		MeasuredRootYawOffset = 0.0f;
		return;
	}

	// 부호 규약을 RootYawOffset 과 맞춘다: 캡슐이 돌면 양수로 커지고 클립이 따라 돌면 0 으로 준다.
	const float SimulatedYaw = CachedOffsetRootBoneNode->GetOffsetRootRotation().Rotator().Yaw;
	const float ComponentYaw = MeshComp->GetComponentRotation().Yaw;
	MeasuredRootYawOffset    = FRotator::NormalizeAxis(SimulatedYaw - ComponentYaw);
}

// 무기 -> 무기별 BlendListByInt 핀 번호. 로스터(WeaponPoseSlots)가 유일한 진실원이다.
// 미등록 무기를 조용히 폴백하지 않는 이유: 종전에는 enum 값이 핀 수를 넘으면 엔진이 마지막 자식으로
//   클램프해 "무기는 바뀌었는데 포즈는 남의 것"이라는 증상만 남았고, 터지지 않으니 발견이 늦었다.
int32 UVBAnimInstance::ResolveWeaponPoseIndex(EVBWeaponType InWeaponType)
{
	// 맨손은 폴백 핀이 정답이므로 조회로 내려보내지 않는다.
	// 이 로스터는 조밀해서 None 항목이 없다. 가드가 없으면 평상 상태(CurrentWeaponType == None)의
	//   매 틱이 "미등록" 분기로 떨어지고, 경고가 안 뜨는 이유가 래치 초기값이 우연히 None 이라는 것뿐이
	//   된다. 미등록 무기가 한 번이라도 나타나 래치가 그 무기로 바뀌면 그 다음 맨손 프레임부터
	//   "무기 None 미등록" 경고가 뜨고, 이후 무기<->맨손 전환마다 번갈아 뜬다. 정상 상태를 결함으로
	//   보고하는 셈이다.
	if (InWeaponType == EVBWeaponType::None)
	{
		return 0;
	}

	const int32 Found = WeaponPoseSlots.IndexOfByKey(InWeaponType);
	if (Found != INDEX_NONE)
	{
		return Found;
	}

	if (LastWarnedPoseSlotWeapon != InWeaponType)
	{
		LastWarnedPoseSlotWeapon = InWeaponType;
		VB_LOG(Warning, "무기 %s 의 포즈 핀 미등록 - 인덱스 0 재생. ABP_VBCharacter 의 WeaponPoseSlots 와 "
		                "BlendListByInt 핀 개수를 확인하라",
		       *UEnum::GetValueAsString(InWeaponType));
	}
	return 0;
}

// crouch 핀 공간 전용. 인덱스 0 이 자기 콘텐츠(납도 공용 ver_A)를 가지므로 로스터에 None 항목이 있다.
int32 UVBAnimInstance::ResolveCrouchPoseIndex(EVBWeaponType InWeaponType)
{
	const int32 Found = CrouchPoseSlots.IndexOfByKey(InWeaponType);
	if (Found != INDEX_NONE)
	{
		return Found;
	}

	if (LastWarnedCrouchSlotWeapon != InWeaponType)
	{
		LastWarnedCrouchSlotWeapon = InWeaponType;
		VB_LOG(Warning, "무기 %s 의 crouch 포즈 핀 미등록 - 인덱스 0(공용) 재생. ABP_VBCharacter 의 "
		                "CrouchPoseSlots 를 확인하라",
		       *UEnum::GetValueAsString(InWeaponType));
	}
	return 0;
}

bool UVBAnimInstance::IsRootMotionTurnActive() const
{
	// 몽타주가 도는 동안에는 무조건 참이다. 루트모션이 캡슐을 돌려 요구각이 줄어들므로,
	// «요청을 낼 행이 있나» 만으로 판정하면 회전 도중에 소유권이 풀리고 캡슐이 다시 카메라를
	// 따라간다 - 그것이 이 문제에서 다섯 번 나온 이중 회전의 모양이다.
	if (ActiveRootMotionTurnMontage && Montage_IsPlaying(ActiveRootMotionTurnMontage))
	{
		return true;
	}
	// 못 내는 요청을 만나 폴백 중이면 캡슐에게 돌려준다. 안 그러면 그 방향으로 아무도 안 돈다.
	if (bTurnFallbackUntilNeutral)
	{
		return false;
	}
	// 소유권은 다시 계산하지 않고 UpdateTurnInPlaceState 가 세운 값을 읽는다 (2026-08-30).
	//   같은 판단(HasProfile)이 두 곳에 있으면 한쪽이 낡고, 그 순간 «캡슐은 잠겼는데 장부는 쌓이는»
	//   식으로 어긋난다. 한 프레임 낡을 수 있으나(틱 순서 무보장) 그 값은 무기를 뽑고 넣을 때만
	//   바뀌므로 한 프레임의 오차는 회전율 x dt 수준이다.
	// 답은 «소유권» 이다. 각도가 아니다 (2026-08-30 되돌림).
	// 2026-08-29 에 이것을 「지금 낼 수 있는 요청이 있는가(90도 이상)」로 바꿨다가 되돌린다.
	//   그때 노린 것은 0~90 사각지대를 없애는 것이었는데, 이 구조에서는 성립하지 않는다.
	//   메시를 돌리는 주체는 애니메이션 하나뿐이고 캡슐 회전은 `OffsetRootBone` 이 흡수한다.
	//   그래서 캡슐이 카메라를 따라가면 (1)뒤처진 각이 안 쌓여 몽타주가 영영 안 나가고
	//   (2)애니가 없으니 메시도 안 돈다. user PIE 실측: 캡슐은 180 -> -77 로 도는데
	//   화면의 몸은 그대로였다(2026-08-30 23:28 진단 로그).
	// 제자리 회전의 정의가 그것이다 - 캡슐이 멈춰 서서 각을 모으고, 애니가 그 각을 소비하며
	//   캡슐과 메시를 함께 돌린다. 작은 각도에서 몸이 안 움직이는 것은 결함이 아니라 그 정의다.
	// 키는 «표현 포즈 계열» 이다(선택 무기를 쓰면 납도(맨손 자세)까지 무기 프로필로 잡힌다).
	//   그 판정은 UpdateTurnInPlaceState 가 HasProfile 로 내리고 여기서는 읽기만 한다.
	return bTurnProfileOwnsTIP;
}

void UVBAnimInstance::UpdateRootMotionTurnInPlace(const UVBTurnInPlaceConfig& InConfig)
{
	// 조합이 바뀌면 이전 실패의 걸쇠를 푼다. 무기를 바꿨는데 옛 실패가 남아 있으면 새 프로필이
	//   멀쩡한데도 회전을 안 한다.
	if (MMWeaponType != LastTurnPoseWeapon || bIsCrouching != bLastTurnCrouching)
	{
		LastTurnPoseWeapon        = MMWeaponType;
		bLastTurnCrouching        = bIsCrouching;
		bTurnFallbackUntilNeutral = false;
	}

	// 이동·낙하는 «회전 중이라도» 먼저 본다. 재생 중 조기 반환을 앞에 두면 회전 도중 이동 입력이
	//   들어와도 몽타주가 안 끊겨 캐릭터가 돌면서 미끄러진다(2026-08-29 Codex 지적).
	// 판정은 «속도» 가 아니라 «입력» 이어야 한다 - `ShouldMove` 는 `GroundSpeed > MoveThreshold` 라
	//   속도 기준인데, 회전 몽타주의 루트모션이 바로 그 속도를 만든다. 속도로 끊으면 몽타주가
	//   자기가 만든 속도에 스스로 끊기고 다음 프레임에 다시 시작한다 - 같은 각도로 재생이 반복되고
	//   회전이 진행되지 않는다(2026-08-29 user PIE 로그: aim -100.2 -> -99.6 으로 재생만 두 번).
	//   VBCharacter 쪽 보류 판정이 진작에 «입력» 을 쓴 이유가 같다.
	// 남은 회전량 장부는 «조기 반환보다 앞» 에서, 회전 중에도 매 프레임 갱신한다 (2026-09-04).
	//   캡슐이 도는 동안 AimYaw 가 줄어드는 것이 곧 «남은 양이 줄어드는 것» 이다. 종전처럼 회전 시작·끝에 장부를
	//   비우면 회전 중 카메라가 더 움직인 각을 잃는다(로그: 요청 164 = 실제 164 인데 남은 aim 111 로 90 이 곧장 다시 시작).
	UnwrappedRemainingYaw += FRotator::NormalizeAxis(AimYaw - PrevAimYawForUnwrap);
	PrevAimYawForUnwrap    = AimYaw;

	bool bWantsToMove = false;
	if (const APawn* MoveOwner = TryGetPawnOwner())
	{
		if (const UCharacterMovementComponent* MoveCMC =
		        Cast<UCharacterMovementComponent>(MoveOwner->GetMovementComponent()))
		{
			bWantsToMove = !MoveCMC->GetCurrentAcceleration().IsNearlyZero();
		}
	}
	if (bWantsToMove || IsFalling)
	{
		if (ActiveRootMotionTurnMontage && Montage_IsPlaying(ActiveRootMotionTurnMontage))
		{
			Montage_Stop(TIPBlendOutTime_Interrupted, ActiveRootMotionTurnMontage);
		}
		ActiveRootMotionTurnMontage = nullptr;
		bTurnFallbackUntilNeutral   = false;
		UnwrappedRemainingYaw       = AimYaw;   // 이동하면 캡슐이 카메라를 따라간다 - 최단 경로로 다시 맞춘다
		PendingTurnRequestYaw       = 0.0f;
		return;
	}

	// 돌고 있으면 기다린다. 루트모션이 캡슐을 돌리는 중이라 여기서 더 할 일이 없다.
	// 신원으로 확인하는 이유: 다른 몽타주가 끼어들었을 때 그것을 «회전 중» 으로 오인하면 안 된다.
	if (ActiveRootMotionTurnMontage && Montage_IsPlaying(ActiveRootMotionTurnMontage))
	{
		return;
	}
	if (ActiveRootMotionTurnMontage)
	{
		// 요청한 각과 캡슐이 실제로 돈 각을 나란히 찍는다. 실제가 0 에 가까우면 루트모션이 캡슐에 안 가고 있다.
		if (const APawn* TurnPawn = TryGetPawnOwner())
		{
			const float EndYaw   = FRotator::NormalizeAxis(TurnPawn->GetActorRotation().Yaw);
			const float Delivered = FRotator::NormalizeAxis(EndYaw - TurnStartCapsuleYaw);
			VB_LOG(Log, "[TIP-RM] end %s 요청=%.1f 실제캡슐회전=%.1f 남은aim=%.1f",
			       *ActiveRootMotionTurnMontage->GetName(), TurnRequestedYaw, Delivered, AimYaw);
		}

		// 끝났으므로 워프 타깃을 거둔다. 남겨두면 다음 회전이 옛 목표를 물려받는다.
		ActiveRootMotionTurnMontage = nullptr;
		// 남은 회전량 장부는 비우지 않는다 - 캡슐이 돈 만큼 이미 줄어 있고, 남은 것은 다음 요청의 몫이다.
		// 정착 판정 장부는 비운다. 이전 요청의 크기가 다음 요청의 «커지는 중» 판정에 섞이면 안 된다(Codex 지적).
		PendingTurnRequestYaw = 0.0f;
		if (AVBCharacter* TurnOwner = Cast<AVBCharacter>(TryGetPawnOwner()))
		{
			if (UMotionWarpingComponent* Warp = TurnOwner->GetMotionWarpingComponent())
			{
				Warp->RemoveWarpTarget(VBMotionWarpingNames::TurnInPlaceTarget);
			}
		}
	}

	// 폴백 중이면 중립으로 돌아올 때까지 다시 시도하지 않는다. 매 프레임 재시도하면 «못 낸다» 를
	// 초당 60번 다시 발견하게 되고, 그 사이 캡슐 회전과 경합한다.
	if (bTurnFallbackUntilNeutral)
	{
		if (FMath::Abs(AimYaw) <= InConfig.RearmYawThreshold)
		{
			bTurnFallbackUntilNeutral = false;
		}
		return;
	}

	// AimYaw = 컨트롤 회전 - 액터 회전. 즉 «아직 안 돈 각도» 이고 부호가 곧 방향이다.
	// 장부값(RootYawOffset)을 안 쓰는 이유: 그건 캡슐이 이미 돈 만큼 뒤늦게 쌓이는 값이라
	// «앞으로 얼마를 돌아야 하나» 를 묻는 이 자리의 답이 아니다.
	// 중립 근처는 «요청» 이 아니다. 요구각이 0 이면 부호가 없어 방향을 못 고르는데, 그것을 아래의
	//   «그 방향에 행이 없다» 와 한 갈래로 묶으면 정지해 있는 동안 매 프레임 폴백이 켜졌다 꺼진다.
	//   그러면 캡슐 정지가 깜빡여 캡슐이 카메라를 절반 속도로 쫓아가고, 회전이 시작될 각도가 안 모인다.
	//   (2026-08-28 user PIE 로그에 `aim=0.0` 으로 그 경고가 찍혀 잡혔다. 내가 넣은 결함이다.)
	if (FMath::Abs(AimYaw) <= InConfig.RearmYawThreshold)
	{
		PendingTurnRequestYaw = 0.0f;
		// 중립에서 장부를 최단 경로에 다시 맞춘다. 프레임 사이 변화가 180 을 넘는 히치가 있었다면 장부가 한 바퀴
		//   어긋나 있는데, 멈춰 선 이 자리가 그것을 고칠 유일한 기회다.
		UnwrappedRemainingYaw = AimYaw;
		return;
	}

	// ── 요청 = 남은 회전량 그 자체 (2026-09-04, Codex 설계) ─────────────────────────
	// 종전(2026-08-31~09-03)에는 «감은 양» 의 부호와 AimYaw 의 부호를 조합해 방향을 추측했다 - 감은 쪽이 최단 경로와
	//   갈리면 먼 길을 택하되 먼 길이 195 이내일 때만. 그 경계 바로 밖(오른쪽으로 196~204 감음)에서 최단 경로(왼쪽 156~164)로
	//   떨어져 왼쪽 180 클립이 나갔다(user PIE 로그 5건). 입력 1도 차이로 방향이 뒤집히는 구조였다.
	// 지금은 정규화하지 않은 남은 회전량 하나가 방향과 크기다. 오른쪽으로 200 감았으면 +200 이고, 캡슐이 200 돌면 0 이다.
	//   잔여 +56 위에 왼쪽으로 22 되감으면 +34 다. 추측할 것이 없다.
	// 180 을 넘는 요청은 행의 담당 구간(RequestMax)까지 한 번에 내고, 그것도 넘으면 ResolveStepRequest 가 같은 방향으로 나눈다.
	const float RequestYaw = UnwrappedRemainingYaw;

	// 중립 판정을 요청값으로도 한 번 더 한다 (2026-08-31).
	// 왜 두 번인가: 위의 판정은 AimYaw(실제 어긋난 각)로 하고 이 판정은 RequestYaw(감은 각)로 한다.
	//   회전이 끝나면 장부를 비우므로 «AimYaw 는 17 인데 요청은 0» 인 프레임이 생긴다. 그 상태로
	//   아래로 내려가면 부호가 없어 방향을 못 고르고 「그 방향에 행이 없다」로 떨어져 폴백이 걸린다 -
	//   그러면 캡슐이 회전을 도로 가져가 몸이 갑자기 돈다(2026-08-31 user PIE: `요청=0.0 aim=17.1`).
	// 바로 아래 블록의 주석이 이 실패를 이미 경고하고 있었는데, 값만 바꾸고 관문을 옛 값에 둬서 되살렸다.
	if (FMath::Abs(RequestYaw) <= InConfig.RearmYawThreshold)
	{
		PendingTurnRequestYaw = 0.0f;
		return;
	}

	// 카메라가 아직 도는 중이면 고르지 않는다. 문턱을 넘는 첫 프레임에 고르면 그 순간의 각도로
	//   클립이 정해져, 빠른 180 플릭이 «90 + 나머지» 두 번으로 쪼개진다.
	//   요구 각도가 커지는 속도가 기준 아래로 떨어질 때 = 카메라가 멎을 때 고른다.
	if (InConfig.RequestSettleRate > 0.0f)
	{
		const float GrowRate = (FMath::Abs(RequestYaw) - FMath::Abs(PendingTurnRequestYaw))
		                       / FMath::Max(GetDeltaSeconds(), KINDA_SMALL_NUMBER);
		PendingTurnRequestYaw = RequestYaw;
		if (GrowRate > InConfig.RequestSettleRate)
		{
			return;   // 아직 커지는 중 - 이 프레임에는 고르지 않는다
		}
	}

	float EnterYaw = 0.0f;
	if (!InConfig.TryGetEnterYaw(MMWeaponType, bIsCrouching, RequestYaw, EnterYaw))
	{
		// 이 방향에 행이 아예 없다. 데이터가 유효하면 나올 수 없는 상태다(IsDataValid 가 방향 쌍을 요구한다).
		// 그래도 나오면 캡슐에게 돌려주고 소리를 낸다 - 조용히 두면 «안 도는 캐릭터» 의 원인이 안 보인다.
		if (!bLoggedTurnRequestUnservable)
		{
			bLoggedTurnRequestUnservable = true;
			VB_LOG(Warning, "[TIP-RM] 그 방향에 행이 없다 (weapon=%d crouch=%d 요청=%.1f aim=%.1f) - 캡슐 회전으로 되돌린다",
			       static_cast<int32>(MMWeaponType), bIsCrouching ? 1 : 0, RequestYaw, AimYaw);
		}
		bTurnFallbackUntilNeutral = true;
		return;
	}
	if (FMath::Abs(RequestYaw) < EnterYaw)
	{
		// 아직 돌 때가 아니다. 캡슐은 멈춘 채로 각도를 모은다 - 이것이 제자리 회전의 정의다
		//   (2026-08-30 에 조건부 잠금을 되돌렸다. 경위는 `IsRootMotionTurnActive` 의 주석에 있다).
		return;
	}

	// 담당 상한을 넘는 남은 양은 같은 방향으로 나눈다. 첫 조각이 이번 요청이고, 나머지는 장부에 남아 다음 회전이 가져간다.
	float StepYaw = RequestYaw;
	InConfig.ResolveStepRequest(MMWeaponType, bIsCrouching, RequestYaw, StepYaw);
	const FVBTurnInPlaceEntry* Entry = InConfig.ResolveRequest(MMWeaponType, bIsCrouching, StepYaw);
	if (!Entry || !Entry->Montage)
	{
		// 진입 각도는 넘었는데 그 각도를 담당하는 행이 없다 = 구간에 빈틈이 있다.
		// IsDataValid 가 막는 상태이므로 여기 오면 자산이 검증을 안 거쳤다는 뜻이다.
		if (!bLoggedTurnRequestUnservable)
		{
			bLoggedTurnRequestUnservable = true;
			VB_LOG(Warning, "[TIP-RM] 담당 구간이 비었다 (weapon=%d crouch=%d 요청=%.1f 남은=%.1f enter=%.1f) - 캡슐 회전으로 되돌린다",
			       static_cast<int32>(MMWeaponType), bIsCrouching ? 1 : 0, StepYaw, RequestYaw, EnterYaw);
		}
		bTurnFallbackUntilNeutral = true;
		return;
	}

	// ── 회전 워프 목표를 «지금» 고정한다 ─────────────────────────────────────
	// 2026-08-28 현재 이 블록은 아무 일도 하지 않는다. 두 몽타주에서 워프 구간(AnimNotifyState_MotionWarping)을
	// 뺐기 때문이다 - 구간이 없으면 타깃을 세워도 무효다. 왜 뺐나: 진입 임계 60도에 클립이 90도라
	// 대부분의 회전이 90도 애니를 60도로 쪼그라들었고(배율 0.67), 발 접지와 몸 회전이 어긋나 뭉갰다.
	// 늘리는 워프와 줄이는 워프는 대칭이 아니다 - 그래서 허용 배율의 아래쪽 기본값이 1.0 이고,
	// 그 값은 이제 전역이 아니라 행이 쥔다(서있기 90 과 크라우치 180 에 같은 정책을 강제하지 않는다).
	// 경위는 FIND-105.
	// 목표를 매 프레임 갱신하지 않는 이유: 도는 중에 카메라가 더 움직이면 발 회전 속도가 입력에 따라
	// 흔들리고, 서버와 클라이언트의 목표도 갈릴 수 있다. 더 움직인 각은 «다음 회전» 이 가져간다.
	float WarpYaw = StepYaw;
	if (AVBCharacter* TurnOwner = Cast<AVBCharacter>(TryGetPawnOwner()))
	{
		if (UMotionWarpingComponent* Warp = TurnOwner->GetMotionWarpingComponent())
		{
			const float RefMag = FMath::Abs(Entry->ReferenceYaw);
			const float MinMag = RefMag * Entry->WarpScaleRange.X;
			const float MaxMag = RefMag * Entry->WarpScaleRange.Y;
			const float ClampedMag = FMath::Clamp(FMath::Abs(StepYaw), MinMag, MaxMag);
			WarpYaw = ClampedMag * FMath::Sign(StepYaw);

			const FRotator TargetRot(0.0f,
			                         FRotator::NormalizeAxis(TurnOwner->GetActorRotation().Yaw + WarpYaw),
			                         0.0f);
			// 위치는 지금 자리 그대로 준다 - 제자리 회전이라 옮길 것이 없다.
			Warp->AddOrUpdateWarpTargetFromLocationAndRotation(
				VBMotionWarpingNames::TurnInPlaceTarget, TurnOwner->GetActorLocation(), TargetRot);
		}
	}

	// 재생 실패는 무음이다(반환 0). 안 보면 «틀었다» 고 착각한 채 진행한다.
	// 재생 속도는 행이 쥔다 - 자산 길이가 자세마다 달라 각속도가 튀는데, 몽타주의 RateScale 과
	// 데이터를 함께 쓰면 홈이 둘이 된다.
	if (Montage_Play(Entry->Montage, Entry->PlayRate) <= 0.0f)
	{
		if (!bLoggedTurnMontagePlayFailed)
		{
			bLoggedTurnMontagePlayFailed = true;
			VB_LOG(Warning, "[TIP-RM] Montage_Play 실패: %s - 캡슐 회전으로 되돌린다", *Entry->Montage->GetName());
		}
		bTurnFallbackUntilNeutral = true;
		return;
	}
	ActiveRootMotionTurnMontage = Entry->Montage;
	TurnRequestedYaw            = StepYaw;
	// 남은 회전량 장부는 그대로 둔다 - 캡슐이 돌면서 줄어든다. 정착 판정 장부만 비운다(다음 요청의 «커지는 중» 판정 오염 방지).
	PendingTurnRequestYaw = 0.0f;
	if (const APawn* StartPawn = TryGetPawnOwner())
	{
		TurnStartCapsuleYaw = FRotator::NormalizeAxis(StartPawn->GetActorRotation().Yaw);
	}

	// aim 은 정규화된 최단 경로, 남은 = 정규화하지 않은 남은 회전량, 요청 = 이번에 내는 조각. 셋이 갈리는 자리가 180 부근이라 함께 찍는다.
	// (먼길) = 요청 부호가 최단 경로와 다름. «클립 반대방향» = 양방향 행이 반대 요청을 받은 경우.
	VB_LOG(Log, "[TIP-RM] play %s (weapon=%d crouch=%d 요청=%.1f 남은=%.1f aim=%.1f%s ref=%.1f warp=%.1f rate=%.2f%s)",
	       *Entry->Montage->GetName(), static_cast<int32>(MMWeaponType),
	       bIsCrouching ? 1 : 0, StepYaw, RequestYaw, AimYaw,
	       (FMath::Sign(StepYaw) != FMath::Sign(AimYaw)) ? TEXT("(먼길)") : TEXT(""),
	       Entry->ReferenceYaw, WarpYaw, Entry->PlayRate,
	       (FMath::Sign(Entry->ReferenceYaw) != FMath::Sign(WarpYaw)) ? TEXT(" 클립반대방향") : TEXT(""));
}

void UVBAnimInstance::UpdateTurnInPlaceState()
{
	// 사망 게이트. TIP 은 몽타주를 트는데 TIP 슬롯이 DefaultGroup 을 공유하므로, 사망 몽타주가
	// 돌고 있을 때 여기서 Montage_Play 가 나가면 사망 몽타주를 끊어 시체가 다시 일어선다.
	// 사망 재생이 PlayAnimation(SingleNode)에서 몽타주로 바뀌면서 이 경로가 처음 도달 가능해졌다.
	//
	// bIsDead 는 같은 프레임의 UpdateDeathState(NativeUpdateAnimation:83)가 이미 갱신했고 이 함수는
	// :315 라 항상 최신이다. 조기 반환 3건도 전부 :83 앞이라 "갱신 없이 여기 도달"이 불가능하다.
	// 순서를 바꾸면 한 프레임 늦은 판정이 되어 사망 몽타주를 TIP 이 끊는다 - 호출 순서가 계약이다.
	if (bIsDead) return;

	// 소유권은 «못 재면 안 가진 것» 으로 떨어뜨린다 (규칙 27). 캐릭터나 설정이 없어 아래 게이트에
	//   도달하지 못하면 이 값이 낡은 채 남는데, 낡은 참은 MM 풀을 영영 닫아 놓고 몽타주도 안 도는
	//   «회전하지 않는 캐릭터» 를 만든다. 낡은 거짓은 옛 경로가 돌 뿐이라 훨씬 가볍다.
	bTurnProfileOwnsTIP = false;

	// ── 루트모션 TIP (FIND-105 1단계, 2026-08-28) ──────────────────────────────────
	// 프로필이 있는 (무기 x 자세) 조합은 이 경로가 회전을 전담한다. 아래 종전 경로(장부 오프셋 +
	// TurnYaw 커브 소비)는 회전이 «몸» 에만 든 클립을 전제로 하는데, 그 전제가 이 문제의 근원이었다 -
	// 몽타주가 끝나면 그 회전이 포즈와 함께 사라져 되돌아온다.
	// 프로필이 없으면 그대로 아래로 떨어진다. 무기·자세별로 하나씩 옮기기 위해서다(설계 1단계 = 맨손 서있기).
	if (const AVBCharacter* OwnerChar = Cast<AVBCharacter>(TryGetPawnOwner()))
	{
		if (const UVBTurnInPlaceConfig* TIPConfig = OwnerChar->GetTurnInPlaceConfig())
		{
			// 소유권을 워커 스레드가 읽을 수 있게 남긴다. 이 값이 CHT 의 ShouldTurnInPlace 열을 닫는다.
			//   왜 여기서 세우나: 소유권 판정의 홈은 이 게이트 하나여야 한다. 두 곳에서 각자 판정하면
			//   한쪽이 낡고, 그 순간 몽타주와 MM 이 같은 회전을 동시에 연기한다(2026-08-30 실증).
			bTurnProfileOwnsTIP = TIPConfig->HasProfile(MMWeaponType, bIsCrouching);

			if (bTurnProfileOwnsTIP)
			{
				// 소유권을 넘겨받기 전에 종전 경로의 «남은 상태» 를 끈다.
				// 왜 필요한가: 이 분기가 아래를 통째로 건너뛰므로 BeginTurnInPlace/EndTurnInPlace 가
				//   돌지 않는다. 그런데 bShouldTurnInPlace 를 false 로 놓는 곳은 그 둘과
				//   NativeInitializeAnimation 뿐이라(매 프레임 리셋이 아니다) 옛 경로가 한 번 켜 두면
				//   플래그가 영영 켜진 채 남는다. 그 플래그는 CHT 의 ShouldTurnInPlace 열을 여는 열쇠라,
				//   남아 있으면 MM 회전 풀이 계속 검색 집합에 들어가 몽타주와 나란히 돈다.
				//   2026-08-29 user PIE 실측: 회전 풀이 418프레임 중 256프레임 «선택» 됐고
				//   RewindDebugger 에 같은 클립이 두 줄로 떴다(두 소유자).
				// EndTurnInPlace 가 멈추는 대상은 TIPMontageSets 소속 몽타주뿐이라 새 몽타주는 안 건드린다.
				if (bShouldTurnInPlace)
				{
					EndTurnInPlace(true);
				}
				bPostTIPBlendOut               = false;
				bRootMotionTurnOwnedLastFrame  = true;
				UpdateRootMotionTurnInPlace(*TIPConfig);
				return;
			}
			// 여기로 떨어지면 «프로필 없음» 이다. 조용히 옛 경로로 가면 배선이 안 걸린 것과 구별이 안 된다 -
			// 2026-08-28 에 실제로 그렇게 한 번 속았다(로그에 새 경로 흔적이 0건인데 원인을 못 짚었다).
			// 그래서 한 번은 소리를 낸다. 무엇을 보고 없다고 판단했는지까지 찍는다.
			if (!bLoggedTurnNoProfile)
			{
				bLoggedTurnNoProfile = true;
				VB_LOG(Log, "[TIP-RM] 프로필 없음 - 옛 경로로 간다 (weapon=%d crouch=%d rows=%d)",
				       static_cast<int32>(MMWeaponType), bIsCrouching ? 1 : 0, TIPConfig->Entries.Num());
			}
		}
	}

	// 여기까지 왔다 = 옛 경로가 회전을 도로 맡는다. 직전 프레임까지 몽타주 경로가 쥐고 있었다면
	//   그 사이 쌓인 장부는 «옛 경로가 만든 요청» 이 아니다. 중립으로 돌아올 때까지 회전을 열지 않는다.
	if (bRootMotionTurnOwnedLastFrame)
	{
		bRootMotionTurnOwnedLastFrame = false;
		bSuppressLegacyTIPUntilNeutral = true;
	}

	// DEC-007 의 분담: VB TIP 몽타주는 SM 경로(crouch/상태3 비-MM무기) 전용이고, MM 경로는 MM TurnInPlace
	//   PSD 가 담당한다. 몽타주를 두 경로에 다 틀면 VB TIP 과 MM TIP 이 이중 연기된다.
	// 2026-08-17 수정 - 그 분담의 MM 쪽이 비어 있었다. PSD 를 여는 열쇠는 CHT 의 ShouldTurnInPlace 열이고
	//   그 열을 참으로 만드는 곳은 BeginTurnInPlace 하나뿐인데, 그게 SM 전용 게이트 뒤에 있었다.
	//   결과: 발도(MM 경로) 제자리 회전에 회전 애니메이션이 아예 없었다 - Idle 포즈가 도는 캡슐 위에
	//   얹혀 "다다다닥" (user FEEL). 증거: [TIP] play 로그가 PIE 5세션 전부 0건.
	// 그래서 임계 판정은 두 경로 공통으로 올리고, 몽타주 재생만 SM 경로로 남긴다.
	const bool bCanTurnInPlace = !ShouldMove && !IsFalling;

	// 게이트 입력의 출처가 경로마다 다르다.
	//   SM 경로 = RootYawOffset (누적 + 몽타주 TurnYaw 커브가 정확히 소비 - 이미 닫힌 루프)
	//   MM 경로 = MeasuredRootYawOffset (OffsetRootBone 노드 실측 - 노드가 닫아 주는 루프)
	// MM 에 장부값을 쓰면 소비자가 없어 감쇠 타이머가 대신하고, 그러면 풀 열림 시간을 애니메이션이
	//   아니라 감쇠율이 정한다(2026-08-17 실측: 모든 회전이 클립 끝나기 전에 잘림). FIND-029.
	// 임계값도 함께 갈린다 - 두 양은 범위가 다르다(장부 피크 108~179 vs 실측 피크 30~71, 헤더 주석 참조).
	//   값을 공유하면 MM 게이트가 거의 안 열린다(2026-08-18 실측).
	const float GateYawOffset = bUsesStateMachinePath ? RootYawOffset : MeasuredRootYawOffset;
	const float GateExitThreshold = bUsesStateMachinePath ? TIPExitThreshold : TIPExitThresholdMM;

	if (bShouldTurnInPlace)
	{
		// 여기서는 커브를 소비하지 않는다. MM 경로의 오프셋 해소는 UpdateRootYawOffset 의 BlendOut 이 맡고,
		// SM(몽타주) 경로의 인계는 EndTurnInPlace 한 곳에서 일어난다 - 아래 이유.
		// 도는 동안에는 장부를 빼지 않는다 (2026-08-28, 골반 계측으로 확정).
		// 종전에는 커브가 회전 내내 장부를 빼먹어서, 몽타주가 끝나는 시점에 뿌리가 넘겨받을 각이
		// 남지 않았다. 그런데 클립의 회전은 «몸» 에 들어 있어 몽타주가 끝나면 포즈에서 사라진다 -
		// 그 순간 뿌리가 그만큼을 받아주지 못하면 몸이 통째로 되돌아간다(실측 158도. user 가 본
		// «회전 뒤 처음부터 다시 도는» 것이 이것이다).
		// 그래서 소비를 «시점 이동» 시킨다: 도는 동안 얼려 두고, 끝나는 순간 EndTurnInPlace 가 0 으로
		// 놓아 뿌리가 한 번에 받는다. 몸이 놓는 것과 뿌리가 받는 것이 같은 순간이어야 화면이 이어진다.


		if (!bCanTurnInPlace)
		{
			EndTurnInPlace(true);
			return;
		}

		if (FMath::Abs(GateYawOffset) <= GateExitThreshold)
		{
			EndTurnInPlace(false);
			return;
		}

		return;
	}

	if (!bCanTurnInPlace)
	{
		return;
	}

	// crouch 는 SM 경로 전용이라 CrouchTIPThreshold 는 장부값 쪽에만 걸린다.
	const float CurrentTIPThreshold = bUsesStateMachinePath
		                                  ? (bIsCrouching ? CrouchTIPThreshold : TIPThreshold)
		                                  : TIPThresholdMM;
	if (bSuppressLegacyTIPUntilNeutral)
	{
		// 방전 중. 중립까지 내려오면 그때부터 다시 정상 판정한다.
		if (FMath::Abs(GateYawOffset) <= GateExitThreshold)
		{
			bSuppressLegacyTIPUntilNeutral = false;
		}
		return;
	}

	if (FMath::Abs(GateYawOffset) > CurrentTIPThreshold)
	{
		BeginTurnInPlace();
	}
}

void UVBAnimInstance::BeginTurnInPlace()
{
	bShouldTurnInPlace   = true;
	bPostTIPBlendOut     = false;
	PreviousTurnYawCurve = 0.0f;

	EVBTurnInPlaceType SelectedTurnType;

	if (bIsCrouching)
	{
		SelectedTurnType = EVBTurnInPlaceType::Turn180;
		TurnType         = EVBTurnInPlaceType::None;
		CrouchTurnType   = EVBTurnInPlaceType::Turn180;
	}
	else
	{
		SelectedTurnType = CalculateTurnType(-RootYawOffset);
		TurnType         = SelectedTurnType;
		CrouchTurnType   = EVBTurnInPlaceType::None;
	}

	// MM 경로는 여기서 끝난다. CHT 의 ShouldTurnInPlace 열은 멤버 bShouldTurnInPlace 가 아니라
	//   같은 이름의 함수에 묶여 있다(2026-08-31 리드백: 세 무기 CHT 12번 열 binding="ShouldTurnInPlace").
	//   그 함수가 이 멤버를 읽어 열을 열고, MM 이 TurnInPlace PSD 를 고른다.
	//   MM 이 TurnInPlace PSD 를 고르고, 그 클립의 루트모션이 실제 회전을 만든다(2026-08-17 A안).
	//   몽타주까지 틀면 DEC-007 이 경고한 이중 연기가 된다.
	if (!bUsesStateMachinePath)
	{
		return;
	}

	// Montage 선택 및 재생 (SM 경로 전용)
	if (UAnimMontage* Montage = SelectTIPMontage(SelectedTurnType))
	{
		// FIND-029 가드 (2026-06-07, user FEEL "TIP 후 계속 카메라 회전"): TurnYaw 커브 없는
		// 몽타주는 소비가 불가능해 stuck-Hold (탈출구는 이동 또는 |offset|<=15 뿐인데 둘 다 막힘).
		// 현 TIP 클립 8종 전부 커브 부재 + 카타나 전용/컨벤션 혼재 (asset census 06-07) —
		// Phase B에서 MM TurnInPlace PSD + 무기별 상체 오버레이로 이전 예정. 그때까지 몽타주
		// 대신 부드러운 blend-out 폴백 (DEBT-006 "smooth-but-unacted" 전례와 동일 문법).
		if (!MontageHasTurnYawCurve(Montage))
		{
			if (!bLoggedTIPCurveMissing)
			{
				VB_LOG(Warning, "[TIP] montage '%s' has no '%s' curve - smooth blend-out fallback (FIND-029)",
				       *Montage->GetName(), *TurnYawCurveName.ToString());
				bLoggedTIPCurveMissing = true;
			}
			EndTurnInPlace(true); // bPostTIPBlendOut → BlendOut 모드 → 오프셋을 부드럽게 0으로
			return;
		}

		Montage_Play(Montage, 1.0f);

		// 무기별 선택 텔레메트리 (FIND-029 배터리·FEEL 진단용 — 무기↔몽타주 매칭 검증 라인)
		VB_LOG(Log, "[TIP] play %s (weapon=%d turn=%d offset=%.1f)",
		       *Montage->GetName(), static_cast<int32>(CurrentWeaponType),
		       static_cast<int32>(SelectedTurnType), RootYawOffset);

		// FIND-029 안전망: 종료를 델리게이트로 보장 — 자연 종료가 커브를 다 소비 못 했거나
		// DefaultSlot 공유 몽타주(무기 전환 연기 등)가 TIP를 캔슬해도 Hold에 갇히지 않는다.
		FOnMontageEnded EndDelegate;
		EndDelegate.BindUObject(this, &UVBAnimInstance::OnTIPMontageEnded);
		Montage_SetEndDelegate(EndDelegate, Montage);
	}
	else
	{
		// Montage가 설정 안 됨 -> TIP 취소
		EndTurnInPlace(true);
	}
}

bool UVBAnimInstance::MontageHasTurnYawCurve(const UAnimMontage* Montage) const
{
	// GetCurveValue는 재생 중 세그먼트의 커브를 평가하므로, 몽타주 자체 커브와
	// 세그먼트 소스 시퀀스 커브 어느 쪽에 있어도 소비가 가능하다 — 양쪽 다 확인.
	if (!Montage)
	{
		return false;
	}
	if (Montage->HasCurveData(TurnYawCurveName))
	{
		return true;
	}
	for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		for (const FAnimSegment& Segment : Track.AnimTrack.AnimSegments)
		{
			if (const UAnimSequenceBase* Seq = Segment.GetAnimReference())
			{
				if (Seq->HasCurveData(TurnYawCurveName))
				{
					return true;
				}
			}
		}
	}
	return false;
}

void UVBAnimInstance::OnTIPMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// 몽타주는 끝났는데 TIP 상태가 살아있으면 강제 정리 — 커브 미소비 잔존 오프셋
	// (>TIPExitThreshold)로는 UpdateTurnInPlaceState에 탈출구가 없다 (FIND-029 stuck-Hold).
	// EndTurnInPlace 내부의 Montage_Stop은 TIPMontages 멤버십을 확인하므로, 이 시점에
	// 다른 몽타주(무기 전환 연기 등)가 활성화되어 있어도 오발 정지는 없다.
	if (bShouldTurnInPlace)
	{
		EndTurnInPlace(true);
	}
}

// 2026-08-28 현재 이 함수는 불리지 않는다. 지우지 않고 남긴 이유: 회전량을 요구 각도에 맞추는
// 장치(회전 스케일)를 넣게 되면 «애니가 지금까지 얼마를 돌렸나»가 다시 필요해지고, 그 답이 이 커브다.
// 그때까지는 인계가 EndTurnInPlace 한 곳에서 일어난다. 이 결정은 FIND-105 에 적어 두었다 -
// 그 항목이 닫힐 때 이 함수를 살릴지 지울지 함께 정한다.
void UVBAnimInstance::ConsumeTurnInPlaceCurve()
{
	const float CurrentTurnYaw = GetCurveValue(TurnYawCurveName);
	const float DeltaTurnYaw   = CurrentTurnYaw - PreviousTurnYawCurve;
	PreviousTurnYawCurve       = CurrentTurnYaw;

	if (FMath::IsNearlyZero(DeltaTurnYaw))
	{
		return;
	}

	const float AdjustedDelta = FMath::Abs(DeltaTurnYaw) * FMath::Sign(RootYawOffset);

	const float NewOffset = FRotator::NormalizeAxis(RootYawOffset - AdjustedDelta);

	if (FMath::Abs(NewOffset) > FMath::Abs(RootYawOffset))
	{
		RootYawOffset = 0.0f;
		return;
	}
	RootYawOffset = NewOffset;
}

void UVBAnimInstance::EndTurnInPlace(bool bInterrupted)
{

	bShouldTurnInPlace   = false;
	PreviousTurnYawCurve = 0.0f;
	TurnType             = EVBTurnInPlaceType::None;
	CrouchTurnType       = EVBTurnInPlaceType::None;

	// TIP Montage 정지
	if (UAnimMontage* CurrentMontage = GetCurrentActiveMontage())
	{
		// TIP Montage는 DefaultGroup에 속하므로 Slot 이름으로 판별 불가.
		// 대신 TIPMontages 배열(NativeInitializeAnimation에서 구축)에 포함되는지 체크.
		if (TIPMontages.Contains(CurrentMontage))
		{
			const float BlendOutTime = bInterrupted ? TIPBlendOutTime_Interrupted : TIPBlendOutTime_Normal;
			Montage_Stop(BlendOutTime, CurrentMontage);
		}
	}

	// FIND-105 (2026-08-28): 몽타주 경로에서는 «여기가 소비 시점» 이다. 도는 동안 얼려 둔 장부를
	// 이 순간 0 으로 놓아, 몸이 놓는 회전을 뿌리가 같은 프레임에 받는다. 둘이 같은 순간이라야 이어진다.
	// MM 경로는 몽타주가 없어 이 인계가 성립하지 않으므로 종전대로 blend-out 에 맡긴다.
	// 여기서 0 으로 놓지 않는다 (2026-08-28 정정). 위 미러가 «장부 x 남은 슬롯 가중치» 로 인계를 만드는데,
	// 장부를 지금 0 으로 만들면 곱할 것이 사라져 인계가 한 프레임에 끝나고 그게 곧 깜빡임이다.
	// 장부는 블렌드아웃이 끝난 뒤 UpdateRootYawOffset 이 0 으로 수렴시킨다.
		bPostTIPBlendOut = true;
}

UAnimMontage* UVBAnimInstance::SelectTIPMontage(EVBTurnInPlaceType InTurnType) const
{
	// FIND-029 무기별 리모델 (2026-06-07): 구 A/B(평상/전투, bIsCombatMode 분기) 세트 은퇴.
	// VB TIP는 SM 경로(상태3=무기 든 전투, 또는 크라우치) 전용이므로 현재 무기가 곧 세트 키 —
	// 무기 불문 카타나 턴이 나오던 user FEEL 문제의 구조적 해소. 빈손 크라우치는 None 키가 받는다.
	const FVBTIPMontageSet* Set = TIPMontageSets.Find(CurrentWeaponType);
	if (!Set)
	{
		// 미등록 무기(Magic 등 미래 타입) — nullptr 반환 시 BeginTurnInPlace가 blend-out 폴백 처리.
		return nullptr;
	}

	switch (InTurnType)
	{
		case EVBTurnInPlaceType::TurnLeft90: return Set->TurnLeft90;
		case EVBTurnInPlaceType::TurnRight90: return Set->TurnRight90;
		case EVBTurnInPlaceType::Turn180: return bIsCrouching ? Set->CrouchTurn180 : Set->Turn180;
		default: return nullptr;
	}
}

// ========== GASP Pin Binding Helpers ==========
// OffsetRootBone 노드의 FoldProperty 속성은 runtime setter 가 없어 AnimGraph property
// pin binding 이 유일한 제어 수단. 아래 pure 헬퍼들을 set_anim_node_pin_binding 으로
// TranslationMode / RotationMode / TranslationHalflife 에 바인딩한다.

namespace
{
	// IsSlotActive 호출마다 FName 생성을 피하기 위한 캐시. DefaultSlot 은 GASP 기본 슬롯명.
	static const FName GASP_DefaultSlotName(TEXT("DefaultSlot"));

}

namespace
{

	// 루트 본과 캡슐의 yaw 격차(절대값, 도). ShouldTurnInPlace(50도)와 ShouldSpinTransition(130도)이
	// 같은 양을 서로 다른 임계로 본다. RootTransformYawCorrection 이 RootTransform 에 보정을 먹이므로
	// 그 보정을 조정하면 두 임계의 의미가 함께 바뀐다 - 계산식이 갈리면 한쪽만 고치는 사고가 난다.
	float RootVsCapsuleYawDelta(const FTransform& Root, const FTransform& Capsule)
	{
		return FMath::Abs(FRotator::NormalizeAxis(Root.Rotator().Yaw - Capsule.Rotator().Yaw));
	}
}

bool UVBAnimInstance::IsMoving() const
{
	// HasVelocity 는 UpdateEssentialValues 에서 1cm/s 기준으로 이미 갱신됨 (GASP Velocity != 0 과 동등).
	// FutureVelocity 는 Trajectory 마지막 샘플의 속도 — 예측 궤적 상 계속 움직일지 판단.
	// 두 조건 AND 로 "지금도 움직이고 앞으로도 움직일" 상태를 식별 → OffsetRootBone Interpolate 트리거.
	return HasVelocity && !FutureVelocity.IsNearlyZero(1.0f);
}

EOffsetRootBoneMode UVBAnimInstance::GetOffsetRootTranslationMode() const
{
	// DefaultSlot 에 Montage 재생 중이면 오프셋이 누적되어 캐릭터가 위치에서 벗어나면 안 됨 → Release.
	// (e.g. 공격/피격/인터랙션 몽타주 — root motion 또는 고정 위치 요구)
	// IsSlotActive 를 쓴다(GASP 1:1). GetSlotMontageLocalWeight>0 으로 대체하지 말 것 (2026-06-04 실측 교훈):
	//   weight 는 블렌드아웃 0.25s 내내 >0 이라 "몽타주 종료 순간"에도 Release 가 유지된다. 그런데 CMC 가
	//   몽타주 중 눌러둔 회전 입력을 종료 순간 한 프레임에 스냅(RotationRate=-1)시키는 게 바로 그 프레임이라,
	//   Release 상태면 오프셋 흡수가 안 돼 root 본이 캡슐 스냅을 1:1 로 따라감 → 트래버설 종료 "포즈 탁" 컷.
	//   IsSlotActive 는 블렌드아웃 시작 순간 false 로 떨어져(GASP 타이밍) 스냅 프레임에 Accumulate 가 받아낸다.
	//   (tick 캡처 실측: weight 방식 dRoot=-73.9°/1tick 통과 vs GASP dRoot=0.0°. 일반 로코모션 스냅은 흡수 확인.)
	//   thread-safety: JustTraversed() 가 동일 IsSlotActive 를 chooser(worker) 컨텍스트에서 상시 호출 중 — 전례 따름.
	if (IsSlotActive(GASP_DefaultSlotName))
	{
		return EOffsetRootBoneMode::Release;
	}

	switch (MovementMode)
	{
	case EVBMovementMode::Grounded:
		// 지면 이동 중이면 Interpolate(오프셋 점진 복귀), 정지 시 Release(즉시 해제).
		return IsMoving() ? EOffsetRootBoneMode::Interpolate : EOffsetRootBoneMode::Release;
	case EVBMovementMode::InAir:
		// 공중에서는 오프셋을 누적하지 않고 Release — 낙하 중 root 이탈 방지.
		return EOffsetRootBoneMode::Release;
	default:
		return EOffsetRootBoneMode::Release;
	}
}

float UVBAnimInstance::GetLegIKAlpha() const
{
	// 값 계산은 UpdateDeathState 가 끝내고 여기서는 읽기만 한다 (사망 시 0).
	// 2026-09-01~03: 한때 여기서 TIP 슬롯 가중치로 IK 를 물러나게 했다가 되돌렸다. 회전 끝단에 다리가 튀던
	//   증상의 원인은 이 노드가 아니라 클립의 IK 부모 뼈(ik_foot_root) 로컬 관례였다(FIND-105 (7)). IK 를 끄는 것은
	//   증상을 가리는 것이고 지면 적응까지 같이 죽는다. 다음에 같은 증상이 나오면 노드보다 클립의 IK 뼈를
	//   로컬 공간에서 먼저 재라 - 검증기 IkRootConvention 이 그것을 본다.
	return LegIKAlpha;
}

EOffsetRootBoneMode UVBAnimInstance::GetOffsetRootRotationMode() const
{
	// GASP 원본 그대로 — DefaultSlot 활성 시 Release(몽타주 포즈 유지), 그 외 항상 Accumulate.
	// Accumulate 는 root 가 capsule 회전을 역상쇄 → root 가 독립 회전. 시각적 회전은 Steering 이 제어한다.
	// (Steering 노드가 mesh 를 GetDesiredFacing 방향으로 끌어당김.)
	// IsSlotActive 를 쓴다(GASP 1:1). weight>0 대체 금지 — 이유/실측은 GetOffsetRootTranslationMode 주석 참조:
	//   IsSlotActive 는 블렌드아웃 시작 프레임(=캡슐 스냅 발생 프레임)에 false → Accumulate 가 스냅을 흡수.

	// FIND-019 A1 → DEC-007 재정의: 캡슐 회전 흡수 이중화 방지 게이트를 "SM 경로"로 좁힘.
	// SM 경로(상태3 SM/crouch SM)는 RotateRootBone(ArmedRootYawOffset)+TIP 이 흡수를 전담 → 여기선 Release.
	// MM 경로(Unarmed / 상태2 — Phase A 에서 MM 재사용)는 Accumulate. 시각 회전은 MM 내장 Steering 이 제어.
	// 구 게이트(state != Unarmed)를 유지하면 상태2-MM 의 회전 흡수가 죽어 통짜 snap 이 재발한다.
	if (bUsesStateMachinePath)
	{
		return EOffsetRootBoneMode::Release;
	}

	// 2026-08-31 추가 - TurnInPlace 슬롯도 같은 이유로 Release 다.
	// 위 규칙은 «몽타주가 포즈를 쥐면 오프셋을 놓는다» 인데, 그 몽타주를 DefaultSlot 하나로만 봤다.
	//   제자리 회전은 전용 슬롯(TurnInPlace)에서 돌므로 이 검사에 안 걸렸고, 그래서 회전 내내
	//   Accumulate 가 걸렸다. Accumulate 는 위 주석대로 «캡슐 회전을 메시에서 역상쇄» 하는 모드다 -
	//   몽타주 루트모션이 캡슐을 돌리면 그만큼 메시에서 빼버리고, 메시의 회전은 애니와 Steering 이
	//   따로 만든다. 두 회전이 겹쳐 도는 그림(user: 「다리가 한 바퀴 더 돈다」)이 여기서 나온다.
	// 근거: 이 함수는 AnimGraph 의 RotationMode 핀에 바인딩돼 있어 노드 속성보다 우선한다
	//   (`get_anim_node_pin_bindings` 로 확인 - 노드 속성만 보고 판단하면 틀린다).
	// TurnInPlace 슬롯만 «가중치» 로 본다. DefaultSlot 은 종전대로 IsSlotActive 다.
	// 왜 다르게 보나: 위 주석의 「블렌드아웃 시작 프레임에 Accumulate 가 스냅을 흡수한다」는
	//   캡슐을 돌리지 않는 DefaultSlot 몽타주 를 전제한다 - 그 몽타주가 끝나면 캡슐이 한 프레임에
	//   스냅하므로 흡수할 대상이 있다. 제자리 회전은 루트모션으로 캡슐을 서서히 돌리므로 그 스냅이 없다.
	//   그런데 IsSlotActive 를 그대로 쓰면 블렌드아웃이 시작되는 순간 Accumulate 로 되돌아가고,
	//   메시 회전의 주인이 그 프레임에 Steering 으로 넘어간다 - user 실측: 「다 돌았다 싶을 때 막 돈다」.
	// 가중치로 보면 블렌드아웃이 끝날 때까지 Release 를 유지해 주인이 한 번에 바뀌지 않는다.
	const bool bTurnSlotEngaged =
		GetSlotMontageLocalWeight(VBTIPSlot::Name) > KINDA_SMALL_NUMBER;

	return (IsSlotActive(GASP_DefaultSlotName) || bTurnSlotEngaged)
		? EOffsetRootBoneMode::Release
		: EOffsetRootBoneMode::Accumulate;
}

float UVBAnimInstance::GetOffsetRootTranslationHalfLife() const
{
	// GASP CDO 정렬 (2026-05-16): Idle=0.1, Moving=0.2.
	// EVBMMState 는 Idle/Moving 만 사용 (Stopping/Starting cleanup 완료).
	switch (MMState)
	{
	case EVBMMState::Idle:
		return OffsetRootTranslationHalfLife_Idle;
	case EVBMMState::Moving:
		return OffsetRootTranslationHalfLife_Moving;
	default:
		return OffsetRootTranslationHalfLife_Default;
	}
}

// ========== GASP MotionMatching 상태 판정 Helpers ==========

bool UVBAnimInstance::IsStarting() const
{
	// GASP 원본 (md §3.2):
	//   bool isPivotDB = CurrentDatabaseTags.Contains("Pivots")
	//   return IsMoving() && !isPivotDB && (FutureVelocity.SizeXY >= Velocity.SizeXY + SomeBias)
	// Pivot DB 가드 — Pivot 중간 pose 가 Start 와 유사해 MM 이 오판하지 않도록.
	if (!IsMoving()) return false;

	// CurrentDatabaseTags (TArray<FName>) 중 "Pivots" 포함 여부. GASP 는 부분 일치(Contains) 사용.
	for (const FName& Tag : CurrentDatabaseTags)
	{
		if (Tag.ToString().Contains(TEXT("Pivots")))
		{
			return false;
		}
	}

	// FutureVelocity 가 현재 Velocity 보다 임계치(IsStartingVelocityBias) 이상 크면 가속 중(=Start).
	return FutureVelocity.Size2D() >= (Velocity.Size2D() + IsStartingVelocityBias);
}

bool UVBAnimInstance::IsPivoting() const
{
	// GASP 원본 (md §3.2):
	//   rot_cur = RotationFromXVector(Velocity)
	//   rot_fut = RotationFromXVector(FutureVelocity)
	//   delta = abs(DeltaRotator(rot_fut, rot_cur).Yaw)
	//   threshold = (RotationMode == Strafe) ? 45° : 90°
	//   return delta >= threshold
	if (Velocity.IsNearlyZero(1.0f)) return false;
	if (FutureVelocity.IsNearlyZero(1.0f)) return false;

	const FRotator RotCur = Velocity.ToOrientationRotator();
	const FRotator RotFut = FutureVelocity.ToOrientationRotator();
	const float Delta = FMath::Abs(FRotator::NormalizeAxis(RotFut.Yaw - RotCur.Yaw));

	const float Threshold = (RotationMode == EVBRotationMode::Strafe)
		? IsPivotingStrafeThreshold
		: IsPivotingFreeThreshold;
	return Delta >= Threshold;
}

bool UVBAnimInstance::ShouldTurnInPlace() const
{
	// GASP 원본 (md §3.2):
	//   root_yaw = RootTransform.Rotator().Yaw
	//   capsule_yaw = CharacterTransform.Rotator().Yaw
	//   delta = abs(DeltaRotator(root_yaw, capsule_yaw).Yaw)
	//   return delta >= 50°
	//          && ( RotationMode == Strafe
	//               || (MovementState==Idle && MovementState_LastFrame==Moving) )
	// 몽타주 경로가 이 조합의 제자리 회전을 소유하면 MM 풀은 열리지 않는다 (2026-08-30).
	// 왜 여기여야 하나: 이 함수가 CHT 의 ShouldTurnInPlace 열에 묶인 «열쇠» 다. 멤버 변수
	//   `bShouldTurnInPlace` 와 이름이 거의 같아서 오래 헷갈렸는데, Chooser 가 읽는 것은 이 함수다
	//   (T3D 의 PropertyBindingChain=("ShouldTurnInPlace") 는 한 칸짜리라 `b` 접두 변수와 안 맞는다).
	//   그래서 옛 플래그를 아무리 꺼도 풀이 계속 열렸다.
	// 왜 이 조합이 특히 위험한가: 아래 판정은 «메시가 캡슐보다 뒤처졌는가» 인데, 몽타주 루트모션이
	//   캡슐을 돌리면 정확히 그 뒤처짐이 생긴다. 즉 몽타주가 스스로 경쟁자를 열어준다.
	//   2026-08-30 PIE 실측: 몽타주는 0회 재생인데 MM 이 카타나 TIP 풀의 «이어가는 포즈» 에 물려
	//   클립 끝 프레임(0.767초)에 얼어붙은 채 7초를 버텼다.
	if (bTurnProfileOwnsTIP)
	{
		return false;
	}

	const float Delta = RootVsCapsuleYawDelta(RootTransform, CharacterTransform);

	if (Delta < ShouldTurnInPlaceYawDelta) return false;

	// FIND-019 U1: enum(RotationMode)이 아니라 CMC 진실로 판정 — 비무장 enum 은 의도적으로
	// OrientToMovement 유지(MM 선택 품질)인데 캡슐은 strafe 로 카메라를 추종해, enum 판정 시
	// 이 게이트가 영구 false → idle 에서 카메라를 아무리 돌려도 TIP DB 가 후보에 못 들어갔다.
	const bool bIsStrafe = bCMCStrafeRotation;
	const bool bJustStopped = (MMState == EVBMMState::Idle) && (MMState_LastFrame == EVBMMState::Moving);
	return bIsStrafe || bJustStopped;
}

bool UVBAnimInstance::ShouldSpinTransition() const
{
	// GASP 원본 (md §3.2):
	//   return delta_root_vs_capsule >= 130° && Speed2D >= threshold && !CurrentDatabaseTags.Contains("SpinTransition")
	const float Delta = RootVsCapsuleYawDelta(RootTransform, CharacterTransform);

	if (Delta < ShouldSpinTransitionYawDelta) return false;
	if (Speed2D < ShouldSpinTransitionMinSpeed) return false;

	// 이미 SpinTransition DB 중이면 재진입 방지.
	for (const FName& Tag : CurrentDatabaseTags)
	{
		if (Tag.ToString().Contains(TEXT("SpinTransition")))
		{
			return false;
		}
	}
	return true;
}

bool UVBAnimInstance::EnableSteering() const
{
	// GASP 원본 그대로 (md §2 주석): "MovementState==Moving OR MovementMode==InAir".
	// 이동 중이거나 공중에서 Steering 활성 — OffsetRootBone.Accumulate 와 결합해 mesh 회전을 target 방향으로 수렴.
	// 제자리 회전은 제외한다. Steering 은 선택된 클립의 루트모션을 사후 스케일할 뿐 포즈 검색 비용에는
	//   관여하지 않는다. 그래서 회전 도중 다른 풀로 넘어가는 것을 막지 못한다 - 2026-08-18 트레이스 실증:
	//   회전 종료 4건 중 3건이 continuing pose 비용이 Idles 에 역전당한 것이었고, Steering 은 그 비용에
	//   손댈 수단이 없다. 회전 지속은 클립의 OverrideContinuingPoseCostBias 가 책임진다.
	return MMState == EVBMMState::Moving || MovementMode == EVBMovementMode::InAir;
}

FVector2D UVBAnimInstance::GetAOValue() const
{
	// GASP 원본 (md §3.3):
	//   rootRot = RootTransform.Rotator()
	//   aimRot = Character.GetBaseAimRotation()
	//   delta = DeltaRotator(aimRot, rootRot)
	//   return (delta.Yaw, delta.Pitch)
	const FRotator RootRot = RootTransform.Rotator();
	FRotator AimRot = FRotator::ZeroRotator;
	if (const APawn* Pawn = Cast<APawn>(GetOwningActor()))
	{
		AimRot = Pawn->GetBaseAimRotation();
	}

	const FRotator Delta = (AimRot - RootRot).GetNormalized();
	return FVector2D(Delta.Yaw, Delta.Pitch);
}

bool UVBAnimInstance::EnableAO() const
{
	// GASP 원본 (md §3.3):
	//   return RotationMode == Strafe
	//       && abs(Get_AOValue().Yaw) <= 90°
	//       && GetSlotLocalWeight('DefaultSlot') < someThreshold
	// FIND-019 U1 동계열 (2026-06-06): enum(RotationMode)이 아니라 CMC 진실로 판정 — 비무장 enum 은
	// MM 선택 품질 때문에 의도적으로 Free 유지라, enum 판정 시 비무장 헤드룩이 영구 꺼짐
	// (FEEL-4 "얼굴이 카메라 안 따라감"). 게이트 의도 = "캡슐이 카메라/타겟 추종 중인가" = bCMCStrafeRotation.
	if (!bCMCStrafeRotation)
	{
		return false;
	}

	const float AbsYaw = FMath::Abs(GetAOValue().X);
	if (AbsYaw > EnableAOYawThreshold)
	{
		return false;
	}

	if (GetSlotMontageLocalWeight(GASP_DefaultSlotName) >= EnableAOSlotWeightThreshold)
	{
		return false;
	}
	return true;
}

float UVBAnimInstance::GetAOValueX() const
{
	return GetAOValue().X;
}

float UVBAnimInstance::GetAOValueY() const
{
	return GetAOValue().Y;
}

float UVBAnimInstance::GetMMBlendTime() const
{
	// GASP 원본 4분기 로직 (Get_MMBlendTime CDO 검증 2026-04-27):
	//   Grounded + LastFrame Grounded → MMDefaultBlendTime (0.4s)
	//   Grounded + LastFrame InAir    → MMFastLandBlendTime (0.2s) — 착지
	//   InAir + Velocity.Z > 100      → MMVeryShortJumpBlendTime (0.15s) — 점프 시작
	//   InAir + Velocity.Z ≤ 100      → MMInAirDefaultBlendTime (0.5s) — 공중 안정
	switch (MovementMode)
	{
	case EVBMovementMode::Grounded:
		if (MovementMode_LastFrame == EVBMovementMode::InAir)
		{
			return MMFastLandBlendTime;
		}
		return MMDefaultBlendTime;

	case EVBMovementMode::InAir:
		if (Velocity.Z > MMJumpSpeedThreshold)
		{
			return MMVeryShortJumpBlendTime;
		}
		return MMInAirDefaultBlendTime;

	default:
		return MMDefaultBlendTime;
	}
}

TArray<UPoseSearchDatabase*> UVBAnimInstance::GetAllActivePoseSearchDBs() const
{
	// GASP K2Node_EvaluateChooser2 의 C++ 동등 + 디버그 로그 (2026-05-06).
	TArray<UPoseSearchDatabase*> Result;
	if (!MasterChooserTable)
	{
		VB_LOG(Warning, "[MM] MasterChooserTable null");
		return Result;
	}

	TArray<UObject*> Raw = UChooserFunctionLibrary::EvaluateChooserMulti(
		this,
		MasterChooserTable,
		UPoseSearchDatabase::StaticClass()
	);
	for (UObject* Obj : Raw)
	{
		if (UPoseSearchDatabase* PSD = Cast<UPoseSearchDatabase>(Obj))
		{
			Result.Add(PSD);
		}
	}
	return Result;
}

EPoseSearchInterruptMode UVBAnimInstance::GetMMInterruptMode() const
{
	// GASP Get_MMInterruptMode 1:1 mirror (2026-05-06 정정):
	// - 이전 ForceInterrupt: 매 state change 마다 ContinuingPose 무시 → MM 매번 처음부터 재search → anim swap 빈번 (jump 시 "빠빠빡")
	// - GASP 정통: InterruptOnDatabaseChange — 약한 interrupt, DB 가 변할 때만 search reset, ContinuingPose 보존
	//
	// 분기:
	//   MovementMode 변경 → InterruptOnDatabaseChange
	//   Grounded 상태에서만 MMState/Gait/Stance 변경 check (InAir 중 toggle 무시 — anim 안정화 핵심)
	//   else → DoNotInterrupt
	const bool bMovementModeChanged = (MovementMode != MovementMode_LastFrame);
	const bool bGroundedAndOtherChanged =
		(MovementMode == EVBMovementMode::Grounded) &&
		((MMState != MMState_LastFrame) || (Gait != Gait_LastFrame) || (Stance != Stance_LastFrame));
	// 무기 스왑 = MM 풀 교체 (K 확장 후속 2026-06-10). 정속 주행 중 슬롯키 스왑(MM↔MM 무기)은 위 4종이
	// 전부 불변이라 DoNotInterrupt → 이전 무기 풀 continuing pose 잔존(시각적으로 "안 바뀜"). 풀 교체는 절대
	// interrupt 사유 — 단 발화 시점은 NativeUpdate 의 래치가 보폭 수렴 후 1프레임 펄스로 공급(과도기 블립 방지).
	const bool bWeaponPoolChanged = bFirePoolInterrupt;

	const bool bShouldInterrupt = bMovementModeChanged || bGroundedAndOtherChanged || bWeaponPoolChanged;
	if (!bShouldInterrupt)
	{
		return EPoseSearchInterruptMode::DoNotInterrupt;
	}

	// Stop 보호 (2026-05-26 추가): Stop pose 재생 중에는 챈서 풀이 Idles 로 바뀌어도 interrupt 보내지 않음.
	// W 떼는 순간 MMState=Moving→Idle 변경 → bGroundedAndOtherChanged=true → 기존 로직은 InterruptOnDatabaseChange 반환.
	// MM 이 fresh search 하면 Stop 애니의 continuing pose 가 무시되고 Idle pose 가 가장 cheap 으로 잡혀 즉시 점프.
	// → CurrentSelectedDatabase 가 Stop 풀이면 DoNotInterrupt 유지 → continuing pose 보존 → Stop 애니가 BlockTransition 활성 시점(~0.79s)까지 안전하게 재생됨.
	// GASP 도 같은 효과를 다른 방식으로 달성하는 것으로 추정 (정확한 메커니즘 미확인). 이 fix 는 GASP 와 정확한 1:1 mirror 는 아니지만 같은 시각적 결과를 만든다.
	// 주의: Stop 보호는 "정지를 유지하는 동안"(MMState==Idle, MovementMode 불변)에만 적용한다. 정지에서 빠져나가는
	// 모든 전환은 반드시 interrupt 해야 새 anim 이 재생된다 (2026-05-31 fix):
	//   - 점프/착지 → MovementMode 변경(bMovementModeChanged) → 보호 우회 → InAir/Jumps 재생.
	//   - 다시 이동(re-move) → MMState Idle→Moving → 보호 우회 → Walk/Run Loops 재생.
	// 보호 없이 두면 W-release 순간 fresh search 가 continuing Stop pose 를 버려 즉시 cut 되므로 보호 자체는 유지.
	// Stop hold 는 continuing_pose_cost_bias -0.20(GASP 정통) + BlockTransition notify 가 담당.
	// 무기 스왑은 보호 우회 — 점프(MovementMode)/재이동(MMState) 우회와 같은 클래스 (2026-06-10):
	// stop hold 중 슬롯키 스왑도 풀이 바뀌므로 반드시 interrupt 해야 새 무기 클립이 나온다.
	if (!bMovementModeChanged && !bWeaponPoolChanged && MMState == EVBMMState::Idle && CurrentSelectedDatabase)
	{
		const FString DBName = CurrentSelectedDatabase->GetName();
		if (DBName.Contains(TEXT("Run_Stops")) || DBName.Contains(TEXT("Walk_Stops")))
		{
			return EPoseSearchInterruptMode::DoNotInterrupt;
		}
	}

	return EPoseSearchInterruptMode::InterruptOnDatabaseChange;
}

bool UVBAnimInstance::JustLandedLight() const
{
	// Landing 직후 프레임 + 수직 속도가 Heavy 임계치 미만이면 Light.
	if (!JustLanded) return false;
	return FMath::Abs(LandVelocity.Z) < HeavyLandSpeedThreshold;
}

bool UVBAnimInstance::JustLandedHeavy() const
{
	// Landing 직후 + 수직 속도 임계치 이상 → Heavy.
	if (!JustLanded) return false;
	return FMath::Abs(LandVelocity.Z) >= HeavyLandSpeedThreshold;
}

bool UVBAnimInstance::JustTraversed() const
{
	// GASP 원문 그대로 ">1.0". 사실상 영영 안 켜지는 봉인된 경로이고, 그게 맞다 (2026-06-04 확정):
	//   MovingTraversal 커브 정점은 GASP 원본·VB 리타겟 모두 "정확히 1.00" → 1.0>1.0=false → GASP 도
	//   FromTraversal 을 실전에서 선택한 적이 없다 (GASP 실측: 트래버설 종료 = Run_Pivots/Run_Stops, cost 0.1~0.35).
	//   한때 ">0.5" 로 "활성화"해 봤으나 blend-out 창에서 FromTraversal(Hurdle 포즈)이 cost 1.3~1.44 로 매칭되는
	//   far-pose 창이 열려 종료 "포즈 탁" 컷을 오히려 유발 ([ExitDiag] 프레임 로그 실측) → >1.0 원복.
	//   FromTraversal PSD 를 살리려면 임계가 아니라 커브 재설계(>1 구간 저작)가 전제 — 백로그.
	const float CurveVal = GetCurveValue(TEXT("MovingTraversal"));
	const bool bSlotActive = IsSlotActive(GASP_DefaultSlotName);
	return (CurveVal > 1.0f && !bSlotActive) || bJustTraversedFlag;
}

float UVBAnimInstance::TimeToLand() const
{
	// GASP TimeToLand — InAir chooser 의 Jumps_Far vs Jumps 분기 입력.
	// 단순 kinematic 추정: Vz < 0 (하강) 일 때, LandPredictionHeight 만큼 등속 낙하 가정 시 걸리는 시간.
	// Grounded → 0 / 상승 중(Vz>=0) → 999 / 하강 중 → H / |Vz| (clamp [0, 999]).
	// 정확한 ground 거리 LineTrace 기반 캐시는 추후 GameThread tick 에서 확장.
	if (MovementMode == EVBMovementMode::Grounded) return 0.0f;
	const float Vz = Velocity.Z;
	if (Vz >= 0.0f) return 999.0f;
	const float Result = LandPredictionHeight / -Vz;
	return FMath::Clamp(Result, 0.0f, 999.0f);
}

FTransform UVBAnimInstance::GetDesiredFacingTransform() const
{
	// GASP 원본: Trajectory 의 DesiredFacingLookaheadSeconds(기본 0.4s) 시점 샘플을 찾아
	// Transform(Position, Facing) 반환. Steering 노드가 이 Transform 방향으로 root 를 끌어당김.
	const TArray<FTransformTrajectorySample>& Samples = Trajectory.Samples;
	if (Samples.Num() == 0)
	{
		// 샘플 없으면 현재 Character 방향 유지 (안전 default).
		return CharacterTransform;
	}

	// Samples 는 TimeInSeconds 오름차순. 목표 t 이상인 첫 샘플 선택 (없으면 마지막).
	const FTransformTrajectorySample* Best = &Samples.Last();
	for (const FTransformTrajectorySample& S : Samples)
	{
		if (S.TimeInSeconds >= DesiredFacingLookaheadSeconds)
		{
			Best = &S;
			break;
		}
	}

	// Facing 은 샘플의 Rotation (quat). Position 은 샘플의 Position (world space).
	return FTransform(Best->Facing, Best->Position);
}

FQuat UVBAnimInstance::GetDesiredFacingOrientation() const
{
	// Steering.TargetOrientation pin binding 대상. FTransform→FQuat 직접.
	return GetDesiredFacingTransform().GetRotation();
}

EOrientationWarpingSpace UVBAnimInstance::GetOrientationWarpingWarpingSpace() const
{
	// GASP Get_OrientationWarpingWarpingSpace 원본 (CDO 검증 2026-04-27):
	//   OffsetRootBoneEnabled ? RootBoneTransform : ComponentTransform
	// MM 의 BlendStack 내부 OrientationWarping 노드의 WarpingSpace pin 매 프레임 갱신.
	return bOffsetRootBoneEnabled
		? EOrientationWarpingSpace::RootBoneTransform
		: EOrientationWarpingSpace::ComponentTransform;
}

FVector UVBAnimInstance::CalculateRelativeAccelerationAmount() const
{
	// GASP CalculateRelativeAccelerationAmount 원본 알고리즘 (cpp:CalculateRelativeAccelerationAmount):
	//   if !(MaxAccel>0 && MaxBraking>0): return ZeroVector
	//   isAccel = Dot(Acceleration, Velocity) > 0
	//   maxScale = isAccel ? MaxAcceleration : MaxBrakingDeceleration
	//   clamped = VelocityAcceleration.ClampSizeMax(maxScale)
	//   normalized = clamped / maxScale  // [-1..1]
	//   return UnrotateVector(normalized, CharacterTransform.Rotation)  // char-local
	const APawn* Pawn = TryGetPawnOwner();
	if (!Pawn) return FVector::ZeroVector;

	const ACharacter* Char = Cast<ACharacter>(Pawn);
	if (!Char) return FVector::ZeroVector;

	const UCharacterMovementComponent* CMC = Char->GetCharacterMovement();
	if (!CMC) return FVector::ZeroVector;

	const float MaxAccel = CMC->GetMaxAcceleration();
	const float MaxBraking = CMC->GetMaxBrakingDeceleration();
	if (MaxAccel <= 0.f || MaxBraking <= 0.f) return FVector::ZeroVector;

	const bool bAccelerating = FVector::DotProduct(Acceleration, Velocity) > 0.f;
	const float MaxScale = bAccelerating ? MaxAccel : MaxBraking;
	if (MaxScale <= KINDA_SMALL_NUMBER) return FVector::ZeroVector;

	// Clamp size to MaxScale, then divide → [-1..1] in world space.
	FVector Clamped = VelocityAcceleration;
	const float SizeSq = Clamped.SizeSquared();
	if (SizeSq > MaxScale * MaxScale)
	{
		Clamped = Clamped.GetSafeNormal() * MaxScale;
	}
	const FVector Normalized = Clamped / MaxScale;

	// World → char-local. UnrotateVector = Quat^-1 * V.
	return CharacterTransform.GetRotation().UnrotateVector(Normalized);
}

FVector2D UVBAnimInstance::GetLeanAmount() const
{
	// GASP Get_LeanAmount 원본:
	//   relAccel = CalculateRelativeAccelerationAmount()
	//   speedScale = MapRangeClamped(Speed2D, [0..MaxSpeed], [0..1])
	//   return FVector2D(X = relAccel.Y * speedScale, Y = 0)
	// Y 항상 0 — BS1D_Additive_Lean_Run 은 X 만 사용 (lateral lean).
	const FVector RelAccel = CalculateRelativeAccelerationAmount();
	const float SpeedScale = FMath::GetMappedRangeValueClamped(
		FVector2D(0.f, RunAnimRootMotionSpeed),
		FVector2D(0.f, 1.f),
		Speed2D);
	return FVector2D(RelAccel.Y * SpeedScale, 0.f);
}

float UVBAnimInstance::GetLeanAmountX() const
{
	return GetLeanAmount().X;
}

void UVBAnimInstance::CacheMMSelectedDatabase(UPARAM(ref) const FAnimNodeReference& Node)
{
	// GASP Update_MotionMatching_PostSelection 원본 로직 그대로:
	//   mmNode = ConvertToMotionMatchingNode(Node)
	//   result = GetMotionMatchingSearchResult(mmNode)
	//   CurrentSelectedDatabase = result.SelectedDatabase
	// BP 에서 BreakStruct(FPoseSearchBlueprintResult) 를 MCP 로 만들 수 없어 C++ 로 동일 체인 구현.
	EAnimNodeReferenceConversionResult ConvertResult;
	const FMotionMatchingAnimNodeReference MMNode =
		UMotionMatchingAnimNodeLibrary::ConvertToMotionMatchingNode(Node, ConvertResult);
	if (ConvertResult != EAnimNodeReferenceConversionResult::Succeeded)
	{
		return;
	}

	FPoseSearchBlueprintResult SearchResult;
	bool bValid = false;
	UMotionMatchingAnimNodeLibrary::GetMotionMatchingSearchResult(MMNode, SearchResult, bValid);
	if (bValid)
	{
		CurrentSelectedDatabase = SearchResult.SelectedDatabase;
		// GASP EventGraph 가 하는 Tags 복사를 여기서 동시 수행 —
		// IsStarting/ShouldSpinTransition 이 CurrentDatabaseTags 로 "Pivots"/"SpinTransition" 가드.
		// worker thread 접근이지만 Tags 는 UPoseSearchDatabase 의 const UPROPERTY 라 안전.
		if (CurrentSelectedDatabase)
		{
			CurrentDatabaseTags = CurrentSelectedDatabase->Tags;
		}
		else
		{
			CurrentDatabaseTags.Reset();
		}
	}
}
