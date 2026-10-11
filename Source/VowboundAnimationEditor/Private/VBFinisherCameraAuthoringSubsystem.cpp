// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "VBFinisherCameraAuthoringSubsystem.h"

#include "VBFinisherCameraAuthoringSession.h"
#include "Data/VBFinisherConfig.h"

UVBFinisherCameraAuthoringSession* UVBFinisherCameraAuthoringSubsystem::FindOrCreateSession(UVBFinisherConfig* Config)
{
	// 자산이 사라진 세션은 먼저 거둔다. 세션 스스로도 다음 동작에서 거두지만, 다시 안 불릴 수도 있다.
	Sessions.RemoveAll([](UVBFinisherCameraAuthoringSession* Session)
	{
		if (Session && !Session->IsAlive())
		{
			Session->ReleasePreview();
			return true;
		}
		return Session == nullptr;
	});

	if (!Config)
	{
		return nullptr;
	}

	for (UVBFinisherCameraAuthoringSession* Session : Sessions)
	{
		if (Session->GetConfig() == Config)
		{
			return Session;
		}
	}

	// RF_Transactional: 패널에서 저작 칸(키 번호·미리보기 시각·따라가기)을 고친 것이 되돌리기에 오르게 한다.
	//   1b 전에는 이 칸들이 자산에 있었고, 에셋 편집기가 편집 대상에 이 플래그를 붙여 줬다
	//   (엔진 SimpleAssetEditor.cpp::FSimpleAssetEditor::InitAssetEditor). 외부 객체 칸으로 끼운 세션에는 그 처리가 없다
	//   - 빠뜨리면 Ctrl+Z 가 칸 변경을 건너뛰고 그 전 자산 편집을 되돌린다(2026-10-03 codex-claude_02 검토).
	//   플래그는 만든 «뒤에» 붙인다. 만들 때부터 붙이면, 열린 되돌리기 기록 안에서 패널이 처음 그려질 때
	//   세션의 생성 자체가 그 기록에 잡혀 Ctrl+Z 가 세션을 지울 수 있다(엔진 UObjectGlobals.cpp::StaticAllocateObject
	//   - 2026-10-03 codex-claude_02 검토).
	UVBFinisherCameraAuthoringSession* Session =
		NewObject<UVBFinisherCameraAuthoringSession>(this, NAME_None, RF_Transient);
	Session->SetFlags(RF_Transactional);
	Session->Initialize(Config);
	Sessions.Add(Session);
	return Session;
}

void UVBFinisherCameraAuthoringSubsystem::Deinitialize()
{
	// 에디터가 내려간다. 레벨의 임시 액터와 재생 타이머를 남기지 않는다.
	for (UVBFinisherCameraAuthoringSession* Session : Sessions)
	{
		if (Session)
		{
			Session->ReleasePreview();
		}
	}
	Sessions.Reset();
	Super::Deinitialize();
}
