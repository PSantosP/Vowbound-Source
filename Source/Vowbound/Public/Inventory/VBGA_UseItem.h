// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "VBGA_UseItem.generated.h"

class UAnimMontage;
class UVBItemDefinition;

/**
 * 몸 동작이 있는 아이템 사용. 설계 .sw/quickslot-input 6·6.1절.
 *
 * 시작 = UVBInventoryComponent::BeginUse 가 폰 ASC 로 보내는 Event.Item.Use(OptionalObject = 아이템). 입력 GA 가 아니다.
 * 확정 = 몽타주의 표식(UVBAN_ItemUseMarker)이 보내는 Event.Item.UseMarker 를 서버가 받은 때 - UVBInventoryComponent::ConfirmUse 가
 *   수·조건을 다시 보고 소모 -> 효과를 낸다. 한 활성화에 한 번뿐이다.
 * 끊김(서버) = 표식 전에 Event.Combat.Hit(실제 피해)이 오면 취소, 회피·가드(Ability.Defense)가 켜지면 언제든 끝낸다(user 결정 2026-10-05).
 *   표식 전에 끊기면 아무것도 소모하지 않는다. 표식 뒤에 끊겨도 확정된 효과는 되돌리지 않는다.
 *   가드로 막은 타격은 Event.Combat.Hit 을 보내지 않지만(UVBHealthAttributeSet), 가드가 켜지는 순간 사용이 이미 끝나 있으므로 빈틈이 아니다.
 * 끝 = 완료·블렌드아웃·끊김·취소 모두 EndAbility 하나. 클라 인스턴스는 몽타주만 튼다 - 끝내는 것은 서버다.
 * 공격·상호작용은 쓰는 동안 막는다(BlockAbilitiesWithTag). 이동은 막지 않는다 - 몽타주의 슬롯이 정한다(상체 슬롯이면 걸으며 쓴다).
 * 넘어짐(플레이어 래그돌)은 그 컴포넌트의 막기 목록(Ability.Item.Use)이 시작을 막고 도는 것을 끊는다.
 */
UCLASS()
class VOWBOUND_API UVBGA_UseItem : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	UVBGA_UseItem();

	// 매달림·등반·사다리·트래버설 동작 중이거나 땅에 서 있지 않으면 시작하지 않는다.
	//   능력 입력은 AVBCharacter::HandleAbilityInputPressed 가 매달림·등반을 거르지만, 이 GA 는 입력이 아니라 인벤토리 요청으로 켜져 그 관문을 지나지 않는다.
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const override;

	// 요청의 아이템을 지금 쓸 수 있나(몽타주·수·조건). 엔진이 서버에서 활성화 «전에» 묻는다(UAbilitySystemComponent::InternalTryActivateAbility) -
	//   켠 뒤 ExecuteAbility 에서 거절하면 주인 클라가 활성화 성공을 받아 몽타주를 한순간 틀었다 끊는다. 확정 때 ConfirmUse 가 한 번 더 본다.
	virtual bool ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayEventData* Payload) const override;

protected:
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	UFUNCTION()
	void HandleMarker(FGameplayEventData Payload);

	UFUNCTION()
	void HandleHit(FGameplayEventData Payload);

	UFUNCTION()
	void HandleDefenseActivated(UGameplayAbility* ActivatedAbility);

	UFUNCTION()
	void HandleMontageEnded();

	// 서버에서만 끝낸다. 이유 = 클래스 주석.
	void EndOnServer(bool bWasCancelled, const TCHAR* Why);

	// 아이템이 원하면 든 무기를 임시 수납한다(서버). 수납하는 동안 State.Weapon.TempSheathed 를 달아 소환 GA(Tab)가 끼어들지 못하게 한다.
	void SheatheIfWanted(const struct FVBItemFragment_Usable& Usable);

	// 이번 활성화가 무기를 임시 수납했나. 끝날 때 돌려놓을지 이것으로 가른다 - 맨손으로 시작했거나 다른 기능이 이미 수납해 둔 경우는 우리 몫이 아니다.
	bool bSheathedWeapon = false;

	// 이번 활성화의 아이템과 몽타주. 표식 때 «내 몽타주가 아직 도나» 를 이것으로 본다.
	UPROPERTY()
	TObjectPtr<const UVBItemDefinition> ActiveItem;

	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveMontage;

	// 표식이 왔다(확정을 시도했다). 두 번째 표식은 버리고, 이 뒤의 피격 이벤트로는 취소하지 않는다(효과가 이미 났다).
	bool bMarkerReached = false;
};
