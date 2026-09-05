// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGameplayAbility.h"
#include "Data/VBDodgeConfig.h"
#include "VBGA_Dodge.generated.h"

/**
 * 회피 어빌리티
 *
 * 이동 주체 2경로 (2026-07-26 A안 — 루트모션):
 *  ① 몽타주 있음 → 무기별/통일 닷지 몽타주 재생, 클립 루트모션이 캡슐을 이동시킨다(ERM=True 전제).
 *  ② 몽타주 없음 → 레거시 `LaunchCharacter` 안전망(현행 동작 보존, 회귀 0).
 *  ①②를 동시에 쓰지 말 것. 물리 추진 + 루트모션이 겹치면 2배 이동한다.
 *
 * 무적창은 몽타주 길이와 분리한다: `State.Combat.Dodging` 은 config `DodgeDuration`(0.4s) 타이머로 해제하고,
 * 몽타주(0.67~1.20s)는 계속 재생된다. 무적을 몽타주에 묶으면 긴 클립에서 과도 무적이 되고, 분리하면
 * 회피 후반이 위험 구간으로 남아 게임성이 산다(소울류 표준).
 *
 * server-authoritative: 방향 판정·이동·타이머는 서버, 몽타주는 양쪽(ServerInitiated 라 클라 로컬 재생).
 */
UCLASS(Abstract)
class VOWBOUND_API UVBGA_Dodge : public UVBGameplayAbility
{
	GENERATED_BODY()
	
public:
	UVBGA_Dodge();
	
protected:
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Dodge")
	TObjectPtr<UVBDodgeConfig> DodgeConfig;
	FTimerHandle DodgeTimerHandle;
	
private:
	/**
	 * ExecuteAbility
	 *
	 * 회피(Dodge) 로직. 캐릭터를 순간 이동시키고 타이머 기반 무적을 부여한다.
	 * 베이스 ActivateAbility가 CommitAbility 자동 호출 후 이 함수에 위임한다.
	 *
	 * 동작:
	 *  - DodgeConfig DataAsset 파라미터로 LaunchCharacter (전방 + DodgeVerticalBoost 합성)
	 *  - DodgeTimerHandle 만료 전까지 ActivationOwnedTags 로 무적 태그 유지
	 *
	 * 부작용:
	 *  - ActivationOwnedTags 활성 (상태 태그)
	 *  - LaunchCharacter → CMC velocity 변경, 공중 상태 트리거 가능
	 *  - LockOn 상태라면 TargetLock 유지(ClearLockOn 호출 없음)
	 */
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	// 입력 방향(월드) → 액터 로컬 4분면. |X|>=|Y| 면 전/후, 아니면 좌/우.
	static EVBDodgeDirection ResolveDirection(const AActor* Avatar, const FVector& WorldDir);
};
