// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Types/VBWeaponStateTypes.h"   // EVBWeaponType - 무기별 궤적 표의 열쇠
#include "VBMeleeTraceShapeSet.generated.h"

class UVBSwingTrailProfile;

/** 날의 좌표를 어디서 가져오나. */
UENUM(BlueprintType)
enum class EVBMeleeShapeSource : uint8
{
	// 뼈(또는 소켓) 공간에 적은 시작·끝. 맨손·발차기·무기 메시가 없는 적.
	Bone,
	// 지금 든 무기의 외형 파트(UVBWeaponVisualConfig)에서 계산. 보이는 칼이 곧 맞는 칼이다.
	WeaponVisual,
};

/**
 * 판정에 쓰는 «날» 하나의 정의. 설계 .sw/weapon-trace 3절·15절.
 *
 * 칼의 위치·길이를 여기 다시 적지 않는다(WeaponVisual). 외형 데이터에 이미 소켓·붙는 변환·손잡이→칼끝 축·메시가 있어서,
 *   여기에 좌표를 또 적으면 외형만 고친 날 «보이는 칼과 맞는 칼이 다르다» 가 된다. 여기엔 판정 조절값(손잡이 비율·반경)만 둔다.
 */
USTRUCT(BlueprintType)
struct FVBMeleeTraceShapeDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat")
	EVBMeleeShapeSource Source = EVBMeleeShapeSource::Bone;

	// Bone: 붙는 뼈·소켓 이름과 그 공간에서의 날 시작·끝(cm).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat", meta=(EditCondition="Source == EVBMeleeShapeSource::Bone"))
	FName Bone = TEXT("hand_r");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat", meta=(EditCondition="Source == EVBMeleeShapeSource::Bone"))
	FVector BaseLocal = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat", meta=(EditCondition="Source == EVBMeleeShapeSource::Bone"))
	FVector TipLocal = FVector(0.0f, 0.0f, 10.0f);

	// WeaponVisual: 외형 스타일의 몇 번째 파트인가(쌍검이면 둘).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat", meta=(EditCondition="Source == EVBMeleeShapeSource::WeaponVisual", ClampMin="0"))
	int32 VisualPartIndex = 0;

	// WeaponVisual: 메시 축 길이 가운데 손잡이 쪽 몇 할을 날에서 뺄지(0~1). 손잡이·코등이가 맞으면 안 되니까.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat", meta=(EditCondition="Source == EVBMeleeShapeSource::WeaponVisual", ClampMin="0", ClampMax="0.9"))
	float HandleRatio = 0.2f;

	// WeaponVisual 인데 지금 무기에 메시가 없을 때(맨손 Fighter) 대신 쓸 모양. 비면 판정하지 않고 경고한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat", meta=(EditCondition="Source == EVBMeleeShapeSource::WeaponVisual", Categories="Melee.Shape"))
	FGameplayTag FallbackShape;

	// 날 두께 반경(cm). 칼날은 작게, 주먹은 크게. 스윕 점 간격과 보간 단계 수도 이 값이 정한다(틈이 반경을 넘지 않게).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat", meta=(ClampMin="1"))
	float Radius = 8.0f;

	// 이 날의 기본 휘두름 궤적(설계 18절). 궤적은 이 날의 시작~끝을 따라 그린다 - 판정과 같은 모양이다. 궤적 노티가 스윙별로 덮어쓸 수 있다.
	//   대체 모양(FallbackShape)으로 떨어지면 그 모양의 궤적을 쓴다(주먹으로 치면 주먹 궤적).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<UVBSwingTrailProfile> TrailProfile;

	// WeaponVisual: 들고 있는 무기 종류별 궤적. 표에 없으면 위 기본(TrailProfile).
	//   왜 여기인가: 같은 «칼» 모양을 카타나·대검이 함께 쓴다. 표가 없던 때는 대검 몽타주의 궤적 노티마다 대검 궤적을 덮어써야 했다 -
	//   무기나 몽타주가 늘 때마다 덮어쓰기가 따라 늘었다(설계 21절). 무기의 궤적은 무기에 한 번만 적는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat", meta=(EditCondition="Source == EVBMeleeShapeSource::WeaponVisual"))
	TMap<EVBWeaponType, TObjectPtr<UVBSwingTrailProfile>> WeaponTrailProfiles;
};

/**
 * 한 캐릭터가 휘두를 수 있는 날들의 표. UVBMeleeTraceComponent 에 캐릭터 BP 마다 하나 꽂는다(플레이어·병사·보스 각자).
 * 판정 창(UVBANS_AttackTraceWindow)은 태그로 어느 날을 쓸지만 고른다 - 같은 몽타주를 다른 무기가 써도 그 무기의 날로 맞는다.
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBMeleeTraceShapeSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Combat", meta=(Categories="Melee.Shape", ForceInlineRow))
	TMap<FGameplayTag, FVBMeleeTraceShapeDef> Shapes;
};
