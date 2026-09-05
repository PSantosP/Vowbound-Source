// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Templates/SubclassOf.h" // TSubclassOf — CoreMinimal 은 Core 모듈이라 이 템플릿을 끌어오지 않는다
#include "VBSwingImpactProfile.generated.h"

class UCameraShakeBase;
class FDataValidationContext; // IsDataValid 인자 — 헤더에서 Misc/DataValidation.h 를 끌지 않는다(VBParryConfig.h:11 동형)

/**
 * UVBSwingImpactProfile
 *
 * 스윙 한 번의 임팩트 쉐이크 저작 단위. "어느 쪽으로 밀 것인가 + 무엇으로 밀 것인가 + 얼마나 세게".
 *
 * 왜 구조체(USTRUCT)가 아니라 자산(DataAsset)인가:
 *  - ShakeClass 가 큐를 타고 클라의 GCN 까지 가야 하는데, FGameplayCueParameters 가 실을 수 있는 것은
 *    UObject 참조(SourceObject)뿐이다. USTRUCT 는 큐 파라미터에 실리지 않는다.
 *  - 즉 자산화는 취향이 아니라 전달 경로가 요구하는 형태다.
 *
 * 저작 지점(입도가 굵은 쪽에서 가는 쪽으로):
 *  1) 공격 몽타주의 Attack Trace 노티.SwingImpactProfile — 스윙 단위(1순위)
 *  2) UVBAttackConfig::DefaultSwingImpactProfile — 무기 leaf 단위(폴백)
 *  둘 다 비면 쉐이크가 발동하지 않는다. 코드에 방향 리터럴을 두지 않기 위한 의도적 무연출이다.
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBSwingImpactProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 아바타 로컬 기준 스윙 축. 부호가 곧 밀림 방향이라 별도 반전 플래그가 필요 없다. 길이는 런타임에 정규화된다.
	//
	// 왜 월드 절대 방향이 아닌가: 모션워핑 Facing 이 스윙 중 캡슐을 돌리고, 락온 strafe 는 캡슐을 카메라에 맞춘다.
	//  월드 방향을 저작하면 캐릭터가 돌아선 순간 저작 의도가 사라진다. 로컬 축은 캐릭터를 따라 같이 돈다.
	// 기본값이 정면(1,0,0)인 이유: 가장 흔한 "앞으로 후려침"이 저작 0회로 나와야 첫 배선이 값 하나로 끝난다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel")
	FVector LocalSwingAxis = FVector::ForwardVector;

	// 재생할 카메라 쉐이크 자산(Root Shake Pattern = Wave Oscillator 전제).
	//
	// 무기별 성격 차이가 나는 유일한 지점이다 — 카타나는 짧고 날카롭게, 대검은 길고 무겁게를 자산 교체로 낸다.
	//  그래서 C++ 에 무기 분기가 들어갈 자리가 애초에 없다.
	// 미할당이면 GCN 이 조용히 스킵한다(쉐이크는 순수 코스메틱이라 게임플레이 영향이 없다).
	// 부수 이득: 쉐이크를 자산으로만 지정하므로 EngineCameras(WaveOscillator 구현 모듈) 의존이 Build.cs 에 생기지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel")
	TSubclassOf<UCameraShakeBase> ShakeClass;

	// StartCameraShake 에 넘길 세기 배율. 자산 진폭과 분리한다.
	// 왜 자산 진폭을 직접 쓰지 않는가: 같은 쉐이크를 여러 스윙이 공유하는 순간, 진폭을 건드리면 그 공유가 깨진다.
	//  세기만 스윙별로 다르게 하려면 곱해지는 노브가 프로필 쪽에 있어야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel", meta=(ClampMin="0.0"))
	float ShakeScale = 1.0f;

	// 정규화된 스윙 축. 영벡터 축이면 영벡터를 그대로 돌려준다 — 판별은 호출부 몫이다.
	FVector GetNormalizedAxis() const;

#if WITH_EDITOR
	// 영벡터 축을 에셋 저장/쿡 시점에 잡는다.
	// 왜 런타임 검사로 충분하지 않은가: 런타임에서 영벡터의 증상은 "쉐이크가 조용히 안 난다"뿐이라
	//  저작 실수인지 배선 누락인지 구분이 안 되고 추적이 오래 걸린다. 저장 시점이면 원인이 즉시 이름을 갖는다.
	// 시그니처: UE 5.8 현행은 const 버전(Object.h, #if WITH_EDITOR 안). 비-const/TArray<FText>& 는 5.3 deprecated.
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
