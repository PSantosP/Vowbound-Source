// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"   // ECollisionChannel
#include "VBReachRules.generated.h"

/**
 * «그 자리에서 닿는가» 조건 - 높이차 · 방향각 · 시야. 판정은 UVBAbilitySystemStatics::PassesReachRules 하나다.
 * 처형 대상 고르기(GASP 의 `CheckForInteractableCharacters` 를 옮긴 것)에서 시작했고,
 *   2026-09-23 에 환경 상호작용이 같은 질문을 하게 되어 이름을 범용으로 올렸다(구 FVBFinisherTargetRules).
 *   같은 조건을 상호작용 쪽에 따로 두면 판정이 두 벌이 된다. 자산의 옛 이름은 DefaultEngine.ini 의
 *   StructRedirects 가 잇는다.
 *
 * 왜 사거리와 따로인가: 사거리는 「얼마나 멀리서 걸 수 있나」라는 기능의 값이고,
 *   여기 셋은 「그 자리에서 짝 애니가 성립하나」라는 자산 쪽 조건이다. 성격이 달라서 같이 두면
 *   사거리를 튜닝하려다 정렬 조건을 건드리게 된다.
 * 왜 필요한가(2026-09-22 실측): GASP 는 이 셋을 다 보는데 우리는 하나도 안 봤다.
 *   상대가 다른 높이에 있거나 벽 뒤에 있어도 짝을 지었고, 그 뒤 모션워핑이 두 몸을 끌어당기면
 *   지형을 뚫거나 한쪽만 끌려간다. 짝 애니는 두 몸이 «같은 바닥에 마주 서 있다» 를 전제로 저작된다.
 */
USTRUCT(BlueprintType)
struct FVBReachRules
{
	GENERATED_BODY()

	// 두 액터의 Z 차이가 이보다 크면 제외(cm). GASP 는 NearlyEqual 의 ErrorTolerance 50 으로 같은 판정을 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Reach", meta=(ClampMin="0.0"))
	float MaxHeightDifference = 50.0f;

	// 기준 방향에서 «야우로» 이 각도 안이어야 한다(도). GASP 는 ±60 이다.
	// 기준 방향은 부르는 쪽이 정한다 - 처형은 공격자 몸 방향, 상호작용은 카메라(컨트롤) 방향.
	// 왜 야우인가: 우리 원뿔 판정(UVBTargetLockComponent::FindConeTarget)은 3D 내적이라 높이가 각도에
	//   섞여 든다. 높이는 위의 MaxHeightDifference 가 따로 보므로 여기서는 평면 각만 본다 - 그래야
	//   두 조건이 서로를 가리지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Reach", meta=(ClampMin="0.0", ClampMax="180.0"))
	float MaxYawAngle = 60.0f;

	// 둘 사이를 캡슐로 쓸어 막히는 것이 있으면 제외한다. 벽 너머 처형을 막는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Reach")
	bool bRequireLineOfSight = true;

	// 그 캡슐의 치수(cm). GASP 는 r20 / h60 이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Reach", meta=(ClampMin="1.0"))
	float LineOfSightRadius = 20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Reach", meta=(ClampMin="1.0"))
	float LineOfSightHalfHeight = 60.0f;

	// 무엇에 막히는가.
	//
	// GASP 는 프로파일 이름 `CharacterCapsule` 로 쓴다 - 즉 「캐릭터가 지나갈 수 있는 길인가」를 묻는다.
	//   그 프로파일은 이 프로젝트에 없다(2026-09-22 실측: Config/ 와 Source/ 전체에서 0건, 커스텀 프로파일은
	//   BlockAll · Pawn · CharacterMesh · IgnoreOnlyPawn 등이고 CharacterCapsule 은 0건).
	//   그래서 이름을 그대로 못 쓰고 «같은 질문을 하는 채널» 로 옮긴다.
	// ECC_Pawn 인 이유: 폰을 막는 것이 곧 캐릭터가 못 지나가는 것이다. 월드 지오메트리에 더해
	//   «사이에 낀 다른 적» 도 막는데, 그것도 GASP 와 같은 판정이다(대상과 자신은 무시 목록에 있다).
	//   ECC_Visibility 를 쓰면 시각적으로만 가리는 것(반투명 벽·데칼)에 걸리고 정작 몸이 못 지나가는
	//   낮은 난간은 통과한다 - 묻는 질문이 다르다.
	// 데이터로 두는 이유: 막을 대상이 바뀌면 빌드 없이 따라가야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Reach")
	TEnumAsByte<ECollisionChannel> LineOfSightChannel = ECC_Pawn;
};
