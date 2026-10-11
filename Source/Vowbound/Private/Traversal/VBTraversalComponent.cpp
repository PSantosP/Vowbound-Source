// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Traversal/VBTraversalComponent.h"

#include "EngineUtils.h"            // TActorIterator - 사다리 액터 검색
#include "Traversal/VBLadder.h"

#include "Traversal/VBLevelBlock_Traversable.h"
#include "Animation/VBAnimInstance.h"   // UVBAnimInstance::InteractionTransform 직접 설정 (ABP 의 BPI_InteractionTransform 백킹 변수)
#include "Data/VBTraversalConfig.h"
#include "Data/VBClimbAnimSet.h"
#include "Components/SplineComponent.h"
#include "Character/VBCharacter.h"
#include "Character/VBWeaponStateComponent.h"
#include "Character/VBPlayerRagdollComponent.h"
#include "CharacterCommon/VBBodyControlPolicy.h"
#include "Interaction/VBGASPSmartObjectComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "MotionWarpingComponent.h"
#include "ChooserFunctionLibrary.h"   // UChooserTable 완전 정의 간접 포함 (→ Chooser.h). ConstructorHelpers 용.
#include "IObjectChooser.h"            // FChooserEvaluationContext
#include "UObject/ConstructorHelpers.h"
#include "StructUtils/InstancedStruct.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "Vowbound/Vowbound.h"

namespace
{
	// GASP 워프 타겟명 — 트래버설 몽타주의 MotionWarping NotifyState 가 참조하는 이름과 일치해야 함.
	static const FName WT_FrontLedge(TEXT("FrontLedge"));
	static const FName WT_BackLedge(TEXT("BackLedge"));
	static const FName WT_BackFloor(TEXT("BackFloor"));

	// 트레이스가 맞은 블록이 «항상 매달림» 으로 표시돼 있나. 지상·공중 두 갈림길이 같은 판정을 쓴다.
	bool IsAlwaysHangBlock(const FVBTraversalCheckResult& Result)
	{
		const AVBLevelBlock_Traversable* Block = Result.HitComponent
			? Cast<AVBLevelBlock_Traversable>(Result.HitComponent->GetOwner()) : nullptr;
		return Block && Block->ShouldAlwaysHang();
	}
}

UVBTraversalComponent::UVBTraversalComponent()
{
	// 틱은 매달림 동안만 켠다(StartHang/복구가 토글). 맨틀류는 몽타주 델리게이트로만 돌아 틱이 필요 없다.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true); // Server/Multicast RPC 용

	// CHT 디폴트 할당 — GASP CBP 의 traversal chooser 변수 디폴트 대응.
	// BP_VBCharacter 의 TraversalComponent 에서 override 가능. CHT 경로 변경 시 이 리터럴도 갱신.
	static ConstructorHelpers::FObjectFinder<UChooserTable> CHTFinder(
		TEXT("/Game/Vowbound/Animations/MotionMatching/CHT_VB_TraversalAnims"));
	if (CHTFinder.Succeeded())
	{
		TraversalChooserTable = CHTFinder.Object;
	}
}

void UVBTraversalComponent::BeginPlay()
{
	Super::BeginPlay();
	OwnerCharacter = Cast<AVBCharacter>(GetOwner());

	// 없는 것은 깨진 것이 아니다 - 다만 조용히 종전 동작으로 떨어지는 것을 한 줄로 밝힌다(규칙 27).
	if (!ClimbAnimSet || !ClimbAnimSet->HangClimbUpMontage)
	{
		VB_LOG(Warning, "Traversal: ClimbAnimSet(올라서기 몽타주) 미할당 - 매달림 비활성, 공중 잡기는 맨틀로만 간다");
	}
	if (ClimbAnimSet && !ClimbAnimSet->ClimbBlendSpace)
	{
		VB_LOG(Warning, "Traversal: 등반 블렌드스페이스 미할당 - 자유 등반 비활성, 렛지 없는 벽은 그냥 벽이다");
	}
	// 대기가 한 벌뿐이면 절반의 경우에 손발이 교차한다. 그 절반은 눈에 보이지만 로그에는 안 남으므로
	//   여기서 한 줄로 밝힌다 - 「왜 가끔만 이상하지」를 묻기 전에 알 수 있게.
	if (ClimbAnimSet && ClimbAnimSet->LadderIdleMontage && !ClimbAnimSet->LadderIdleMirrorMontage)
	{
		VB_LOG(Warning, "Ladder: 반대쪽 대기 몽타주 미할당 - 한 벌로 간다. "
			"오르내리기는 1.2초마다 좌우가 뒤집히므로 절반의 경우에 손발 넷이 40cm 씩 교차한다");
	}
	// 몽타주가 비면 등반이 «움직이긴 하는데 미끄러지는» 상태로 조용히 떨어진다. 그것을 부팅 때 밝힌다.
	//   2026-09-19 에 실제로 물렸다 - 몽타주를 데이터 자산에 물린 뒤 그 자산들을 지우고 다시 만들어
	//   참조가 전부 끊겼는데, 화면에서는 「움직이긴 한다」라서 PIE 한 번을 더 태우고서야 알았다.
	if (ClimbAnimSet)
	{
		int32 Missing = 0;
		for (const UAnimMontage* M : { ClimbAnimSet->ClimbMontageUp.Get(), ClimbAnimSet->ClimbMontageDown.Get(),
			ClimbAnimSet->ClimbMontageLeft.Get(), ClimbAnimSet->ClimbMontageRight.Get(),
			ClimbAnimSet->ClimbMontageUpLeft.Get(), ClimbAnimSet->ClimbMontageUpRight.Get(),
			ClimbAnimSet->ClimbMontageDownLeft.Get(), ClimbAnimSet->ClimbMontageDownRight.Get() })
		{
			if (!M) { ++Missing; }
		}
		if (Missing > 0)
		{
			VB_LOG(Warning, "등반 몽타주 %d/8 미할당 - 그 방향은 일정 속도로 밀려 발이 미끄러진다. "
				"DA_ClimbAnimSet 의 ClimbMontage* 를 채워라", Missing);
		}
	}
}

const UVBTraversalConfig* UVBTraversalComponent::GetConfig() const
{
	return Config ? Config : GetDefault<UVBTraversalConfig>();
}

bool UVBTraversalComponent::CapsuleSweep(const FVector& Start, const FVector& End, FHitResult& OutHit) const
{
	const ACharacter* Ch = OwnerCharacter.Get();
	if (!Ch || !Ch->GetCapsuleComponent() || !GetWorld())
	{
		return false;
	}
	const float R = Ch->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HalfH = Ch->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(VBTraversalSweep), false, Ch);
	return GetWorld()->SweepSingleByChannel(
		OutHit, Start, End, FQuat::Identity,
		GetConfig()->TraceChannel.GetValue(),
		FCollisionShape::MakeCapsule(R, HalfH), Params);
}

bool UVBTraversalComponent::TraceFloorBelowLedge(const FVector& LedgePoint, const FVector& LedgeNormal, float DownDepth, FHitResult& OutFloorHit) const
{
	const ACharacter* Ch = OwnerCharacter.Get();
	if (!Ch || !Ch->GetCapsuleComponent())
	{
		return false;
	}
	const float CapR = Ch->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HalfH = Ch->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float ProbeSkin = GetConfig()->LedgeProbeSkin;
	const FVector HOffset = LedgeNormal * (CapR + ProbeSkin);
	const FVector DownStart = LedgePoint + HOffset + FVector(0.0f, 0.0f, HalfH + ProbeSkin);
	const FVector DownEnd = LedgePoint + HOffset - FVector(0.0f, 0.0f, DownDepth);
	return CapsuleSweep(DownStart, DownEnd, OutFloorHit);
}

bool UVBTraversalComponent::PerformTraversalCheck(FVBTraversalCheckResult& R, bool bAirMode) const
{
	const ACharacter* Ch = OwnerCharacter.Get();
	if (!Ch || !Ch->GetCapsuleComponent())
	{
		return false;
	}
	const UVBTraversalConfig* C = GetConfig();
	const FVector ActorLoc = Ch->GetActorLocation();
	const FVector Forward = Ch->GetActorForwardVector();
	const float HalfH = Ch->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	// Step 2.1: 전방 Capsule 트레이스 → LevelBlock_Traversable.
	// [지상] 트레이스 거리 = GASP GetTraversalForwardTraceDistance 1:1 — 전방 로컬속도에 비례(빠를수록 멀리서 발동).
	//   UnrotateVector(Velocity, ActorRot).X = 캐릭터 기준 "앞으로" 움직이는 속도 성분. 후진/측면은 음수/0이라
	//   MapRangeClamped 하한(DistanceMin)으로 잘려 발동거리가 늘지 않는다(GASP 의도 동일).
	// [공중] AirMantleTraceDistance 고정 단거리 — 속도비례를 그대로 쓰면 점프 중 공중 속도(run≈500)로
	//   3m+ 밖 벽을 잡아 워프가 공중을 가로질러 끌고 간다(user 보고 2026-06-04). 충돌 직전에만 잡는다.
	float TraceDist;
	if (bAirMode)
	{
		TraceDist = C->AirMantleTraceDistance;
	}
	else
	{
		const float ForwardLocalSpeed = static_cast<float>(Ch->GetActorRotation().UnrotateVector(Ch->GetVelocity()).X);
		TraceDist = FMath::GetMappedRangeValueClamped(
			FVector2f(C->TraceForwardSpeedMin, C->TraceForwardSpeedMax),
			FVector2f(C->TraceForwardDistanceMin, C->TraceForwardDistanceMax),
			ForwardLocalSpeed);
	}
	FHitResult FwdHit;
	if (!CapsuleSweep(ActorLoc, ActorLoc + Forward * TraceDist, FwdHit))
	{
		return false;
	}
	AVBLevelBlock_Traversable* Block = Cast<AVBLevelBlock_Traversable>(FwdHit.GetActor());
	if (!Block)
	{
		return false;
	}
	R.HitComponent = FwdHit.GetComponent();

	// Step 2.2: 렛지 transform 질의.
	Block->GetLedgeTransforms(FwdHit.ImpactPoint, ActorLoc, R);

	// Step 3.1: front 렛지 유효?
	if (!R.bHasFrontLedge)
	{
		return false;
	}

	// Step 3.2: 캐릭터→front 렛지 스탠드 위치로 공간 확인(장애물은 무시). 막히면 실패.
	const FVector FrontStand = R.FrontLedgeLocation + FVector(0.0f, 0.0f, HalfH);
	FHitResult RoomHit;
	if (CapsuleSweep(ActorLoc, FrontStand, RoomHit))
	{
		// 장애물 자신이 아닌 무언가에 막힘 → 공간 없음.
		if (RoomHit.GetComponent() != R.HitComponent)
		{
			R.bHasFrontLedge = false;
			return false;
		}
	}

	// Step 3.3: 장애물 높이 = |front 렛지 Z − 캐릭터 발 Z|.
	// GASP 그대로: 액터 위치(캡슐 중심)에서 CapsuleHalfHeight 를 빼 발(지면 접점) 기준으로 잰다(_39 노드).
	// 중심 기준으로 재면 캡슐 절반높이(~86)만큼 작게 나와 1m 장애물이 H≈14 로 임계(50) 미달 → 일반 점프로 빠짐.
	const float FootZ = ActorLoc.Z - HalfH;
	R.ObstacleHeight = FMath::Abs(R.FrontLedgeLocation.Z - FootZ);

	// Step 3.4/3.5: 상단 sweep(front→back)로 깊이 산출(back 렛지 있을 때).
	if (R.bHasBackLedge)
	{
		const FVector FrontTop = R.FrontLedgeLocation + FVector(0.0f, 0.0f, HalfH);
		const FVector BackTop = R.BackLedgeLocation + FVector(0.0f, 0.0f, HalfH);
		FHitResult TopHit;
		if (CapsuleSweep(FrontTop, BackTop, TopHit) && TopHit.GetComponent() != R.HitComponent)
		{
			// 가로지를 공간 없음 → 깊이=front↔충돌점, back 렛지 무효화.
			R.ObstacleDepth = FVector::Dist2D(R.FrontLedgeLocation, TopHit.ImpactPoint);
			R.bHasBackLedge = false;
		}
		else
		{
			R.ObstacleDepth = FVector::Dist2D(R.FrontLedgeLocation, R.BackLedgeLocation);
		}
	}
	else
	{
		R.ObstacleDepth = 0.0f; // back 렛지 없음 → 얇은 장애물.
	}

	// Step 3.6: back 렛지에서 하향 트레이스 → 바닥. (GASP TryTraversalAction Step3.6 EXACT)
	// 오프셋/Z수치 상세는 TraceFloorBelowLedge 헬퍼 주석 참조. 깊이 = ObstacleHeight-HalfH+50 (GASP 그대로).
	if (R.bHasBackLedge)
	{
		FHitResult FloorHit;
		if (TraceFloorBelowLedge(R.BackLedgeLocation, R.BackLedgeNormal, R.ObstacleHeight - HalfH + GetConfig()->BackFloorProbeExtraDepth, FloorHit))
		{
			R.bHasBackFloor = true;
			R.BackFloorLocation = FloorHit.ImpactPoint;
			R.BackLedgeHeight = FMath::Abs(R.BackLedgeLocation.Z - FloorHit.ImpactPoint.Z);
		}
		else
		{
			R.bHasBackFloor = false;
		}
	}
	else
	{
		R.bHasBackFloor = false;
	}

	return true;
}

EVBTraversalActionType UVBTraversalComponent::DecideActionType(const FVBTraversalCheckResult& R) const
{
	// GASP Step4.1 (2026-06-01 스크린샷 정확값, UVBTraversalConfig 로 외부화).
	// In Range = ObstacleHeight(양끝 포함), 비교 = ObstacleDepth(임계).
	if (!R.bHasFrontLedge)
	{
		return EVBTraversalActionType::None;
	}
	const UVBTraversalConfig* C = GetConfig();
	const float H = R.ObstacleHeight;
	const float D = R.ObstacleDepth;
	const bool bThin = D < C->ObstacleDepthThreshold;   // <59 → Vault/Hurdle
	const bool bThick = D >= C->ObstacleDepthThreshold;  // >=59 → Mantle

	// Vault: Height∈[Min,Max] ∧ 얇음 ∧ NOT HasBackFloor
	if (bThin && !R.bHasBackFloor && H >= C->VaultHeightMin && H <= C->VaultHeightMax)
	{
		return EVBTraversalActionType::Vault;
	}
	// Hurdle: Height∈[Min,Max] ∧ 얇음 ∧ HasBackFloor ∧ BackLedgeHeight>Min
	if (bThin && R.bHasBackFloor && H >= C->HurdleHeightMin && H <= C->HurdleHeightMax
		&& R.BackLedgeHeight > C->HurdleMinBackLedgeHeight)
	{
		return EVBTraversalActionType::Hurdle;
	}
	// Mantle: Height∈[Min,Max] ∧ 두꺼움 (chooser가 ≤150 Mantle / 150-275 Climb 분기)
	if (bThick && H >= C->MantleHeightMin && H <= C->MantleHeightMax)
	{
		return EVBTraversalActionType::Mantle;
	}
	return EVBTraversalActionType::None;
}

bool UVBTraversalComponent::TryTraversalAction(bool bAirMantleOnly)
{
	if (bDoingTraversalAction || !OwnerCharacter.IsValid())
	{
		return false;
	}
	// 시작을 막는 몸 상태는 몸 소유 정책 한 곳이 정한다(구조 정리 2단계 1항). 상호작용 «중» 과 래그돌 둘뿐이다 -
	//   다른 소비처의 목록과 합치지 않는다(합집합으로 바꾸면 처형·환경 상호작용 중 맨틀이 새로 막힌다).
	const UVBGASPSmartObjectComponent* SmartObject = OwnerCharacter->FindComponentByClass<UVBGASPSmartObjectComponent>();
	const UVBPlayerRagdollComponent* Ragdoll = OwnerCharacter->FindComponentByClass<UVBPlayerRagdollComponent>();
	VBBodyControlPolicy::FTraversalStartFacts StartFacts;
	StartFacts.bSmartObjectInteracting = SmartObject && SmartObject->IsInteracting();
	StartFacts.bPlayerRagdoll = Ragdoll && Ragdoll->IsRagdollActive();
	if (VBBodyControlPolicy::BlocksTraversalStart(StartFacts)) return false;

	// Step 1-3: 트레이스. (공중 모드는 전방 트레이스가 AirMantleTraceDistance 고정 단거리)
	FVBTraversalCheckResult Result;
	if (!PerformTraversalCheck(Result, bAirMantleOnly))
	{
		// 렛지가 없다. 사다리가 먼저다 - 사다리 앞에 서서 오르려 했는데 벽으로 붙으면 그게 오진이다.
		//   사다리는 전용 액터라 «거기 있나» 를 정확히 안다(트레이스로 추정하지 않는다).
		if (TryStartLadder())
		{
			return true;
		}
		// 바라보는 쪽이 등반 가능한 면이면 거기 붙는다 -
		//   "렛지 없는 높은 벽" 이 자유 등반의 자리다. 아니면 종전대로 그냥 벽이다.
		return TryStartClimb();
	}

	// Step 4.1: ActionType 결정.
	Result.ActionType = DecideActionType(Result);
	if (Result.ActionType == EVBTraversalActionType::None)
	{
		return false;
	}

	// 지상에서 «항상 매달림» 블록을 만나면 몽타주를 고르지 않고 물러나 일반 점프로 넘긴다.
	//   그래야 점프 홀드 중 공중 체크가 렛지를 잡아 매달린다. 2026-09-18 첫 PIE: 2.5m 블록은 H_feet=248 이
	//   MantleHeightMax 안이라 지상 경로가 먼저 Climb 몽타주를 물어 매달림 갈림길에 한 번도 못 닿았다.
	//   매달림이 꺼져 있으면(클립 묶음 없음) 종전대로 올라간다 - 물러났다가 공중에서도 못 잡으면 그냥 벽이다.
	if (!bAirMantleOnly && ClimbAnimSet && ClimbAnimSet->HangClimbUpMontage && IsAlwaysHangBlock(Result))
	{
		VB_LOG(Verbose, "Traversal GROUND: 항상 매달림 블록 - 몽타주 대신 점프로 넘김");
		return false;
	}

	// 공중 모드(Vowbound 확장, user 승인 2026-06-04) — 두 가지 추가 게이트:
	// ① Mantle(/Climb)만 허용. Vault/Hurdle 은 지상 도약 전제 애니라 공중 발동 시 부자연 + 제어 강탈.
	// ② 렛지가 발(캡슐 하단)보다 MantleHeightMin 이상 "위"일 때만. DecideActionType 의 H 는
	//    |렛지Z−발Z| (GASP abs 그대로)라 공중에선 렛지가 발 아래(=자연 착지로 충분)여도 임계를 넘어
	//    "아래로 끌려가는 맨틀"이 발동할 수 있음 — 낮은 벽을 점프로 그냥 넘는 경우를 부호 있는 높이로 보호.
	if (bAirMantleOnly)
	{
		// 놓은 직후에는 같은 렛지를 다시 물지 않는다 - 점프 키를 쥔 채 놓으면 다음 틱에 바로 다시 잡힌다.
		if (GetWorld() && GetWorld()->GetTimeSeconds() < HangRegrabBlockUntil)
		{
			return false;
		}
		if (Result.ActionType != EVBTraversalActionType::Mantle)
		{
			return false;
		}
		const ACharacter* Ch = OwnerCharacter.Get();
		const float FootZ = Ch->GetActorLocation().Z - Ch->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		if (Result.FrontLedgeLocation.Z - FootZ < GetConfig()->MantleHeightMin)
		{
			return false;
		}
		// ③ 캡슐 중심(허리)이 렛지보다 아래일 때만 그랩. 몽타주 진입 가능 구간(BranchIn 윈도우)은 전부
		//    "벽 아래에서 시작"하는 도입부라, 상체가 이미 렛지 위로 넘어간 자세에서 잡으면 블렌드+워프가
		//    몸을 한 번 끌어내렸다가 올라가는 부자연("멈칫→하강→상승" user 보고 2026-06-04(b)).
		//    여기서 거르면 낙하로 허리가 렛지 아래로 내려온 다음 틱에 잡아 진입 포즈가 일치한다.
		if (Ch->GetActorLocation().Z >= Result.FrontLedgeLocation.Z)
		{
			VB_LOG(Verbose, "AirMantle 보류: 캡슐중심(%.0f) >= 렛지(%.0f) — 하강 후 재시도",
				Ch->GetActorLocation().Z, Result.FrontLedgeLocation.Z);
			return false;
		}
	}

	USkeletalMeshComponent* Mesh = OwnerCharacter->GetMesh();
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Anim || !TraversalChooserTable)
	{
		return false;
	}

	// Step 5.2: front 렛지 transform 을 AnimInstance 에 전달(PSS_Traversal "attach" 채널용).
	// rotation = MakeRotFromZ(FrontLedgeNormal). GASP CBP_SandboxCharacter::TryTraversalAction Step5.2 검증.
	//   FrontLedgeNormal(수평 바깥) 을 Z(up)축으로 넣는다. 워프(UpdateWarpTargets)의 MakeRotFromX(-normal) 과는
	//   다른 변환 — 워프는 캐릭터 facing(=-normal=X), interaction 은 attach 채널 프레임(=normal=Z).
	//   이전엔 워프 변환(MakeFromX(-normal)) 을 interaction 에도 재사용 → attach 채널(weight15) 쿼리 프레임 오류 →
	//   음수축 normal(-X/-Y) 접근 시 MotionMatch 검색 결과 없음(selDB=0, cost=MAX). Z축 정렬로 GASP 와 일치.
	// 전제: ABP_VBCharacter(BP)가 BPI_InteractionTransform 을 구현하고 이 InteractionTransform 변수를 반환해야 PSC 채널이 읽는다.
	if (UVBAnimInstance* VBAnim = Cast<UVBAnimInstance>(Anim))
	{
		VBAnim->InteractionTransform = FTransform(FRotationMatrix::MakeFromZ(Result.FrontLedgeNormal).Rotator(), Result.FrontLedgeLocation);
	}

	// 공중 모드 chooser 높이 보정(Vowbound 확장, user 보고 2026-06-04 "너무 가볍게 넘는다"):
	// Result.ObstacleHeight 는 GASP 그대로 "발 기준" |렛지Z−발Z| — 공중에선 발이 이미 벽 중턱이라
	// 2m 벽도 H≈1m 로 측정 → Mantles chooser 가 150 미만 구간의 가벼운 Mantle 애니만 고른다.
	// 애니 "선택"만큼은 벽의 실제 높이(렛지→벽 앞쪽 바닥)로 평가해 150 이상이면 Climb(손 짚고
	// 끌어올리는 묵직한) 애니가 잡히게 한다. 발동 "판정"(DecideActionType)은 발 기준 유지 —
	// "지금 위치에서 잡을 수 있는가"는 발 기준이 맞다. MotionWarping 이 실제 거리에 맞춰 보정.
	float ChooserObstacleHeight = Result.ObstacleHeight;
	if (bAirMantleOnly)
	{
		// Step3.6 back-floor 와 동일 기법의 앞면 하향 트레이스(TraceFloorBelowLedge 헬퍼) —
		// 깊이 = MantleHeightMax + FrontFloorProbeExtraDepth: 발 기준이 아닌 "벽 실높이" 측정이 목적이라 최대 맨틀 높이까지 쓸어내린다.
		FHitResult FrontFloorHit;
		const bool bFrontFloorFound = TraceFloorBelowLedge(Result.FrontLedgeLocation, Result.FrontLedgeNormal, GetConfig()->MantleHeightMax + GetConfig()->FrontFloorProbeExtraDepth, FrontFloorHit);

		// 매달림 갈림길(user 결정 2026-09-18): 그 깊이 안에 바닥이 없으면 «한 번에 올라갈 벽» 이 아니라 «매달릴 렛지» 다.
		//   바닥이 있으면 아래 종전 경로(맨틀/Climb 몽타주) 그대로 - 지금 되는 것은 하나도 안 바뀐다.
		if (ShouldHangInsteadOfMantle(Result, bFrontFloorFound))
		{
			// 손이 닿을 때만 잡는다. 너무 이르면 보류 - 점프 궤적이 올려 주는 다음 틱에 다시 본다.
			const ACharacter* Ch = OwnerCharacter.Get();
			const float FootZ = Ch->GetActorLocation().Z - Ch->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const float LedgeAboveFeet = Result.FrontLedgeLocation.Z - FootZ;
			const float Reach = ClimbAnimSet->HangRootBelowLedge + ClimbAnimSet->HangReachTolerance;
			if (LedgeAboveFeet > Reach)
			{
				VB_LOG(Verbose, "Hang 보류: 렛지-발 %.0f > 손 닿는 높이 %.0f", LedgeAboveFeet, Reach);
				return false;
			}
			Result.ActionType = EVBTraversalActionType::Hang;
			VB_LOG(Log, "Traversal AIR: type=Hang H_feet=%.0f(reach %.0f) ledge=(%.0f,%.0f,%.0f) spline=%s",
				Result.ObstacleHeight, Reach, Result.FrontLedgeLocation.X, Result.FrontLedgeLocation.Y, Result.FrontLedgeLocation.Z,
				*GetNameSafe(Result.FrontLedgeSpline));
			ServerStartHang(Result);
			return true;
		}

		if (bFrontFloorFound)
		{
			ChooserObstacleHeight = FMath::Abs(Result.FrontLedgeLocation.Z - FrontFloorHit.ImpactPoint.Z);
		}
		else
		{
			// 바닥 미발견인데 매달림이 꺼져 있는 경우(클립 묶음 미할당) → 벽이 사실상 그 이상으로 높음 —
			// 최댓값으로 클램프해 가장 묵직한 Climb 계열이 선택되게 한다(종전 동작 보존).
			ChooserObstacleHeight = GetConfig()->MantleHeightMax;
		}
	}

	// Step 5.3: CHT_VB_TraversalAnims 평가(struct 컨텍스트) → 후보 몽타주 배열.
	FVBTraversalChooserParams Params;
	Params.ActionType = Result.ActionType;
	Params.ObstacleHeight = ChooserObstacleHeight;
	Params.ObstacleDepth = Result.ObstacleDepth;
	Params.Speed = OwnerCharacter->GetVelocity().Size2D();
	// Hurdle/Vault rows distinguish the requested gait in addition to speed.
	// Sprint is an independent input flag; GetGait() contains the walk/run preference.
	if (const AVBCharacter* Player = Cast<AVBCharacter>(OwnerCharacter))
	{
		Params.Gait = Player->GetIsSprinting() ? EVBGait::Sprint : Player->GetGait();
	}
	else if (const UVBAnimInstance* State = Cast<UVBAnimInstance>(Anim))
	{
		Params.Gait = State->GetCurrentGait();
	}

	FChooserEvaluationContext Context = UChooserFunctionLibrary::MakeChooserEvaluationContext();
	Context.AddStructParam(Params); // 참조 저장 — Params 는 본 평가 스코프 내 유지.
	const FInstancedStruct Eval = UChooserFunctionLibrary::MakeEvaluateChooser(TraversalChooserTable);
	TArray<UObject*> Candidates = UChooserFunctionLibrary::EvaluateObjectChooserBaseMulti(
		Context, Eval, UAnimMontage::StaticClass());
	if (Candidates.Num() == 0)
	{
		VB_LOG(Warning, "Traversal: chooser가 후보 몽타주 0개 반환 (ActionType=%d)", (int32)Result.ActionType);
		return false;
	}

	// Step 5.4: MotionMatch — 현재 포즈/렛지 거리로 최적 몽타주 + 시작시간 선택.
	FPoseSearchBlueprintResult MM;
	UPoseSearchLibrary::MotionMatch(
		Anim, Candidates, PoseHistoryName,
		FPoseSearchContinuingProperties(), FPoseSearchFutureProperties(), MM);

	UAnimMontage* Selected = Cast<UAnimMontage>(MM.SelectedAnim);
	if (!Selected)
	{
		VB_LOG(Warning, "Traversal: MotionMatch 결과 무효(몽타주 미선택)");
		return false;
	}

	// 발동 1줄 계측 — 공중/지상 모드, 높이(발기준/chooser), 선택 몽타주/진입시간. 어색한 케이스 진단용.
	VB_LOG(Log, "Traversal %s: type=%d H_feet=%.0f H_chooser=%.0f D=%.0f speed=%.0f montage=%s start=%.2f rate=%.2f",
		bAirMantleOnly ? TEXT("AIR") : TEXT("GROUND"), (int32)Result.ActionType,
		Result.ObstacleHeight, ChooserObstacleHeight, Result.ObstacleDepth, Params.Speed,
		*Selected->GetName(), MM.SelectedTime, MM.WantedPlayRate);

	// Step 5.5: 서버 권위로 수행 요청(클라가 선택 → 서버가 전원 복제).
	ServerPerformTraversal(Result, Selected, MM.SelectedTime, MM.WantedPlayRate);
	return true;
}

bool UVBTraversalComponent::ServerPerformTraversal_Validate(const FVBTraversalCheckResult& Result, UAnimMontage* Montage, float StartTime, float PlayRate)
{
	// 악성 패킷 조기 차단(실패 = 연결 종료, WithValidation 계약). 몽타주 non-null /
	// ActionType 유효(Vault~Mantle, None=무동작 거부) / 재생 파라미터 유한·양수 / 위치 NaN 아님.
	if (!Montage)
	{
		return false;
	}
	// None 거부는 반드시 남긴다 — IsValidEnumValue 는 None 도 선언된 값이라 통과시킨다.
	// 상한을 Mantle 리터럴로 박으면 EVBTraversalActionType 에 Climb 을 정식 값으로 추가하는 순간
	// 그 트래버설이 통째로 거부되고, WithValidation 계약상 그건 연결 종료다.
	// (IsValidEnumValue 의 _MAX 관련 주의사항은 VBWeaponStateComponent::ServerSummonWeapon_Validate 참조.)
	const UEnum* ActionEnum = StaticEnum<EVBTraversalActionType>();
	if (Result.ActionType == EVBTraversalActionType::None
		|| !ActionEnum || !ActionEnum->IsValidEnumValue(static_cast<int64>(Result.ActionType)))
	{
		return false;
	}
	if (!FMath::IsFinite(PlayRate) || PlayRate <= 0.0f)
	{
		return false;
	}
	// StartTime 은 몽타주 길이 내(음수/초과 금지 — Montage 는 위에서 non-null 보장).
	if (!FMath::IsFinite(StartTime) || StartTime < 0.0f || StartTime > Montage->GetPlayLength())
	{
		return false;
	}
	if (Result.FrontLedgeLocation.ContainsNaN() || Result.BackLedgeLocation.ContainsNaN())
	{
		return false;
	}
	return true;
}

void UVBTraversalComponent::ServerPerformTraversal_Implementation(const FVBTraversalCheckResult& Result, UAnimMontage* Montage, float StartTime, float PlayRate)
{
	// [서버 권위] 클라가 보낸 ledge/warp 좌표를 서버 캐릭터 위치 기준 거리 상한으로 검증 —
	// 임의 원거리 워프(텔레포트) 조작 차단. 전체 재계산(PerformTraversalCheck) 대신 거리 상한만
	// 두는 이유: 서버-클라 위치 desync 로 정당 파쿠르가 거부되는 회귀를 피하면서 원거리 조작만 막는다.
	// 상한값의 홈은 UVBTraversalConfig::MaxPlausibleLedgeDistance 다. 일부러 TraceForwardDistanceMax 나
	// MantleHeightMax 에서 파생시키지 않았다 - 그러면 맨틀 높이를 올리는 연출 튜닝이 서버 검증을 조용히
	// 느슨하게 만든다. 앞의 두 값을 크게 올릴 때는 이 값도 사람이 직접 올릴 것.
	if (OwnerCharacter.IsValid())
	{
		const FVector CharLoc = OwnerCharacter->GetActorLocation();
		const float MaxPlausibleLedgeDist = GetConfig()->MaxPlausibleLedgeDistance;
		// 전방 렛지는 모든 트래버설이 사용 → 항상 검증. 뒤 렛지(Vault/Hurdle)·뒤 바닥(Hurdle)은 해당
		// 동작만 사용하고 그 외엔 ZeroVector → 비영일 때만 검증(미사용 ZeroVector 를 원거리로 오판해
		// Mantle 을 거부하는 것 방지). 모든 MotionWarping 타겟을 상한 안으로 묶어 원거리 워프 차단.
		const bool bFrontFar     = FVector::Dist(CharLoc, Result.FrontLedgeLocation) > MaxPlausibleLedgeDist;
		const bool bBackLedgeFar = !Result.BackLedgeLocation.IsNearlyZero() && FVector::Dist(CharLoc, Result.BackLedgeLocation) > MaxPlausibleLedgeDist;
		const bool bBackFloorFar = !Result.BackFloorLocation.IsNearlyZero() && FVector::Dist(CharLoc, Result.BackFloorLocation) > MaxPlausibleLedgeDist;
		if (bFrontFar || bBackLedgeFar || bBackFloorFar)
		{
			VB_LOG(Warning, "Traversal 거부: 서버-클라 워프 타겟 거리 초과 (조작 의심 front=%d backL=%d backF=%d)",
				bFrontFar, bBackLedgeFar, bBackFloorFar);
			return;
		}
	}
	MulticastPerformTraversal(Result, Montage, StartTime, PlayRate);
}

void UVBTraversalComponent::MulticastPerformTraversal_Implementation(const FVBTraversalCheckResult& Result, UAnimMontage* Montage, float StartTime, float PlayRate)
{
	PerformTraversalAction(Result, Montage, StartTime, PlayRate);
}

void UVBTraversalComponent::PerformTraversalAction(const FVBTraversalCheckResult& Result, UAnimMontage* Montage, float StartTime, float PlayRate)
{
	if (!OwnerCharacter.IsValid() || !Montage)
	{
		return;
	}

	bDoingTraversalAction = true;
	LastActionType = Result.ActionType;
	// GASP Select 핀 리터럴(CBP uasset 바이너리 추출 2026-06-04): Vault→Falling(얇은 벽 너머로 낙하 중 종료 — 중력 인계), 그 외→Walking.
	RecoverMovementMode = (Result.ActionType == EVBTraversalActionType::Vault) ? MOVE_Falling : MOVE_Walking;

	// 파쿠르 중 무기 임시수납 (user 2026-06-09): 트래버설은 양손이 필요 → 무장 상태면 무기를 잠깐 치우고(temp-sheathe)
	// 종료 시 복원한다. 서버 권한·armed(IsWeaponSummoned) 가드는 TempSheatheForParkour 내부가 처리(비무장이면 no-op).
	// CombatExitTimer 도 그 안에서 일시정지되어, 파쿠르 동안 자동 납도 평가가 멈춘다.
	if (UVBWeaponStateComponent* WSC = OwnerCharacter->GetWeaponStateComponent())
	{
		WSC->TempSheatheForParkour();
	}

	// Z 루트모션 허용 + 장애물 콜리전 무시.
	if (UCharacterMovementComponent* CMC = OwnerCharacter->GetCharacterMovement())
	{
		CMC->SetMovementMode(MOVE_Flying);
	}
	if (Result.HitComponent)
	{
		if (UCapsuleComponent* Capsule = OwnerCharacter->GetCapsuleComponent())
		{
			Capsule->IgnoreComponentWhenMoving(Result.HitComponent, true);
			IgnoredObstacle = Result.HitComponent;
		}
	}

	UpdateWarpTargets(Result, Montage);

	// 다른 클라도 FromTraversal 블렌드/채널용으로 InteractionTransform 보유.
	if (UVBAnimInstance* VBAnim = Cast<UVBAnimInstance>(OwnerCharacter->GetMesh() ? OwnerCharacter->GetMesh()->GetAnimInstance() : nullptr))
	{
		VBAnim->InteractionTransform = FTransform(FRotationMatrix::MakeFromZ(Result.FrontLedgeNormal).Rotator(), Result.FrontLedgeLocation);
	}
	PlayTraversalMontage(Montage, StartTime, PlayRate);
}

void UVBTraversalComponent::PlayTraversalMontage(UAnimMontage* Montage, float StartTime, float PlayRate)
{
	USkeletalMeshComponent* Mesh = OwnerCharacter.IsValid() ? OwnerCharacter->GetMesh() : nullptr;
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (Anim && Montage)
	{
		// 마지막 인자 false = 같은 그룹의 몽타주를 안 끊는다. DriveLoopingMontage 는 2026-09-19 에
		//   이것을 true 로 뒤집었는데(교차 페이드를 엔진에 맡기려고) 여기는 그대로 둔다.
		//   이유: 이쪽은 한 번 재생하는 트래버설 동작이고 보통 몽타주가 없는 상태에서 들어온다.
		//   같은 결함이 날 수 있는 자리이긴 하나 실제로 발화한 것을 못 봤고, 지금 작동하는 볼트·맨틀의
		//   전환 느낌을 근거 없이 바꾸지 않는다. 그 전환에서 자세가 비쳐 나오는 것이 보이면 여기부터 본다.
		Anim->Montage_Play(Montage, PlayRate, EMontagePlayReturnType::MontageLength, StartTime, false);
		// GASP PlayMontage 노드의 OnInterrupted/OnCompleted 양 핀 대응 — 두 델리게이트 모두 바인딩.
		// 조기 탈출(BP_NotifyState_MontageBlendOut 의 Montage_Stop)= BlendingOut(bInterrupted=true) 에서 즉시 복구,
		// 자연 종료 = Ended(bInterrupted=false) 에서 복구. RecoverFromTraversal 가드로 1회만 실행.
		MontageBlendingOutDelegate.BindUObject(this, &UVBTraversalComponent::OnTraversalMontageBlendingOut);
		Anim->Montage_SetBlendingOutDelegate(MontageBlendingOutDelegate, Montage);
		MontageEndedDelegate.BindUObject(this, &UVBTraversalComponent::OnTraversalMontageEnded);
		Anim->Montage_SetEndDelegate(MontageEndedDelegate, Montage);
	}
	else
	{
		// 애님 없으면 즉시 복구. 콜백을 경유하지 않고 직접 부른다 — 콜백 둘은 bInterrupted 로 역할을 나눠
		// 가지므로(Ended 는 !bInterrupted 일 때만 복구) 어느 쪽에 어떤 값을 넘겨도 "무조건 복구"가 안 된다.
		// 복구가 누락되면 bDoingTraversalAction 이 굳어 MOVE_Flying/콜리전무시/임시수납이 남고,
		// VBCharacter Tick 의 회전 소유 판정도 계속 참이라 루트모션 중 캡슐 회전이 영구히 멈춘다.
		RecoverFromTraversal();
	}
}

void UVBTraversalComponent::UpdateWarpTargets(const FVBTraversalCheckResult& Result, UAnimMontage* /*Montage*/)
{
	UMotionWarpingComponent* MW = OwnerCharacter.IsValid() ? OwnerCharacter->GetMotionWarpingComponent() : nullptr;
	if (!MW)
	{
		return;
	}

	// FrontLedge — 항상. rotation = MakeRotFromX(-frontNormal) (캐릭터가 장애물 안쪽을 향하게).
	// 매달림 올라서기만 예외로 렛지 «안쪽 stand 지점» 을 타겟으로 쓴다 - 그 몽타주의 워프 창은 전 구간이라
	//   타겟이 곧 최종 root 위치다(맨틀류는 워프 창이 도입부만 덮어 모서리 자체가 타겟이다).
	const FRotator FrontRot = FRotationMatrix::MakeFromX(-Result.FrontLedgeNormal).Rotator();
	FVector FrontTarget = Result.FrontLedgeLocation;
	if (Result.ActionType == EVBTraversalActionType::Hang && ClimbAnimSet)
	{
		FrontTarget -= Result.FrontLedgeNormal * ClimbAnimSet->HangClimbUpStandInset;
	}
	MW->AddOrUpdateWarpTargetFromLocationAndRotation(WT_FrontLedge, FrontTarget, FrontRot);

	// BackLedge — Hurdle/Vault 만.
	if (Result.bHasBackLedge &&
		(Result.ActionType == EVBTraversalActionType::Hurdle || Result.ActionType == EVBTraversalActionType::Vault))
	{
		const FRotator BackRot = FRotationMatrix::MakeFromX(-Result.BackLedgeNormal).Rotator();
		MW->AddOrUpdateWarpTargetFromLocationAndRotation(WT_BackLedge, Result.BackLedgeLocation, BackRot);
	}
	else
	{
		MW->RemoveWarpTarget(WT_BackLedge);
	}

	// BackFloor — Hurdle 만. (GASP는 몽타주 커브 기반 위치 보정; T5 몽타주 워프 윈도우 확보 후 정밀화.)
	if (Result.bHasBackFloor && Result.ActionType == EVBTraversalActionType::Hurdle)
	{
		MW->AddOrUpdateWarpTargetFromLocationAndRotation(WT_BackFloor, Result.BackFloorLocation, FrontRot);
	}
	else
	{
		MW->RemoveWarpTarget(WT_BackFloor);
	}
}

void UVBTraversalComponent::OnTraversalMontageBlendingOut(UAnimMontage* /*Montage*/, bool bInterrupted)
{
	// 조기 탈출(notify 의 Montage_Stop / 외부 인터럽트)만 — 블렌드아웃 "시작" 즉시 복구해야
	// 블렌드 중에도 Walking 가속이 걸려 GASP 처럼 흐르듯 이어진다. 자연 종료는 Ended 쪽에서.
	if (bInterrupted)
	{
		RecoverFromTraversal();
	}
}

void UVBTraversalComponent::OnTraversalMontageEnded(UAnimMontage* /*Montage*/, bool bInterrupted)
{
	// 자연 종료(완주)만 — 조기 탈출은 BlendingOut(bInterrupted=true)에서 이미 복구됨.
	if (!bInterrupted)
	{
		RecoverFromTraversal();
	}
}

void UVBTraversalComponent::RecoverFromTraversal()
{
	if (!bDoingTraversalAction)
	{
		return; // 이미 복구됨 (양 콜백 중 첫 호출만 유효).
	}
	bDoingTraversalAction = false;
	// 매달림 흔적도 여기서 지운다 - 어느 길로 끝났든(올라서기·놓기·외부 인터럽트) 이 함수 하나가 복구의 홈이다.
	bIsHanging = false;
	HangLedge = nullptr;
	HangMoveInput = FVector2D::ZeroVector;
	HangMoveAxis = 0.0f;
	bHangExitRequested = false;
	bHangExitInputArmed = false;
	HangIKAlpha = 0.0f;
	HangIKTargets = FVBHangIKTargets(); // 가중치 0 = ABP 의 IK 노드가 통째로 꺼진다
	if (ActiveShimmyMontage)
	{
		if (USkeletalMeshComponent* Mesh = OwnerCharacter.IsValid() ? OwnerCharacter->GetMesh() : nullptr)
		{
			if (UAnimInstance* Anim = Mesh->GetAnimInstance())
			{
				Anim->Montage_Stop(0.15f, ActiveShimmyMontage);
			}
		}
		ActiveShimmyMontage = nullptr;
	}
	ShimmyMontagePosition = 0.0f;

	// 등반 흔적도 같은 자리에서 지운다. 놓기뿐 아니라 죽음·외부 인터럽트로 끝나도 여기를 지난다.
	//   특히 루트모션 모드는 되돌리지 않으면 지상 로코모션이 통째로 죽는다 - 캐릭터 전체에 걸리는 설정이다.
	if (bIsClimbing)
	{
		bIsClimbing = false;
		ClimbMoveInput = FVector2D::ZeroVector;
		ClimbMoveAxis = FVector2D::ZeroVector;
		ClimbIKTargets = FVBHangIKTargets(); // 가중치 0 = ABP 의 IK 노드가 통째로 꺼진다
		bClimbPlacedOnce = false;
		if (ActiveClimbMontage)
		{
			if (USkeletalMeshComponent* Mesh = OwnerCharacter.IsValid() ? OwnerCharacter->GetMesh() : nullptr)
			{
				if (UAnimInstance* Anim = Mesh->GetAnimInstance())
				{
					Anim->Montage_Stop(0.15f, ActiveClimbMontage);
				}
			}
			ActiveClimbMontage = nullptr;
			ActiveClimbDir = FVector2D::ZeroVector;
			ClimbPendingMontage = nullptr;
		}
	}
	// 사다리도 같은 자리에서 정리한다 - 복구의 홈은 하나다.
	if (bIsOnLadder)
	{
		bIsOnLadder = false;
		ActiveLadder = nullptr;
		bLadderPlacedOnce = false;
		bLadderMontageTracked = false;
		bLadderSettling = false;
		LadderResumeMontage = nullptr;
		if (USkeletalMeshComponent* Mesh = OwnerCharacter.IsValid() ? OwnerCharacter->GetMesh() : nullptr)
		{
			if (UAnimInstance* Anim = Mesh->GetAnimInstance())
			{
				if (ActiveLadderMontage)
				{
					Anim->Montage_Stop(0.15f, ActiveLadderMontage);
				}
			}
		}
		ActiveLadderMontage = nullptr;
	}
	SetComponentTickEnabled(false);

	if (OwnerCharacter.IsValid())
	{
		// 파쿠르 종료 → 무기 복원 (temp-sheathe 했던 경우만; RestoreFromTempSheathe 가 가드 + CombatTimer 재개).
		if (UVBWeaponStateComponent* WSC = OwnerCharacter->GetWeaponStateComponent())
		{
			WSC->RestoreFromTempSheathe();
		}
		if (UCharacterMovementComponent* CMC = OwnerCharacter->GetCharacterMovement())
		{
			// 진입 때 정한 모드로(Vault·매달림 놓기 = Falling, 그 외 = Walking).
			CMC->SetMovementMode(RecoverMovementMode);
		}
		// 매달림 게이트가 껐던 캡슐 회전 플래그를 되돌린다(ApplyRotationMode 가 IsHanging 을 본다).
		OwnerCharacter->ApplyRotationMode();
		OwnerCharacter->SetAnimRootMotionTranslationScale(1.0f);
		if (IgnoredObstacle)
		{
			if (UCapsuleComponent* Capsule = OwnerCharacter->GetCapsuleComponent())
			{
				Capsule->IgnoreComponentWhenMoving(IgnoredObstacle, false);
			}
		}
		if (UMotionWarpingComponent* MW = OwnerCharacter->GetMotionWarpingComponent())
		{
			MW->RemoveWarpTarget(WT_FrontLedge);
			MW->RemoveWarpTarget(WT_BackLedge);
			MW->RemoveWarpTarget(WT_BackFloor);
		}
	}
	IgnoredObstacle = nullptr;
}

// ===================== 매달림 (Hang) =====================

bool UVBTraversalComponent::ShouldHangInsteadOfMantle(const FVBTraversalCheckResult& Result, bool bFrontFloorFound) const
{
	// 올라설 길이 없으면 매달리지 않는다 - 클립 묶음이 곧 기능 스위치다.
	if (!ClimbAnimSet || !ClimbAnimSet->HangClimbUpMontage || !Result.FrontLedgeSpline)
	{
		return false;
	}
	if (IsAlwaysHangBlock(Result))
	{
		return true;
	}
	// 렛지 아래 MantleHeightMax(+여유) 안에 바닥이 없다 = 한 번에 올라갈 벽이 아니다.
	return !bFrontFloorFound;
}

FTransform UVBTraversalComponent::ComputeHangCapsuleTransform(const FVector& LedgePoint, const FVector& LedgeNormal) const
{
	// 얼굴은 벽 쪽(-법선). root 는 렛지에서 바깥으로 HangFootDepth, 아래로 HangRootBelowLedge.
	// 캡슐 중심 = root - (메시 상대 위치를 액터 회전으로 돌린 것). 메시가 캡슐 어디에 붙어 있든 root 가 그 자리에 온다.
	const FRotator Facing = FRotationMatrix::MakeFromX(-LedgeNormal).Rotator();
	// 높이도 깊이와 같이 상태에 따라 다르다 - 옆걸음 자세는 손이 더 내려와 렛지 윗면 아래로 잠긴다.
	const float Below  = HangBelowCurrent;
	// 깊이 기준은 «손» 이다. 손이 렛지에 정확히 놓여야 «잡고 있다» 로 읽히고, 그 자리는 클립이 이미 맞춰 놨다.
	//   손보다 20cm 깊은 발은 이대로 두면 벽에 박히므로 발 IK 가 바깥으로 민다 - 다리는 접으면 되니 항상 풀린다.
	//   반대로 발 기준으로 놓고 손을 밀면 팔이 모자랄 때 못 푼다(2026-09-18 판단).
	// 깊이는 상태에 따라 다르다 - 대기 자세와 옆걸음 자세의 손 깊이가 다르기 때문이다. UpdateHang 이 옮겨 놓은 현재값을 쓴다.
	const float Behind = HangDepthCurrent;
	const FVector RootWorld = LedgePoint + LedgeNormal * Behind - FVector(0.0f, 0.0f, Below);
	FVector MeshRel = FVector::ZeroVector;
	if (OwnerCharacter.IsValid() && OwnerCharacter->GetMesh())
	{
		MeshRel = OwnerCharacter->GetMesh()->GetRelativeLocation();
	}
	return FTransform(Facing, RootWorld - Facing.RotateVector(MeshRel));
}

bool UVBTraversalComponent::ServerStartHang_Validate(const FVBTraversalCheckResult& Result)
{
	// 패킷 위생만(실패 = 연결 종료). 거리 상한은 _Implementation 이 거부로 처리한다.
	return Result.ActionType == EVBTraversalActionType::Hang
		&& Result.FrontLedgeSpline != nullptr
		&& !Result.FrontLedgeLocation.ContainsNaN() && !Result.FrontLedgeNormal.ContainsNaN();
}

void UVBTraversalComponent::ServerStartHang_Implementation(const FVBTraversalCheckResult& Result)
{
	if (bDoingTraversalAction)
	{
		return; // 이미 도는 중(맨틀 몽타주·매달림) - 원격 클라의 재요청 무시
	}
	if (OwnerCharacter.IsValid()
		&& FVector::Dist(OwnerCharacter->GetActorLocation(), Result.FrontLedgeLocation) > GetConfig()->MaxPlausibleLedgeDistance)
	{
		VB_LOG(Warning, "Traversal 거부: 매달림 렛지 거리 초과 (조작 의심)");
		return;
	}
	MulticastStartHang(Result);
}

void UVBTraversalComponent::MulticastStartHang_Implementation(const FVBTraversalCheckResult& Result)
{
	StartHang(Result);
}

void UVBTraversalComponent::StartHang(const FVBTraversalCheckResult& Result)
{
	USplineComponent* Spline = Result.FrontLedgeSpline;
	if (!OwnerCharacter.IsValid() || !Spline || !GetWorld())
	{
		return;
	}

	bDoingTraversalAction = true;
	bIsHanging = true;
	LastActionType = EVBTraversalActionType::Hang;
	RecoverMovementMode = MOVE_Walking; // 올라서기 기본. 놓기가 Falling 으로 덮는다.
	HangLedge = Spline;
	HangMoveInput = FVector2D::ZeroVector;
	HangMoveAxis = 0.0f;
	bHangExitRequested = false;
	bHangExitInputArmed = false; // 쥐고 들어온 W/S 는 무시 - 중립을 한 번 지나야 앞/뒤 나가기가 산다

	// 옆걸음 범위 = 렛지 폭에서 양끝 MinLedgeWidth/2 를 뺀 구간(GetLedgeTransforms 의 코너 클램프와 같은 값).
	const AVBLevelBlock_Traversable* Block = Cast<AVBLevelBlock_Traversable>(Spline->GetOwner());
	const float HalfWidth = Block ? Block->GetMinLedgeWidth() * 0.5f : 0.0f;
	const float Length = Spline->GetSplineLength();
	HangMinDistance = FMath::Min(HalfWidth, Length * 0.5f);
	HangMaxDistance = FMath::Max(Length - HalfWidth, Length * 0.5f);
	HangDistance = FMath::Clamp(
		Spline->GetDistanceAlongSplineAtLocation(Result.FrontLedgeLocation, ESplineCoordinateSpace::World),
		HangMinDistance, HangMaxDistance);

	BeginSurfaceAttachment(Result.HitComponent);
	// 루트모션은 켜 둔다. 옆걸음 몽타주가 캡슐을 직접 몰고, 이 컴포넌트는 그 결과를 렛지 위로 되돌려 놓기만 한다.
	//   한때 이것을 0 으로 껐다가 클립에서 값을 꺼내 흉내 냈는데(2026-09-18), 흉내는 타이밍이 어긋나
	//   미끄러져 보였다. 실제로 적용된 이동을 재는 쪽이 어긋날 자리가 없다(user 지적).

	HangGrabStartLocation = OwnerCharacter->GetActorLocation();
	HangGrabStartTime = GetWorld()->GetTimeSeconds();
	HangIKAlpha = 0.0f;
	HangIKTargets = FVBHangIKTargets();
	ActiveShimmyMontage = nullptr;
	ShimmyMontagePosition = 0.0f;
	bHandReachingL = false;
	bHandReachingR = false;
	ShimmySwayTowardWall = 0.0f;
	ShimmySwayRise = 0.0f;
	HangDepthBase = ClimbAnimSet ? ClimbAnimSet->HangHandDepth : 33.0f;   // 잡는 순간은 대기 자세다
	HangBelowBase = ClimbAnimSet ? ClimbAnimSet->HangRootBelowLedge : 130.0f;
	HangDepthCurrent = HangDepthBase;
	HangBelowCurrent = HangBelowBase;
	bHangPlacedOnce = false; // 첫 틱은 비교할 직전 위치가 없다 - 없는 것을 0 으로 읽지 않는다
	SetComponentTickEnabled(true);
	UpdateHang(0.0f); // 첫 프레임부터 자리에 붙인다(블렌드 0 이면 즉시)
}

void UVBTraversalComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bIsHanging)
	{
		UpdateHang(DeltaTime);
	}
	else if (bIsClimbing)
	{
		UpdateClimb(DeltaTime);
	}
	if (bIsOnLadder)
	{
		UpdateLadder(DeltaTime);
	}
}

void UVBTraversalComponent::UpdateHang(float DeltaTime)
{
	USplineComponent* Spline = HangLedge.Get();
	if (!OwnerCharacter.IsValid() || !Spline || !GetWorld())
	{
		// 렛지 액터가 사라졌다(스트리밍 아웃 등) - 매달릴 데가 없으니 놓는다.
		EndHangByDrop();
		return;
	}

	// 거리 갱신은 서버와 소유 클라만(입력을 아는 쪽). 시뮬 프록시는 복제된 위치를 받는다.
	const bool bDrives = OwnerCharacter->HasAuthority() || OwnerCharacter->IsLocallyControlled();
	const FTransform LedgeXform = Spline->GetTransformAtDistanceAlongSpline(HangDistance, ESplineCoordinateSpace::World);
	const FVector LedgeNormal = LedgeXform.GetRotation().GetUpVector();
	if (bDrives && ClimbAnimSet)
	{
		// 렛지 끝에 붙은 채 그 바깥으로 미는 중인가. 그러면 옆걸음을 «걸지 않는다».
		//   종전에는 걸고 나서 막힘을 보고 껐는데, 다음 틱에 다시 걸어 11ms 마다 시작-정지를 반복했다
		//   (2026-09-18 로그: 끝에서 초당 40여 번). 못 갈 것을 아는 자리는 시작 전에 걸러야 한다.
		const FVector TangentAtNow = Spline->GetDirectionAtDistanceAlongSpline(HangDistance, ESplineCoordinateSpace::World);
		const FVector RightAtNow = FRotationMatrix::MakeFromX(-LedgeNormal).GetUnitAxis(EAxis::Y);
		const float SignAtNow = (FVector::DotProduct(TangentAtNow, RightAtNow) >= 0.0f) ? 1.0f : -1.0f;
		const float PushDir = ((HangMoveInput.X >= 0.0f) ? 1.0f : -1.0f) * SignAtNow;
		const bool bAtEndPushing = !FMath::IsNearlyZero(HangMoveInput.X) &&
			((PushDir > 0.0f && HangDistance >= HangMaxDistance - 0.5f) ||
			 (PushDir < 0.0f && HangDistance <= HangMinDistance + 0.5f));

		// 몽타주 걸기/끊기/루프만 맡긴다. 몽타주가 도는 동안 반환값은 0 이고, 이동은 아래에서 «실제로 옮겨진 양» 으로 잰다.
		const float Advance = TickShimmyMontage(DeltaTime, bAtEndPushing ? 0.0f : HangMoveInput.X);

		// 루트모션이 이 틱에 캡슐을 얼마나 옮겼나. 내가 직전에 놓은 자리와 지금 자리의 차이가 그것이다.
		//   렛지 접선 성분만 취한다 - 벽 쪽·위아래 성분은 아래 재배치가 어차피 렛지 위로 되돌린다.
		float RootMotionAlong = 0.0f;
		if (bHangPlacedOnce)
		{
			const FVector Moved = OwnerCharacter->GetActorLocation() - HangLastPlacedLocation;
			const FVector TangentDir = Spline->GetDirectionAtDistanceAlongSpline(HangDistance, ESplineCoordinateSpace::World);
			RootMotionAlong = FVector::DotProduct(Moved, TangentDir);
		}

		// 입력 X(+오른쪽) 를 스플라인 진행 방향 부호로 바꾼다 - 스플라인이 어느 쪽으로 그어졌든 오른쪽 입력은 오른쪽으로.
		const FVector Tangent = Spline->GetDirectionAtDistanceAlongSpline(HangDistance, ESplineCoordinateSpace::World);
		const FVector Right = FRotationMatrix::MakeFromX(-LedgeNormal).GetUnitAxis(EAxis::Y);
		const float Sign = FVector::DotProduct(Tangent, Right) >= 0.0f ? 1.0f : -1.0f;
		const float Direction = (HangMoveInput.X >= 0.0f) ? 1.0f : -1.0f;
		// 몽타주가 돌면 RootMotionAlong 이 본체이고 Advance 는 0, 몽타주가 없으면 반대다.
		const float Step = RootMotionAlong + Advance * Direction * Sign;
		const float NewDistance = FMath::Clamp(HangDistance + Step, HangMinDistance, HangMaxDistance);
		// 렛지 끝에 닿았고 그 바깥으로 계속 미는가. 끝에서 몽타주를 놔두면 제자리 걸음이 된다.
		// 진행량 0 을 «막혔다» 로 읽으면 안 된다 - 손을 심은 구간에서는 원래 0 이고,
		//   몽타주를 거는 첫 프레임도 0 이라 방금 건 것을 바로 꺼서 옆걸음이 통째로 죽는다(2026-09-18 실측 회귀).
		const bool bBlocked =
			(Step > 0.0f && FMath::IsNearlyEqual(NewDistance, HangMaxDistance)) ||
			(Step < 0.0f && FMath::IsNearlyEqual(NewDistance, HangMinDistance));
		HangDistance = NewDistance;
		// 블렌드스페이스를 모는 축. 끝에 붙어 미는 중이면 0 으로 내려 «제자리 옆걸음» 을 지운다.
		// 왜 bBlocked 만으로는 모자랐나(2026-09-18 user 「끝에 다다랐을 때는 애니메이션 안되어야 하지 않아?」):
		//   bBlocked 는 «이번 틱에 실제로 얼마나 갔나» 로 판정하는데, 끝에서는 위에서 몽타주를 이미 껐으므로
		//   진행량이 0 이라 bBlocked 가 거짓으로 떨어진다. 그래서 몽타주는 꺼지는데 축은 살아 남아
		//   블렌드스페이스가 그 자리에서 계속 옆걸음을 그렸다. 절반만 멈춘 것이다.
		//   bAtEndPushing 은 진행량이 아니라 «지금 끝이고 그 바깥으로 미는가» 를 보므로 이 자리에서 참이다.
		// bBlocked 는 그대로 둔다 - 몽타주가 없어 일정 속도로 가는 폴백 경로에서는 그쪽이 판정한다.
		const float AxisTarget = (bAtEndPushing || bBlocked) ? 0.0f : FMath::Clamp(HangMoveInput.X, -1.0f, 1.0f);
		HangMoveAxis = FMath::FInterpTo(HangMoveAxis, AxisTarget, DeltaTime, ClimbAnimSet->HangMoveAxisInterpSpeed);
	}

	// 몸통을 렛지에서 얼마나 떼고 얼마나 내릴지. 옆걸음 중에는 여기에 클립 root 의 흔들림을 더한다.
	// 왜 더하나(2026-09-18 실측): 클립 안에서 손의 절대 위치는 거의 붙어 있고 흔들리는 것은 root 다
	//   (왼쪽 클립 손 깊이 폭 4.5cm 대 root 폭 7.1cm, 손 높이 폭 6.1cm 대 root 폭 10.5cm).
	//   그런데 이 배치는 매 틱 깊이·높이를 고정값으로 되돌려 root 의 흔들림을 통째로 지운다.
	//   지워진 흔들림은 사라지지 않고 손으로 옮겨간다 - user 가 본 «이동 중 손이 들어간다/뜬다» 가 그것이다.
	//   그래서 고정값을 최악값으로 키우는 대신(종전 47/146) 흔들림 자체를 되살린다.
	// 부호 검산: 왼쪽 클립 위상 0.5 에서 root 는 시작보다 Y 로 -7(벽에서 멀어짐), Z 로 +4(위) 다.
	//   깊이 40.1 - (-7) = 47.1 이고, 이 47.1 이 손으로 맞췄던 옛 고정값 47 과 맞는다.
	//   높이 142.0 - 4 = 138.0 - root 가 올라간 만큼 덜 내린다.
	if (ClimbAnimSet)
	{
		const float TargetDepth = ActiveShimmyMontage ? ClimbAnimSet->HangHandDepthShimmy : ClimbAnimSet->HangHandDepth;
		const float TargetBelow = ActiveShimmyMontage ? ClimbAnimSet->HangRootBelowLedgeShimmy : ClimbAnimSet->HangRootBelowLedge;
		HangDepthBase = FMath::FInterpConstantTo(HangDepthBase, TargetDepth, DeltaTime, ClimbAnimSet->HangDepthInterpSpeed);
		HangBelowBase = FMath::FInterpConstantTo(HangBelowBase, TargetBelow, DeltaTime, ClimbAnimSet->HangDepthInterpSpeed);
		HangDepthCurrent = HangDepthBase - ShimmySwayTowardWall;
		HangBelowCurrent = HangBelowBase - ShimmySwayRise;
	}

	// 캡슐 트랜스폼은 매 틱 스플라인에서 파생한다. 잡는 순간에는 짧게 미끄러져 들어간다.
	const FTransform Target = ComputeHangCapsuleTransform(Spline->GetLocationAtDistanceAlongSpline(HangDistance, ESplineCoordinateSpace::World), LedgeNormal);
	FVector Location = Target.GetLocation();
	const float BlendTime = ClimbAnimSet ? ClimbAnimSet->HangGrabBlendTime : 0.0f;
	if (BlendTime > 0.0f)
	{
		const float Alpha = FMath::Clamp(static_cast<float>((GetWorld()->GetTimeSeconds() - HangGrabStartTime) / BlendTime), 0.0f, 1.0f);
		Location = FMath::Lerp(HangGrabStartLocation, Location, Alpha);
	}
	OwnerCharacter->SetActorLocationAndRotation(Location, Target.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
	HangLastPlacedLocation = OwnerCharacter->GetActorLocation();
	bHangPlacedOnce = true;

	// 1초에 한 줄. 옆걸음이 «안 간다» 고 보일 때 어느 쪽이 0 인지, 그리고 몸의 어느 부위가 벽 안쪽인지 답한다.
	//   화면으로는 «박혔다» 까지만 보이고 어느 축으로 얼마나인지 못 본다 - 그래서 부위별 부호 거리를 찍는다.
	//   양수 = 벽 바깥(정상), 음수 = 벽 안쪽(파고듦).
	if (GetWorld()->GetTimeSeconds() >= HangNextTraceTime)
	{
		HangNextTraceTime = GetWorld()->GetTimeSeconds() + 1.0;
		const USkeletalMeshComponent* M = OwnerCharacter->GetMesh();
		FString Parts;
		if (M)
		{
			// 벽 평면 = 렛지 점 + 법선. 각 뼈의 월드 위치를 그 평면에 대고 부호 거리를 잰다.
			const FVector Plane = Spline->GetLocationAtDistanceAlongSpline(HangDistance, ESplineCoordinateSpace::World);
			auto Signed = [&](const FName Bone) -> float
			{
				return static_cast<float>(FVector::DotProduct(M->GetSocketLocation(Bone) - Plane, LedgeNormal));
			};
			Parts = FString::Printf(TEXT("손L%+.0f 손R%+.0f 골반%+.0f 무릎L%+.0f 발L%+.0f 발끝L%+.0f 발끝R%+.0f | 손높이L%+.0f"),
				Signed(TEXT("hand_l")), Signed(TEXT("hand_r")), Signed(TEXT("pelvis")),
				Signed(TEXT("calf_l")), Signed(TEXT("foot_l")), Signed(TEXT("ball_l")), Signed(TEXT("ball_r")),
				static_cast<float>(M->GetSocketLocation(TEXT("hand_l")).Z - Plane.Z));
		}
		// 흔들림을 함께 찍는다. 「여전히 손이 들어간다」가 «흔들림이 0 이라 안 걸렸다» 인지
		//   «걸렸는데도 모자란다» 인지 이 두 숫자가 가른다.
		// 축(HangMoveAxis)도 찍는다. 이것이 BS_VB_LedgeHang 의 X 핀에 직접 물려 있어서(ABP 리드백 확인)
		//   「끝인데 아직 움직인다」가 «축이 안 내려갔다» 인지 «축은 0 인데 다른 것이 그린다» 인지 이 숫자가 가른다.
		//   몽타주 이름과 이 축이 옆걸음을 그리는 두 기제라, 둘 다 안 보면 절반만 멈춘 것을 못 읽는다.
		VB_LOG(Log, "Hang 상태: dist=%.0f (1초간 %+.0f) 입력X=%.2f 축=%.2f 몽타주=%s 깊이=%.1f 높이=%.1f 흔들림(앞뒤%+.1f 상하%+.1f) 스플라인=%s",
			HangDistance, HangDistance - HangDistanceAtTrace, HangMoveInput.X, HangMoveAxis,
			ActiveShimmyMontage ? *ActiveShimmyMontage->GetName() : TEXT("없음"), HangDepthCurrent, HangBelowCurrent,
			ShimmySwayTowardWall, ShimmySwayRise, *GetNameSafe(Spline));
		VB_LOG(Log, "Hang 접지: %s   (양수=벽 바깥, 음수=벽 안쪽. 손높이는 렛지 윗면 기준)", *Parts);
		HangDistanceAtTrace = HangDistance;
	}
	if (UCharacterMovementComponent* CMC = OwnerCharacter->GetCharacterMovement())
	{
		// 위치를 손으로 옮기므로 CMC 속도는 항상 0 이어야 한다 - 남으면 모션매칭 궤적이 «이동 중» 으로 읽는다.
		CMC->Velocity = FVector::ZeroVector;
	}

	// 캡슐이 제자리에 선 뒤에 IK 목표를 뽑는다 - 순서가 계약이다.
	//   반대로 하면 목표가 한 프레임 낡은 캡슐 기준이 되어 옆걸음 중 손이 뒤로 끌린다.
	UpdateHangIKTargets(Spline->GetLocationAtDistanceAlongSpline(HangDistance, ESplineCoordinateSpace::World), LedgeNormal, DeltaTime);
}

void UVBTraversalComponent::UpdateHangIKTargets(const FVector& LedgePoint, const FVector& LedgeNormal, float DeltaTime)
{
	USplineComponent* Spline = HangLedge.Get();
	const USkeletalMeshComponent* Mesh = OwnerCharacter.IsValid() ? OwnerCharacter->GetMesh() : nullptr;
	if (!Spline || !Mesh || !ClimbAnimSet)
	{
		HangIKTargets = FVBHangIKTargets();
		return;
	}

	// 차오르는 세기. 잡는 순간 1 로 튀면 손이 순간이동한다.
	const float BlendIn = ClimbAnimSet->HangIKBlendInTime;
	HangIKAlpha = (BlendIn > 0.0f) ? FMath::Clamp(HangIKAlpha + DeltaTime / BlendIn, 0.0f, 1.0f) : 1.0f;

	// 손: 잡은 자리를 붙든다. 매 틱 몸 기준으로 새로 계산하면 손이 몸을 따라 흘러 미끄러짐이 된다.
	//   위로 살짝 띄우는 것은 손목이 모서리에 딱 붙으면 손가락이 블록 안으로 들어가기 때문이다.
	// 손: 클립이 그린 자리에서 «벽 바깥으로» 상수만큼 밀어낸다(손뼈 자기 공간 오프셋).
	//   월드 한 점을 주면 IK 가 클립과 싸워 손이 떨린다 - 경위는 UVBClimbAnimSet::HangHandPushOut 주석.
	//   그래서 여기 «목표» 는 월드 좌표가 아니라 오프셋 벡터다. ABP 의 TwoBoneIK 가 BCS_BoneSpace 로 읽는다.
	const FVector PushLocal = ClimbAnimSet->HangHandPushDirLocal.GetSafeNormal() * ClimbAnimSet->HangHandPushOut;

	// 지금 각 손이 뻗는 중인가. 옆걸음 몽타주의 위상으로 가른다(실측 구간, UVBClimbAnimSet).
	//   몽타주가 없으면(대기) 두 손 다 심은 것으로 본다.
	float ReachAlphaL = 0.0f;
	float ReachAlphaR = 0.0f;
	if (ActiveShimmyMontage)
	{
		const USkeletalMeshComponent* M = OwnerCharacter->GetMesh();
		const UAnimInstance* A = M ? M->GetAnimInstance() : nullptr;
		const float Len = ActiveShimmyMontage->GetPlayLength();
		const float Phase = (A && Len > 0.0f) ? FMath::Frac(FMath::Max(0.0f, A->Montage_GetPosition(ActiveShimmyMontage)) / Len) : 0.0f;
		const float Blend = FMath::Max(ClimbAnimSet->HandReachBlendPhase, KINDA_SMALL_NUMBER);
		// 구간 안이면 1, 밖이면 0, 가장자리에서 Blend 만큼 완만히. 한 프레임에 켜졌다 꺼지면 손이 튄다.
		auto ReachAlpha = [Blend](float Phase01, float Start, float End) -> float
		{
			const float In  = FMath::Clamp((Phase01 - (Start - Blend)) / Blend, 0.0f, 1.0f);
			const float Out = FMath::Clamp(((End + Blend) - Phase01) / Blend, 0.0f, 1.0f);
			return FMath::Min(In, Out);
		};
		const float LeadAlpha  = ReachAlpha(Phase, ClimbAnimSet->HandReachLeadStart,  ClimbAnimSet->HandReachLeadEnd);
		const float TrailAlpha = ReachAlpha(Phase, ClimbAnimSet->HandReachTrailStart, ClimbAnimSet->HandReachTrailEnd);
		// 왼쪽 몽타주는 왼손이 앞서고, 오른쪽 몽타주는 오른손이 앞선다(실측 대칭).
		const bool bLeftMontage = (ActiveShimmyMontage == ClimbAnimSet->ShimmyMontageL);
		ReachAlphaL = bLeftMontage ? LeadAlpha : TrailAlpha;
		ReachAlphaR = bLeftMontage ? TrailAlpha : LeadAlpha;
	}

	bHandReachingL = ReachAlphaL > 0.5f;
	bHandReachingR = ReachAlphaR > 0.5f;

	// 두 손 모두 같은 오프셋. 뻗는 손은 허공이라 안 밀어도 되므로 세기만 낮춘다.
	HangIKTargets.HandL = PushLocal;
	HangIKTargets.HandR = PushLocal;
	const float HandBase = HangIKAlpha * ClimbAnimSet->HangHandIKWeight;
	HangIKTargets.HandWeightL = HandBase * (1.0f - ReachAlphaL);
	HangIKTargets.HandWeightR = HandBase * (1.0f - ReachAlphaR);

	// 발: 지금 애니가 둔 발 위치에서 벽 쪽으로 쏴 표면을 찾는다. 평평한 벽이면 배치만으로 이미 닿아 있어
	//   보정이 거의 0 이고, 기울거나 울퉁불퉁한 면에서만 실제로 일한다(UpRoom 절차적 등반 devlog 와 같은 기법).
	// 못 찾으면 가중치 0 - 애니 그대로 둔다. 못 재면 안 건드린다.
	const float Probe = ClimbAnimSet->HangFootProbeDistance;
	const float FootWeight = ClimbAnimSet->HangFootIKWeight * HangIKAlpha;
	auto TraceFoot = [&](const FName BoneName, FVector& OutTarget, float& OutWeight)
	{
		OutWeight = 0.0f;
		if (!GetWorld() || Mesh->GetBoneIndex(BoneName) == INDEX_NONE)
		{
			return;
		}
		const FVector FootNow = Mesh->GetSocketLocation(BoneName);
		const FVector Start = FootNow + LedgeNormal * Probe;   // 벽 바깥에서 시작
		const FVector End   = FootNow - LedgeNormal * Probe;   // 벽 안쪽으로 통과
		FCollisionQueryParams Params(SCENE_QUERY_STAT(VBHangFootProbe), false, OwnerCharacter.Get());
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, GetConfig()->TraceChannel.GetValue(), Params))
		{
			// 발목을 표면에서 띄운다 - 딱 붙이면 앞으로 뻗은 발가락이 벽을 파고든다.
			const FVector WorldTarget = Hit.ImpactPoint + LedgeNormal * ClimbAnimSet->HangFootWallClearance;
			// 노드가 받는 것은 «그 뼈 자기 공간의 오프셋» 이다. 월드 위치가 아니다.
			//   2026-09-19 에 네 TwoBoneIK 를 전부 BCS_BoneSpace 로 통일하면서 여기도 같이 바꿨다 -
			//   종전에는 발 둘만 BCS_WorldSpace 였고 여기가 월드 위치를 넣고 있었다.
			//   왜 통일했나: 월드 이펙터는 말단을 목표에 «놓아» 클립을 덮는다. 그러면 다음 프레임에 읽는
			//   소켓이 곧 내 목표가 되어 면 안쪽 방향이 얼어붙는다(등반에서 실측). 뼈 공간은 «더한다».
			//   이 함수는 지금 세기 0 이라 안 돈다(HangFootIKWeight=0). 그래도 같이 고친 이유는,
			//   안 고치면 나중에 켜는 날 월드 위치가 뼈 공간으로 해석돼 발이 엉뚱한 데로 간다 -
			//   그런 지뢰는 켠 사람이 «몽타주 탓» 으로 오진한다.
			OutTarget = Mesh->GetSocketTransform(BoneName).InverseTransformVector(WorldTarget - FootNow);
			OutWeight = FootWeight;
		}
	};
	TraceFoot(TEXT("foot_l"), HangIKTargets.FootL, HangIKTargets.FootLWeight);
	TraceFoot(TEXT("foot_r"), HangIKTargets.FootR, HangIKTargets.FootRWeight);
}

void UVBTraversalComponent::SetHangMoveInput(const FVector2D& Input)
{
	if (!bIsHanging)
	{
		return;
	}
	HangMoveInput = Input;
	// 소유 클라만 축을 보낸다. 형제인 사다리·등반은 IsLocallyControlled 를 같이 보는데
	//   여기만 빠져 있었다(2026-09-23 /verify). 지금은 호출부가 로컬 폰뿐이라 무증상이지만,
	//   시뮬레이트 프록시에서 이 경로가 열리면 남의 입력으로 서버 RPC 를 쏜다.
	//   셋 중 하나만 다른 가드는 의도가 아니라 빠뜨림으로 읽는 것이 맞다.
	if (OwnerCharacter.IsValid() && !OwnerCharacter->HasAuthority() && OwnerCharacter->IsLocallyControlled())
	{
		ServerSetHangMoveInputX(Input.X);
	}
	// 앞 = 올라서기, 뒤 = 놓기. 잡을 때 쥐고 있던 축은 중립을 한 번 지나야 살고, 같은 홀드로 한 번만 보낸다.
	if (FMath::Abs(Input.Y) <= 0.5f)
	{
		bHangExitInputArmed = true;
	}
	if (bHangExitInputArmed && !bHangExitRequested)
	{
		if (Input.Y > 0.5f)
		{
			RequestHangClimbUp();
		}
		else if (Input.Y < -0.5f)
		{
			RequestHangDrop();
		}
	}
}

bool UVBTraversalComponent::ServerSetHangMoveInputX_Validate(float InputX)
{
	return FMath::IsFinite(InputX);
}

void UVBTraversalComponent::ServerSetHangMoveInputX_Implementation(float InputX)
{
	HangMoveInput.X = FMath::Clamp(InputX, -1.0f, 1.0f);
}

void UVBTraversalComponent::RequestHangClimbUp()
{
	if (!bIsHanging || bHangExitRequested)
	{
		return;
	}
	bHangExitRequested = true;
	ServerHangClimbUp();
}

void UVBTraversalComponent::RequestHangDrop()
{
	if (!bIsHanging || bHangExitRequested)
	{
		return;
	}
	bHangExitRequested = true;
	ServerHangDrop();
}

bool UVBTraversalComponent::ServerHangClimbUp_Validate() { return true; }
void UVBTraversalComponent::ServerHangClimbUp_Implementation()
{
	if (bIsHanging)
	{
		MulticastHangClimbUp();
	}
}
void UVBTraversalComponent::MulticastHangClimbUp_Implementation()
{
	StartHangClimbUp();
}

bool UVBTraversalComponent::ServerHangDrop_Validate() { return true; }
void UVBTraversalComponent::ServerHangDrop_Implementation()
{
	if (bIsHanging)
	{
		MulticastHangDrop();
	}
}
void UVBTraversalComponent::MulticastHangDrop_Implementation()
{
	EndHangByDrop();
}

void UVBTraversalComponent::StartHangClimbUp()
{
	USplineComponent* Spline = HangLedge.Get();
	if (!bIsHanging || !OwnerCharacter.IsValid() || !Spline || !ClimbAnimSet || !ClimbAnimSet->HangClimbUpMontage)
	{
		EndHangByDrop(); // 올라설 재료가 없으면 놓는다 - 매달린 채 굳지 않게
		return;
	}
	// 매달림 상태는 끝나고 트래버설(몽타주) 상태로 넘어간다. bDoingTraversalAction 은 복구까지 유지.
	bIsHanging = false;
	HangMoveAxis = 0.0f;
	SetComponentTickEnabled(false);
	RecoverMovementMode = MOVE_Walking;
	// 올라서기는 루트모션이 몸을 렛지 위로 옮기는 동작이다 - 매달림 동안 껐던 것을 되돌린다.
	OwnerCharacter->SetAnimRootMotionTranslationScale(1.0f);

	// 워프 타겟: 지금 매달린 지점의 렛지 안쪽 stand 위치(UpdateWarpTargets 의 Hang 분기).
	const FTransform LedgeXform = Spline->GetTransformAtDistanceAlongSpline(HangDistance, ESplineCoordinateSpace::World);
	FVBTraversalCheckResult R;
	R.ActionType = EVBTraversalActionType::Hang;
	R.bHasFrontLedge = true;
	R.FrontLedgeLocation = LedgeXform.GetLocation();
	R.FrontLedgeNormal = LedgeXform.GetRotation().GetUpVector();
	UpdateWarpTargets(R, ClimbAnimSet->HangClimbUpMontage);

	VB_LOG(Log, "Traversal Hang: 올라서기 montage=%s dist=%.0f", *ClimbAnimSet->HangClimbUpMontage->GetName(), HangDistance);
	PlayTraversalMontage(ClimbAnimSet->HangClimbUpMontage, 0.0f, 1.0f);
}

void UVBTraversalComponent::EndHangByDrop()
{
	if (!bIsHanging)
	{
		return;
	}
	VB_LOG(Log, "Traversal Hang: 놓기 dist=%.0f", HangDistance);
	if (GetWorld())
	{
		HangRegrabBlockUntil = GetWorld()->GetTimeSeconds() + GetConfig()->HangRegrabDelay;
	}
	RecoverMovementMode = MOVE_Falling;
	RecoverFromTraversal(); // bIsHanging 등 흔적은 복구가 지운다
}

UAnimMontage* UVBTraversalComponent::DriveLoopingMontage(UAnimInstance* Anim, UAnimMontage* Wanted,
	UAnimMontage* Active, float StopBlend, bool& bOutStarted, float StartTime)
{
	bOutStarted = false;
	if (!Anim)
	{
		return nullptr;
	}
	if (!Wanted)
	{
		if (Active)
		{
			Anim->Montage_Stop(StopBlend, Active);
		}
		return nullptr;
	}
	// 방향이 바뀌었거나 한 주기가 끝났으면 다시 건다. 누르고 있는 동안 계속 돈다.
	if (Active != Wanted || !Anim->Montage_IsPlaying(Active))
	{
		// 마지막 인자 = 「같은 그룹의 몽타주를 끊어라」. 여기를 켜는 것이 이 함수의 요점이다.
		// 왜(2026-09-19 user 「W눌렀다가 떼었을 때 튄다」): 종전에는 이것을 false 로 넘기고
		//   옛 몽타주를 손으로 0.1 초에 껐다. 그런데 몽타주의 블렌드인은 0.25 초다(실측 - 사다리·등반 셋 다).
		//   그래서 0.1 초 뒤부터 0.25 초까지 슬롯의 총 가중치가 1 에 못 미치고, 모자란 몫만큼
		//   슬롯 아래에 깔린 것이 비쳐 나온다. 0.15 초 동안 트래버설 자세가 아닌 것이 섞이는 것이라
		//   방향을 바꾸거나 손을 뗄 때마다 보인다.
		// true 로 넘기면 엔진이 `StopAllMontagesByGroupName(그룹, 새 몽타주의 블렌드인 설정)` 을 불러
		//   나가는 쪽과 들어오는 쪽을 같은 시간으로 맞물린다
		//   (`AnimInstance.cpp::UAnimInstance::Montage_PlayInternal`). 두 값을 손으로 맞추면
		//   블렌드인을 고치는 날 다시 어긋나므로, 맞추는 일 자체를 엔진에 맡긴다.
		// 같은 그룹만 끊는다 - 다른 슬롯의 몽타주는 건드리지 않는다(위 함수가 그룹 이름으로 거른다).
		Anim->Montage_Play(Wanted, 1.0f, EMontagePlayReturnType::MontageLength, StartTime, true);
		// 섹션이 자기 자신을 가리키게 해서 스스로 돈다. 전문은 헤더 선언부 주석에 있다.
		if (Wanted->GetNumSections() > 0)
		{
			const FName LoopSection = Wanted->GetSectionName(0);
			Anim->Montage_SetNextSection(LoopSection, LoopSection, Wanted);
		}
		bOutStarted = true;
	}
	return Wanted;
}

float UVBTraversalComponent::TickShimmyMontage(float DeltaTime, float InputX)
{
	USkeletalMeshComponent* Mesh = OwnerCharacter.IsValid() ? OwnerCharacter->GetMesh() : nullptr;
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	UAnimMontage* Wanted = nullptr;
	if (ClimbAnimSet && FMath::Abs(InputX) > 0.2f)
	{
		Wanted = (InputX >= 0.0f) ? ClimbAnimSet->ShimmyMontageR : ClimbAnimSet->ShimmyMontageL;
	}

	// 몽타주 경로가 없으면 옛 경로(일정 속도)로 떨어진다. 없는 것은 깨진 것이 아니다.
	if (!Anim || !Wanted)
	{
		if (ActiveShimmyMontage && Anim)
		{
			Anim->Montage_Stop(0.15f, ActiveShimmyMontage);
			ActiveShimmyMontage = nullptr;
		}
		ShimmySwayTowardWall = 0.0f;
		ShimmySwayRise = 0.0f;
		// 조용히 옛 경로로 떨어지는 것을 밝힌다 - 조용한 것은 「문제 없음」이 아니다(규칙 27).
		if (ClimbAnimSet && FMath::Abs(InputX) > 0.2f && !bShimmyFallbackWarned)
		{
			bShimmyFallbackWarned = true;
			VB_LOG(Warning, "Hang 옆걸음: 몽타주 미할당 - 일정 속도(%.0f cm/s) 폴백. 심은 손이 미끄러진다",
				ClimbAnimSet->HangShimmySpeed);
		}
		return ClimbAnimSet ? ClimbAnimSet->HangShimmySpeed * DeltaTime * FMath::Abs(FMath::Clamp(InputX, -1.0f, 1.0f)) : 0.0f;
	}

	// 걸기·자기섹션 루프·끊기는 DriveLoopingMontage 가 맡는다(옆걸음·등반·사다리 공통).
	//   왜 «끝나면 다시 걸기» 가 아닌가(2026-09-18 user 「계속 반복해서 재생한다」): 몽타주는 끝보다
	//   BlendOut(0.25초) 만큼 일찍 «재생 아님» 이 된다. 그 시점에 다시 걸면 매 1.08초마다
	//   한창 진행 중인 자세에서 처음 자세로 튀고, 남은 0.25초 구간의 루트모션(약 19cm)도 버려진다.
	//   이 클립의 시작·끝 자세 차이는 실측 2.0cm(왼쪽)/0.1cm(오른쪽)라 섹션 루프는 끊김이 없다.
	bool bShimmyStarted = false;
	ActiveShimmyMontage = DriveLoopingMontage(Anim, Wanted, ActiveShimmyMontage, 0.15f, bShimmyStarted);
	if (bShimmyStarted)
	{
		ShimmyMontagePosition = 0.0f;
		ShimmySwayTowardWall = 0.0f;
		ShimmySwayRise = 0.0f;
		// 한 주기에 한 줄. 「여전히 미끄러진다」가 «몽타주가 안 걸림» 인지 «걸렸는데 값이 이상함» 인지 이 줄이 가른다.
		VB_LOG(Log, "Hang 옆걸음: %s dist=%.0f 입력=%.2f", *Wanted->GetName(), HangDistance, InputX);
		return 0.0f; // 시작 프레임은 진행 0 - 아직 아무것도 재생 안 됐다
	}

	// 몽타주가 도는 동안 렛지를 따라가는 이동은 루트모션이 맡는다. 여기서 또 값을 내면 두 주인이 같은 이동을 더한다.
	ShimmyMontagePosition = Anim->Montage_GetPosition(ActiveShimmyMontage);

	// 앞뒤·상하 흔들림은 반대로 루트모션이 못 맡는다 - 배치가 매 틱 그 두 축을 고정값으로 되돌리기 때문이다.
	//   그래서 «위상 0 부터 지금까지» 의 누적 루트모션에서 직접 꺼내 배치에 돌려준다.
	//   증분이 아니라 누적을 쓰는 이유: 증분을 더해 가면 오차가 쌓이고 루프 자리에서 되돌릴 기준이 없다.
	//   누적은 위상이 0 으로 돌아가면 스스로 0 이 된다.
	// 축 규약: 메시 컴포넌트 공간이라 +Y 가 벽 쪽이다(실측 - 손이 root 보다 Y 로 33~43cm 앞에 있다).
	const FTransform Accum = ActiveShimmyMontage->ExtractRootMotionFromTrackRange(0.0f, ShimmyMontagePosition, FAnimExtractContext());
	ShimmySwayTowardWall = static_cast<float>(Accum.GetTranslation().Y);
	ShimmySwayRise = static_cast<float>(Accum.GetTranslation().Z);
	return 0.0f;
}

void UVBTraversalComponent::BeginSurfaceAttachment(UPrimitiveComponent* Obstacle)
{
	if (!OwnerCharacter.IsValid())
	{
		return;
	}
	// 파쿠르 중 무기 임시수납(맨틀류와 같은 계약). 서버 권한·무장 가드는 안에서.
	if (UVBWeaponStateComponent* WSC = OwnerCharacter->GetWeaponStateComponent())
	{
		WSC->TempSheatheForParkour();
	}
	// 위치의 주인은 이 컴포넌트다: 중력·가속이 없는 Flying 에 속도 0. 캡슐 회전 플래그는 ApplyRotationMode 가 끈다.
	if (UCharacterMovementComponent* CMC = OwnerCharacter->GetCharacterMovement())
	{
		CMC->SetMovementMode(MOVE_Flying);
		CMC->StopMovementImmediately();
	}
	if (Obstacle)
	{
		if (UCapsuleComponent* Capsule = OwnerCharacter->GetCapsuleComponent())
		{
			Capsule->IgnoreComponentWhenMoving(Obstacle, true);
			IgnoredObstacle = Obstacle;
		}
	}
	OwnerCharacter->ApplyRotationMode();
	SetComponentTickEnabled(true);
}

bool UVBTraversalComponent::TraceClimbSurface(const FVector& Around, const FVector& Normal, FHitResult& OutHit) const
{
	const UVBTraversalConfig* Cfg = GetConfig();
	if (!Cfg || !GetWorld() || !OwnerCharacter.IsValid())
	{
		return false;
	}
	// 면 바깥에서 시작해 안쪽으로 쏜다. 면 위의 점에서 시작하면 그 면을 못 맞히는 수가 있다.
	const FVector Start = Around + Normal * Cfg->ClimbProbeOutset;
	const FVector End = Around - Normal * (Cfg->ClimbProbeDepth - Cfg->ClimbProbeOutset);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VBClimbSurface), false, OwnerCharacter.Get());
	if (!GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, Cfg->ClimbTraceChannel.GetValue(), Params))
	{
		return false;
	}
	// 충분히 서 있는 면만. 바닥(법선 Z=1)에 붙어 기어가는 것을 막는다.
	return FMath::Abs(OutHit.ImpactNormal.Z) <= Cfg->ClimbMaxSurfaceTiltZ;
}

// ===== 사다리 (2026-09-19, 설계 .sw/traversal/design.md 14) =====

bool UVBTraversalComponent::TryStartLadder()
{
	const UVBTraversalConfig* Cfg = GetConfig();
	if (bIsHanging || bIsClimbing || bIsOnLadder || !Cfg || !OwnerCharacter.IsValid() || !GetWorld())
	{
		return false;
	}
	if (!ClimbAnimSet || !ClimbAnimSet->LadderIdleMontage)
	{
		// 대기 몽타주가 없으면 사다리는 꺼진 것이다 - 없는 것은 깨진 것이 아니다.
		return false;
	}

	const FVector Origin = OwnerCharacter->GetActorLocation();
	const FVector Forward = OwnerCharacter->GetActorForwardVector();
	const float RadiusSq = Cfg->LadderFindRadius * Cfg->LadderFindRadius;

	AVBLadder* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	for (TActorIterator<AVBLadder> It(GetWorld()); It; ++It)
	{
		AVBLadder* Ladder = *It;
		if (!Ladder)
		{
			continue;
		}
		// 축 위에서 가장 가까운 점으로 거리를 잰다. 액터 원점으로 재면 긴 사다리의 위쪽이 멀어 보인다.
		const FVector Near = Ladder->GetAxisPoint(Ladder->ProjectToHeight(Origin));
		const float DistSq = static_cast<float>(FVector::DistSquared(Near, Origin));
		if (DistSq > RadiusSq || DistSq >= BestDistSq)
		{
			continue;
		}
		// 사다리를 마주 보고 있나. 두 가지를 같이 본다 -
		//   내가 사다리 쪽을 보는가(앞), 그리고 사다리의 «타는 면» 쪽에 있는가(바깥).
		const FVector ToLadder = (Near - Origin).GetSafeNormal();
		const FVector Normal = Ladder->GetFacingNormal();
		if (FVector::DotProduct(Forward, ToLadder) < Cfg->LadderFacingDot)
		{
			continue;
		}
		if (FVector::DotProduct(Origin - Near, Normal) <= 0.0f)
		{
			continue; // 사다리 뒤편이다
		}
		Best = Ladder;
		BestDistSq = DistSq;
	}
	if (!Best)
	{
		return false;
	}

	const float Height = Best->ProjectToHeight(OwnerCharacter->GetMesh()
		? OwnerCharacter->GetMesh()->GetComponentLocation() : Origin);
	VB_LOG(Log, "Ladder 진입: %s 높이=%.0f/%.0f", *GetNameSafe(Best), Height, Best->GetLadderHeight());
	// 등반과 같은 길 - 권한이 있으면 바로 멀티캐스트, 없으면 서버에 부탁한다.
	if (OwnerCharacter->HasAuthority())
	{
		MulticastStartLadder(Best, Height);
	}
	else
	{
		ServerStartLadder(Best, Height);
	}
	return true;
}

void UVBTraversalComponent::StartLadder(AVBLadder* Ladder, float Height)
{
	if (!Ladder || !OwnerCharacter.IsValid() || !GetWorld())
	{
		return;
	}
	// 등반과 같이 «트래버설 중» 으로 표시한다. 이 줄이 없으면 RecoverFromTraversal 이 첫 줄에서 되돌아가
	//   무기 복원·콜리전 복구·이동 모드가 하나도 안 돌아온다(등반에서 겪은 것 그대로).
	bDoingTraversalAction = true;
	bIsOnLadder = true;
	ActiveLadder = Ladder;
	LadderHeightOnAxis = Height;
	ActiveLadderMontage = nullptr;
	LadderMoveInput = 0.0f;
	bLadderPlacedOnce = false; // 첫 틱은 비교할 직전 위치가 없다 - 없는 것을 0 으로 읽지 않는다
	bLadderSettling = false;
	LadderSettleTarget = 0.0f;
	LadderIdleQuietTime = 0.0f;
	LadderResumeMontage = nullptr;
	LadderResumePhase = 0.0f;
	LadderNextClampLogTime = 0.0;
	LadderNextJumpLogTime = 0.0;
	LadderMontagePosition = 0.0f;
	bLadderMontageTracked = false;
	LadderEnterLocation = OwnerCharacter->GetActorLocation();
	LadderEnterRotation = OwnerCharacter->GetActorQuat();
	LadderEnterTime = GetWorld()->GetTimeSeconds();
	RecoverMovementMode = MOVE_Falling;
	BeginSurfaceAttachment(nullptr);
	UpdateLadder(0.0f);
}

bool UVBTraversalComponent::ServerStartLadder_Validate(AVBLadder* Ladder, float Height)
{
	return Ladder != nullptr && FMath::IsFinite(Height);
}

void UVBTraversalComponent::ServerStartLadder_Implementation(AVBLadder* Ladder, float Height)
{
	if (!Ladder)
	{
		return;
	}
	// 서버가 다시 잰다. 클라가 보낸 높이를 그대로 믿지 않는다 - 축 밖의 값이 오면 캡슐이 엉뚱한 데로 간다.
	MulticastStartLadder(Ladder, Ladder->ProjectToHeight(Ladder->GetAxisPoint(Height)));
}

void UVBTraversalComponent::MulticastStartLadder_Implementation(AVBLadder* Ladder, float Height)
{
	StartLadder(Ladder, Height);
}

void UVBTraversalComponent::SetLadderMoveInput(float InputY)
{
	if (!bIsOnLadder)
	{
		return;
	}
	LadderMoveInput = FMath::Clamp(InputY, -1.0f, 1.0f);
	// 서버가 높이를 굴리므로 소유 클라는 축 값을 보낸다. 매 변경마다라 Unreliable.
	if (OwnerCharacter.IsValid() && !OwnerCharacter->HasAuthority() && OwnerCharacter->IsLocallyControlled())
	{
		ServerSetLadderMoveInput(LadderMoveInput);
	}
}

bool UVBTraversalComponent::ServerSetLadderMoveInput_Validate(float InputY)
{
	return FMath::IsFinite(InputY);
}

void UVBTraversalComponent::ServerSetLadderMoveInput_Implementation(float InputY)
{
	LadderMoveInput = FMath::Clamp(InputY, -1.0f, 1.0f);
}

bool UVBTraversalComponent::ServerLadderDrop_Validate() { return true; }
void UVBTraversalComponent::ServerLadderDrop_Implementation() { MulticastLadderDrop(); }
void UVBTraversalComponent::MulticastLadderDrop_Implementation() { EndLadder(); }

void UVBTraversalComponent::RequestLadderDrop()
{
	if (!bIsOnLadder || !OwnerCharacter.IsValid())
	{
		return;
	}
	if (OwnerCharacter->HasAuthority())
	{
		MulticastLadderDrop();
	}
	else
	{
		ServerLadderDrop();
	}
}

void UVBTraversalComponent::EndLadder()
{
	if (!bIsOnLadder)
	{
		return;
	}
	// 뒷정리는 전부 RecoverFromTraversal 이 맡는다 - 복구의 홈은 하나다.
	//   여기서 bIsOnLadder 를 먼저 끄면 그 함수의 사다리 분기가 안 돈다(등반에서 배운 순서).
	RecoverMovementMode = MOVE_Falling;
	RecoverFromTraversal();
}

FTransform UVBTraversalComponent::ComputeLadderCapsuleTransform(float Height) const
{
	// 얼굴은 사다리 쪽(-법선). root 는 축에서 바깥으로 LadderDepth.
	const AVBLadder* Ladder = ActiveLadder.Get();
	if (!Ladder || !ClimbAnimSet)
	{
		return OwnerCharacter.IsValid() ? OwnerCharacter->GetActorTransform() : FTransform::Identity;
	}
	const FVector Normal = Ladder->GetFacingNormal();
	const FRotator Facing = FRotationMatrix::MakeFromX(-Normal).Rotator();
	const FVector RootWorld = Ladder->GetAxisPoint(Height) + Normal * ClimbAnimSet->LadderDepth;
	FVector MeshRel = FVector::ZeroVector;
	if (OwnerCharacter.IsValid() && OwnerCharacter->GetMesh())
	{
		MeshRel = OwnerCharacter->GetMesh()->GetRelativeLocation();
	}
	return FTransform(Facing, RootWorld - Facing.RotateVector(MeshRel));
}

UAnimMontage* UVBTraversalComponent::PickLadderIdle() const
{
	// 오르내리기는 한 주기 안에서 좌우가 뒤집힌다(실측: 발 좌우차가 1.2초마다 +38 <-> -38).
	//   대기가 한 벌뿐이면 그 두 위상 중 한쪽하고만 맞아서, 나머지 절반에서 멈추면
	//   손발 넷이 한꺼번에 40cm 씩 교차한다. 그래서 반대쪽 벌을 두고 «지금 자세» 로 고른다.
	// 왜 위상이 아니라 발 높이로 고르나: 위상 구간표는 클립마다 다시 재야 하고 블렌드 중에는
	//   답이 없다. 발 높이는 어느 클립이든, 블렌드 중이든 같은 질문이고 지금 화면에 있는 사실이다.
	// 기준(실측 2026-09-19): VB_Ladder_Idle 은 오른발이 위다(발볼 L 42.7 / R 80.5).
	UAnimMontage* const Base = ClimbAnimSet ? ClimbAnimSet->LadderIdleMontage : nullptr;
	UAnimMontage* const Mirror = ClimbAnimSet ? ClimbAnimSet->LadderIdleMirrorMontage : nullptr;
	if (!Mirror)
	{
		return Base; // 반대쪽 벌이 없다 = 한 벌로 간다. 없는 것은 깨진 것이 아니다(BeginPlay 가 한 줄 밝힌다).
	}
	const USkeletalMeshComponent* Mesh = OwnerCharacter.IsValid() ? OwnerCharacter->GetMesh() : nullptr;
	const AVBLadder* Ladder = ActiveLadder.Get();
	if (!Mesh || !Ladder
		|| Mesh->GetBoneIndex(TEXT("ball_l")) == INDEX_NONE
		|| Mesh->GetBoneIndex(TEXT("ball_r")) == INDEX_NONE)
	{
		return Base;
	}
	// 사다리 축에 투영한다. 지금은 축이 수직이라 Z 와 같지만, 기운 사다리가 와도 이 식은 산다.
	const FVector Axis = Ladder->GetActorUpVector();
	const float Left  = static_cast<float>(FVector::DotProduct(Mesh->GetSocketLocation(TEXT("ball_l")), Axis));
	const float Right = static_cast<float>(FVector::DotProduct(Mesh->GetSocketLocation(TEXT("ball_r")), Axis));
	return (Right >= Left) ? Base : Mirror;
}

float UVBTraversalComponent::FindLadderMontageStart(UAnimMontage* Montage, float HeightNow) const
{
	// 발이 가로대 «윗면» 에 떨어지도록 몽타주의 시작 위상을 고른다.
	//
	// 왜 위상을 고르나: 가로대는 정해진 높이에만 있는데 캐릭터의 지금 높이는 아무 값이나 된다.
	//   캐릭터를 옮겨 맞추면 최대 «간격의 절반»(약 20cm)을 순간이동해야 하고 그건 눈에 보인다.
	//   반대로 클립의 시작 위상을 옮기면 몸은 그대로 두고 리듬만 어긋나는데, 그 어긋남은 안 보인다.
	//
	// 식: 몽타주를 t0 에서 시작하면 클립 시각 t 의 캐릭터 높이는 HeightNow + f(t) - f(t0) 다
	//   (f = 클립 시작부터의 누적 루트모션 Z). 디딤은 클립 좌표로 늘 같은 높이 S 에서 일어나므로
	//   그때 발의 높이는 HeightNow - f(t0) + S 이고, 이것이 가로대 윗면과 같아야 한다:
	//     HeightNow - f(t0) + S ≡ 윗면오프셋  (mod 간격)
	//     f(t0) ≡ HeightNow + (S mod 간격) - 윗면오프셋  (mod 간격)
	//   S mod 간격 이 LadderStancePhase 다(실측 1.81 - Up 과 Down 이 같은 격자를 쓴다).
	//
	// 한 주기를 다 돌면 위상이 0 으로 돌아가는데(자기 섹션 루프) 그때 정렬이 안 깨지는 이유는
	//   간격을 «총 상승 / 디딤 횟수» 로 잡았기 때문이다 - 한 주기가 정확히 가로대 여섯 칸이다.
	const AVBLadder* Ladder = ActiveLadder.Get();
	if (!Montage || !Ladder || !ClimbAnimSet)
	{
		return 0.0f;
	}
	const float Spacing = Ladder->GetRungSpacing();
	const float Len = Montage->GetPlayLength();
	if (Spacing <= KINDA_SMALL_NUMBER || Len <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}
	const float Target = HeightNow + ClimbAnimSet->LadderStancePhase - Ladder->GetRungTopOffset();

	// f 를 증분으로 쌓는다. 매번 (0,t) 를 뽑으면 같은 구간을 몇 번씩 다시 훑게 된다.
	// 128 칸인 근거: 한 칸의 루트모션이 총 상승 241.5 / 128 = 1.9cm 라 최악 오차가 그 절반인 0.94cm 다.
	//   IK 보정 상한(6cm) 안이므로 남는 어긋남은 IK 가 먹는다.
	const int32 Steps = 128;
	float Acc = 0.0f;
	float PrevT = 0.0f;
	float BestT = 0.0f;
	float BestErr = MAX_FLT;
	for (int32 i = 0; i <= Steps; ++i)
	{
		const float T = Len * static_cast<float>(i) / static_cast<float>(Steps);
		if (i > 0)
		{
			const FTransform D = Montage->ExtractRootMotionFromTrackRange(PrevT, T, FAnimExtractContext());
			Acc += static_cast<float>(D.GetTranslation().Z);
		}
		PrevT = T;
		// (Acc - Target) 을 간격으로 감아 -간격/2 ~ +간격/2 에 둔다. 0 에 가장 가까운 칸이 답이다.
		float Err = FMath::Fmod(Acc - Target, Spacing);
		if (Err >  Spacing * 0.5f) { Err -= Spacing; }
		if (Err < -Spacing * 0.5f) { Err += Spacing; }
		if (FMath::Abs(Err) < BestErr)
		{
			BestErr = FMath::Abs(Err);
			BestT = T;
		}
	}
	return BestT;
}

void UVBTraversalComponent::UpdateLadder(float DeltaTime)
{
	if (!OwnerCharacter.IsValid() || !GetWorld() || !ClimbAnimSet || !ActiveLadder.IsValid())
	{
		EndLadder();
		return;
	}

	// 이동의 주인은 클립이다. 몽타주를 먼저 걸고, 그것이 낸 루트모션이 축 위의 높이를 올린다.
	//   왜 고정 속도가 아닌가(2026-09-19 user 「Climb 이랑 처음이랑 똑같은 버그」): 코드가 일정 속도로 밀면
	//   애니는 제자리에서 돌고 몸만 끌려가 미끄러진다. 등반에서 같은 증상을 같은 방법으로 고쳤다.
	// 클립이 원래 세로 이동을 골반에 들고 있어서(root Z=0) 그대로는 루트모션이 없었다.
	//   그래서 자산 쪽에서 그 이동을 root 트랙으로 옮겼다 - 자세는 그대로 두고 주인만 바꾼 것이다.
	//   대조: 골반 기준 상대 자세의 최대 오차 0.0001cm, 골반의 컴포넌트 위치 차 0 (2026-09-19 실측).
	//   원본 사본은 /Game/Vowbound/Backup/Ladder/VB_Ladder_{Up,Down}_PreRootMotion.
	// 끝에서는 그 방향 입력을 버린다. 그러면 몽타주도 안 걸려 제자리걸음을 안 한다 -
	//   등반의 벽 끝 처리와 같은 규칙이고, 여기서는 액터가 끝을 알아서 트레이스가 필요 없다.
	// 입력을 아는 쪽만 축 위를 움직인다. 시뮬 프록시는 복제된 위치를 받는다(등반과 같은 규칙).
	const bool bDrives = OwnerCharacter->HasAuthority() || OwnerCharacter->IsLocallyControlled();
	// 꼭대기의 기준은 «손이 아직 가로대에 닿는가» 다. root 가 꼭대기에 닿을 때까지 올리면
	//   손이 사다리보다 189cm 위, 즉 허공을 쥔다(2026-09-19 user 「공중 손잡고있잖아」).
	//   잡는 것이 손이므로 손이 닿는 데까지가 끝이다. 진짜 답은 올라서는 동작인데 그 클립이 없다.
	// 그리고 그 한계를 «발이 놓이는 가로대» 로 내려 맞춘다. 안 그러면 한계(211)와 서는 자리(201.3)가
	//   달라서, 누르면 한계까지 올라가고 떼면 가로대로 내려앉는 것을 반복한다
	//   (2026-09-19 실측 211 <-> 201 왕복, user 「꼭대기 올라가서 꾹 눌렀더니 사다리 애니메이션이 아니던데」).
	//   두 값이 하나가 되면 꼭대기에서 입력이 죽고 대기로 넘어간다 - 제자리 오르기가 없어진다.
	float Top = FMath::Max(ActiveLadder->GetLadderHeight() - ClimbAnimSet->LadderTopHandReach, 0.0f);
	{
		const float TopSpacing = ActiveLadder->GetRungSpacing();
		if (TopSpacing > KINDA_SMALL_NUMBER)
		{
			const float FootAtTop = Top + ClimbAnimSet->LadderIdleFootOffset - ActiveLadder->GetRungTopOffset();
			Top = FMath::Max(Top - FMath::Fmod(FootAtTop, TopSpacing), 0.0f);
		}
	}
	float Want = bDrives ? LadderMoveInput : 0.0f;
	if ((Want > 0.0f && LadderHeightOnAxis >= Top) || (Want < 0.0f && LadderHeightOnAxis <= 0.0f))
	{
		Want = 0.0f;
	}

	// 0.2 는 「그 축을 실제로 밀고 있다」의 문턱이다. 등반의 8방향 판정과 같은 값을 쓴다.
	UAnimMontage* Wanted = nullptr;
	if (Want > 0.2f)
	{
		Wanted = ClimbAnimSet->LadderUpMontage;
	}
	else if (Want < -0.2f)
	{
		Wanted = ClimbAnimSet->LadderDownMontage;
	}
	if (!Wanted)
	{
		// 대기로 간다. 이미 대기 중이면 «그 벌» 을 그대로 둔다 - 매 틱 다시 고르면
		//   두 발 높이가 엇비슷해지는 구간에서 두 벌 사이를 깜빡인다.
		const bool bAlreadyIdle = ActiveLadderMontage != nullptr
			&& (ActiveLadderMontage == ClimbAnimSet->LadderIdleMontage
			 || ActiveLadderMontage == ClimbAnimSet->LadderIdleMirrorMontage);
		Wanted = bAlreadyIdle ? ActiveLadderMontage.Get() : PickLadderIdle();

	}
	if (USkeletalMeshComponent* DriveMesh = OwnerCharacter->GetMesh())
	{
		// 새로 걸리는 것이 «오르내리기» 면 발이 가로대에 떨어지는 위상을 골라 준다.
		//   대기는 루트모션이 없어 위상을 골라도 발 높이가 안 바뀌므로 계산하지 않는다.
		//   갈아 끼울 때만 센다 - 매 틱 세면 128번 추출을 매 프레임 돌리게 된다.
		float StartTime = 0.0f;
		const bool bWantedIsIdle = Wanted == ClimbAnimSet->LadderIdleMontage
			|| Wanted == ClimbAnimSet->LadderIdleMirrorMontage;
		if (Wanted && Wanted != ActiveLadderMontage && !bWantedIsIdle)
		{
			// 직전에 돌던 것과 같은 방향이면 그 위상에서 이어 붙인다. 처음부터 다시 걸면
			//   탭할 때마다 같은 도입부만 반복한다(2026-09-19 user 「계속 처음부터 반복해서 재생」).
			//   이어 붙여도 정렬은 안 깨진다 - 높이와 위상이 함께 나아간 것이라 관계가 유지된다.
			StartTime = (Wanted == LadderResumeMontage)
				? LadderResumePhase
				: FindLadderMontageStart(Wanted, LadderHeightOnAxis);
		}
		bool bStarted = false;
		ActiveLadderMontage = DriveLoopingMontage(DriveMesh->GetAnimInstance(),
			Wanted, ActiveLadderMontage, 0.15f, bStarted, StartTime);
		if (bStarted)
		{
			// 몽타주가 갈렸다. 들고 있던 위상은 «다른 몽타주의 위상» 이라 그대로 빼면 엉뚱한 구간이 나온다.
			bLadderMontageTracked = false;
			LadderMontagePosition = 0.0f;
		}
	}
	// 몽타주가 돌면 이동은 전부 그쪽이 맡는다. 대기 클립은 루트모션이 없어서(실측 enable_root_motion=false)
	//   걸려 있어도 높이가 안 움직이고, 오름에서 대기로 넘어가는 블렌드아웃 구간의 남은 이동은 이 술어라야 세진다.
	//   「대기가 아닌 몽타주를 원했는가」로 물으면 그 구간이 통째로 버려진다 - 등반이 «몽타주가 있는가» 로
	//   묻는 것과 같은 자리다.
	const bool bMontageDrivesHeight = ActiveLadderMontage != nullptr;

	// 이 틱에 클립이 몸을 얼마나 올렸나. 액터가 얼마나 움직였는지로 재지 않고 «몽타주에게 직접 묻는다».
	//   왜(2026-09-19, user 「음...? 엄청 튀는데」): 액터의 이동에는 루트모션 말고도 들어오는 것이 있다 -
	//   캡슐이 벽이나 사다리에 파묻히면 CMC 가 밀어내고(ResolvePenetration), 그 밀려난 양까지
	//   「클립이 올린 양」으로 세어 높이에 적립된다. 배치가 절대 좌표로 덮던 시절에는 그 오차가
	//   다음 프레임에 그냥 지워졌는데, 높이에 적립하기 시작한 순간 누적되는 값이 됐다.
	//   옆걸음이 흔들림을 잴 때 쓰는 방법과 같다 - 소비자가 아니라 기계에게 묻는다.
	// 축 규약: 루트모션은 메시 컴포넌트 공간이라 월드로 돌린 뒤 사다리 축에 투영한다.
	//   지금은 사다리가 수직이고 클립 root 도 수직이라 Z 하나지만, 기운 사다리가 오면 이 식이 살아남는다.
	float Along = 0.0f;
	const USkeletalMeshComponent* Mesh = OwnerCharacter->GetMesh();
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	FVector RootMotionMoved = FVector::ZeroVector;
	if (bMontageDrivesHeight && Anim && Mesh)
	{
		const float Pos = Anim->Montage_GetPosition(ActiveLadderMontage);
		if (!bLadderMontageTracked)
		{
			// 첫 프레임은 비교할 직전 위상이 없다. 없는 것을 0 으로 읽지 않는다.
			bLadderMontageTracked = true;
		}
		else
		{
			FTransform Delta;
			if (Pos >= LadderMontagePosition)
			{
				Delta = ActiveLadderMontage->ExtractRootMotionFromTrackRange(LadderMontagePosition, Pos, FAnimExtractContext());
			}
			else
			{
				// 자기 섹션으로 돌아 위상이 감겼다. 끝까지와 처음부터를 이어 붙인다 -
				//   감긴 것을 「뒤로 갔다」로 읽으면 한 주기마다 클립 전체 길이만큼 되돌아간다.
				const float Len = ActiveLadderMontage->GetPlayLength();
				Delta = ActiveLadderMontage->ExtractRootMotionFromTrackRange(LadderMontagePosition, Len, FAnimExtractContext());
				const FTransform Head = ActiveLadderMontage->ExtractRootMotionFromTrackRange(0.0f, Pos, FAnimExtractContext());
				Delta.AddToTranslation(Head.GetTranslation());
			}
			RootMotionMoved = Mesh->GetComponentQuat().RotateVector(Delta.GetTranslation());
			Along = static_cast<float>(FVector::DotProduct(RootMotionMoved, ActiveLadder->GetActorUpVector()));
		}
		LadderMontagePosition = Pos;
		// 방향 몽타주면 «어디까지 갔나» 를 남긴다. 다시 걸 때 여기서 이어 붙인다.
		if (ActiveLadderMontage != ClimbAnimSet->LadderIdleMontage
			&& ActiveLadderMontage != ClimbAnimSet->LadderIdleMirrorMontage)
		{
			LadderResumeMontage = ActiveLadderMontage;
			LadderResumePhase = Pos;
		}
	}
	else
	{
		bLadderMontageTracked = false;
		LadderMontagePosition = 0.0f;
	}
	if (bDrives && DeltaTime > 0.0f)
	{
		if (!bMontageDrivesHeight)
		{
			if (!FMath::IsNearlyZero(Want))
			{
				// 그 방향 몽타주가 아예 없는 경우다. 일정 속도로 민다 - 그 구간만 미끄러진다.
				Along = ClimbAnimSet->LadderSpeed * DeltaTime * Want;
			}
		}
		else
		{
			// 8 배인 근거: 기준 속도가 33.5 라 순간 최고가 그 8배(268 cm/s)를 넘으면 클립이 낼 수 있는 값이 아니다.
			//   조용히 자르지 않는다 - 잘렸다는 사실 자체가 「어딘가 겹쳐 실린다」의 증거다.
			const float MaxStep = ClimbAnimSet->LadderSpeed * DeltaTime * 8.0f;
			if (FMath::Abs(Along) > MaxStep && MaxStep > KINDA_SMALL_NUMBER)
			{
				if (GetWorld()->GetTimeSeconds() >= LadderNextClampLogTime)
				{
					LadderNextClampLogTime = GetWorld()->GetTimeSeconds() + 1.0;
					VB_LOG(Log, "Ladder: 한 틱 이동 %.1fcm 가 상한 %.1f 을 넘어 잘랐다(몽타주 전환·감김 구간일 수 있다)",
						Along, MaxStep);
				}
				Along = FMath::Sign(Along) * MaxStep;
			}
		}
		LadderHeightOnAxis = FMath::Clamp(LadderHeightOnAxis + Along, 0.0f, Top);
	}
	else
	{
		Along = 0.0f;
	}

	// 당겨 앉기는 «진짜로 멈춘 뒤» 한 번만 한다.
	//   왜 때를 가리나(2026-09-19 user 「다다다닥 누르니까」): 탭 사이마다 끼어들면 방금 누른 것을
	//   되돌리거나 한 칸을 통째로 끌어내린다. 방향으로 막아도 끌어내리는 쪽이 남는다 -
	//   «어느 쪽으로» 가 아니라 «언제» 가 문제였다. 두드리는 동안 아예 안 끼어들면 둘 다 없어진다.
	//   목표는 이 순간에 잡는다. 미리 잡아 두면 탭 사이에 움직인 만큼 낡은 값이 된다.
	if (!FMath::IsNearlyZero(Want))
	{
		LadderIdleQuietTime = 0.0f;
		bLadderSettling = false;
	}
	else if (DeltaTime > 0.0f)
	{
		LadderIdleQuietTime += DeltaTime;
		if (!bLadderSettling && LadderIdleQuietTime >= ClimbAnimSet->LadderIdleSettleDelay)
		{
			const float Spacing = ActiveLadder->GetRungSpacing();
			if (Spacing > KINDA_SMALL_NUMBER)
			{
				// 대기 자세의 아래쪽 발이 가로대 윗면에 오도록 «가장 가까운» 자리로 옮긴다.
				const float FootNow = LadderHeightOnAxis + ClimbAnimSet->LadderIdleFootOffset;
				float Err = FMath::Fmod(FootNow - ActiveLadder->GetRungTopOffset(), Spacing);
				if (Err >  Spacing * 0.5f) { Err -= Spacing; }
				if (Err < -Spacing * 0.5f) { Err += Spacing; }
				LadderSettleTarget = FMath::Clamp(LadderHeightOnAxis - Err, 0.0f, Top);
				bLadderSettling = true;
			}
		}
	}
	if (bLadderSettling && bDrives && DeltaTime > 0.0f && ClimbAnimSet->LadderIdleSettleSpeed > 0.0f)
	{
		LadderHeightOnAxis = FMath::FInterpTo(LadderHeightOnAxis, LadderSettleTarget,
			DeltaTime, ClimbAnimSet->LadderIdleSettleSpeed);
		if (FMath::IsNearlyEqual(LadderHeightOnAxis, LadderSettleTarget, 0.05f))
		{
			LadderHeightOnAxis = LadderSettleTarget;
			bLadderSettling = false;
		}
	}

	if (GetWorld()->GetTimeSeconds() >= LadderNextLogTime)
	{
		LadderNextLogTime = GetWorld()->GetTimeSeconds() + 1.0;
		VB_LOG(Log, "Ladder 상태: 높이=%.0f/%.0f 입력=%.2f 깊이=%.1f 몽타주=%s 루트모션=%.2fcm(축%.2f) 적용=%.2f",
			LadderHeightOnAxis, ActiveLadder->GetLadderHeight(), LadderMoveInput, ClimbAnimSet->LadderDepth,
			ActiveLadderMontage ? *ActiveLadderMontage->GetName() : TEXT("없음"),
			RootMotionMoved.Size(),
			static_cast<float>(FVector::DotProduct(RootMotionMoved, ActiveLadder->GetActorUpVector())),
			Along);
	}

	const FTransform Target = ComputeLadderCapsuleTransform(LadderHeightOnAxis);
	FVector Location = Target.GetLocation();
	FQuat Rotation = Target.GetRotation();
	const float BlendTime = ClimbAnimSet->LadderEnterBlendTime;
	if (BlendTime > 0.0f)
	{
		// 위치와 회전을 같은 알파로 섞는다. 회전을 안 섞으면 붙는 프레임에 각도가 통째로 꺾인다
		//   (등반 진입에서 겪은 것 - 눈은 위치보다 회전을 먼저 본다).
		const float Alpha = FMath::Clamp(static_cast<float>((GetWorld()->GetTimeSeconds() - LadderEnterTime) / BlendTime), 0.0f, 1.0f);
		Location = FMath::Lerp(LadderEnterLocation, Location, Alpha);
		Rotation = FQuat::Slerp(LadderEnterRotation, Rotation, Alpha).GetNormalized();
	}

	// 이 틱에 «눈에 보이는 자리» 가 얼마나 뛰었나. user 가 말하는 「튄다」는 이 값이 커진 프레임에서 난다.
	//   위의 1초 진단으로는 그 프레임을 못 보므로 여기만 프레임 단위로 본다(등반과 같은 장치).
	//   문턱 100 cm/s 의 근거: 클립 root 의 세로 최고속이 실측 98 cm/s 다. 그 위는 클립이 못 내는 값이다.
	//   조용히 넘기지 않는다 - 안 뜨면 원인이 이 함수 밖(카메라·애니 블렌드)이라는 뜻이라 그 자체가 정보다.
	if (bLadderPlacedOnce && DeltaTime > 0.0f)
	{
		const FVector Visible = Location - LadderLastPlacedLocation;
		const float Limit = 100.0f * DeltaTime;
		if (Visible.SizeSquared() > Limit * Limit && GetWorld()->GetTimeSeconds() >= LadderNextJumpLogTime)
		{
			LadderNextJumpLogTime = GetWorld()->GetTimeSeconds() + 0.05;
			// 배치와 무관하게 «액터가 스스로 움직인 양» 도 같이 낸다. 이것이 크면 캡슐이 무언가에 밀리고 있다는 뜻이다.
			const FVector Drifted = OwnerCharacter->GetActorLocation() - LadderLastPlacedLocation;
			VB_LOG(Warning, "Ladder 튐: dt=%.1fms 보임=%.1fcm | 클립=%.2f(축%.2f) 적용=%.2f 높이=%.1f "
				"액터표류=%.2fcm(축%.2f) 위상=%.2f 몽타주=%s 입력=%.2f",
				DeltaTime * 1000.0f, Visible.Size(),
				RootMotionMoved.Size(),
				static_cast<float>(FVector::DotProduct(RootMotionMoved, ActiveLadder->GetActorUpVector())),
				Along, LadderHeightOnAxis,
				Drifted.Size(),
				static_cast<float>(FVector::DotProduct(Drifted, ActiveLadder->GetActorUpVector())),
				LadderMontagePosition,
				ActiveLadderMontage ? *ActiveLadderMontage->GetName() : TEXT("없음"),
				LadderMoveInput);
		}
	}

	OwnerCharacter->SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	// 실제로 놓인 자리를 되읽는다. 「액터표류」가 순수한 외부 개입이 되려면 여기가 마지막으로
	//   위치를 만진 자리여야 한다(등반과 같은 계약).
	LadderLastPlacedLocation = OwnerCharacter->GetActorLocation();
	bLadderPlacedOnce = true;
}

bool UVBTraversalComponent::TryStartClimb()
{
	const UVBTraversalConfig* Cfg = GetConfig();
	if (bIsHanging || bIsClimbing || !Cfg || !OwnerCharacter.IsValid() || !GetWorld())
	{
		return false;
	}
	// 블렌드스페이스가 없으면 자유 등반은 꺼진 것이다 - 없는 것은 깨진 것이 아니다.
	if (!ClimbAnimSet || !ClimbAnimSet->ClimbBlendSpace)
	{
		return false;
	}
	const FVector Forward = OwnerCharacter->GetActorForwardVector();
	const FVector Origin = OwnerCharacter->GetActorLocation();
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VBClimbEnter), false, OwnerCharacter.Get());
	// 못 붙었을 때 왜인지 한 줄로 남긴다. 조용히 false 를 돌려주면 「안 붙는데?」에서 다시 가설부터 세우게 된다.
	//   0.5초에 한 번으로 묶는다 - 점프를 연타해도 로그가 안 쏟아진다.
	const bool bLogThis = GetWorld()->GetTimeSeconds() >= ClimbNextProbeLogTime;
	if (bLogThis)
	{
		ClimbNextProbeLogTime = GetWorld()->GetTimeSeconds() + 0.5;
	}
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Forward * Cfg->ClimbProbeDistance,
		Cfg->ClimbTraceChannel.GetValue(), Params))
	{
		if (bLogThis)
		{
			// 같은 선을 Visibility 로도 쏴 본다. 거기서 맞으면 「벽은 있는데 등반 표시가 없다」로 갈린다.
			FHitResult Vis;
			const bool bAnyWall = GetWorld()->LineTraceSingleByChannel(Vis, Origin,
				Origin + Forward * Cfg->ClimbProbeDistance, Cfg->TraceChannel.GetValue(), Params);
			VB_LOG(Log, "Climb 진입 실패: 등반 면 없음(%.0fcm 앞까지). 같은 선에 일반 벽은 %s%s",
				Cfg->ClimbProbeDistance, bAnyWall ? TEXT("있다 -> 그 면에 VBClimbable 콜리전 프로필이 안 걸렸다: ") : TEXT("없다(너무 멀거나 다른 곳을 본다)"),
				bAnyWall ? *GetNameSafe(Vis.GetComponent()) : TEXT(""));
		}
		return false;
	}
	if (FMath::Abs(Hit.ImpactNormal.Z) > Cfg->ClimbMaxSurfaceTiltZ)
	{
		if (bLogThis)
		{
			VB_LOG(Log, "Climb 진입 실패: 면이 너무 누웠다(법선 Z %.2f, 상한 %.2f) - %s",
				Hit.ImpactNormal.Z, Cfg->ClimbMaxSurfaceTiltZ, *GetNameSafe(Hit.GetComponent()));
		}
		return false;
	}
	// 붙는 점은 root 높이다 - 캡슐 배치가 root 를 그 점 앞에 두기 때문이다.
	//   맞은 점(캡슐 중심 높이)을 그대로 쓰면 몸이 그만큼 떠오른다.
	FVector SurfacePoint = Hit.ImpactPoint;
	if (const USkeletalMeshComponent* Mesh = OwnerCharacter->GetMesh())
	{
		SurfacePoint.Z = Mesh->GetComponentLocation().Z;
	}
	FHitResult AtRoot;
	if (TraceClimbSurface(SurfacePoint, Hit.ImpactNormal, AtRoot))
	{
		SurfacePoint = AtRoot.ImpactPoint;
	}

	VB_LOG(Log, "Climb 진입: 면=%s 법선=(%.2f,%.2f,%.2f)", *GetNameSafe(Hit.GetComponent()),
		Hit.ImpactNormal.X, Hit.ImpactNormal.Y, Hit.ImpactNormal.Z);
	if (OwnerCharacter->HasAuthority())
	{
		MulticastStartClimb(SurfacePoint, Hit.ImpactNormal, Hit.GetComponent());
	}
	else
	{
		ServerStartClimb(SurfacePoint, Hit.ImpactNormal, Hit.GetComponent());
	}
	return true;
}

bool UVBTraversalComponent::ServerStartClimb_Validate(FVector SurfacePoint, FVector SurfaceNormal, UPrimitiveComponent* Surface)
{
	// 패킷 위생만(실패 = 연결 종료). 거리 상한은 _Implementation 이 거부로 처리한다.
	return !SurfacePoint.ContainsNaN() && !SurfaceNormal.ContainsNaN() && SurfaceNormal.IsNormalized();
}

void UVBTraversalComponent::ServerStartClimb_Implementation(FVector SurfacePoint, FVector SurfaceNormal, UPrimitiveComponent* Surface)
{
	if (!OwnerCharacter.IsValid())
	{
		return;
	}
	// 클라가 보낸 좌표를 서버가 아는 위치로 검증한다. 매달림 진입과 같은 상한을 쓴다.
	const UVBTraversalConfig* Cfg = GetConfig();
	const float MaxDist = Cfg ? Cfg->MaxPlausibleLedgeDistance : 600.0f;
	const float Dist = static_cast<float>(FVector::Dist(SurfacePoint, OwnerCharacter->GetActorLocation()));
	if (Dist > MaxDist)
	{
		VB_LOG(Warning, "Climb 진입 거부: 면이 %.0fcm 밖이다(상한 %.0f)", Dist, MaxDist);
		return;
	}
	MulticastStartClimb(SurfacePoint, SurfaceNormal, Surface);
}

void UVBTraversalComponent::MulticastStartClimb_Implementation(FVector SurfacePoint, FVector SurfaceNormal, UPrimitiveComponent* Surface)
{
	StartClimb(SurfacePoint, SurfaceNormal, Surface);
}

void UVBTraversalComponent::StartClimb(const FVector& SurfacePoint, const FVector& SurfaceNormal, UPrimitiveComponent* Surface)
{
	if (!OwnerCharacter.IsValid() || !GetWorld())
	{
		return;
	}
	// 매달림과 같이 «트래버설 중» 으로 표시한다. 이 줄이 없으면 RecoverFromTraversal 이 첫 줄에서
	//   되돌아가 무기 복원·콜리전 복구·이동 모드·루트모션 모드가 하나도 안 돌아온다(2026-09-19 자가검토에서 적발).
	bDoingTraversalAction = true;
	bIsClimbing = true;
	ClimbSurfacePoint = SurfacePoint;
	ClimbSurfaceNormal = SurfaceNormal;
	ClimbMoveInput = FVector2D::ZeroVector;
	ClimbMoveAxis = FVector2D::ZeroVector;
	ClimbDepthBase = ClimbAnimSet ? ClimbAnimSet->ClimbWallDepthIdle : 48.7f;
	ClimbDepthCurrent = ClimbDepthBase;
	ClimbIKTargets = FVBHangIKTargets();
	ActiveClimbMontage = nullptr;
	ActiveClimbDir = FVector2D::ZeroVector;
	ClimbPendingMontage = nullptr;
	bClimbPlacedOnce = false; // 첫 틱은 비교할 직전 위치가 없다 - 없는 것을 0 으로 읽지 않는다
	ClimbWeightHandL = ClimbWeightHandR = ClimbWeightFootL = ClimbWeightFootR = 0.0f;
	ClimbCorrHandL = ClimbCorrHandR = ClimbCorrFootL = ClimbCorrFootR = 0.0f;
	ClimbEnterLocation = OwnerCharacter->GetActorLocation();
	ClimbEnterRotation = OwnerCharacter->GetActorQuat();
	ClimbEnterTime = GetWorld()->GetTimeSeconds();
	ClimbMaxBodyRate = 0.0f;
	ClimbMaxLimbRate = 0.0f;
	bClimbLimbRatePrimed = false;
	BeginSurfaceAttachment(Surface);
	UpdateClimb(0.0f);
}

void UVBTraversalComponent::SetClimbMoveInput(const FVector2D& Input)
{
	ClimbMoveInput = Input;
	// 서버가 위치를 굴리므로 소유 클라는 축 값을 보낸다. 매 변경마다라 Unreliable.
	if (OwnerCharacter.IsValid() && !OwnerCharacter->HasAuthority() && OwnerCharacter->IsLocallyControlled())
	{
		ServerSetClimbMoveInput(Input);
	}
}

bool UVBTraversalComponent::ServerSetClimbMoveInput_Validate(FVector2D Input)
{
	return !Input.ContainsNaN();
}

void UVBTraversalComponent::ServerSetClimbMoveInput_Implementation(FVector2D Input)
{
	ClimbMoveInput = FVector2D(FMath::Clamp(Input.X, -1.0f, 1.0f), FMath::Clamp(Input.Y, -1.0f, 1.0f));
}

void UVBTraversalComponent::RequestClimbDrop()
{
	if (!bIsClimbing || !OwnerCharacter.IsValid())
	{
		return;
	}
	if (OwnerCharacter->HasAuthority())
	{
		MulticastClimbDrop();
	}
	else
	{
		ServerClimbDrop();
	}
}

bool UVBTraversalComponent::ServerClimbDrop_Validate() { return true; }
void UVBTraversalComponent::ServerClimbDrop_Implementation() { MulticastClimbDrop(); }
void UVBTraversalComponent::MulticastClimbDrop_Implementation() { EndClimbByDrop(); }

void UVBTraversalComponent::EndClimbByDrop()
{
	if (!bIsClimbing)
	{
		return;
	}
	// 뒷정리는 전부 RecoverFromTraversal 이 맡는다 - 복구의 홈은 하나다.
	//   여기서 bIsClimbing 을 먼저 끄면 그 함수의 등반 분기가 안 돌아 루트모션 모드가 남는다.
	RecoverMovementMode = MOVE_Falling;
	RecoverFromTraversal();
}

void UVBTraversalComponent::UpdateClimb(float DeltaTime)
{
	if (!OwnerCharacter.IsValid() || !GetWorld() || !ClimbAnimSet)
	{
		EndClimbByDrop();
		return;
	}

	// 끝에서 «이 틱에 보이는 자리가 얼마나 움직였나» 를 되짚기 위한 기준값들.
	//   튀는 것은 프레임 하나에서 생기는데 위의 진단 줄은 1초에 하나라 그 프레임을 못 본다.
	const FVector SurfaceBefore = ClimbSurfacePoint;
	const float DepthBefore = ClimbDepthCurrent;
	FVector RootMotionMoved = FVector::ZeroVector;
	float AppliedRight = 0.0f;
	float AppliedUp = 0.0f;

	// 입력을 아는 쪽만 면 위를 움직인다. 시뮬 프록시는 복제된 위치를 받는다.
	const bool bDrives = OwnerCharacter->HasAuthority() || OwnerCharacter->IsLocallyControlled();
	if (bDrives && DeltaTime > 0.0f)
	{
		// 벽 평면의 두 축. 첫 판은 "서 있는 평면" 만 받으므로 위 축이 곧 월드 위다.
		//   기울어진 면·곡면은 이 가정이 깨진다 - 설계 .sw/traversal/design.md 12.6 에 위험으로 적어 두었다.
		// 오른쪽 축의 부호를 유도해 둔다(2026-09-19, 처음에 반대로 썼다):
		//   언리얼에서 오른쪽 = 위 x 앞 이고, 벽에 붙은 캐릭터의 앞은 -법선이다.
		//   오른쪽 = 위 x (-법선) = -(위 x 법선) = 법선 x 위.
		//   벽이 +X 에 있어 법선이 -X 라면 (-X) x Z = +Y 이고, +X 를 보는 캐릭터의 오른쪽이 바로 +Y 다.
		const FVector PlaneRight = FVector::CrossProduct(ClimbSurfaceNormal, FVector::UpVector).GetSafeNormal();
		const FVector PlaneUp = FVector::UpVector;
		// 방향에 맞는 몽타주를 건다. 그것이 돌면 이동은 루트모션이 맡고, 나는 그 결과를 벽 위로 되돌려 놓기만 한다.
		//   왜 루트모션인가(2026-09-19 user 「몸이 먼저 움직여 카메라가 못 따라감」): 고정 속도로 밀면
		//   애니는 제자리에서 돌고 몸만 끌려가 미끄러진다. IK 는 말단의 «깊이» 만 잡지 벽을 따라가는 미끄러짐은 못 막는다.
		// 몽타주가 바뀌는 프레임은 «직전 프레임과 이어진다» 는 전제가 깨진다 - 자세가 갈아 끼워지기 때문이다.
		//   그 프레임의 차이를 그대로 쓰면 IK 판정이 오염되고, 그 오염은 세기가 1 에 가까울 때
		//   스스로를 참조해 안 풀린다(2026-09-19 user 「W 갔다가 S 가면 그 후에 다리가 망가져」).
		// 가려는 방향에 벽이 있나를 「걸기 전에」 본다. 없으면 그 성분을 지우고 몽타주도 안 건다 -
		//   끝에 닿았는데 제자리에서 애니만 도는 것을 막는다(렛지 옆걸음 끝에서 배운 것 그대로).
		//   두 높이를 본다: root 자리와 그 위 ClimbBodyProbeRise. 위쪽이 비면 거기가 벽의 끝이다.
		//   위 성분만 죽이고 옆은 살린다 - 끝에 닿았다고 옆으로도 못 가면 갇힌다.
		FVector2D EffectiveInput = ClimbMoveInput;
		if (const UVBTraversalConfig* Cfg = GetConfig())
		{
			const FVector2D Want(FMath::Clamp(ClimbMoveInput.X, -1.0f, 1.0f), FMath::Clamp(ClimbMoveInput.Y, -1.0f, 1.0f));
			if (!Want.IsNearlyZero())
			{
				const float Ahead = Cfg->ClimbEdgeProbeAhead;
				const FVector Look = ClimbSurfacePoint + PlaneRight * (Want.X * Ahead) + PlaneUp * (Want.Y * Ahead);
				FHitResult Probe;
				if (!TraceClimbSurface(Look, ClimbSurfaceNormal, Probe))
				{
					EffectiveInput = FVector2D::ZeroVector;
				}
				else if (Want.Y > 0.0f && !TraceClimbSurface(Look + PlaneUp * Cfg->ClimbBodyProbeRise, ClimbSurfaceNormal, Probe))
				{
					EffectiveInput.Y = 0.0f;
				}
			}
		}
		// 몽타주 전환 구간에 IK 판정을 쉬던 처리가 여기 있었다. 판정 자체가 사라져(트레이스 목표는
		//   지금 자리만 보므로) 쉴 것이 없다 - 전환 중에도 목표는 같은 벽면이다.
		TickClimbMontage(EffectiveInput);
		const FVector2D MontageDir = ActiveClimbMontage ? ActiveClimbDir : FVector2D::ZeroVector;

		// 이 틱에 루트모션이 몸을 얼마나 옮겼나. 내가 직전에 놓은 자리와 지금 자리의 차이가 그것이다.
		if (bClimbPlacedOnce)
		{
			RootMotionMoved = OwnerCharacter->GetActorLocation() - ClimbLastPlacedLocation;
		}
		FVector Moved = RootMotionMoved;
		// 한 틱에 갈 수 있는 상한. user 가 「W 갔다가 바로 S 가니까 순간이동 가는 느낌」이라고 했고(2026-09-19)
		//   전환 프레임에 진단값이 크게 튀는 것은 확인했다. 다만 «겹친 두 몽타주의 루트모션이 함께 실린다» 는
		//   [가설]이다 - 확인하려면 이 상한이 실제로 걸리는지를 봐야 한다.
		//   그래서 조용히 자르지 않는다: 잘렸다는 사실 자체가 그 가설의 증거다. 로그가 안 뜨면 원인은 다른 데 있다.
		// 8 배인 근거: 클립 root 속도의 최고가 125 cm/s 이고 기준 속도가 72 라 정상 최대가 1.7 배쯤이다.
		//   정상 구간을 절대 안 건드리도록 넉넉히 두되, 순간이동급(수십 배)은 확실히 잡는 자리다.
		const float MaxStep = ClimbAnimSet->ClimbSpeed * DeltaTime * 8.0f;
		if (Moved.SizeSquared() > MaxStep * MaxStep && MaxStep > KINDA_SMALL_NUMBER)
		{
			if (GetWorld()->GetTimeSeconds() >= ClimbNextClampLogTime)
			{
				ClimbNextClampLogTime = GetWorld()->GetTimeSeconds() + 1.0;
				VB_LOG(Log, "Climb: 한 틱 이동 %.0fcm 가 상한 %.0f 을 넘어 잘랐다(몽타주 전환 구간일 수 있다)",
					Moved.Size(), MaxStep);
			}
			Moved = Moved.GetSafeNormal() * MaxStep;
		}
		// 루트모션을 «그 몽타주가 맡은 방향» 에만 투영해서 쓴다. 그 축과 직각인 흔들림은 버린다.
		//   왜(2026-09-19 실측): VB_Climb_Up 의 root 가로 성분은 한 주기(3.2초)를 다 돌면 시작값으로
		//   돌아오지만(5.49 -> 6.30) 진폭이 ±5cm 라 도중에 손을 떼면 최대 9.7cm 가 남는다.
		//   그 남은 값이 면 위의 자리에 영구히 적립되고 다시 누르면 또 쌓인다 -
		//   실측에서 W 만 눌렀는데 면의 가로 좌표가 4초에 23cm 밀렸다(user 「튄다」 로그).
		//   깊이에서 걷어낸 것과 같은 결함이다: 「한 주기를 다 돌면 0 이 된다」는 사람이 끝까지 안 누르면 거짓이다.
		//   맡은 축의 성분만 쓰면 주기를 어디서 끊든 남는 것이 없다 - 그 축은 단조 증가라 끊긴 자리가 곧 간 만큼이다.
		// 벽 쪽 성분도 여기서 같이 버려진다. 배치가 어차피 깊이를 기준값으로 되돌리고 말단의 접지는 IK 가 맡는다.
		//   한때 그것을 "흔들림" 으로 누적해 배치에 돌려줬다가 같은 이유로 걷어냈다
		//   (깊이가 47.7 에서 55.5 까지 흘러 몸이 벽에서 10cm 밀려났고 새는 속도가 초당 2cm 라 몇 초를 갔다).
		float AlongRight = 0.0f;
		float AlongUp    = 0.0f;
		if (MontageDir.IsNearlyZero())
		{
			// 이 방향을 맡을 몽타주가 없다(미할당 등). 일정 속도로 민다 - 그 구간만 미끄러진다.
			//   MontageDir 가 0 인 것과 ActiveClimbMontage 가 비는 것은 같은 사실이다(TickClimbMontage 가 한 쌍으로 낸다).
			//   같은 사실을 두 술어로 물으면 한쪽이 낡는 날 분기가 갈리므로 여기서는 한 술어만 쓴다.
			const float Step = ClimbAnimSet->ClimbSpeed * DeltaTime;
			AlongRight = FMath::Clamp(EffectiveInput.X, -1.0f, 1.0f) * Step;
			AlongUp    = FMath::Clamp(EffectiveInput.Y, -1.0f, 1.0f) * Step;
		}
		else
		{
			const FVector AskedDir = (PlaneRight * MontageDir.X + PlaneUp * MontageDir.Y).GetSafeNormal();
			const float Progress = static_cast<float>(FVector::DotProduct(Moved, AskedDir));
			AlongRight = Progress * MontageDir.X;
			AlongUp    = Progress * MontageDir.Y;
		}
		AppliedRight = AlongRight;
		AppliedUp = AlongUp;
		const FVector Desired = ClimbSurfacePoint + PlaneRight * AlongRight + PlaneUp * AlongUp;

		// 가려는 자리에서 면을 다시 찾는다. 못 찾으면 그 방향은 면의 가장자리다 - 안 간다.
		//   "못 찾았다" 를 "거기 있다" 로 읽지 않는 것이 여기서 유일한 안전장치다.
		FHitResult Hit;
		if (TraceClimbSurface(Desired, ClimbSurfaceNormal, Hit))
		{
			ClimbSurfacePoint = Hit.ImpactPoint;
			ClimbSurfaceNormal = Hit.ImpactNormal;
		}

		// 축도 「갈 수 있는 입력」을 본다. 원래 입력을 그대로 넣으면 끝에 막혀 안 움직이는데
		//   블렌드스페이스만 이동 자세를 들어 제자리걸음이 된다.
		const FVector2D AxisTarget(FMath::Clamp(EffectiveInput.X, -1.0f, 1.0f), FMath::Clamp(EffectiveInput.Y, -1.0f, 1.0f));
		ClimbMoveAxis = FMath::Vector2DInterpTo(ClimbMoveAxis, AxisTarget, DeltaTime, ClimbAnimSet->ClimbMoveAxisInterpSpeed);
	}

	// 대기 자세가 이동 자세보다 벽에 깊다(실측 48.7 대 42.5~47.4). 한 값으로 두면 한쪽이 어긋난다.
	const bool bMoving = ClimbMoveInput.SizeSquared() > 0.04f;
	const float TargetDepth = bMoving ? ClimbAnimSet->ClimbWallDepthMoving : ClimbAnimSet->ClimbWallDepthIdle;
	ClimbDepthBase = FMath::FInterpConstantTo(ClimbDepthBase, TargetDepth, DeltaTime, ClimbAnimSet->ClimbDepthInterpSpeed);
	ClimbDepthCurrent = ClimbDepthBase;

	// 말단을 벽에 붙인다. 배치가 몸통만 맞추므로 손발 접지는 여기가 맡는다.
	//   몽타주 전환 블렌드 동안에는 쉰다 - 그 구간의 자세 변화는 «말단이 움직인다» 가 아니라 «자세가 갈린다» 다.
	// 전환 블렌드 동안에는 «판정만» 쉰다. 목표 계산과 가중치는 그대로 간다.
	//   종전에는 여기서 가중치를 0 으로 내리고 이어짐도 끊었는데, 그러면 그 순간 말단이 클립 원래 자리
	//   (실측 벽에서 20~31cm)로 튕겼다가 다시 당겨진다. 전환이 1초에 여섯 번 일어나면 그것이 곧 떨림이다
	//   (2026-09-19 user 「손 덜덜 떨리고 W 누르다가 바로 S 누르면 사삭」).
	//   목표는 매 프레임 몸을 따라 다시 계산되므로 얼어붙지 않는다 - 쉬는 것은 판정 하나뿐이다.
	UpdateClimbIKTargets(DeltaTime);

	// 말단이 몸 기준으로 얼마나 빨리 움직이나. IK 판정과 별도로 늘 잰다 -
	//   판정은 전환 구간에 쉬는데 자세가 튀는 자리가 바로 그 구간이라, 같이 쉬면 그 자리를 못 본다.
	if (const USkeletalMeshComponent* LimbMesh = OwnerCharacter->GetMesh())
	{
		static const FName LimbBones[4] = { TEXT("hand_l"), TEXT("hand_r"), TEXT("foot_l"), TEXT("foot_r") };
		const FTransform LimbActorXform = OwnerCharacter->GetActorTransform();
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector Rel = LimbActorXform.InverseTransformPosition(LimbMesh->GetSocketLocation(LimbBones[i]));
			if (bClimbLimbRatePrimed && DeltaTime > 0.0f)
			{
				ClimbMaxLimbRate = FMath::Max(ClimbMaxLimbRate, static_cast<float>((Rel - ClimbPrevLimbRel[i]).Size()) / DeltaTime);
			}
			ClimbPrevLimbRel[i] = Rel;
		}
		bClimbLimbRatePrimed = true;
	}

	// 1초에 한 줄. 매달림의 같은 줄과 역할이 같다 - "안 붙는다"·"엉뚱하게 간다" 가 들어왔을 때
	//   면을 못 찾은 것인지 찾았는데 자리가 틀린 것인지 이 줄이 가른다. 없으면 가설부터 세우게 된다.
	if (GetWorld()->GetTimeSeconds() >= ClimbNextLogTime)
	{
		ClimbNextLogTime = GetWorld()->GetTimeSeconds() + 1.0;
		FString Parts;
		if (const USkeletalMeshComponent* M = OwnerCharacter->GetMesh())
		{
			// 각 말단이 벽 평면에서 얼마나 바깥인가. 양수=벽 바깥, 음수=벽 안쪽.
			auto Signed = [&](const FName Bone) -> float
			{
				return static_cast<float>(FVector::DotProduct(M->GetSocketLocation(Bone) - ClimbSurfacePoint, ClimbSurfaceNormal));
			};
			// root 뼈가 메시 원점에서 어디에 있나(컴포넌트 공간, 세 성분 다). 이것이 «몸이 캡슐을 따라오는가» 다.
			//   왜 세 성분인가(2026-09-19): ABP 의 OffsetRootBone 이 등반 자세보다 하류에 있고, 그 노드의 일이
			//   「캡슐이 애니가 내지 않은 만큼 움직이면 메시를 붙잡아 두기」다. 우리 배치는 매 프레임 캡슐을
			//   옮겨 놓으므로 그 어긋남을 설계상 매 프레임 만든다. Z 하나만 찍으면 그 어긋남을 못 본다.
			// 왜 블렌드스페이스가 원시 root 를 실었나: 엔진이 root 를 잠그는 조건이
			//   `(bExtractRootMotion && bEnableRootMotion) || bForceRootLock` 이다
			//   (`AnimSequence.cpp::UAnimSequence::GetBonePose`). 블렌드스페이스는 루트모션 추출을 요청하지 않으므로
			//   앞 항이 거짓이고, 클립들의 bForceRootLock 이 false 라 뒤 항도 거짓이었다 - 그래서 안 잠겼다.
			//   대조군: 완전 대기일 때 잰 root=(3,5,10) 이 VB_Climb_Idle 의 원시 root (3.55,4.70,9.52) 와 같았다.
			// 모드는 노드 속성이 아니라 핀 바인딩이 정한다(`get_anim_node_pin_bindings` 로 확인 -
			//   TranslationMode <- UVBAnimInstance::GetOffsetRootTranslationMode, RotationMode <- ...RotationMode).
			//   등반 중 이동 모드는 Release 다: 몽타주가 돌면 IsSlotActive(DefaultSlot) 가, 안 돌면 이동모드가
			//   Flying 이라 switch 의 default 가 받는다. Release 는 오프셋을 안 쌓고 있는 것을 녹인다.
			//   회전 모드는 무장 경로가 아니고 몽타주도 안 돌면 Accumulate 로 떨어진다 - 그건 캡슐 회전을
			//   메시에서 역상쇄하는 모드이고 상한(MaxRotationError)이 -1 이라 한계가 없다.
			//   판별 기준은 0 이다. 등반 클립 아홉에 bForceRootLock 을 걸어(2026-09-19) 어느 경로로 재생되든
			//   자세의 root 가 레퍼런스로 잠긴다. 그러니 이 값이 0 에서 크게 벗어나면 그 자체가 결함이다.
			//   그 플래그를 건 이유가 바로 이 숫자였다 - 걸기 전에는 블렌드스페이스가 클립의 원시 root 를
			//   그대로 자세에 실어 축 0.46 에서 root Z 가 97 까지 올라갔다(실측). 아래 «왜» 참조.
			const FVector RootRel = M->GetBoneTransform(0, FTransform::Identity).GetLocation();
			// 발은 발목이 아니라 발볼을 찍는다 - 벽에 닿는 자리가 그쪽이고, IK 가 맞추는 대상도 그쪽이다.
			//   발목을 찍으면 IK 가 제대로 도는데도 3 이 아니라 10 쯤 나와서 틀린 줄 알게 된다.
			Parts = FString::Printf(TEXT("손L%+.0f 손R%+.0f 발볼L%+.0f 발볼R%+.0f root=(%.0f,%.0f,%.0f)"),
				Signed(TEXT("hand_l")), Signed(TEXT("hand_r")), Signed(TEXT("ball_l")), Signed(TEXT("ball_r")),
				RootRel.X, RootRel.Y, RootRel.Z);
		}
		// 접지는 «벽면에서 몇 cm» 로 찍는다. 양수=바깥, 음수=벽 안쪽. IK 가 듣는지 이 숫자가 말한다.
		VB_LOG(Log, "Climb 상태: 면(%.0f,%.0f,%.0f) 입력(%.2f,%.2f) 축(%.2f,%.2f) 깊이=%.1f "
			"몽타주=%s 최대(몸%.0f 말단%.0f) IK세기(손%.2f/%.2f 발%.2f/%.2f) | %s",
			ClimbSurfacePoint.X, ClimbSurfacePoint.Y, ClimbSurfacePoint.Z,
			ClimbMoveInput.X, ClimbMoveInput.Y, ClimbMoveAxis.X, ClimbMoveAxis.Y, ClimbDepthCurrent,
			ActiveClimbMontage ? *ActiveClimbMontage->GetName() : TEXT("없음"),
			ClimbMaxBodyRate, ClimbMaxLimbRate,
			ClimbIKTargets.HandWeightL, ClimbIKTargets.HandWeightR,
			ClimbIKTargets.FootLWeight, ClimbIKTargets.FootRWeight,
			*Parts);
		// 이 줄이 그 1초를 대표하므로 여기서 다시 센다. 안 비우면 최대값이 영원히 안 내려간다.
		ClimbMaxBodyRate = 0.0f;
		ClimbMaxLimbRate = 0.0f;
	}

	const FTransform Target = ComputeClimbCapsuleTransform(ClimbSurfacePoint, ClimbSurfaceNormal);
	FVector Location = Target.GetLocation();
	FQuat Rotation = Target.GetRotation();
	// 진입 보간이 이 틱에 몸을 얼마나 끌었나. 아래 튐 진단에 이름 붙은 항으로 넣는다 -
	//   2026-09-19 에 이 몫을 «다른 항을 빼고 남은 것» 으로 역산했는데, 역산은 안 센 항이 하나라도
	//   있으면 그 몫까지 여기에 얹어 버린다. 잴 수 있는 것을 추론으로 두지 않는다.
	FVector EnterPull = FVector::ZeroVector;
	const float BlendTime = ClimbAnimSet->ClimbEnterBlendTime;
	if (BlendTime > 0.0f)
	{
		// 위치와 회전을 같은 알파로 섞는다. 회전을 안 섞으면 붙잡는 프레임에 각도가 통째로 꺾인다 -
		//   2026-09-19 진입 튐(0.15초에 54cm)의 나머지 절반이 그것이었다. 위치만 부드럽게 해도 눈은 회전을 본다.
		const float Alpha = FMath::Clamp(static_cast<float>((GetWorld()->GetTimeSeconds() - ClimbEnterTime) / BlendTime), 0.0f, 1.0f);
		const FVector Unblended = Location;
		Location = FMath::Lerp(ClimbEnterLocation, Location, Alpha);
		Rotation = FQuat::Slerp(ClimbEnterRotation, Rotation, Alpha).GetNormalized();
		EnterPull = Location - Unblended;
	}
	// 이 틱에 «눈에 보이는 자리» 가 얼마나 뛰었나. user 가 말하는 「튄다」는 이 값이 커진 프레임에서 난다.
	//   위의 1초 진단으로는 그 프레임을 못 보므로 여기만 프레임 단위로 본다. 조용히 넘기지 않고 말한다 -
	//   안 뜨면 원인이 이 함수 밖(카메라·애니 블렌드)에 있다는 뜻이라 그 자체가 정보다.
	// 문턱 200 cm/s 의 근거: 클립 root 의 최고 세로 속도가 실측 119 cm/s 라 그 1.7배다.
	//   처음에는 3배(360)로 뒀는데 진입 구간만 걸리고 등반 중에는 0건이었다 - 문턱이 높으면
	//   「안 걸렸다」가 「없다」로 읽힌다. 분포는 위의 최대값 줄이 늘 말하고 여기는 사건만 잡는다.
	if (bClimbPlacedOnce && DeltaTime > 0.0f)
	{
		const FVector Visible = Location - ClimbLastPlacedLocation;
		const float Limit = 200.0f * DeltaTime;
		ClimbMaxBodyRate = FMath::Max(ClimbMaxBodyRate, static_cast<float>(Visible.Size()) / DeltaTime);
		if (Visible.SizeSquared() > Limit * Limit && GetWorld()->GetTimeSeconds() >= ClimbNextJumpLogTime)
		{
			ClimbNextJumpLogTime = GetWorld()->GetTimeSeconds() + 0.05;
			const FVector RightNow = FVector::CrossProduct(ClimbSurfaceNormal, FVector::UpVector).GetSafeNormal();
			const FVector SurfaceDelta = ClimbSurfacePoint - SurfaceBefore;
			VB_LOG(Warning, "Climb 튐: dt=%.1fms 보임=%.1fcm | 면=%.1f(가로%.1f 위%.1f) 깊이=%+.1f 진입보간=%.1f "
				"루트모션=%.1f(가로%.1f 위%.1f 법선%.1f) 적용(가로%.1f 위%.1f) 몽타주=%s 입력(%.2f,%.2f)",
				DeltaTime * 1000.0f, Visible.Size(),
				SurfaceDelta.Size(),
				static_cast<float>(FVector::DotProduct(SurfaceDelta, RightNow)),
				static_cast<float>(SurfaceDelta.Z),
				ClimbDepthCurrent - DepthBefore,
				EnterPull.Size(),
				RootMotionMoved.Size(),
				static_cast<float>(FVector::DotProduct(RootMotionMoved, RightNow)),
				static_cast<float>(RootMotionMoved.Z),
				static_cast<float>(FVector::DotProduct(RootMotionMoved, ClimbSurfaceNormal)),
				AppliedRight, AppliedUp,
				ActiveClimbMontage ? *ActiveClimbMontage->GetName() : TEXT("없음"),
				ClimbMoveInput.X, ClimbMoveInput.Y);
		}
	}

	OwnerCharacter->SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	ClimbLastPlacedLocation = OwnerCharacter->GetActorLocation();
	bClimbPlacedOnce = true;
}

FTransform UVBTraversalComponent::ComputeClimbCapsuleTransform(const FVector& SurfacePoint, const FVector& SurfaceNormal) const
{
	// 얼굴은 벽 쪽(-법선). root 는 면에서 바깥으로 ClimbDepthCurrent.
	//   매달림과 달리 높이 오프셋이 없다 - 등반은 높이가 배치축이 아니라 이동축이라 입력이 직접 정한다.
	const FRotator Facing = FRotationMatrix::MakeFromX(-SurfaceNormal).Rotator();
	const FVector RootWorld = SurfacePoint + SurfaceNormal * ClimbDepthCurrent;
	FVector MeshRel = FVector::ZeroVector;
	if (OwnerCharacter.IsValid() && OwnerCharacter->GetMesh())
	{
		MeshRel = OwnerCharacter->GetMesh()->GetRelativeLocation();
	}
	return FTransform(Facing, RootWorld - Facing.RotateVector(MeshRel));
}

void UVBTraversalComponent::UpdateClimbIKTargets(float DeltaTime)
{
	const USkeletalMeshComponent* Mesh = OwnerCharacter.IsValid() ? OwnerCharacter->GetMesh() : nullptr;
	const UVBTraversalConfig* Cfg = GetConfig();
	if (!Mesh || !ClimbAnimSet || !Cfg || !GetWorld() || DeltaTime <= 0.0f)
	{
		return;
	}

	// 말단을 벽면에서 여유만큼 떨어진 자리에 둔다. 두 가지가 이 구현의 뼈대다.
	//
	// 하나. 오프셋은 「그 뼈 자기 공간」으로 준다(네 노드 모두 BCS_BoneSpace + 이펙터 타깃 = 자기 자신).
	//   그러면 노드가 클립이 둔 자리에 오프셋을 더한다 - 덮지 않는다.
	//   월드 좌표로 주면 노드가 말단을 내 목표에 그대로 놓아 버려서, 다음 프레임에 내가 읽는 소켓이
	//   곧 내 목표가 되고 클립이 어디로 가려 했는지가 사라진다. 그러면 면 안쪽 방향이 얼어붙는다
	//   (2026-09-19 실측: 발이 그 상태였고 user 「다리는 특히 이상하다」).
	//
	// 둘. 보정량을 상태로 들고 오차만큼 조금씩 고친다(적분).
	//   한 방에 맞추는 식은 「노드가 내 숫자를 그대로 적용한다」를 전제하는데 그것이 거짓이다 -
	//   뼈 공간이라 그 뼈의 회전을 타고, 팔다리 길이 한계에도 걸린다.
	//   적분은 그 전제가 필요 없다. 결과를 보고 고치므로 노드가 무엇을 하든 수렴한다.
	//   이것이 오늘 두 번 실패한 뒤의 고침이다 - 전문은 SystemMap 09 의 같은 제목 절에 있다.
	const float Probe = Cfg->ClimbProbeOutset;
	const float MaxCorr = ClimbAnimSet->ClimbIKMaxCorrection;
	const float Gain = FMath::Clamp(ClimbAnimSet->ClimbIKGain, 0.01f, 1.0f);
	const FVector N = ClimbSurfaceNormal;

	// ContactBone 은 벽에 닿는 뼈, Bone 은 IK 가 옮기는 뼈다. 손은 같고 발은 다르다(발볼 대 발목).
	//   보정은 법선 방향 평행이동이라 둘에 같은 양이 걸린다 - 그래서 장부가 하나로 충분하다.
	auto Solve = [&](const FName Bone, const FName ContactBone, const float Clearance,
		float& InOutCorr, float& InOutWeight, const float ConfiguredWeight, FVector& OutLocalOffset)
	{
		OutLocalOffset = FVector::ZeroVector;
		bool bHit = false;
		if (Mesh->GetBoneIndex(ContactBone) != INDEX_NONE)
		{
			const FVector Contact = Mesh->GetSocketLocation(ContactBone);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(VBClimbLimbProbe), false, OwnerCharacter.Get());
			FHitResult Hit;
			if (GetWorld()->LineTraceSingleByChannel(Hit, Contact + N * Probe, Contact - N * Probe,
				Cfg->ClimbTraceChannel.GetValue(), Params))
			{
				bHit = true;
				// 지금 이 말단이 면에서 얼마나 떨어져 있나. 목표는 Clearance 다.
				//   이 값에는 내가 이미 넣은 보정이 들어 있다 - 빼지 않는다. 오차가 0 이 되면
				//   보정이 저절로 지금 값에 머문다. 그것이 적분의 요점이다.
				const float Measured = static_cast<float>(FVector::DotProduct(Contact - Hit.ImpactPoint, N));
				InOutCorr = FMath::Clamp(InOutCorr + (Clearance - Measured) * Gain, -MaxCorr, MaxCorr);
			}
		}
		if (!bHit)
		{
			// 면이 없다 = 허공으로 뻗는 중. 보정을 녹여 클립에 돌려준다.
			InOutCorr = FMath::FInterpTo(InOutCorr, 0.0f, DeltaTime, 10.0f);
		}
		InOutWeight = FMath::FInterpTo(InOutWeight, bHit ? ConfiguredWeight : 0.0f, DeltaTime,
			ClimbAnimSet->ClimbIKWeightInterpSpeed);
		// 월드 방향을 그 뼈 공간으로 되돌린다. 위치가 아니라 방향만 옮기면 되므로 회전만 되돌린다.
		OutLocalOffset = Mesh->GetSocketTransform(Bone).InverseTransformVector(N * InOutCorr);
	};

	Solve(TEXT("hand_l"), TEXT("hand_l"), ClimbAnimSet->ClimbLimbWallClearance,
		ClimbCorrHandL, ClimbWeightHandL, ClimbAnimSet->ClimbHandIKWeight, ClimbIKTargets.HandL);
	Solve(TEXT("hand_r"), TEXT("hand_r"), ClimbAnimSet->ClimbLimbWallClearance,
		ClimbCorrHandR, ClimbWeightHandR, ClimbAnimSet->ClimbHandIKWeight, ClimbIKTargets.HandR);
	Solve(TEXT("foot_l"), TEXT("ball_l"), ClimbAnimSet->ClimbFootWallClearance,
		ClimbCorrFootL, ClimbWeightFootL, ClimbAnimSet->ClimbFootIKWeight, ClimbIKTargets.FootL);
	Solve(TEXT("foot_r"), TEXT("ball_r"), ClimbAnimSet->ClimbFootWallClearance,
		ClimbCorrFootR, ClimbWeightFootR, ClimbAnimSet->ClimbFootIKWeight, ClimbIKTargets.FootR);

	ClimbIKTargets.HandWeightL = ClimbWeightHandL;
	ClimbIKTargets.HandWeightR = ClimbWeightHandR;
	ClimbIKTargets.FootLWeight = ClimbWeightFootL;
	ClimbIKTargets.FootRWeight = ClimbWeightFootR;
}

UAnimMontage* UVBTraversalComponent::PickClimbMontage(const FVector2D& Input, FVector2D& OutDir) const
{
	// 고른 몽타주와 «그 몽타주가 맡은 방향» 은 같은 판단이라 한 자리에서 같이 낸다.
	//   방향을 호출처에서 다시 세면 문턱값이 두 곳이 되고, 그 둘이 어긋나는 날 투영이 엉뚱해진다.
	OutDir = FVector2D::ZeroVector;
	if (!ClimbAnimSet || Input.SizeSquared() <= 0.04f)
	{
		return nullptr; // 입력 없음 = 대기. 블렌드스페이스가 대기 자세를 든다.
	}
	// 8방향 중 가장 가까운 것. 0.4 는 "그 축을 실제로 밀고 있다" 의 문턱이다.
	const bool bUp    = Input.Y >  0.4f;
	const bool bDown  = Input.Y < -0.4f;
	const bool bLeft  = Input.X < -0.4f;
	const bool bRight = Input.X >  0.4f;
	const float Diag = UE_INV_SQRT_2;
	if (bUp   && bLeft)  { OutDir = FVector2D(-Diag,  Diag); return ClimbAnimSet->ClimbMontageUpLeft; }
	if (bUp   && bRight) { OutDir = FVector2D( Diag,  Diag); return ClimbAnimSet->ClimbMontageUpRight; }
	if (bDown && bLeft)  { OutDir = FVector2D(-Diag, -Diag); return ClimbAnimSet->ClimbMontageDownLeft; }
	if (bDown && bRight) { OutDir = FVector2D( Diag, -Diag); return ClimbAnimSet->ClimbMontageDownRight; }
	if (bUp)    { OutDir = FVector2D( 0.0f,  1.0f); return ClimbAnimSet->ClimbMontageUp; }
	if (bDown)  { OutDir = FVector2D( 0.0f, -1.0f); return ClimbAnimSet->ClimbMontageDown; }
	if (bLeft)  { OutDir = FVector2D(-1.0f,  0.0f); return ClimbAnimSet->ClimbMontageLeft; }
	if (bRight) { OutDir = FVector2D( 1.0f,  0.0f); return ClimbAnimSet->ClimbMontageRight; }
	return nullptr; // 여기까지 오면 입력이 문턱을 못 넘은 것이다(대각도 아니고 축도 아닌 애매한 값).
}

FVector2D UVBTraversalComponent::TickClimbMontage(const FVector2D& Input)
{
	// 걸기·자기섹션 루프·끊기는 DriveLoopingMontage 로 뺐다(2026-09-19, 사다리가 세 번째가 되어).
	//   여기 남는 것은 등반만의 부기다 - 8방향 선택과 방향 전환 유지 시간.

	USkeletalMeshComponent* Mesh = OwnerCharacter.IsValid() ? OwnerCharacter->GetMesh() : nullptr;
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Anim)
	{
		ActiveClimbMontage = nullptr;
		ActiveClimbDir = FVector2D::ZeroVector;
		return FVector2D::ZeroVector;
	}
	FVector2D WantedDir = FVector2D::ZeroVector;
	UAnimMontage* Wanted = PickClimbMontage(Input, WantedDir);
	if (!Wanted)
	{
		if (ActiveClimbMontage)
		{
			Anim->Montage_Stop(0.15f, ActiveClimbMontage);
			ActiveClimbMontage = nullptr;
		}
		ActiveClimbDir = FVector2D::ZeroVector;
		ClimbPendingMontage = nullptr;
		return FVector2D::ZeroVector;
	}
	// 방향이 바뀌었으면 잠깐 유지되는지 보고 나서 갈아끼운다.
	//   실측(2026-09-19): 키보드로 W 에서 S 로 갈 때 중간 구획을 스치며 1초에 여섯 번 바뀌었고
	//   (15ms / 95ms / 109ms 간격 포함) 바뀔 때마다 블렌드가 새로 돌아 손발이 튀었다.
	//   대기에서 처음 거는 것에는 안 걸린다 - 입력이 늦게 먹는 것으로 느껴지기 때문이다.
	if (ActiveClimbMontage && Wanted != ActiveClimbMontage && ClimbAnimSet)
	{
		const double Now = GetWorld()->GetTimeSeconds();
		if (ClimbPendingMontage != Wanted)
		{
			ClimbPendingMontage = Wanted;
			ClimbPendingSince = Now;
		}
		if (Now - ClimbPendingSince < ClimbAnimSet->ClimbDirectionHoldTime)
		{
			return ActiveClimbDir; // 아직 안 바꾼다. 돌고 있는 것의 방향을 그대로 돌려준다
		}
	}
	ClimbPendingMontage = nullptr;
	bool bClimbStarted = false;
	ActiveClimbMontage = DriveLoopingMontage(Anim, Wanted, ActiveClimbMontage, 0.15f, bClimbStarted);
	if (bClimbStarted)
	{
		VB_LOG(Log, "Climb 몽타주: %s 입력(%.2f,%.2f)", *Wanted->GetName(), Input.X, Input.Y);
	}
	ActiveClimbDir = WantedDir;
	return WantedDir;
}
