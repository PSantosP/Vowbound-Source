// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "VBLootContainerSubsystem.generated.h"

/**
 * 서랍장 남은 몫의 주인: 서랍장 키 -> 남은 줄. 설계 .sw/narrative 14.5 W1(user 결정 2026-10-02 «남은 몫은 서랍장에 남는다»).
 *
 * 기록 없음 = 아직 안 굴림, 빈 기록 = 다 털림. 둘을 가르는 것이 서랍장 일회용 잠금의 근거라 빈 기록도 저장한다.
 * 월드 서브시스템인 이유: 서랍장이 스트리밍으로 내려갔다 올라와도 남은 몫이 남아야 한다(상점 판 수 UVBShopStateSubsystem 과 같은 자리).
 *   액터에 들면 상호작용 상태처럼 «로드한 값 대기 목록» 을 한 벌 더 짜야 한다.
 * 아이템을 태그(FVBSavedItem)로 드는 이유: 못 찾는 아이템(이름 바뀜)을 잃지 않는다 - 줄 때 되찾고, 못 찾은 줄은 남는다(VBLoot::GiveStacks).
 * 복제하지 않는다 - 남은 몫을 보이는 화면이 없고, 잠금(상호작용 켜짐)은 상호작용 컴포넌트가 복제한다.
 * 쓰기는 서버만. 저장 참여자 - 세계의 것이라 문맥의 플레이어와 무관하다. 읽기는 통째로 바꾸고, 쓰기는 세이브의 기록에 합친다
 *   (이어하기 뒤 다른 맵에서 저장해도 앞 맵 서랍장 기록이 남게 - WriteSave 주석).
 * [MP 구멍] 불러오기는 플레이어가 들어올 때마다 돈다(AVBGameMode::HandleStartingNewPlayer) - 둘째 플레이어가 들어오면 같은 월드에서 기록이 다시 읽힌다. FIND-128.
 */
UCLASS()
class VOWBOUND_API UVBLootContainerSubsystem : public UWorldSubsystem, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	static UVBLootContainerSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// 기록이 없으면(아직 안 굴림) nullptr. 빈 배열 = 다 털림.
	const TArray<FVBSavedItem>* Find(FName Key) const;

	// 서버에서만. 굴린 결과·준 뒤 남은 줄을 적는다(빈 배열도 적는다 - 다 털림).
	void SetRemaining(FName Key, const TArray<FVBSavedItem>& Remaining);

	// 불러오기가 기록을 바꿨다. 서랍장이 잠금을 다시 본다 - 상호작용 켜짐과 남은 몫 중 무엇이 먼저 읽히든 마지막에 맞게.
	FSimpleMulticastDelegate OnContentsLoaded;

	//~ IVBSaveParticipant
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

private:
	bool HasWriteAuthority() const;

	// 태그·이름뿐이라 GC 가 볼 것이 없다 - UPROPERTY 가 아니어도 된다.
	TMap<FName, TArray<FVBSavedItem>> Remaining;
};
