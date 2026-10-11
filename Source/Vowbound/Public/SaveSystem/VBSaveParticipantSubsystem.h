// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/WeakInterfacePtr.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "VBSaveParticipantSubsystem.generated.h"

/**
 * 이 월드의 저장 참여자 목록. 설계 .sw/narrative 9.3절.
 *
 * 왜 저장 서브시스템(게임 인스턴스) 이 아니라 월드에 두나: PIE 는 월드를 초기화한 «뒤» 에 게임 인스턴스를 붙인다
 *   (UEditorEngine::CreatePIEWorldByDuplication 이 InitWorld 를 부르고, 그 뒤 UGameInstance::InitializeForPlayInEditor 가 SetGameInstance).
 *   그래서 월드 서브시스템(상호작용 등)이 초기화 때 게임 인스턴스 쪽에 등록하려 하면 PIE 에서만 조용히 빠진다 - 저장이 비어도 아무도 모른다.
 *   월드는 참여자가 생길 때 언제나 이미 있다. 레벨이 바뀌면 이 목록도 같이 사라지므로 옛 월드의 참여자를 거를 일도 없다.
 * 월드 서브시스템인 참여자는 Initialize 에서 Collection.InitializeDependency<UVBSaveParticipantSubsystem>() 로 이것을 먼저 세운 뒤 등록한다.
 */
UCLASS()
class VOWBOUND_API UVBSaveParticipantSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 참여자의 월드에서 목록을 찾아 등록·해제한다. 월드가 없으면(에디터 미리보기 등) 아무것도 안 한다.
	static void Register(IVBSaveParticipant* Participant);
	static void Unregister(IVBSaveParticipant* Participant);

	// 살아 있는 참여자만.
	TArray<IVBSaveParticipant*> GetParticipants();

	// 참여자 모두에게 세이브를 읽힌다. 읽힌 참여자 수를 돌려준다. 끝나면 OnSaveApplied 를 한 번 방송한다.
	//   돌려주는 수는 «건넨 수» 지 «적용한 수» 가 아니다. 참여자는 문맥의 플레이어가 자기 것이 아니면 조용히 넘기고(리슨 서버의 다른 플레이어 - 정상),
	//   권한이 없으면 각자 경고를 낸다. 2차 검증 3(부분 로드가 안 보인다)에 인터페이스를 «적용했나» 를 돌려주게 바꾸지 않은 이유가 이것이다 -
	//   조용한 길은 정상인 경우 하나뿐이고, 거절은 이미 소리를 낸다. 참여자 일곱을 다 고치는 값에 비해 새로 보이는 것이 없다(2026-10-02 vowbound-29).
	int32 ReadAll(const UVBSaveGame& Save, const FVBSaveContext& Context);

	// ReadAll 이 참여자를 하나씩 읽히는 동안 참. 읽기 순서는 등록 순서라 정해져 있지 않다 -
	//   한 참여자의 상태 변화를 받아 다른 참여자의 상태로 판단하는 쪽(퀘스트가 이야기 사실을 보고 단계를 넘기고 보상을 준다)은
	//   아직 안 읽힌 쪽의 낡은 값을 보게 된다. 그런 판단은 이 값이 참이면 미뤘다가 OnSaveApplied 때 한 번 한다(검증 W3, 2026-10-02).
	bool IsApplyingSave() const { return bApplyingSave; }

	FSimpleMulticastDelegate OnSaveApplied;

private:
	TArray<TWeakInterfacePtr<IVBSaveParticipant>> Participants;

	bool bApplyingSave = false;

	static UVBSaveParticipantSubsystem* FindFor(IVBSaveParticipant* Participant);
};
