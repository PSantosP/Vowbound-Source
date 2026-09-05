// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "Data/VBEnemyConfig.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"   // FDataValidationContext::AddError
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

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
