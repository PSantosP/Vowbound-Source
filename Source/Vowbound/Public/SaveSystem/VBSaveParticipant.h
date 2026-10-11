// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "VBSaveParticipant.generated.h"

class UVBSaveGame;
class UWorld;
class APawn;
class APlayerController;
class APlayerState;

/**
 * 저장·적용 한 번의 문맥. 저장 서브시스템이 만들어 모든 참여자에게 같은 것을 준다.
 * 리슨 서버에서는 PlayerState 가 여럿이라, 플레이어의 것을 쓰는 참여자는 문맥의 PlayerState 가 자기인지 보고 움직인다.
 */
struct FVBSaveContext
{
	UWorld* World = nullptr;
	APawn* Pawn = nullptr;
	APlayerController* PlayerController = nullptr;
	APlayerState* PlayerState = nullptr;

	// 체크포인트(설계 .sw/prologue-opening 3.6a): 위치 구역이 폰의 지금 자리 대신 이 자리를 쓴다. 비면 지금 자리.
	//   무기를 주운 «상태» 와 창 착지 부근 «자리» 를 한 번에 저장하려면 둘을 따로 정할 수 있어야 한다.
	TOptional<FTransform> RestoreTransformOverride;
};

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UVBSaveParticipant : public UInterface
{
	GENERATED_BODY()
};

/**
 * 게임 슬롯에 자기 구역을 쓰고 읽는 것. 설계 .sw/narrative 4절·9.3절.
 *
 * 왜 인터페이스인가: 종전에는 UVBSaveGameSubsystem::SaveCurrentGameToSlot 과 AVBGameMode 의 Continue 적용이 대상마다 직접 불렀다
 *   (PlayerState 빌드·상호작용 상태·위치). 사실·인벤토리·퀘스트·상점이 붙으면 일곱이 되고, 저장과 적용 두 곳을 매번 같이 고쳐야 한다.
 *   참여자가 스스로 월드의 목록(UVBSaveParticipantSubsystem)에 등록하면 저장 서브시스템은 그 목록을 돌 뿐 누가 있는지 모른다.
 * 등록 시점: 적용(Continue)은 AVBGameMode::HandleStartingNewPlayer 에서 월드 BeginPlay 보다 먼저 돈다(UEngine::LoadMap 이
 *   SpawnPlayActor 뒤에 BeginPlay) - BeginPlay 가 아니라 더 이른 자리(PostInitializeComponents·InitializeComponent·OnRegister·
 *   서브시스템 Initialize)에서 등록하고 반대 자리에서 해제한다.
 * 쓰기·읽기는 서버에서만 불린다. 자기 구역만 건드린다 - 남의 구역을 읽어 판단하면 순서에 기대게 된다(지금은 순서 칸이 없다, 9.3절).
 */
class VOWBOUND_API IVBSaveParticipant
{
	GENERATED_BODY()

public:
	// 저장 직전. Save 의 자기 구역을 지금 상태로 채운다.
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) = 0;

	// 불러온 세이브를 적용할 때(Continue). 새 게임이면 불리지 않는다.
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) = 0;
};
