// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Traversal/VBTraversalComponent.h"

#include "Traversal/VBLevelBlock_Traversable.h"
#include "Animation/VBAnimInstance.h"   // UVBAnimInstance::InteractionTransform 직접 설정 (ABP 의 BPI_InteractionTransform 백킹 변수)
#include "Data/VBTraversalConfig.h"
#include "Character/VBCharacter.h"
#include "Character/VBWeaponStateComponent.h"
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
}

UVBTraversalComponent::UVBTraversalComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
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

	// Step 1-3: 트레이스. (공중 모드는 전방 트레이스가 AirMantleTraceDistance 고정 단거리)
	FVBTraversalCheckResult Result;
	if (!PerformTraversalCheck(Result, bAirMantleOnly))
	{
		return false;
	}

	// Step 4.1: ActionType 결정.
	Result.ActionType = DecideActionType(Result);
	if (Result.ActionType == EVBTraversalActionType::None)
	{
		return false;
	}

	// 공중 모드(Vowbound 확장, user 승인 2026-06-04) — 두 가지 추가 게이트:
	// ① Mantle(/Climb)만 허용. Vault/Hurdle 은 지상 도약 전제 애니라 공중 발동 시 부자연 + 제어 강탈.
	// ② 렛지가 발(캡슐 하단)보다 MantleHeightMin 이상 "위"일 때만. DecideActionType 의 H 는
	//    |렛지Z−발Z| (GASP abs 그대로)라 공중에선 렛지가 발 아래(=자연 착지로 충분)여도 임계를 넘어
	//    "아래로 끌려가는 맨틀"이 발동할 수 있음 — 낮은 벽을 점프로 그냥 넘는 경우를 부호 있는 높이로 보호.
	if (bAirMantleOnly)
	{
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
		if (TraceFloorBelowLedge(Result.FrontLedgeLocation, Result.FrontLedgeNormal, GetConfig()->MantleHeightMax + GetConfig()->FrontFloorProbeExtraDepth, FrontFloorHit))
		{
			ChooserObstacleHeight = FMath::Abs(Result.FrontLedgeLocation.Z - FrontFloorHit.ImpactPoint.Z);
		}
		else
		{
			// 바닥 미발견(절벽 가장자리/높은 단차 위에서 점프 등) → 벽이 사실상 그 이상으로 높음 —
			// 최댓값으로 클램프해 가장 묵직한 Climb 계열이 선택되게 한다.
			ChooserObstacleHeight = GetConfig()->MantleHeightMax;
		}
	}

	// Step 5.3: CHT_VB_TraversalAnims 평가(struct 컨텍스트) → 후보 몽타주 배열.
	FVBTraversalChooserParams Params;
	Params.ActionType = Result.ActionType;
	Params.ObstacleHeight = ChooserObstacleHeight;
	Params.ObstacleDepth = Result.ObstacleDepth;
	Params.Speed = OwnerCharacter->GetVelocity().Size2D();
	// Gait: Mantle chooser는 Speed만 사용. Hurdle/Vault는 추후 실제 Gait 연동.
	Params.Gait = EVBGait::Walk;

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
	LastActionType = Result.ActionType; // 복구 시 MovementMode 분기(Vault→Falling)용.

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

	USkeletalMeshComponent* Mesh = OwnerCharacter->GetMesh();
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (Anim)
	{
		// 다른 클라도 FromTraversal 블렌드/채널용으로 InteractionTransform 보유.
		if (UVBAnimInstance* VBAnim = Cast<UVBAnimInstance>(Anim))
		{
			VBAnim->InteractionTransform = FTransform(FRotationMatrix::MakeFromZ(Result.FrontLedgeNormal).Rotator(), Result.FrontLedgeLocation);
		}
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
	const FRotator FrontRot = FRotationMatrix::MakeFromX(-Result.FrontLedgeNormal).Rotator();
	MW->AddOrUpdateWarpTargetFromLocationAndRotation(WT_FrontLedge, Result.FrontLedgeLocation, FrontRot);

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

	if (OwnerCharacter.IsValid())
	{
		// 파쿠르 종료 → 무기 복원 (temp-sheathe 했던 경우만; RestoreFromTempSheathe 가 가드 + CombatTimer 재개).
		if (UVBWeaponStateComponent* WSC = OwnerCharacter->GetWeaponStateComponent())
		{
			WSC->RestoreFromTempSheathe();
		}
		if (UCharacterMovementComponent* CMC = OwnerCharacter->GetCharacterMovement())
		{
			// GASP Select 핀 리터럴(CBP uasset 바이너리 추출 2026-06-04): Vault→Falling(얇은 벽 너머로
			// 낙하 중 종료 — 중력 인계), None/Hurdle/Mantle→Walking.
			CMC->SetMovementMode(LastActionType == EVBTraversalActionType::Vault ? MOVE_Falling : MOVE_Walking);
		}
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
