// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "VBTeamTypes.generated.h"

/**
 * 세력 명부. 값은 곧 엔진 FGenericTeamId 다.
 *
 * 왜 열거형인가: 세력은 튜닝값이 아니라 정체성이다(EVBWeaponType 과 같은 부류).
 *   "누가 어느 세력인가"는 데이터로 정하지만(UVBEnemyConfig::Team, AVBCharacter::Team),
 *   "어떤 세력이 존재하는가"는 코드가 아는 목록이어야 분기와 태그가 그 위에 설 수 있다.
 *
 * 적대 판정은 엔진 기본 규칙을 그대로 쓴다 - 같은 ID = Friendly, 다른 ID = Hostile, NoTeam = Neutral.
 *   즉 세력을 나눠 두는 것만으로 "나중에 세력끼리 싸운다"가 이미 열려 있다. 새 세력은 여기 한 줄이다.
 *   동맹(서로 다른 ID 인데 Friendly)이 필요해지면 FGenericTeamId::SetAttitudeSolver 가 확장점이다.
 */
UENUM(BlueprintType)
enum class EVBTeam : uint8
{
	// 255 = FGenericTeamId::NoTeam. 이 값이면 모두에게 Neutral 이고,
	// 지각 설정이 bDetectNeutrals=false 이므로 AI 에게 보이지 않는다. 소품/장식 전용.
	None     = 255 UMETA(DisplayName = "None (소속 없음 - AI 에게 안 보임)"),

	Player   = 0   UMETA(DisplayName = "Player"),
	Infected = 1   UMETA(DisplayName = "Infected"),
};

namespace VBTeam
{
	// EVBTeam -> 엔진 팀 ID. 열거형 값이 곧 ID 라 변환은 단순 캐스트지만,
	// 캐스트를 호출처마다 흩으면 "어느 쪽이 진짜 ID 인가"가 흐려지므로 통로를 하나로 둔다.
	FORCEINLINE FGenericTeamId ToGenericId(EVBTeam Team)
	{
		return FGenericTeamId(static_cast<uint8>(Team));
	}
}
