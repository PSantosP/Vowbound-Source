// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "AttributeSet.h"                 // FGameplayAttribute — TArray 멤버라 전방선언 불가
#include "SaveSystem/VBSaveParticipant.h" // 빌드 구역의 저장 참여자
#include "VBPlayerState.generated.h"

// 전방선언 (헤더 include 최소화)
class UVBAbilitySystemComponent;
class UVBHealthAttributeSet;
class UVBCombatAttributeSet;
class UVBReputationAttributeSet;
class UVBInventoryComponent;
class UVBEquipmentComponent;
class UVBProgressionComponent;
class UVBQuestLogComponent;
class UGameplayAbility;
class UGameplayEffect;
struct FVBPlayerBuildSaveData;

/**
 * GAS의 집 - ASC와 AttributeSet을 소유
 * 이유 : Respawn 시 Attribute 유지, 네트워크 안정성
 */
UCLASS()
class VOWBOUND_API AVBPlayerState : public APlayerState, public IAbilitySystemInterface, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	AVBPlayerState();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	//~ IVBSaveParticipant - 빌드 구역(UVBSaveGame::PlayerBuild). 문맥의 PlayerState 가 자기일 때만(리슨 서버의 다른 플레이어 것은 안 쓴다).
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

protected:
	// 저장 참여 등록은 BeginPlay 가 아니라 여기 - Continue 적용이 월드 BeginPlay 보다 먼저 온다(IVBSaveParticipant 주석).
	virtual void PostInitializeComponents() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	
	// 커스텀 ASC 접근자 (다운 캐스트 없이 바로 사용)
	UVBAbilitySystemComponent* GetVBAbilitySystemComponent() const;

	// AttributeSet 접근자
	const UVBHealthAttributeSet* GetVBHealthAttributeSet() const;
	
	const UVBCombatAttributeSet* GetVBCombatAttributeSet() const;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|GAS")
	TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|GAS")
	TArray<TSubclassOf<UGameplayEffect>> DefaultEffects;
	
	bool AreStartupAbilitiesGranted() const;
	void MarkStartupAbilitiesGranted();
	bool AreStartupEffectsApplied() const;
	void MarkStartupEffectsApplied();

	// 세이브: 현재 플레이어 빌드/스탯을 스냅샷하거나(SnapshotBuild) 로드된 스냅샷을 적용(ApplyBuild)한다.
	// ASC/AttributeSet 소유자가 여기(PlayerState)라 저장/복원의 권위 홈이다. 서버 권위에서 호출.
	// 둘은 SavedAttributes 라는 같은 배열을 같은 순서로 돈다 - 목록이 갈리면 저장과 복원이 어긋난다.
	void SnapshotBuild(FVBPlayerBuildSaveData& Out) const;
	void ApplyBuild(const FVBPlayerBuildSaveData& In);

	const TArray<FGameplayAttribute>& GetSavedAttributes() const { return SavedAttributes; }
	const TMap<FGameplayAttribute, FGameplayAttribute>& GetSavedAttributeCaps() const { return SavedAttributeCaps; }

	// 세이브 파일의 키. 어트리뷰트 프로퍼티 이름 그대로다(세트 클래스명 접두어 없음).
	// 어트리뷰트를 rename 하면 이 키도 바뀌어 구 세이브 값이 조용히 유실된다 - 단위 테스트가 키 집합을 고정한다.
	static FName MakeAttributeSaveKey(const FGameplayAttribute& Attribute);

protected:
	// 저장/복원 대상 어트리뷰트. 홈이 C++ 인 이유: 여기 들어갈 수 있는 것은 C++ 에 실재하는 어트리뷰트뿐이고,
	// 새 어트리뷰트를 저장하려면 어차피 C++ 을 고친다. 디자이너 튜너블이 아니라 직렬화 계약이다.
	// EditDefaultsOnly 로 열지 않는 이유: BP 델타가 항목을 조용히 비울 수 있고, 그것이 이 배열이 고치는
	// 결함(저장 목록과 복원 목록이 갈리는 것)과 같은 종류의 실패다. 보이되 못 고치게 VisibleAnywhere.
	UPROPERTY(VisibleAnywhere, Category="Vowbound|Save")
	TArray<FGameplayAttribute> SavedAttributes;

	// 복원값을 자를 상한. 상한을 GE 가 소유하게 되면서(FIND-078) 저장값이 현재 상한을 넘을 수 있다 -
	// 디자이너가 MaxHealth 를 내리면 옛 세이브의 Health 가 그보다 크다.
	// 왜 자르나: SetNumericAttributeBase 는 base 를 그대로 쓰고 클램프는 CurrentValue 에만 걸린다.
	//   base>상한 인 채 두면 화면에 보이는 값과 다음 저장에 들어갈 값이 갈린다.
	// 홈이 SavedAttributes 바로 옆인 이유: 새로 저장하는 어트리뷰트에 상한이 있으면 여기도 함께 채워야 한다.
	UPROPERTY(VisibleAnywhere, Category="Vowbound|Save")
	TMap<FGameplayAttribute, FGameplayAttribute> SavedAttributeCaps;

	// 상한이 있으면 [0, 상한] 으로, 없으면 그대로. 상한은 GE 가 방금 세운 값이다
	// (GE_InitStats 적용 -> ApplyBuild 순서는 AVBGameMode::HandleStartingNewPlayer 가 보장).
	float ClampToCap(const UAbilitySystemComponent& ASC, const FGameplayAttribute& Attribute, float Value) const;

	UPROPERTY(VisibleAnywhere, Category="Vowbound|GAS")
	TObjectPtr<UVBAbilitySystemComponent> AbilitySystemComponent;
	
	// ASC에 등록되는 AttributeSet (ASC가 라이프타임 관리)
	// NOTE: UPROPERTY() alone — GC-only reference, not exposed to editor/BP.
	// Category는 exposure 키워드(VisibleAnywhere 등)와 함께 사용해야 UHT warning 회피.
	UPROPERTY()
	TObjectPtr<UVBHealthAttributeSet> HealthAttributeSet;

	UPROPERTY()
	TObjectPtr<UVBCombatAttributeSet> CombatAttributeSet;

	UPROPERTY()
	TObjectPtr<UVBReputationAttributeSet> ReputationAttributeSet;

	// 가진 아이템의 주인(.sw/narrative 10.2). 폰이 죽고 다시 나도 남는다. 찾기 = UVBInventoryComponent::FindFor.
	UPROPERTY(VisibleAnywhere, Category="Vowbound|Item")
	TObjectPtr<UVBInventoryComponent> InventoryComponent;

	// 입은 장비의 주인(.sw/remaining-features/b1-equipment.md). 인벤토리·ASC 와 같은 자리라 폰이 바뀌어도 장비와 효과가 남는다. 찾기 = UVBEquipmentComponent::FindFor.
	UPROPERTY(VisibleAnywhere, Category="Vowbound|Item")
	TObjectPtr<UVBEquipmentComponent> EquipmentComponent;

	// 성장(경험치·레벨·스킬 트리)의 주인(.sw/remaining-features/b2-growth.md). 같은 자리라 죽고 다시 나도 남는다. 찾기 = UVBProgressionComponent::FindFor.
	UPROPERTY(VisibleAnywhere, Category="Vowbound|Growth")
	TObjectPtr<UVBProgressionComponent> ProgressionComponent;

	// 퀘스트 진행의 주인(.sw/narrative 12.2). 인벤토리와 같은 이유로 PlayerState 에 둔다. 찾기 = UVBQuestLogComponent::FindFor.
	UPROPERTY(VisibleAnywhere, Category="Vowbound|Quest")
	TObjectPtr<UVBQuestLogComponent> QuestLogComponent;

	// GAS를 주었는지에 대한 플래그
	bool bStartupAbilitiesGranted = false;
	bool bStartupEffectsApplied = false;
};
