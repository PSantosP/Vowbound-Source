// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGameplayAbility.h"
#include "Data/VBMedicalConfig.h"
#include "VBGA_MedicalAbilityBase.generated.h"

/**
 * VBGA_MedicalAbilityBase
 *
 * 의료 행위(정화/처치) GA의 기초 클래스. Purify/Execute가 공통으로 사용.
 *
 * 의도:
 *  - 처형 창(State.Combat.FinisherWindow)에 들어간 적 탐색 + 즉사 데미지 + 명성 변화 파이프라인
 *  - Vowbound의 "의료 자원으로 싸우는" 핵심 테마를 GA로 구현
 *  - 파생(Purify/Execute)은 명성 증감 방향만 다름
 *
 * 전제:
 *  - MedicalConfig (UVBMedicalConfig) 할당
 *  - 대상이 «처형 창» 안에 있어야 한다. 판정은 State.Combat.FinisherWindow 태그 하나다 (2026-09-11 전환).
 *    그 태그의 단독 생산자는 UVBGA_FinisherWindow 이고, 창의 문턱(체력 비율)은 그쪽 설정 자산에 있다.
 *    여기서 체력을 다시 계산하지 않는 이유: 같은 숫자를 두 곳에서 읽으면 한쪽만 고친 날 조용히 어긋난다.
 *    (종전에는 이 클래스가 Health/MaxHealth 를 직접 계산했고, 창이라는 개념이 없어 30% 아래면 언제나 가능했다)
 *  - 소유자에게 UVBTargetLockComponent 가 있어야 대상을 찾을 수 있다. 대상 지정은
 *    «락온 대상 우선, 없으면 전방 원뿔» 이다 - 종전의 굵기 0 짜리 라인 트레이스는 2026-09-11 user PIE 에서
 *    「잘 안 맞는다」로 보고됐다.
 *  - NetExecutionPolicy=ServerInitiated (베이스 디폴트) — 클라에서도 ExecuteAbility 가 돌므로
 *    GE 적용 경로(PerformMedicalTrace)는 IsNetAuthority 가드 필수 (2026-06-04 docstring 정정)
 *
 * 부작용:
 *  - GE 적용 (즉사 데미지)
 *  - Reputation 변동 (Purify=+, Execute=-)
 *  - HitStop 재생
 */
UCLASS(Abstract)
class VOWBOUND_API UVBGA_MedicalAbilityBase : public UVBGameplayAbility
{
	GENERATED_BODY()
public:
	/**
	 * 대상이 없으면 아예 활성화되지 않게 한다.
	 *
	 * 왜 여기인가: 베이스가 ActivateAbility 에서 CommitAbility 를 «먼저» 부른다(VBGameplayAbility.cpp:86).
	 *   즉 대상이 있는지 알기 전에 비용과 쿨다운이 지불된다. 2026-09-11 user PIE 의
	 *   「한 번 못 맞추면 쿨타임이 있어」가 그것이다. 엔진은 CanActivateAbility 를 CommitAbility 보다
	 *   앞에서 부르므로 판정을 이리로 옮기면 헛발이 쿨다운을 태우지 않는다.
	 * 왜 파생이 아니라 베이스인가: 대상 규칙(FindMedicalTarget)이 이미 베이스의 것이고 Purify 와 Execute 가
	 *   그것을 공유한다. 판정만 파생으로 내리면 같은 규칙의 홈이 둘이 된다.
	 * 부작용 없음. HUD 가 「처형 가능」 표시에 쓸 수 있지만 매 프레임 폴링용으로 만든 것은 아니다 -
	 *   콘 탐색(SphereOverlapActors)이 들어 있다.
	 */
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

protected:
	virtual void ExecuteAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, 
		const FGameplayAbilityActivationInfo ActivationInfo, 
		const FGameplayEventData* TriggerEventData) override;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Medical")
	TObjectPtr<UVBMedicalConfig> MedicalConfig;

	// 지금 의료 행위를 걸 수 있는 대상. 없으면 nullptr.
	//
	// 규칙: 락온 대상이 자격을 갖췄으면 그것. 아니면 전방 원뿔에서 자격을 갖춘 최고점수.
	//   자격 = 처형 창이 열려 있고(State.Combat.FinisherWindow) MedicalConfig->MedicalRange 안에 있음.
	// 왜 락온이 먼저인가: 플레이어가 이미 「저 놈」이라고 말한 상태다. 그것을 무시하고 콘 점수가
	//   고른 다른 적에게 처형이 걸리면 조작이 배신당한다.
	// const 이고 부작용이 없다 - CanActivateAbility(활성화 전 판정)와 실제 적용이 같은 함수를 부른다.
	//   판정과 적용이 다른 함수를 쓰면 「눌리는데 안 걸리는」 상태가 생긴다.
	AActor* FindMedicalTarget(const FGameplayAbilityActorInfo* ActorInfo) const;

	void PerformMedicalTrace(const FGameplayAbilityActorInfo* ActorInfo);

	/**
	 * 이미 정해진 대상에게 의료 효과(피해 + 명성)를 적용한다.
	 *
	 * 왜 갈랐나: 처형 연출은 장면을 «걸 때» 대상을 정하고 «끝날 때» 피해를 준다. 그 사이 3.3초가 있다.
	 *   그 끝에서 대상을 다시 찾으면(종전 동작) 락온이 바뀌었거나 콘 안의 다른 적이 더 높은 점수를 받은 경우
	 *   처형 애니를 다 맞은 적은 살고 옆의 적이 죽는다. 시작할 때 잡은 액터에 그대로 적용해야 한다.
	 * 권위 가드는 이 함수가 든다 - 호출자가 둘이므로 한쪽만 막으면 뚫린다.
	 */
	void ApplyMedicalEffects(const FGameplayAbilityActorInfo* ActorInfo, AActor* Target);
};
