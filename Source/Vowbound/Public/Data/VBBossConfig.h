// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/VBEnemyConfig.h"
#include "GameplayTagContainer.h"
#include "VBBossConfig.generated.h"

class UAnimMontage;

/**
 * 보스 페이즈 하나의 정의 (design .sw/boss/design.md §11).
 * 무엇: «체력이 이 비율 아래로 내려오면 이 공격을 쓰고, 들어갈 때 이 몽타주를 튼다».
 * 첫 페이즈는 여기 없다 - 그것은 UVBEnemyConfig 자체(AttackAbilityInputTag, 전환 없음)다. 같은 태그를
 *   두 곳에 적으면 한쪽이 낡으므로 «첫 페이즈의 공격» 의 홈을 하나로 둔다.
 */
USTRUCT(BlueprintType)
struct FVBBossPhase
{
	GENERATED_BODY()

	// 이 비율 이하로 체력이 내려오면 이 페이즈에 들어간다(0 초과 1 미만, 현재/최대). 앞 페이즈보다 작아야 한다 - IsDataValid 가 본다.
	//   Clamp 도 같은 뜻이다 - 에디터가 받는 값과 검증기가 거부하는 값이 갈리면 안 된다.
	// 올라가도 되돌아가지 않는다. 페이즈는 «달라졌다» 를 보여주는 장치라 되돌리면 그 표시가 무효가 된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Boss", meta=(ClampMin="0.01", ClampMax="0.99"))
	float HealthRatioThreshold = 0.5f;

	// 이 페이즈에서 쓸 공격 GA 의 InputTag. AVBBossBase::GetCurrentAttackInputTag 가 돌려주고, 그 뒤는
	//   UVBEnemyCombatComponent::GetAttackInputTag 가 종전대로 처리한다(BT 태스크·코디네이터가 그 하나를 본다).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Boss", meta=(Categories="Input"))
	FGameplayTag AttackInputTag;

	// 페이즈에 들어갈 때 트는 몽타주(선택). 트는 동안 State.Combat.PhaseTransition 이 붙어 BT 가 이동·공격 분기를 닫는다.
	//   비면 전환 연출 없이 공격만 바뀐다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Boss")
	TObjectPtr<UAnimMontage> TransitionMontage = nullptr;

	// 전환 몽타주 동안 슈퍼아머(State.Combat.SuperArmor)를 줄지. 무적이 아니다 - 체력은 그대로 깎인다(«연출 중에도 때릴 수 있다», 설계 결정 2).
	// false 의 실제 뜻은 «연출을 사실상 생략» 이다: 문턱을 넘긴 바로 그 타격의 히트리액트가 같은 프레임에 이어서 오고
	//   (VBHealthAttributeSet.cpp::PostGameplayEffectExecute 가 SetHealth 뒤에 Event.Combat.Hit 를 쏜다), 히트리액트 GA 를 막는 것은
	//   SuperArmor 뿐이라 전환 몽타주가 그 자리에서 끊긴다. 히트리액트 GA 를 안 가진 아키타입(지금의 트롤)에서만 false 가 «맞으면서 연출» 이 된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Boss")
	bool bSuperArmorDuringTransition = true;
};

/**
 * 보스 튜닝 DataAsset. UVBEnemyConfig 에 페이즈 목록만 더한다.
 * 왜 상속인가: 보스도 걸음·시야·토큰·예고는 잡몹과 같은 홈을 쓴다. 다른 것은 «체력 문턱으로 갈리는 공격» 뿐이다.
 * 소비자: AVBBossBase (EnemyConfig 를 이 클래스로 캐스트해 읽는다. 아니면 페이즈 없는 잡몹처럼 행동하고 부팅 시 경고).
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBBossConfig : public UVBEnemyConfig
{
	GENERATED_BODY()

public:
	// 첫 페이즈 «다음» 의 페이즈들. [0] 이 두 번째 페이즈다. 문턱 내림차순으로 적는다.
	// AVBBossBase::CurrentPhaseIndex 와의 대응: 인덱스 0 = 첫 페이즈(이 자산 자체), N = AdditionalPhases[N-1].
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Boss")
	TArray<FVBBossPhase> AdditionalPhases;

#if WITH_EDITOR
	// 문턱 내림차순·(0,1) 범위·태그 유효를 본다. 셋 다 위반해도 런타임 로그가 없는 부류다 -
	//   문턱이 뒤집히면 «두 번째 페이즈를 건너뛴다» 로만 보이고, 태그가 비면 그 페이즈에서 영영 안 때린다.
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
