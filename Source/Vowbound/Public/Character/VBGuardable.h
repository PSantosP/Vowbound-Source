// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "VBGuardable.generated.h"

class UVBParryConfig;

/**
 * 가드/패링을 할 수 있는 액터 계약 (FIND-057a, 2026-07-26).
 *
 * 왜 인터페이스인가:
 *  데미지 hub(UVBHealthAttributeSet::PostGameplayEffectExecute)가 가드 판정을 위해 구체 클래스 AVBCharacter 를
 *  include 하고 Cast 하고 있었다 — verify-architecture module_separation 위반.
 *  (인용은 심볼로 한다. 라인 번호는 이 변경 자체가 밀어버린다 — 초판 주석이 적었던 :10/:86 은
 *   수정 직후 다른 코드가 됐다. 장수명 계약 주석에서 라인 인용 금지.)
 *  실질 파급은 문법 문제가 아니다: AVBEnemyBase 는 구조적으로 영원히 가드할 수 없었다. 적 방패/가드를
 *  넣으려면 데미지 hub 를 다시 써야 했다. 락온이 이미 같은 문제를 IVBTargetable 로 푼 선례가 있다
 *  (Character/VBTargetable.h — reuse-catalog §3 "never Cast<AVBEnemyBase>").
 *
 * 부수 이득(설계 옵션 보존):
 *  나중에 가드 상태를 UVBGuardComponent 로 옮겨도 hub 코드는 한 글자도 바뀌지 않는다 — 액터가 인터페이스를
 *  구현한 채 컴포넌트에 위임만 하면 된다. 즉 "컴포넌트로 뺄지"를 무비용으로 연기시켜 준다(YAGNI).
 *
 * 구현체 계약(반드시 지킬 것):
 *  1. GetParryConfig() 는 null 을 반환하지 않는다. 미할당이면 CDO 를 돌려준다
 *     (AVBCharacter::GetParryConfig — CDO 폴백). 그래서 소비처의 null 폴백은 도달 불가
 *     죽은 코드가 되며, hub 는 폴백 대신 ensure 로 이 계약을 못박는다.
 *  2. IsGuarding()/GetGuardElapsed() 는 서버 권위 상태를 읽는다. 판정이 서버(PostGameplayEffectExecute)에서만
 *     일어나므로 클라 값은 의미가 없다(AVBCharacter::GuardStartServerTime 은 비복제).
 *  3. GetGuardElapsed() 는 비가드 시 아주 큰 값을 돌려준다(패링 윈도우 밖 취급) — hub 가 별도 분기하지 않게.
 *
 * 일부러 넣지 않은 것: BeginGuard/EndGuard. 소비처가 UVBGA_Guard 한 곳뿐이고 그 GA 는 WSC 무기 게이트로
 *  AVBCharacter 에 묶여 있다(UVBGA_Guard::ExecuteAbility — 2026-07-26 이후 게이트는 카타나 리터럴이 아니라
 *  UVBParryConfig::FindGuardSet 데이터 조회다). "적도 가드한다"가 실제 요구가 될 때 승격한다.
 *  가드 각도(phase-2)는 IsGuardingAgainst(Attacker) 를 이 인터페이스에 추가하는 방식으로 확장한다.
 */
UINTERFACE(MinimalAPI)
class UVBGuardable : public UInterface
{
	GENERATED_BODY()
};

class IVBGuardable
{
	GENERATED_BODY()

public:
	// 현재 가드 중인가 (서버 진실).
	virtual bool IsGuarding() const = 0;
	// 가드 개시 후 경과(초). 비가드면 매우 큰 값 → 항상 패링 윈도우 밖.
	virtual float GetGuardElapsed() const = 0;
	// 받아넘김(Accept) 연출. 데미지 hub 가 블록/패링 확정 시 호출.
	virtual void PlayGuardAccept() = 0;
	// 가드/패링 튜닝. null 이 아니다(미할당 시 CDO — 위 계약 1).
	virtual const UVBParryConfig* GetParryConfig() const = 0;

	// 패링 성공 시 반격 창을 연다. 타이머 소유자는 캐릭터다 — 데미지 hub(AttributeSet)는
	// 프레임 단위 계산기지 상태 수명을 관리하는 주체가 아니다.
	// Attacker: 패링당한 공격의 주체. 반격 창을 여는 김에 그쪽으로 자동 락온한다.
	// 왜 인자로 받나: hub 는 AVBCharacter 로 캐스트할 수 없어(FIND-057a) 락온 컴포넌트에 직접 못 닿는다.
	virtual void OpenRiposteWindow(float Duration, AActor* Attacker) = 0;

	// 가드 결과에 따른 스태미나 증감. 능력 코스트가 아니라 반응형이라 GE 가 아니다
	// (CommitAbility 경로와 무관 — GAS 규칙의 Cost/Cooldown 금지 조항에 해당하지 않는다).
	virtual void ApplyGuardStaminaDelta(float Delta) = 0;
};
