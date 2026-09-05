// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VBCombatSettings.generated.h"

/**
 * 적 그룹 전투 조율의 프로젝트 전역 설정.
 * Project Settings > Game > Vowbound Combat 에 나타나고 Config/DefaultGame.ini 에 저장된다
 * (config=Game 이면 엔진 UDeveloperSettings 가 컨테이너/카테고리를 스스로 정한다 - UVBGameFlowSettings 와 동형).
 *
 * 왜 DataAsset 이 아닌가: 이 값들의 소비자는 UVBCombatCoordinatorSubsystem 하나뿐이고 그것은 월드당 하나다.
 *   즉 갈릴 소비자가 없다. DataAsset 으로 두면 "그 자산을 가리키는 포인터"가 생기고, 포인터가 생기는 순간
 *   하나만 다른 것을 가리키는 사고가 가능해진다(FIND-087 실사례 - 같은 값이 위젯 둘에 각각 있었다).
 *   DeveloperSettings 는 GetDefault<T>() 로 배선 없이 닿으므로 가리킬 포인터 자체가 존재하지 않는다.
 *
 * 접미사가 Config 가 아니라 Settings 인 이유: 에디터에서 꽂을 수 있는 자산(UVB*Config)과 꽂을 수 없는
 *   프로젝트 설정을 이름으로 구분한다(UVBGameFlowSettings 헤더 주석과 같은 규약).
 *
 * 난이도 확장: 난이도별 차등이 필요해지면 여기에 키드 프로파일을 컴포지션한다(전역 설정의 홈은 안 바뀐다).
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="Vowbound Combat"))
class VOWBOUND_API UVBCombatSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 한 타겟에게 동시에 붙을 수 있는 공격자의 총 코스트(D2). 잡몹 코스트 1 기준 = 동시 2마리.
	// 되돌리기 노브이기도 하다 - 크게(99) 두면 전원이 Attacker 가 되어 그룹 조율 이전 동작으로 강등된다.
	UPROPERTY(config, EditAnywhere, Category="Vowbound|AI|Coordination", meta=(ClampMin="0"))
	int32 MaxAttackTokens = 2;

	// 타겟 주위 대기 링의 슬롯 개수(FR3). 슬롯 수가 대기 인원보다 적으면 남는 적은 슬롯 없이 제자리 대기한다.
	UPROPERTY(config, EditAnywhere, Category="Vowbound|AI|Coordination", meta=(ClampMin="1"))
	int32 SurroundSlotCount = 8;

	// 대기 반경은 여기 없다. 얼마나 물러나는가는 그 적의 사거리에 상대적인 양이라
	//   UVBEnemyConfig::WaitingStandoff 가 아키타입별로 든다(2026-08-21 이관).
	//   위의 SurroundSlotCount 는 각도 분배라 그룹 개념이므로 전역이 맞다.

	// 역할/슬롯 재계산 주기(초). Tick 을 쓰지 않는 대신 이 타이머가 그룹당 1개 돈다(NFR2).
	UPROPERTY(config, EditAnywhere, Category="Vowbound|AI|Coordination", meta=(ClampMin="0.05"))
	float SlotRecomputeInterval = 0.5f;

	// 토큰 임대 만료(초). 초과가 감지되면 갈림길이 둘이다 - 어느 쪽이든 Warning 은 남는다.
	//   안전망이 조용하면 결함이 영원히 숨기 때문이다.
	//   ASC 태그가 아직 붙어 있으면 = 공격이 정말 길게 도는 중이라 임대를 유지한다(회수하면 스윙 중인
	//     멤버의 슬롯이 빈 것으로 계산돼 동시 공격 상한이 무너진다).
	//   태그가 내려갔는데 반납이 없으면 = 원래 이 안전망이 잡으려던 그 결함이라 그때 회수한다.
	// 이 값은 공격이 시작된 임대에만 걸린다. 접근 구간의 상한은 UVBEnemyConfig::MaxApproachHoldSeconds 다.
	// 값의 의미: "한 번의 공격 행동(텔레그래프+스윙+후딜)이 이보다 오래 걸릴 리 없다"의 상한.
	UPROPERTY(config, EditAnywhere, Category="Vowbound|AI|Coordination", meta=(ClampMin="0.5"))
	float MaxTokenHoldSeconds = 4.0f;

	// 링 슬롯을 네비메시로 투영할 때의 질의 범위(cm). 투영 실패 시 원래 링 좌표를 그대로 쓴다.
	// 왜 투영하는가: 벽 안쪽에 찍힌 슬롯으로 MoveTo 하면 적이 벽을 향해 비빈다.
	UPROPERTY(config, EditAnywhere, Category="Vowbound|AI|Coordination", meta=(ClampMin="0.0"))
	float SlotNavProjectionExtent = 200.0f;
};
