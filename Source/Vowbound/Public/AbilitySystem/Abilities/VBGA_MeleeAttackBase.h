// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGameplayAbility.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBSwingCombatProfile.h" // FVBSwingCombatScales 를 값으로 반환 — 전방선언으로는 부족하다.
#include "Types/VBWeaponStateTypes.h" // EVBWeaponType - 회피 오프셋이 «같은 무기» 를 기억한다.
                                       //  AbilitySystem -> Data 는 이미 확립된 헤더 계약이라 새 간선이 아니다.
#include "AbilitySystem/VBHitImpact.h" // EVBHitImpact - 스윙 문맥이 값으로 든다
#include "VBGA_MeleeAttackBase.generated.h"

class UVBWeaponMovesetConfig;
class UVBSwingImpactProfile;
class UVBMeleeTraceComponent;
class URootMotionModifier_Warp;

/**
 * VBGA_MeleeAttackBase
 *
 * 근접 공격 GA의 기초 클래스. 현재 파생은 LightAttack 하나(강공격은 DEC-009 로 폐지, RMB 는 가드로 이관).
 *
 * 의도:
 *  - 몽타주 재생 → 판정 → GE 적용 파이프라인을 한 곳에 정의
 *    판정은 둘이다. 판정 창(UVBANS_AttackTraceWindow)이면 UVBMeleeTraceComponent 가 칼 궤적을 쓸고,
 *    옛 한 프레임 노티(VBAN_SendAttackTraceEvent)면 정면 구 스윕(PerformAttackTrace)이다. 맞은 뒤 처리는 둘이 같은 함수다.
 *  - 파생 GA는 파라미터(AttackConfig)와 ApplyAdditionalEffects 오버라이드만 신경 쓰면 됨
 *  - 콤보 시스템(ComboWindow*)과 통합
 *
 * 전제:
 *  - 파생 GA에 AttackConfig (UVBAttackConfig DataAsset) 할당 필수
 *  - 공격 몽타주에 판정 창(UVBANS_AttackTraceWindow) 또는 옛 노티(VBAN_SendAttackTraceEvent) 필수
 *  - 판정 창을 쓰는 캐릭터는 UVBMeleeTraceComponent + 모양 자산을 가진다. 없으면 옛 구 스윕으로 대신하고 경고한다
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

	// 플레이어가 꺼낼 무기를 하나도 안 가졌으면 공격이 시작되지 않는다(.sw/prologue-opening 3.1).
	//   맨손 공격으로 떨어뜨리지 않는다 - 프롤로그는 «무기를 주워야 싸운다» 이고, 맨손(Fighter)도 가진 무기의 하나다.
	//   적(AVBCharacter 아님)과 무기 상태 컴포넌트가 없는 아바타는 이 판정을 건너뛴다.
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

protected:
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	TObjectPtr<UVBAttackConfig> AttackConfig;

	// 무기 타입별 무브셋. 할당 시 현재 무기 타입으로 leaf 를 조회해 사용(강공격 폐지 후 leaf 는 Light 단일).
	// 미할당(null)이면 위 레거시 AttackConfig + GA의 AnimMontage 사용(회귀 안전 폴백).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	TObjectPtr<UVBWeaponMovesetConfig> MovesetConfig;

	// 이 공격이 처형을 열 수 있나. 켜져 있으면 활성화 첫머리에
	// Event.Combat.ExecuteRequested 를 쏘고, 처형이 실제로 열리면 이 공격은 몽타주 없이 끝난다.
	//
	// 왜 파생이 아니라 여기인가: 진입점(ExecuteAbility)이 이 클래스에 있어서 파생에서 가로챌 자리가 없다.
	// ★왜 기본이 false 인가★: 적 공격(UVBGA_EnemyMeleeAttack)도 이 클래스를 상속한다.
	//   기본을 true 로 두면 적이 플레이어를 처형한다. 모르는 경우는 «안 연다» 로 떨어져야 한다(규칙 27) —
	//   켜는 것을 잊으면 지금 동작(전용 키)이 남고, 끄는 것을 잊으면 적이 처형한다. 실패의 값이 다르다.
	// 왜 가상 함수가 아닌가: 판정이 「이 어빌리티가 처형을 열 수 있나」 하나뿐이고 파생이 재정의할
	//   «동작» 이 없다. 가상 함수를 두면 파생마다 빈 구현이 생긴다.
	// user 결정 2026-09-19: 「공격 버튼만 눌러도 바로 자연스럽게 처형 되게끔 어쌔신 크리드처럼」.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	bool bCanOpenFinisher = false;

	// 처형을 열어 본다. 열렸으면 true - 그때 부르는 쪽은 자기 일을 중단한다.
	//
	// ★부르는 자리가 셋이다 — 상태가 넘어가는 자리가 셋이기 때문이다.	//   ①`ExecuteAbility` 첫머리     — 콤보 «첫 타»
	//   ②`OnComboInputReceived` 첫머리 — 콤보 중 «입력». 여기가 유일하게 «즉시» 다
	//   ③`TryChainToNextSection` 첫머리 — `bAutoChainCombo`(입력 없이 이어지는 무기)용
	//
	// 왜 셋이나 되나(user PIE 두 번에 걸쳐 드러났다):
	//   1회차 — 첫 타에만 넣었더니 콤보 중에 안 나갔다. 2·3타는 같은 활성화 안에서 몽타주 섹션만
	//     바꾸므로 `ExecuteAbility` 를 다시 지나가지 않는다(「콤보중에도 바로 나가야 하는거 아니야?」).
	//   2회차 — ③만 더했더니 「살짝 슬로우 걸려서 진행되는 것처럼」 나왔다. ③에 닿는 경로가
	//     `TryChainAfterHit` 의 ★타이머 지연(ChainDelayAfterHitSeconds)★ 을 거치기 때문이다.
	//     그 지연은 콤보 리듬감을 위한 것이고 처형과는 무관하다. 그래서 ②를 더해 입력에서 바로 끊는다.
	// 사거리도 대상도 여기서 판정하지 않는다. 이벤트만 쏘고 UVBGA_Execute 가 정한다.
	bool TryOpenFinisher(const FGameplayAbilityActorInfo* ActorInfo);

	virtual void ExecuteAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	// 종료 훅 override — 슈퍼아머(SuperArmor) 로스태그 제거를 위해. 몽타주 종료 3경로(Completed/Interrupted/Cancelled)가
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
	void CaptureSwingTarget();
	void ResetSwingTarget();
	void OnSwingTargetDeath(const FGameplayTag Tag, int32 NewCount);
	UFUNCTION()
	void OnSwingTargetDestroyed(AActor* DestroyedActor);
	TWeakObjectPtr<AActor> SwingTarget;
	TWeakObjectPtr<UAbilitySystemComponent> SwingTargetASC;
	FDelegateHandle SwingTargetDeathHandle;

	// 옛 판정: 정면 구 스윕 한 번(서버). 옛 한 프레임 노티, 몽타주가 없을 때, 판정 창을 못 열었을 때 쓴다.
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

	// 판정 창 닫힘(서버). 페이로드의 모양 태그로 어느 창인지 가른다.
	UFUNCTION()
	void OnAttackTraceEndEvent(FGameplayEventData PayLoad);

	
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

	// 오프셋을 밖에서 정해 세우는 판. 적 GA 가 «클립 전진 x 허용 압축비» 로 오프셋을 다시 잰 뒤 이것을 부른다 - 목적지 write 는 여전히 한 곳이다.
	// bFollowTarget=false 면 지금의 한 점에 고정한다(적 - 2026-09-30 user 「멀리서 미끄러지며 온다」). true 면 대상을 매 프레임 따라간다(플레이어).
	void ApplyApproachWarpWithOffset(const USceneComponent* TargetComponent, float ApproachOffset, bool bFollowTarget = true);

	// 두 캡슐 반경의 합(cm). 워프 목적지가 이보다 안쪽이면 CMC 가 매 프레임 밀어내 떨린다. ComputeApproachOffset 과 적 GA 의 대역 clamp 가 같이 쓴다.
	float ComputeMinSeparation(const USceneComponent* TargetComponent) const;

	// 접근 워프의 «멈추는 거리»(대상 중심에서 공격자 쪽으로, cm). ApplyApproachWarpToTarget 이 쓰던 계산을 뽑아 둔 것 -
	//   변형 선택(SelectVariantIndex)도 «실제로 가야 하는 거리 = 지금 거리 - 이 값» 을 알아야 해서 홈을 하나로 둔다.
	//   ActiveConfig 가 해석된 뒤에만 뜻이 있다(무브셋 해석 블록이 ActiveConfig 를 세운 직후부터).
	float ComputeApproachOffset(const USceneComponent* TargetComponent) const;

	// 위와 같은 clamp 사슬(먼 시작·BUG-007·캡슐 합)을 «주어진 기본 멈춤» 에서 시작한다. 적 GA 가 변형을 고르기 전에 변형마다
	//   다른 멈춤으로 «가야 하는 거리» 를 재려고 쓴다 - 변형이 해석되기 전이라 ActiveVariantApproachOffset 이 아직 없다.
	float ComputeApproachOffset(const USceneComponent* TargetComponent, float BaseOffset) const;

	// 이번 스윙의 변형 번호를 고른다. 기본은 순환 커서(아래 VariantCursor 주석의 결정론 이유). 적 GA 가 «거리에 맞는 변형» 으로 덮는다 -
	//   적은 서버에서만 도는 GA 라 위치 기반 선택이 서버·클라 불일치를 만들지 않는다. 반환값은 ResolveMontageVariant 의 인덱스.
	virtual int32 SelectVariantIndex(const UVBAttackConfig& Leaf, int32 VariantCount);

	// 몽타주가 처음부터 «접근 워프 창이 닫히는 시각» 까지 루트로 가는 2D 거리(cm, 월드). 창이 없으면 클립 끝까지.
	//   배율은 메시 스케일 - 루트모션은 메시 공간이라 액터 배율이 곱해진다. 변형 선택·계획·대역이 «클립이 원래 가는 거리» 를
	//   손으로 적은 값이 아니라 자산에서 직접 잰다(작성 시점 규칙 - 안 잰 원료 금지). 창 뒤를 빼는 이유는 cpp 주석.
public:
	static float MeasureMontageLungeCm(const UAnimMontage* Montage, float MeshScale);

	// 몽타주의 접근 워프 창(AttackTarget 을 겨눈 MotionWarping 노티) 하나와 그 창이 닫히는 시각. 없으면 null.
	//   창을 찾는 규칙의 홈 - 전진 측정과 «배율을 아는 워프인가» 검사가 같은 창을 본다.
	static const URootMotionModifier_Warp* FindApproachWarpModifier(const UAnimMontage* Montage, float* OutWindowEndTime = nullptr);
protected:

	// 이번 활성화가 해석한 몽타주. 파생(적 GA)이 워프 대역을 재는 데 쓴다 - 필드는 private 로 두고 읽기만 연다.
	const UAnimMontage* GetActiveMontage() const { return ActiveMontage; }
	
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

	// 이번 활성화에서 고른 변형의 멈추는 거리(UVBAttackConfig::GetVariantApproachOffset). 음수 = 아직 해석 전.
	// ActiveVariantRate 와 같은 수명. ComputeApproachOffset(1인자) 이 이것을 기본 멈춤으로 쓴다.
	float ActiveVariantApproachOffset = -1.0f;
	bool bComboWindowOpen = false;
	bool bSaveAttack = false;

	// === 캔슬 문법 (2026-09-05, .sw/combat-feel/design.md §3) ===
	// 마지막 콤보 입력 시각(월드 초). 창이 닫혀 있어도 기억해 창이 열릴 때 나이로 소비/폐기를 가른다. -1 = 없음.
	double ComboInputTime = -1.0;
	// 이 창에서 이미 다음 섹션으로 넘어갔다는 표지. 섹션 점프는 옛 창 노티의 NotifyEnd 를 한 번 더 내므로
	//   그 닫힘 이벤트가 회복 캔슬을 발동하지 않게 한 번 삼킨다.
	bool bChainedThisWindow = false;
	TWeakObjectPtr<const UObject> ActiveComboWindowNotify;
	friend class FVBCombatComboWindowOwnershipTest;
	// 회피 오프셋 기억: 재개할 타(끊긴 타가 안 닿았으면 그 타, 닿았으면 다음 타)·시각·무기. 다음 공격이 유예 안에 같은 무기로 오면 그 섹션에서 시작.
	int32         DodgeOffsetComboIndex = -1;
	double        DodgeOffsetTime       = -1.0;
	EVBWeaponType DodgeOffsetWeapon     = EVBWeaponType::None;

	// 이 섹션의 닿는 프레임(AttackTrace 노티)이 지났는가와 그 시각. 전환은 «창 열림» 과 «닿음» 중 늦은 쪽 뒤에만 한다.
	//   창이 닿기 직전에 열리므로(카타나 0.27/0.30) 창 열림만 보면 닿는 프레임을 건너뛴다(2026-09-05 user 「스킵」).
	bool   bHitFrameReached = false;
	double HitFrameTime     = -1.0;
	// 닿은 뒤 여운(ChainDelayAfterHitSeconds)을 기다리는 타이머.
	FTimerHandle ChainDelayTimer;

	// === 맞은 뒤 처리의 문맥 (.sw/weapon-trace 7·16절) ===
	// 스윙 하나의 무게·방향. 판정 창이면 창이 열릴 때 한 번 정해 창 동안 고정한다 - 몇 프레임에 걸쳐 맞든 스윙 하나의 값은 하나다.
	//   옛 구 스윕도 같은 구조체를 한 번 만들고 버린다. 그래서 대상별 처리(ApplySwingHits)가 두 판정 경로에 하나다.
	struct FSwingHitContext
	{
		// 조준 보정된 수평 정면. 넉백 방향(KnockbackYaw)과 옛 구 스윕의 방향이 여기서 나온다.
		FVector Forward = FVector::ForwardVector;
		float KnockbackYaw = 0.0f;
		FVBSwingCombatScales SwingScales;
		EVBHitImpact SwingImpact = EVBHitImpact::Light;
		float AttackPower = 0.0f;
		float FinalDamage = 0.0f;
		// 창이 열린 뒤 다른 창이 노티 프로필을 덮어써도 이 스윙은 제 것을 쓴다.
		TWeakObjectPtr<const UVBSwingImpactProfile> ImpactProfile;
		TWeakObjectPtr<const UVBSwingCombatProfile> CombatProfile;
		// 스윙당 한 번 항목(자동 락온·카메라 재중심·히트스톱·스윙 쉐이크)을 이미 냈는가.
		bool bAutoLocked = false;
		bool bCameraReported = false;
		bool bContactFinished = false;
	};
	// 거짓 = 이 스윙은 피해를 낼 수 없다(아바타·공격 설정·피해 GE·ASC 중 하나가 없음). 옛 판정도 그때 조용히 끝났다.
	bool BuildSwingHitContext(FSwingHitContext& OutContext) const;
	// 새로 맞은 표적들에 피해·반응·큐를 적용하고, 문맥의 첫 접촉이면 히트스톱·스윙 쉐이크를 한 번 낸다.
	void ApplySwingHits(const TArray<FHitResult>& Hits, FSwingHitContext& Context);

	// 이 GA 가 연 판정 창. 콜백은 Key 로 제 문맥을 찾는다 - 닫힌 창의 늦은 콜백은 못 찾아 버려진다.
	struct FOpenTraceWindow
	{
		int32 Key = INDEX_NONE;
		int32 WindowId = INDEX_NONE;   // UVBMeleeTraceComponent 쪽 번호
		FGameplayTag Shape;
		FSwingHitContext Context;
	};
	TArray<FOpenTraceWindow> OpenTraceWindows;
	int32 NextTraceWindowKey = 1;

	UVBMeleeTraceComponent* GetMeleeTraceComponent() const;
	// 창을 연다(서버). 못 열면 경고하고 옛 구 스윕으로 대신한다 - 조용히 피해가 사라지는 것보다 낫다.
	void BeginTraceWindow(const FGameplayEventData& Payload);
	void OnTraceWindowHits(const TArray<FHitResult>& Hits, int32 Key);
	// Shape 모양의 가장 먼저 연 창 하나를 닫는다.
	void EndTraceWindowByShape(FGameplayTag Shape, bool bSweepFinalPose);
	// 이 GA 가 연 창을 전부 닫는다. 섹션 전환과 EndAbility 가 부른다(창이 열린 채 끊겨도 뼈 갱신 강제가 풀린다).
	void EndAllTraceWindows();

	// 조기 전환 시도: 저장 입력·창 열림·닿음·여운 네 조건이 다 맞으면 넘어간다. 여운만 남았으면 타이머로 다시 온다.
	UFUNCTION()
	void TryChainAfterHit();
	// 저장된 입력이 있으면 다음 섹션으로 넘어간다. 반환 = 넘어갔는가(최대 콤보·설정 없음이면 거짓).
	bool TryChainToNextSection();
	// 창이 닫혔는데 다음 타가 없을 때: 워프를 거두고 몽타주를 짧게 멈춘 뒤 어빌리티를 끝낸다.
	void CancelRecovery();
	// 지금 든 무기(무브셋 해석과 같은 출처). 없으면 None.
	EVBWeaponType GetCurrentWeaponType() const;

protected:
	// 무브셋의 현재 무기 leaf. 무브셋 미할당·미등록이면 nullptr(= 레거시 AttackConfig 폴백). ExecuteAbility 와 PeekAttackConfig 가 같은 규칙을 쓴다.
	UVBAttackConfig* ResolveMovesetLeaf() const;
	// 스윙 진입 전에 «이 활성화가 쓸 공격 설정» 을 미리 본다(적 예고 단계가 답 태그를 읽는다 - design §11).
	//   ExecuteAbility 가 ActiveConfig 를 세우기 전이라 ActiveConfig 를 읽으면 직전 활성화 값이다. leaf 없으면 레거시 AttackConfig.
public:
	// public 인 이유: UVBEnemyCombatComponent::PlanNextAttack 이 «다음 공격의 설정» 을 토큰 부여 시점에 읽는다(design .sw/boss/design.md §12.7).
	const UVBAttackConfig* PeekAttackConfig() const;
protected:

	// 슈퍼아머(SuperArmor) 로스태그를 한 번만 건다(래치). 베이스는 스윙 진입에서, 적 공격 GA 는 슈퍼아머 반격의 예고 시작에서 부른다 -
	//   두 곳이 각자 AddLooseGameplayTag 를 하면 카운트가 2 가 되고 EndAbility 의 제거 1 로는 안 떨어진다. 제거는 EndAbility 한 곳.
	void GrantSuperArmorOnce();

private:

	// 이 공격이 슈퍼아머(SuperArmor) 로스태그를 부여했는지. EndAbility에서만 제거(부여한 경우에만 — 누수/중복제거 방지). 게임스레드 전용.
	bool bGrantedSuperArmor = false;

	// 활성화마다 1 씩 는다. 이 GA 는 액터당 한 인스턴스라 «활성 중인가» 만으로는 끊긴 스윙과 다시 켜진 새 스윙을 못 가른다(ApplySwingHits).
	uint32 ActivationSerial = 0;

	UFUNCTION()
	void OnComboWindowOpen(FGameplayEventData Payload);
	
	UFUNCTION()
	void OnComboWindowClose(FGameplayEventData Payload);
	
	UFUNCTION()
	void OnComboInputReceived(FGameplayEventData Payload);
};
