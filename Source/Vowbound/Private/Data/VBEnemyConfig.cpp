// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "Data/VBEnemyConfig.h"
#include "Animation/AnimMontage.h" // IsDataValid - 사망 몽타주 길이

// 접근 시간 식의 홈. 에디터 전용이 아니다 - 단위 테스트(비에디터 빌드 포함)도 이 함수를 부른다.
float UVBEnemyConfig::GetNormalApproachSeconds() const
{
	const float ApproachDistance = SightRadius - AttackCommitDistance;
	// 마지막 구간은 걷는다. 뛰기에서 걸음으로 떨어지는 지점이 RunExitDistance 이므로 그것이 걷는 구간이다.
	const float WalkLeg = FMath::Clamp(RunExitDistance, 0.0f, FMath::Max(0.0f, ApproachDistance));
	return (ApproachDistance - WalkLeg) / FMath::Max(1.0f, RunSpeed) + WalkLeg / FMath::Max(1.0f, WalkSpeed);
}

#if WITH_EDITOR
#include "Misc/DataValidation.h"   // FDataValidationContext::AddError
#include "Data/VBEnemyAnimSet.h"   // 걸음 속도를 대조할 상대 - AnimSet->Locomotion
#include "Animation/BlendSpace.h"  // GetBlendParameter / GetBlendSamples (UE 5.8 엔진 헤더에서 확인)
#include "Data/VBDeathDissolveConfig.h" // IsDataValid - 디졸브 길이가 정리 지연 안에 드는지
#include "Data/VBDeathDissolveSettings.h" // ResolveConfig - 비어 있으면 프로젝트 기본값
#include "Narrative/Story/VBRuleTypes.h"  // OnKilledEffects 칸 검사
#define LOCTEXT_NAMESPACE "VBEnemyConfig"

EDataValidationResult UVBEnemyConfig::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// 태그가 비면 UVBEnemyCombatComponent::IsReadyToAttack 이 조용히 false 를 돌려주고
	// 그 적은 영원히 공격 후보가 되지 않는다. 역할이 Surrounder 로 고정되어 BT 의 공격 분기에
	// 도달조차 못 하므로 플레이 중에는 "링만 돌고 안 때린다" 말고 단서가 없다.
	// 런타임 부팅 검증(ValidateAttackAbilityContract)과 중복이 아니다 - 이쪽은 저작 시점이고
	// 저쪽은 어빌리티 부여 결과와의 대조라, 볼 수 있는 범위가 다르다.
	if (!AttackAbilityInputTag.IsValid())
	{
		Context.AddError(LOCTEXT("VBEnemy_NoAttackInputTag",
			"AttackAbilityInputTag 가 비어 있습니다. 이 자산을 쓰는 적은 공격 토큰 후보가 되지 않아 "
			"영원히 공격하지 않습니다(역할이 Surrounder 로 고정됩니다)."));
		Result = EDataValidationResult::Invalid;
	}

	// 슈퍼아머 반격은 태그와 임계가 짝이다. 한쪽만 있으면 «반격이 있는 줄 알았는데 영영 안 나온다» 가 되고 로그는 없다.
	if (ArmoredCounterAttackInputTag.IsValid() != (ArmoredCounterAfterHits > 0))
	{
		Context.AddError(LOCTEXT("VBEnemy_ArmoredCounterHalf",
			"슈퍼아머 반격은 ArmoredCounterAttackInputTag 와 ArmoredCounterAfterHits(>0) 가 함께 있어야 합니다. "
			"한쪽만 저작되면 반격이 영영 나오지 않습니다."));
		Result = EDataValidationResult::Invalid;
	}

	// 클립 묶음이 비면 참조 포즈(T 포즈)로 선다. 2026-09-07 감염체까지 ABP_Enemy_Base 로 옮겨 모든 적이 이 값을 쓰므로
	// 경고가 아니라 에러다(design .sw/enemy-anim/design.md §5 의 예고대로 올렸다).
	if (!AnimSet)
	{
		Context.AddError(LOCTEXT("VBEnemy_NoAnimSet",
			"AnimSet 이 비어 있습니다. 모든 적이 ABP_Enemy_Base 를 쓰므로 이 적은 참조 포즈로 서게 됩니다."));
		Result = EDataValidationResult::Invalid;
	}

	// 히스테리시스가 성립하려면 Release > Commit 이어야 한다(헤더가 선언한 계약).
	// 뒤집히거나 같으면 역할이 재계산 주기마다 왕복하는 발진기가 되고 로그는 한 줄도 안 남는다.
	if (AttackReleaseDistance <= AttackCommitDistance)
	{
		Context.AddError(FText::Format(LOCTEXT("VBEnemy_BadHysteresis",
			"AttackReleaseDistance({0}) 는 AttackCommitDistance({1}) 보다 커야 합니다. "
			"같거나 작으면 플레이어가 경계선에서 스트레이프할 때 역할이 매 재계산 주기마다 뒤집힙니다."),
			FText::AsNumber(AttackReleaseDistance), FText::AsNumber(AttackCommitDistance)));
		Result = EDataValidationResult::Invalid;
	}

	// 대기 거리가 공격 유지 거리보다 안쪽이면 대기자와 공격자가 같은 거리에 서서
	// "누가 지금 위협인가"를 거리로 구분할 수 없게 된다.
	// 이 자산 안에서 완결되는 검사다 - 종전에는 전역 SurroundRadius 와 대조했으나,
	//   대기 거리가 이 자산의 사거리에서 파생되면서 다른 자산을 볼 이유가 없어졌다.
	//
	// 이 검사가 보지 못하는 항이 하나 있다. 엔진의 실효 도착 반경이다.
	//   UPathFollowingComponent::HasReachedInternal 이
	//   UseRadius = AcceptableRadius + GoalRadius + AgentRadius x MinAgentRadiusPct(1.1) 로 재므로,
	//   대기자는 목표점에서 그만큼 안쪽에 멈춘다. AcceptableRadius 는 BT 자산에, AgentRadius 는 BP 캡슐에
	//   있어 이 자산이 볼 수 없다. 그래서 여기서는 필요조건만 검사한다.
	//   실효 안착 거리는 코디네이터의 "슬롯 배정" 로그가 런타임에 보여준다.
	const float WaitingDistance = AttackCommitDistance + WaitingStandoff;
	if (WaitingDistance <= AttackReleaseDistance)
	{
		Context.AddError(FText::Format(LOCTEXT("VBEnemy_WaitingInsideAttackRange",
			"대기 거리(AttackCommitDistance {0} + WaitingStandoff {1} = {2}) 가 "
			"AttackReleaseDistance({3}) 보다 커야 합니다. "
			"작으면 대기하는 적이 공격 사거리 안에 서서 공격자와 구분되지 않고, "
			"역할이 매 재계산 주기마다 뒤집힙니다. "
			"(엔진의 실효 도착 반경 때문에 실제 대기 거리는 이보다 더 안쪽입니다 - 여유를 충분히 두십시오.)"),
			FText::AsNumber(AttackCommitDistance), FText::AsNumber(WaitingStandoff),
			FText::AsNumber(WaitingDistance),      FText::AsNumber(AttackReleaseDistance)));
		Result = EDataValidationResult::Invalid;
	}

	// === 걸음 (design .sw/enemy-gait/design.md §5) ===
	// 왜 여기서 남의 자산을 여는가: 이 값들이 맞아야 하는 상대가 이 적의 클립이기 때문이다.
	//   설정끼리만 비교하는 검증기는 장식이다(2026-08-30) - 그래서 축 최대와 샘플 좌표를 실제로 읽는다.
	//   이 대조가 없어서 2026-09-07 까지 단일 300 이 병사에게는 조깅, 감염체에게는 걷기였고 아무도 안 말했다.
	if (WalkSpeed >= RunSpeed)
	{
		Context.AddError(FText::Format(LOCTEXT("VBEnemy_GaitOrder",
			"WalkSpeed({0}) 는 RunSpeed({1}) 보다 작아야 합니다. 같거나 크면 둘러쌀 때와 쫓아올 때의 "
			"걸음이 구분되지 않습니다."),
			FText::AsNumber(WalkSpeed), FText::AsNumber(RunSpeed)));
		Result = EDataValidationResult::Invalid;
	}

	// 진입 > 이탈 이어야 히스테리시스가 성립한다. 같거나 뒤집히면 경계에서 걸음이 매 주기 떨린다
	//   (역할 히스테리시스 검사와 같은 모양).
	if (RunExitDistance >= RunEnterDistance)
	{
		Context.AddError(FText::Format(LOCTEXT("VBEnemy_GaitHysteresis",
			"RunExitDistance({0}) 는 RunEnterDistance({1}) 보다 작아야 합니다. 같거나 크면 경계에서 "
			"걸음이 매 주기 뒤집혀 다리가 떨립니다."),
			FText::AsNumber(RunExitDistance), FText::AsNumber(RunEnterDistance)));
		Result = EDataValidationResult::Invalid;
	}

	if (AnimSet && AnimSet->Locomotion)
	{
		const UBlendSpace* Locomotion = AnimSet->Locomotion;

		// 축 1(Y)이 속도라는 것은 UVBEnemyAnimSet 이 선언한 계약이다(X 방향 / Y 속도).
		// 그 계약이 깨진 자산이면 아래 최대값 비교가 먼저 걸린다 - 1D 블렌드스페이스의 미사용 축은 0..100 이라
		// 사람 걸음 속도가 반드시 이 검사에 걸린다. 즉 조용히 통과하는 경우가 없다.
		const FBlendParameter& SpeedAxis = Locomotion->GetBlendParameter(1);

		if (RunSpeed > SpeedAxis.Max)
		{
			Context.AddError(FText::Format(LOCTEXT("VBEnemy_RunSpeedOverAxis",
				"RunSpeed({0}) 가 {1} 의 속도 축 최대({2}) 를 넘습니다. 넘는 만큼은 축에서 잘려 "
				"다리는 그대로인 채 몸만 빨라집니다(발 미끄러짐). 더 빠른 클립을 블렌드스페이스에 "
				"한 줄 더 넣거나 RunSpeed 를 낮추십시오."),
				FText::AsNumber(RunSpeed), FText::FromString(Locomotion->GetName()),
				FText::AsNumber(SpeedAxis.Max)));
			Result = EDataValidationResult::Invalid;
		}

		// 샘플에서 먼 속도는 «없는 걸음» 이다 - 옆 샘플과 섞여 어중간한 보폭이 나온다.
		// 에러가 아니라 경고인 이유: 두 샘플 사이 값이 의도인 경우가 있다(감염체 뛰기 500 = 걷기 300 과
		//   조깅 600 의 사이). 막을 수 있는 것(축을 넘음)은 막고, 판단이 갈리는 것은 말만 한다(규칙 27).
		auto HasSampleNear = [Locomotion](float Speed)
		{
			const float Tolerance = FMath::Max(Speed * 0.15f, 10.0f);
			for (const FBlendSample& Sample : Locomotion->GetBlendSamples())
			{
				if (FMath::Abs(static_cast<float>(Sample.SampleValue.Y) - Speed) <= Tolerance)
				{
					return true;
				}
			}
			return false;
		};

		if (!HasSampleNear(WalkSpeed))
		{
			Context.AddWarning(FText::Format(LOCTEXT("VBEnemy_WalkSpeedOffSample",
				"WalkSpeed({0}) 근처(±15%) 에 {1} 의 샘플이 없습니다. 이 속도에서는 옆 샘플이 섞여 "
				"보폭이 실제 이동과 어긋납니다."),
				FText::AsNumber(WalkSpeed), FText::FromString(Locomotion->GetName())));
		}
		if (!HasSampleNear(RunSpeed))
		{
			Context.AddWarning(FText::Format(LOCTEXT("VBEnemy_RunSpeedOffSample",
				"RunSpeed({0}) 근처(±15%) 에 {1} 의 샘플이 없습니다. 두 샘플 사이를 의도한 값이면 "
				"그대로 두십시오."),
				FText::AsNumber(RunSpeed), FText::FromString(Locomotion->GetName())));
		}
	}

	// 접근 상한이 정상 접근 시간보다 짧으면 이 아키타입의 모든 적이 붙기 전에 토큰을 뺏긴다.
	//   증상은 «계속 다가오기만 하고 안 때린다» 뿐이고 로그는 정상 재배분으로 찍혀 결함처럼 안 보인다.
	// 종전에는 이 부등식을 단위 테스트가 C++ 디폴트에 대해서만 봤다. 걸음이 둘로 갈리면서
	//   느린 걷기를 쓰는 아키타입(병사 92)에서 값이 빠듯해져, 저작된 자산도 볼 필요가 생겼다.
	// 두 단으로 나눈다 - 막을 수 있는 것은 막고, 판단이 갈리는 것은 말만 한다(규칙 27).
	//   식 자체를 넘으면 그 아키타입은 «반드시» 붙기 전에 토큰을 뺏긴다 = 에러.
	//   식의 1.25배 안쪽이면 «빠듯하다» 일 뿐이다 = 경고. 식이 직선·즉시가속·정지대상을 가정하므로
	//   실제 접근은 반드시 더 걸리는데, 그 초과분은 내비 경로와 플레이어 움직임에 달려 못 잰다.
	//   딱 맞게 두면 검사는 초록인데 실전에서 회수가 나고 로그는 정상 재배분으로 찍혀 안 보인다
	//   (2026-09-07 /verify 지적, feedback_safety_net_must_not_lie).
	const float NormalApproachSeconds = GetNormalApproachSeconds();
	if (MaxApproachHoldSeconds <= NormalApproachSeconds)
	{
		Context.AddError(FText::Format(LOCTEXT("VBEnemy_ApproachBudgetTooShort",
			"MaxApproachHoldSeconds({0}) 가 정상 접근 시간({1}초) 보다 짧습니다. "
			"시야 끝({2})에서 사거리({3})까지 뛰기 {4} / 걷기 {5} 로 오는 데 그만큼 걸리므로, "
			"이 아키타입의 적은 붙기 전에 매번 토큰을 뺏깁니다(로그는 정상 재배분으로 찍힙니다)."),
			FText::AsNumber(MaxApproachHoldSeconds), FText::AsNumber(NormalApproachSeconds),
			FText::AsNumber(SightRadius),            FText::AsNumber(AttackCommitDistance),
			FText::AsNumber(RunSpeed),               FText::AsNumber(WalkSpeed)));
		Result = EDataValidationResult::Invalid;
	}
	else if (MaxApproachHoldSeconds <= NormalApproachSeconds * 1.25f)
	{
		Context.AddWarning(FText::Format(LOCTEXT("VBEnemy_ApproachBudgetTight",
			"MaxApproachHoldSeconds({0}) 가 정상 접근 시간({1}초) 의 1.25배 안쪽입니다. "
			"이 식은 직선·즉시가속·정지한 대상을 가정하므로 실제 접근은 반드시 더 걸립니다 - "
			"내비 우회나 후퇴하는 플레이어에서 토큰 회수가 납니다."),
			FText::AsNumber(MaxApproachHoldSeconds), FText::AsNumber(NormalApproachSeconds)));
	}

	// 사망 연출이 정리 지연보다 길면 시체가 연출 도중에 사라진다.
	//   이 검사가 없으면 화면에서는 «죽는 모션이 끊긴다» 로 보이고, 원인이 몽타주가 아니라 타이머에 있어 추적이 길다.
	//   재생배율은 사망 경로가 1.0 고정이므로(AVBEnemyBase::HandleDeathCosmetic) 길이를 그대로 비교한다.
	// 디졸브까지 더해서 재는 이유: 그 연출은 정리 시각에 «끝나도록» 시작을 역산한다
	//   (UVBDeathDissolveComponent::ScheduleDissolve). 그래서 몽타주가 쓰고 남은 시간이 디졸브 길이보다
	//   짧으면 런타임이 디졸브를 잘라 넣고, 화면에서는 몸이 반쯤 깎이다 사라진다.
	// 여기서 ResolveConfig 를 거치는 이유: 이 필드는 «보통 비어 있다». 비면 프로젝트 기본값
	//   (UVBDeathDissolveSettings::DefaultConfig)이 걸리고, 그게 몬스터마다 배선하지 않아도
	//   전부 디졸브가 도는 구조의 핵심이다. 그래서 날것으로 읽으면 흔한 쪽이 0 이 되어
	//   검사가 초록을 내주고 런타임만 잘린다 - 검사가 «안 재고 통과» 하는 자리가 된다.
	// 2026-09-23 /verify 적발: 폴백을 ScheduleDissolve 와 시험 버튼에만 넣고 이 세 번째 읽는
	//   자리를 빠뜨렸다. 그 함수의 주석이 바로 그 실패를 경고하고 있었는데도 같은 모양이 났다.
	const UVBDeathDissolveConfig* EffectiveDissolve = UVBDeathDissolveSettings::ResolveConfig(DeathDissolve);
	const float DissolveSeconds = EffectiveDissolve ? FMath::Max(0.0f, EffectiveDissolve->Duration) : 0.0f;

	for (const TObjectPtr<UAnimMontage>& DeathMontage : DeathMontages)
	{
		if (!DeathMontage)
		{
			Context.AddError(LOCTEXT("VBEnemy_DeathMontageNull",
				"DeathMontages 에 빈 칸이 있습니다. 빈 칸이 뽑히면 그 죽음만 연출 없이 래그돌로 떨어져 «가끔 죽는 모션이 안 나온다» 가 됩니다."));
			Result = EDataValidationResult::Invalid;
			continue;
		}
		const float MontageLength = DeathMontage->GetPlayLength();
		if (MontageLength + DissolveSeconds > DeathCleanupDelay)
		{
			Context.AddError(FText::Format(LOCTEXT("VBEnemy_DeathCleanupTooShort",
				"DeathCleanupDelay({0}초) 가 사망 몽타주 {1}({2}초) + 디졸브({3}초) 보다 짧습니다. "
				"시체가 연출 도중에 사라집니다 - 정리 지연을 그 둘의 합보다 길게 잡으세요."),
				FText::AsNumber(DeathCleanupDelay), FText::FromString(DeathMontage->GetName()),
				FText::AsNumber(MontageLength), FText::AsNumber(DissolveSeconds)));
			Result = EDataValidationResult::Invalid;
		}
	}

	// 몽타주가 하나도 없는 아키타입(잡몹 셋)은 죽는 즉시 래그돌이므로 위 루프가 한 번도 안 돈다.
	//   그 경우에도 디졸브는 그대로 도니 따로 잰다 - 루프 안에만 두면 이 조합이 조용히 검사를 빠져나간다.
	if (DeathMontages.Num() == 0 && DissolveSeconds > DeathCleanupDelay)
	{
		Context.AddError(FText::Format(LOCTEXT("VBEnemy_DeathCleanupShorterThanDissolve",
			"DeathCleanupDelay({0}초) 가 디졸브 길이({1}초) 보다 짧습니다. "
			"몸이 다 사라지기 전에 액터가 제거되어 시체가 툭 없어집니다."),
			FText::AsNumber(DeathCleanupDelay), FText::AsNumber(DissolveSeconds)));
		Result = EDataValidationResult::Invalid;
	}

	// 처치 효과의 빈 칸·엉뚱한 구조체는 실행 때 조용히 건너뛰어 «드롭이 안 나온다» 로만 보인다 - 저작 때 잡는다.
	if (!VBRules::ValidateStructArray(OnKilledEffects, FVBEffect::StaticStruct(), LOCTEXT("VBEnemy_OnKilledEffectsWhere", "OnKilledEffects"), Context))
	{
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
