// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGameplayAbility.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBSwingCombatProfile.h" // FVBSwingCombatScales 를 값으로 반환 — 전방선언으로는 부족하다.
                                       //  AbilitySystem -> Data 는 이미 확립된 헤더 계약이라 새 간선이 아니다.
#include "VBGA_MeleeAttackBase.generated.h"

class UVBWeaponMovesetConfig;
class UVBSwingImpactProfile;

/**
 * VBGA_MeleeAttackBase
 *
 * 근접 공격 GA의 기초 클래스. 현재 파생은 LightAttack 하나(강공격은 DEC-009 로 폐지, RMB 는 가드로 이관).
 *
 * 의도:
 *  - 몽타주 재생 → AnimNotify 트리거 → Sphere Trace → GE 적용 파이프라인을 한 곳에 정의
 *  - 파생 GA는 파라미터(AttackConfig)와 ApplyAdditionalEffects 오버라이드만 신경 쓰면 됨
 *  - 콤보 시스템(ComboWindow*)과 통합
 *
 * 전제:
 *  - 파생 GA에 AttackConfig (UVBAttackConfig DataAsset) 할당 필수
 *  - 공격 몽타주에 VBAN_SendAttackTraceEvent AnimNotify 삽입 필수
 *  - NetExecutionPolicy=ServerInitiated (2026-04-20 전환 — 소유자 클라에 인스턴스 복제해서 로컬 몽타주 재생)
 *
 * 부작용:
 *  - CommitAbility → Cost/Cooldown GE 자동 적용
 *  - 몽타주 재생 + MotionWarping (Lock-On 시 타겟 방향 와핑)
 *  - SphereTrace 결과 각 대상에 Damage GE 적용 + PlayHitStop
 *  - 콤보 윈도우 중 입력 재수신 시 다음 콤보 인덱스로 전이
 */
UCLASS(Abstract)
class VOWBOUND_API UVBGA_MeleeAttackBase : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	UVBGA_MeleeAttackBase();

protected:
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	TObjectPtr<UVBAttackConfig> AttackConfig;

	// 무기 타입별 무브셋. 할당 시 현재 무기 타입으로 leaf 를 조회해 사용(강공격 폐지 후 leaf 는 Light 단일).
	// 미할당(null)이면 위 레거시 AttackConfig + GA의 AnimMontage 사용(회귀 안전 폴백).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	TObjectPtr<UVBWeaponMovesetConfig> MovesetConfig;

	virtual void ExecuteAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	// 종료 훅 override — 뚝심(SuperArmor) 로스태그 제거를 위해. 몽타주 종료 3경로(Completed/Interrupted/Cancelled)가
	//  전부 EndAbility로 수렴하므로 단일 제거점. (BlendOut 은 EndAbility 를 호출하지 않는다 — .cpp EndAbility 위 주석 참조.)
	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	// SphereTrace로 전방 대상 탐색 -> GE 적용
	// 현재 공격이 겨누는 대상. 하드락 우선, 없으면 소프트 런지 스캔.
	// 워프(SetWarpTargetLockedEnemy)와 판정(PerformAttackTrace)이 같은 대상을 봐야
	// "워프는 저쪽으로 붙는데 판정은 정면으로 나가는" 어긋남이 생기지 않는다.
	const USceneComponent* ResolveAimTargetComponent() const;

	void PerformAttackTrace();

	// 이번 스윙에 적용할 임팩트 쉐이크 프로필. 노티(스윙 입도) -> 공격 leaf(무기 입도) 2단 사다리.
	// 3단째 기본값을 두지 않는다 — 폴백 층이 늘수록 같은 값이 층마다 갈라진다
	//  (GetParryConfig() 의 CDO 폴백과 소비처 폴백이 갈라져 0.0 대 2.0 으로 이원화된 실제 사고가 근거).
	// 둘 다 비면 nullptr — 호출부가 큐를 쏘지 않고 Warning 을 남긴다.
	const UVBSwingImpactProfile* ResolveSwingImpactProfile() const;

	// 이번 스윙에 적용할 전투 수치 배율. 노티가 프로필을 실어 보냈으면 그 배율, 아니면 전부 1.0.
	// 위와 달리 nullptr 를 돌려주지 않고 값으로 반환하는 이유: 배율의 부재는 "값 없음"이 아니라
	//  "곱하지 않음"이고, 그건 항등원 1.0 으로 정확히 표현된다. 호출부에 null 분기가 생기지 않는다.
	// 1.0 은 3단째 폴백 층이 아니다 - 자산에도 leaf 에도 GA 에도 "기본 배율"을 저작할 자리가 없어
	//  갈라질 두 번째 홈이 만들어지지 않는다.
	FVBSwingCombatScales ResolveSwingCombatScales() const;

	// 적중 시 추가 효과. 베이스는 ActiveConfig->OnHitExtraEffects를 적용(데이터 기반).
	// 파생 GA 는 Super 호출 후 고유 효과를 확장한다(무기별 추가효과는 AttackConfig.OnHitExtraEffects 로 데이터화).
	virtual void ApplyAdditionalEffects(UAbilitySystemComponent* TargetASC, const FHitResult& Hit);

	// 스택형 추가효과가 한도에 도달했을 때의 폭발. 짝은 AttackConfig.StackFullEffects 가 정한다.
	// 엔진의 OverflowEffects 를 쓰지 않는 이유는 구현부 주석 참조(UE 5.8 회귀, FIND-081).
	void ApplyStackFullEffects(UAbilitySystemComponent* TargetASC, TSubclassOf<UGameplayEffect> StackingEffect);
	
	
	// AnimNotify에서 GameplayEvent 수신 -> Trace 실행
	UFUNCTION()
	void OnAttackTraceEvent(FGameplayEventData PayLoad);
	
	
	// 몽타주 정상 완료
	UFUNCTION()
	void OnMontageCompleted();
	
	// 몽타주 BlendOut 시작
	UFUNCTION()
	void OnMontageBlendOut();
	
	// 몽타주가 다른 몽타주에 의해 중단됨
	UFUNCTION()
	void OnMontageInterrupted();
	
	// GA 취소로 몽타주 중단
	UFUNCTION()
	void OnMontageCancelled();
	
	void SetWarpTargetLockedEnemy();
	void ClearWarpTarget();

	// 워프 목적지 write 단일화(DRY). 락 경로와 소프트 런지 경로가 공유.
	// 오프셋 clamp([0, AttackRange-AttackRadius]) + BUG-007 런타임 거리 clamp + AddOrUpdateWarpTargetFromComponent.
	// TargetComponent = 워프가 추종할 대상 루트컴포넌트(bFollowComponent=true). 넷: 양쪽 공통 실행(권위 루트모션 구동).
	void ApplyApproachWarpToTarget(const USceneComponent* TargetComponent);
	
private:
	// 현재 활성화에서 사용할 무브셋(무기 타입으로 해석된 결과). ExecuteAbility 진입 시 세팅.
	// 무브셋 미할당/미등록이면 레거시 AttackConfig/AnimMontage로 폴백. GC-only 내부 캐시.
	UPROPERTY()
	TObjectPtr<UVBAttackConfig> ActiveConfig;

	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveMontage;

	// 직전에 수신한 트레이스 노티가 실어 보낸 임팩트 쉐이크 프로필. GC-only 내부 캐시라 bare UPROPERTY(Category 금지).
	// 수명이 활성화가 아니라 노티 단위인 이유는 OnAttackTraceEvent 주석 참조.
	// const 포인터인 이유: FGameplayEventData::OptionalObject 가 TObjectPtr<const UObject> 라
	//  const 를 벗기려면 const_cast 가 필요해진다. 읽기만 하므로 const 로 받는다(엔진 선례: BehaviorTreeComponent.h).
	UPROPERTY()
	TObjectPtr<const UVBSwingImpactProfile> ActiveSwingProfile;

	// 직전 노티가 실어 보낸 전투 수치 보정 프로필. 위와 같은 이유로 bare UPROPERTY + const 다.
	// 수명도 같다 - 노티 단위이고, 활성화 시작에서 반드시 리셋해야 직전 스윙의 배율이 다음 활성화
	//  첫 타로 새지 않는다. 그 리셋을 빠뜨리면 증상이 완전히 조용하다.
	UPROPERTY()
	TObjectPtr<const UVBSwingCombatProfile> ActiveSwingCombatProfile;

	int32 ComboIndex = 0;

	// 몽타주 변형 순환 커서. 활성화마다 1 증가해 같은 변형이 연속으로 나오지 않게 한다.
	// 리셋하지 않는다 - ComboIndex 와 달리 이 값의 의미는 "지난번에 무엇을 썼는가"이고
	//   활성화 경계에서 초기화하면 순환이 성립하지 않는다(항상 0번만 재생).
	// 왜 무작위가 아닌가: 변형이 2개일 때 균등 무작위는 50% 확률로 직전과 같은 것을 내
	//   이 기능의 목적을 스스로 부순다. 더 중요한 것은 ExecuteAbility 가 ServerInitiated 라
	//   서버와 소유 클라 양쪽에서 실행된다는 점이다 - RNG 를 넣으면 두 쪽이 다른 몽타주를 골라
	//   루트모션 위치가 갈라진다. 순환은 양쪽이 같은 결정론적 커서를 밟으므로 통신 없이 일치한다.
	// 비-UPROPERTY: 순수 int32 라 GC 대상이 아니다.
	int32 VariantCursor = 0;

	// 이번 활성화에서 고른 변형의 재생배율. MontagePlayRate 에 곱해진다.
	// VariantCursor 와 달리 활성화마다 1.0 으로 리셋한다 - 이번 변형의 값이지 누적 상태가 아니다.
	float ActiveVariantRate = 1.0f;

	// 이번 활성화에서 고른 변형의 데미지 배율. ActiveConfig->DamageMultiplier 에 곱해진다.
	// ActiveVariantRate 와 같은 수명 - 활성화마다 1.0 으로 리셋한다.
	float ActiveVariantDamage = 1.0f;
	bool bComboWindowOpen = false;
	bool bSaveAttack = false;

	// 이 공격이 뚝심(SuperArmor) 로스태그를 부여했는지. EndAbility에서만 제거(부여한 경우에만 — 누수/중복제거 방지). 게임스레드 전용.
	bool bGrantedSuperArmor = false;
	
	UFUNCTION()
	void OnComboWindowOpen(FGameplayEventData Payload);
	
	UFUNCTION()
	void OnComboWindowClose(FGameplayEventData Payload);
	
	UFUNCTION()
	void OnComboInputReceived(FGameplayEventData Payload);
};
