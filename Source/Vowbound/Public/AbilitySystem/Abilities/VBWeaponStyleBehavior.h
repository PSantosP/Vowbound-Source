// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VBWeaponStyleBehavior.generated.h"

class UVBGA_MeleeAttackBase;

/**
 * UVBWeaponStyleBehavior
 *
 * 무기 스타일 고유 동작 전략(Strategy). UVBAttackConfig(무브셋 leaf)에 인라인 인스턴스로 선택 연결되어,
 * 공격 GA(UVBGA_MeleeAttackBase)가 공격 라이프사이클에서 호출한다.
 *
 * 의도:
 *  - 데이터(GE/수치)로 표현 불가한 스타일 고유 메커닉만 여기서 코드로 구현 (개방-폐쇄 원칙)
 *  - 대표 예: 마법 스탠스의 투사체 spawn (근접 SphereTrace 대신) — Phase 4의 UVBProjectileAttackBehavior
 *
 * 전제:
 *  - EditInlineNew + DefaultToInstanced: UVBAttackConfig DataAsset 안에 인라인 인스턴스로 편집
 *  - null 이면 엔진은 기본 동작(SphereTrace) 수행 → 멜리 3종(주먹/카타나/거대검)은 Behavior 불필요
 *
 * 부작용:
 *  - 파생 구현에 따라 액터 spawn / GE 적용 등 (구현부 책임)
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, DefaultToInstanced)
class VOWBOUND_API UVBWeaponStyleBehavior : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * DeliverHit
	 *
	 * AttackTrace AnimNotify 시점의 "타격 전달" 훅. 엔진의 기본 SphereTrace를 대체할 수 있다.
	 *
	 * @param Ability 호출한 공격 GA (ActiveConfig/아바타 접근용)
	 * @return true  = 이 Behavior가 타격을 처리함 → 엔진은 기본 PerformAttackTrace 스킵
	 *         false = 미처리(기본값) → 엔진이 기본 SphereTrace 수행 (멜리)
	 *
	 * 전제: 서버 권위 컨텍스트에서만 호출됨 (AttackTrace 이벤트 Task가 서버 전용이므로).
	 */
	virtual bool DeliverHit(UVBGA_MeleeAttackBase* Ability) { return false; }
};
