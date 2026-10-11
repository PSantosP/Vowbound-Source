// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StructUtils/InstancedStruct.h"
#include "Types/VBWeaponStateTypes.h"
#include "VBWeaponPickup.generated.h"

class UStaticMeshComponent;
class UVBInteractableComponent;

/**
 * 땅에 놓인 무기 - 집으면 그 무기를 «가진다». 설계 .sw/prologue-opening 3.1·8.4·10절.
 *
 * 획득 계약을 여기 한 곳에서 끝낸다(03 검토 8.4):
 *   UVBWeaponStateComponent::GrantWeapon 의 결과를 보고 -
 *   - 새로 얻음: 이 줍기를 소진(켜짐 = 주움, 저장됨)하고 그 «다음에만» AcquiredEffects(체크포인트 저장·사실·대사)를 적용한다.
 *   - 이미 가짐: 소진은 하되 AcquiredEffects 는 걸지 않는다 - 다른 자리의 같은 무기로 체크포인트·획득 대사가 다시 돌면 안 된다.
 *   - 실패: 아무것도 소진하지 않는다. 다시 집을 수 있다.
 * 왜 상호작용의 CompletionEffects 를 쓰지 않는가: 그것은 줍기 성공과 무관하게 돈다(UVBInteractableComponent::NotifyInteracted 순서 -
 *   OnInteracted 다음 사건, 그 다음 효과). 그 칸은 이 액터에서 비워 둔다. 컴포넌트의 그 칸은 protected 라 여기서 검증하지 못한다 - 배치 때 지킨다.
 * 재획득 방지는 아이템 줍기(AVBWorldItem)와 같다 - 켜짐 저장이 이어하기 때 이 대상을 켜진 채 돌려놓고, 켜진 줍기는 숨는다.
 *   가진 무기 목록 자체의 저장은 플레이어 빌드 저장이 맡는다(FVBPlayerBuildSaveData::OwnedWeaponIds, AVBPlayerState::SnapshotBuild·ApplyBuild).
 * 집는 동작·문구·거리는 상호작용 설정 자산(보통 DA_Interactable_Pickup)이 정한다.
 */
UCLASS()
class VOWBOUND_API AVBWeaponPickup : public AActor
{
	GENERATED_BODY()

public:
	AVBWeaponPickup();

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Weapon")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Weapon")
	TObjectPtr<UVBInteractableComponent> Interactable;

	// 집으면 가지는 무기.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Weapon")
	EVBWeaponType Weapon = EVBWeaponType::Katana;

	// 새로 얻었을 때만 적용한다(이미 가진 무기면 안 건다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Weapon", meta=(BaseStruct="/Script/Vowbound.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> AcquiredEffects;

private:
	UFUNCTION()
	void HandleInteracted(AActor* InteractingActor);

	UFUNCTION()
	void HandleActivationChanged(bool bActivated, bool bInstant);

	void ApplyPickedUp(bool bPickedUp);
};
