// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "VBAN_ItemUseMarker.generated.h"

class UAnimMontage;

/**
 * 아이템 사용 동작의 «확정» 시각. 설계 .sw/quickslot-input 6·6.1절.
 *
 * 서버에서 소유자 ASC 로 Event.Item.UseMarker 를 보낸다. UVBGA_UseItem 이 그것을 받아 소모·효과를 확정한다 -
 *   이 시각 전에 끊기면 아무것도 소모하지 않는다. 서버도 몽타주를 틀므로 서버에서 발화한다(공격 판정 노티와 같은 전제).
 * 몽타주마다 정확히 하나다. 0 이면 쓰는 시늉만 하고 확정이 영영 안 오고, 둘 이상은 저작 실수다 -
 *   UVBItemDefinition::IsDataValid 가 CountIn 으로 센다.
 */
UCLASS(DisplayName = "Item Use Marker")
class VOWBOUND_API UVBAN_ItemUseMarker : public UAnimNotify
{
	GENERATED_BODY()

public:
	UVBAN_ItemUseMarker();

	// 몽타주 자신과 그 슬롯 조각이 가리키는 애니에 놓인 표식 수. 조각의 애니에 놓인 노티도 몽타주 재생 중에 발화하므로 함께 센다.
	//   같은 애니가 조각 둘에 쓰이면 둘로 센다 - 재생도 두 번 발화한다.
	static int32 CountIn(const UAnimMontage& Montage);

	// 사용 몽타주가 사용 GA 의 약속을 지키나 - 지키지 않는 것의 목록(비면 유효). 아이템 IsDataValid 와 시험이 같이 쓴다.
	//   표식이 정확히 하나이고 몽타주 자체에 있다(조각 애니에 두면 그 애니를 쓰는 다른 몽타주에도 생기고 시각을 몽타주 기준으로 잴 수 없다).
	//   표식이 자동 블렌드아웃 시작보다 앞이다 - 사용 GA 는 블렌드아웃에서 끝나므로 그 뒤의 표식은 확정을 못 낸다.
	//   끝난다 - 자동 블렌드아웃이 켜져 있고 섹션이 되돌아오지 않는다. 끝나지 않으면 사용 GA 가 계속 켜져 공격·상호작용이 막힌다.
	//   섹션이 순서대로 이어진다고 보고 «끝» = 몽타주 길이로 잰다.
	static TArray<FText> FindUseMontageProblems(const UAnimMontage& Montage);

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	                    const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};
