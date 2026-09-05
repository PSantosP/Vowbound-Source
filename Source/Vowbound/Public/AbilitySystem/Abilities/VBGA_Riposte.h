// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGA_MeleeAttackBase.h"
#include "VBGA_Riposte.generated.h"

/**
 * 리포스트 — 패링 성공 직후 열리는 반격 (DEC-009 phase-2).
 *
 * 활성 조건: State.Combat.RiposteWindow 보유. 그 태그는 패링이 성립한 순간에만
 * AVBCharacter::OpenRiposteWindow 가 붙이고 UVBParryConfig::RiposteWindow 뒤에 사라진다.
 *
 * 왜 경공격과 같은 입력을 써도 안전한가: UVBGA_LightAttack 은 State.Combat.Guarding 을
 * ActivationBlockedTags 로 갖는다(VBGA_LightAttack.cpp:30). 반격 창은 가드 유지 중에만
 * 열리므로 두 능력의 활성 조건이 겹치지 않는다 — 어느 쪽이 뜰지는 태그가 정한다.
 *
 * 단, 그것만으로는 부족했다: AVBCharacter::HandleAbilityInputPressed 가 같은 InputTag 의
 * 첫 후보만 시도하고 성공 여부와 무관하게 끊고 있었다. 경공격이 게이트에 막혀 실패해도
 * 반격까지 내려오지 않았다(2026-07-29 실측). 디스패처가 실패 시 다음 후보를 잇도록 고쳤다.
 * 하나의 입력에 상호배타적 능력을 매다는 설계는 그 순회가 있어야 성립한다.
 *
 * 왜 별도 GA 인가: 몽타주·데미지·히트 판정을 UVBGA_MeleeAttackBase 에서 그대로 물려받으면서
 * 게이트만 다르게 두면 되기 때문. 경공격 GA 에 분기를 심으면 "가드 중 공격 금지"라는
 * 단일 규칙이 조건부가 되어 FIND-054 가 되살아난다.
 *
 * 코드가 ctor 뿐인 이유: 무기별 반격 몽타주는 베이스가 MovesetConfig 로 이미 해석한다.
 * 리포스트 전용 MovesetConfig(DA)를 CDO 에 물리면 override 가 한 줄도 필요 없다 —
 * 무기 확장이 코드가 아니라 데이터로 끝난다(config 데이터 구조 표준).
 */
UCLASS()
class VOWBOUND_API UVBGA_Riposte : public UVBGA_MeleeAttackBase
{
	GENERATED_BODY()

public:
	UVBGA_Riposte();
};
