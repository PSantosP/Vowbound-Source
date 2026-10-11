// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VBDeathDissolveSettings.generated.h"

class UVBDeathDissolveConfig;

/**
 * 사망 디졸브의 프로젝트 전역 기본값.
 * Project Settings > Game > Vowbound Death Dissolve 에 나타나고 Config/DefaultGame.ini 에 저장된다
 * (UVBCombatSettings · UVBGameFlowSettings 와 같은 모양).
 *
 * 왜 생겼나 (2026-09-20, user 지시 「모든 몬스터 다 되게끔 구조를 만들었어야지」):
 *   종전에는 적 설정(UVBEnemyConfig::DeathDissolve)이 비어 있으면 이 연출이 그냥 안 났다.
 *   새 몬스터를 넣으면 기본이 «없음» 이라 아무 경고 없이 조용히 빠진다 - 실제로 보스 트롤이 그랬다.
 *   여기 기본을 두면 배선의 뜻이 뒤집힌다: 적 설정은 «켜는 자리» 가 아니라 «이 적만 다르게 하는 자리» 가 된다.
 *
 * 왜 DataAsset 포인터가 아니라 설정인가: 「아무도 정하지 않았을 때 무엇을 쓰나」의 답은 프로젝트에 하나뿐이다.
 *   자산으로 두면 그것을 가리키는 포인터가 또 생기고, 하나만 다른 것을 가리키는 사고가 가능해진다.
 *   GetDefault<T>() 는 배선 없이 닿으므로 가리킬 포인터 자체가 없다(UVBCombatSettings 주석과 같은 근거).
 *
 * 왜 Soft 인가: 이 값은 «죽을 때» 처음 필요하다. 하드 참조면 설정이 로드될 때 파편 Niagara 까지 끌려 들어와
 *   부팅 시점에 그 비용을 낸다. 필요한 순간에 동기 로드해도 프레임 하나 안에 끝나는 크기다.
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="Vowbound Death Dissolve"))
class VOWBOUND_API UVBDeathDissolveSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 적 설정이 비어 있을 때 쓸 디졸브 자산.
	//
	// 비워 두면 이 연출이 프로젝트 전체에서 꺼진다 - 그것이 이 필드의 «끄는 법» 이기도 하다.
	//   끌 때 코드를 고치지 않아도 되도록 값 하나가 켜짐 여부를 겸한다(UVBDeathDissolveConfig::Duration 과 같은 규약).
	UPROPERTY(config, EditAnywhere, Category="Vowbound|GameFeel")
	TSoftObjectPtr<UVBDeathDissolveConfig> DefaultConfig;

	// 「이 연출에 쓸 설정이 무엇인가」의 유일한 홈. 명시된 것이 없으면 위의 DefaultConfig 로 떨어진다.
	//
	// 왜 함수로 빼나 (2026-09-20, 값을 치르고): 폴백을 UVBDeathDissolveComponent::ScheduleDissolve 안에만
	//   넣었더니 시험 버튼(AVBEnemyBase::PreviewDeathDissolve)이 자기 null 검사에서 먼저 돌아서 거기까지
	//   못 갔다. 보스 트롤이 그대로 안 됐고 로그 한 줄이 그것을 말해 줬다.
	//   같은 판단이 두 곳에 있으면 한쪽만 고쳐진다.
	// 왜 컴포넌트가 아니라 여기 있나 (2026-10-02, 구조 정리 1a): 이 판단은 «설정을 고르는 것» 이라
	//   설정 쪽이 주인이다. 컴포넌트에 있으면 데이터 검사(UVBEnemyConfig::IsDataValid)가 이 함수 하나 때문에
	//   컴포넌트를 include 해야 했다 - 데이터가 그것을 쓰는 쪽을 아는 방향이다.
	// 널을 돌려줄 수 있다. 그때는 프로젝트 설정도 비어 있다는 뜻이고, 그것이 「이 연출을 끄는 법」이다.
	static UVBDeathDissolveConfig* ResolveConfig(UVBDeathDissolveConfig* Explicit);
};
