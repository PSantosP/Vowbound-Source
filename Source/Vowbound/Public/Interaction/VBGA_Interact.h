// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "VBGA_Interact.generated.h"

class UVBInteractableComponent;

/**
 * 환경 상호작용(문·레버). 설계 .sw/interaction/design.md
 *
 * 판정의 주인이다. 서버가 UVBInteractorComponent::FindBestInteractable 로 대상을 고르고,
 *   탭이면 그 자리에서, 홀드면 HoldDuration 뒤에 대상의 NotifyInteracted 를 부른다.
 * 몸 동작(2단계): 탭 대상의 설정에 이번 방향(켜기/끄기)의 몸 동작이 있으면, 설 자리로 옮긴 뒤(루트모션 소스)
 *   몽타주를 시작하고 CommitTime에 NotifyInteracted를 건다. 손 동작은 0초, 발차기는 접촉 시각이다.
 *   몽타주가 끝나면(끊겨도) 끝난다. 그 동안 캡슐 방향의 주인이 된다(UVBInteractorComponent::OwnsCapsuleFacing).
 *   [MP 구멍] 몸 동작 선택과 이동은 서버에서만 돈다 - 원격 클라는 보정으로 따라온다(FIND-128 과 같은 마일스톤).
 * 홀드 취소 = 입력을 떼면 AVBCharacter::HandleAbilityInputReleased 가 이 GA 를 취소한다(가드·질주와 같은 길).
 *   피격·경직은 그쪽 GA 의 CancelAbilitiesWithTag(Ability.Interact)가, 사망은 CancelAllAbilities 가 끊는다.
 * 원격 클라에서는 판정하지 않는다 - 홀드 진행 표시만 한다(ServerInitiated 라 클라에도 인스턴스가 돈다).
 * 칼 수납: 설정의 bSheatheWeapon 이면 무기 상태 컴포넌트의 임시 수납을 빌려 쓴다(트래버설과 같은 길).
 *   끝날 때 원래 무기로 돌린다. 죽어서 끝났으면 돌리지 않는다.
 */
UCLASS()
class VOWBOUND_API UVBGA_Interact : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	UVBGA_Interact();
	// 탭도 정렬/몽타주 동안 활성이다. 입력 해제로 취소할 수 있는 것은 홀드뿐이다.
	bool ShouldCancelOnInputRelease() const { return bHoldInteraction; }
	void HandleRepeatPress();

protected:
	virtual void ExecuteAbility(
		const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	// 서버에서 홀드 시간이 찼다.
	void HandleHoldCompleted();

	// 몸 동작 갈래(서버). 설 자리로 옮기고 -> 몽타주 시작 -> 접촉 시점에 상태 확정.
	void StartMotion(UVBInteractableComponent* Target, const struct FVBInteractionMotion& Motion);

	UFUNCTION()
	void HandleAlignFinished();

	UFUNCTION()
	void HandleMotionMontageEnded();

	// 발 접촉 전에 취소되면 상태를 바꾸지 않는다. 확정 후 취소되어도 열린 문은 유지한다.
	void CommitMotion();

	// 칼 수납을 걸 조건이 맞으면 건다. 홀드와 몸 동작 둘이 부른다.
	void SheatheIfWanted(const class UVBInteractableConfig* Config);

	// 대상의 조건 칸에 걸린 시도(서버): 거절 효과 + 주인 클라에 거절 글. 시작 판정과 완료 거절이 같은 길을 쓴다.
	void Refuse(UVBInteractableComponent* Target, AActor* Avatar);

	// 몸 동작 갈래가 도착 뒤 틀 몽타주. 옮기는 동안 들고 있는다.
	UPROPERTY()
	TObjectPtr<UAnimMontage> PendingMontage;

	// 몸 동작의 설 자리(캡슐 중심). StartMotion 이 한 번 정하고 도착 검사가 그대로 쓴다 - 다시 계산하지 않는다.
	//   «다가온 방향» 기준(bStandFromApproach)은 캐릭터가 움직이면 방향이 바뀌어, 다시 재면 도착 검사가 다른 자리를 본다.
	FVector AlignDestination = FVector::ZeroVector;

	// 서버: 시작할 때 고른 대상. 홀드 완료 시 다시 고르지 않는다 - 그 사이 카메라가 돌아 옆 대상이
	//   더 가까워졌을 수 있다. 약참조인 이유: 홀드 도중 대상이 파괴될 수 있다.
	TWeakObjectPtr<UVBInteractableComponent> ActiveTarget;

	FTimerHandle HoldTimerHandle;
	FTimerHandle MotionCommitTimerHandle;
	bool bMotionCommitted = false;
	bool bTargetInitiallyActivated = false;
	bool bFastCloseRequested = false;

	// 이번 상호작용이 칼을 임시 수납했는가. 끝날 때 돌려놓을지 이것으로 가른다 -
	//   맨손으로 시작했거나 트래버설이 이미 수납해 둔 경우 우리가 돌려놓으면 안 된다.
	bool bSheathedWeapon = false;
	bool bHoldInteraction = false;
};
