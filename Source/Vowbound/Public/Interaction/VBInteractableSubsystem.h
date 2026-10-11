// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "VBInteractableSubsystem.generated.h"

class UVBInteractableComponent;

/**
 * 월드의 상호작용 대상 목록. 목록만 든다 - 고르는 것은 UVBInteractorComponent 가 한다.
 *
 * 왜 충돌 겹침 질의가 아닌가: 대상은 레벨당 수십 개 규모라 목록 순회가 매 프레임 겹침 질의보다 싸고,
 *   새 충돌 채널(DefaultEngine.ini)과 모든 문 메시의 충돌 설정을 요구하지 않는다.
 * 서버와 클라가 각자 같은 목록을 세운다 - 컴포넌트가 양쪽에서 BeginPlay 하므로 복제가 필요 없다.
 * 약참조라 파괴된 대상은 GetInteractables 가 걷어낸다.
 */
UCLASS()
class VOWBOUND_API UVBInteractableSubsystem : public UWorldSubsystem, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//~ IVBSaveParticipant - 문·스위치 구역(Progress.InteractableStates). 아래 CollectSavedStates / ApplySavedStates 를 부른다.
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

	void Register(UVBInteractableComponent* Interactable);
	void Unregister(const UVBInteractableComponent* Interactable);

	// 살아 있는 항목만 돌려준다.
	const TArray<TWeakObjectPtr<UVBInteractableComponent>>& GetInteractables();

	// 저장: 지금 떠 있는 대상의 켜짐 상태를 InOutStates 에 덮어쓴다. 스트리밍으로 내려가 있는 대상의 기존 값은 남긴다 -
	//   안 보이는 문을 «기록 없음» 으로 지우면 로드 때 닫힌 채로 돌아온다.
	void CollectSavedStates(TMap<FName, bool>& InOutStates);

	// 로드: 떠 있는 대상에는 바로(애니 없이) 적용하고, 전부를 대기 목록에 둔다 - 나중에 스트리밍으로 들어오는 대상은
	//   Register 에서 받는다(World Partition). 서버에서만 부른다.
	void ApplySavedStates(const TMap<FName, bool>& States);

private:
	TArray<TWeakObjectPtr<UVBInteractableComponent>> Interactables;

	// 로드한 세이브의 상태. 대상이 등록될 때 꺼내 적용한다. 새 게임이면 비어 있다.
	TMap<FName, bool> PendingSavedStates;

	void ApplyPendingState(UVBInteractableComponent* Interactable) const;
};
