// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGameplayAbility.h"
#include "VBGA_Guard.generated.h"

/**
 * 가드/패링 어빌리티 (Held 패턴 — DEC-009)
 * 무엇: RMB 를 누르고 있는 동안 가드 상태 유지. 데미지 hub 가 State.Combat.Guarding + 가드 경과시각을 읽어
 *       패링(윈도우 안 무효+공격자 경직) / 블록(윈도우 밖 감쇠) 을 분기한다.
 * 왜: 강공격을 RMB 에서 뺀 자리에 방어 행동을 넣는다. GAS 태그로 가드 상태를 표현해 다른 시스템(anim/입력 블록)과 통합.
 * 가정: InputTag=Input.Combat.Guard, ServerInitiated(베이스 기본) — HandleAbilityInputReleased 에서 CancelAbility → EndAbility.
 *       무기 게이트는 데이터다(2026-07-26, 카타나 리터럴 제거). UVBParryConfig::WeaponGuardSets 에
 *       현재 무기 키가 있는지로 판단한다 — 없으면 즉시 EndAbility(no-op). 규약은 "키 존재 = 가드 권한,
 *       GuardMontage 유무 = 콘텐츠 유무"라 게이트와 콘텐츠가 같은 데이터에서 나와 어긋날 수 없다.
 *       무기 확장 = DA 행 1개, 이 파일 무변경.
 *       게이트 위치를 BeginGuard 안으로 옮기지 말 것: ActivationOwnedTags(Guarding)가 남아 공격까지 막힌다.
 * 부작용: ExecuteAbility(서버) → AVBCharacter::BeginGuard()(가드 시각 기록 + 몽타주). EndAbility(서버) → EndGuard().
 *         ActivationBlockedTags 로 회피/공격/사망 중 가드 차단.
 * 경직: 패링 성공 시 공격자 경직은 데미지 hub(VBHealthAttributeSet)가 히트리액트 이벤트로 낸다. GE_Stagger(no-op) 사용 금지.
 */
UCLASS(Abstract)
class VOWBOUND_API UVBGA_Guard : public UVBGameplayAbility
{
	GENERATED_BODY()
public:
	UVBGA_Guard();

protected:
	// 가드를 올리는 순간 자동 락온할지. 저작 지점은 BP_GA_Guard Class Defaults.
	// 소유 클라가 카메라 기준으로 대상을 찾아 서버에 락을 요청한다 — 가드를 올리는 순간엔 캡슐이
	// 아직 카메라 쪽으로 안 돌아 있을 수 있어 액터 정면 기준이면 엉뚱한 적을 잡는다.
	// 이미 락온 중이면 무시한다(수동 락을 뺏지 않는다).
	//
	// 2026-08-09 UVBTargetLockConfig 에서 이관. 이관 전 저작값 true(리드백 실측) = 이 기본값.
	// 왜 옮겼나: 소비처가 이 GA 하나뿐인데 남의 컴포넌트 config 를 3홉으로 읽고 있었고,
	//   적에게 가드를 주는 순간 GetTargetLockComponent() 가 null 이라 플래그를 읽는 것 자체가 불가능했다.
	//   이제 컴포넌트 유무와 무관하게 읽힌다(락온 시도만 컴포넌트가 있을 때 한다).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Guard")
	bool bAutoLockOnGuard = true;

private:
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
};
