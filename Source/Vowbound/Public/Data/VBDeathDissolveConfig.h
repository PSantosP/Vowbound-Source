// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VBDeathDissolveConfig.generated.h"

class UMaterialInterface;
class UNiagaraSystem;

/**
 * UVBDeathDissolveConfig
 * 시체가 가루가 되어 사라지는 연출의 튜닝 홈.
 *
 * 무엇: 사망 몽타주가 끝나고 래그돌로 누운 뒤, 몸이 한쪽부터 깎여 나가며 그 경계에서 파편이
 *   떨어지는 표현의 수치 묶음이다. 소비자는 UVBDeathDissolveComponent 하나다.
 *
 * 왜 DataAsset 인가: 표현 수치는 아키타입마다 다르다. 잡몹은 빨리 치워야 하고 보스는 천천히
 *   사라져야 여운이 남는다. 코드에 두면 그 차이가 if 문이 되고, 적이 늘 때마다 그 if 가 자란다.
 *
 * 왜 여기에 Duration 이 있고 DeathCleanupDelay 는 UVBEnemyConfig 에 있는가: 둘은 다른 것을 정한다.
 *   정리 지연은 「시체가 몇 초 뒤 없어지는가」라는 게임플레이 수치고, 여기 길이는 「사라지는 데
 *   몇 초 걸리는가」라는 표현 수치다. 관계는 하나뿐이다 - 후자가 전자 안에 들어가야 한다.
 *   그 검사는 UVBEnemyConfig::IsDataValid 가 한다(정리 지연을 아는 쪽이 거기이기 때문이다).
 *
 * 부작용: 없다(순수 데이터).
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBDeathDissolveConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 깎여 사라지는 데 걸리는 시간(실초). 0 이하 = 이 연출을 쓰지 않는다.
	//
	// 왜 별도의 bEnabled 가 없는가: 켜짐 플래그와 길이를 따로 두면 「켜져 있는데 길이가 0」이라는
	//   조합이 생기고, 그때 무엇이 맞는지를 소비 코드가 또 정해야 한다. 길이 하나가 둘을 다 말한다.
	//   UVBHitFlashConfig 의 IntensityByDamage 가 「키 0개 = 적용 안 함」인 것과 같은 규약이다.
	// 왜 실초인가: 히트스톱(시간 정지) 중에도 화면상 길이가 같아야 한다. 머티리얼이 View.RealTime 을
	//   읽으므로 CPU 쪽 기준도 실시간으로 맞춘다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel", meta=(ClampMin="0.0"))
	float Duration = 1.2f;

	// 깎이는 경계에 얹는 발광색. 재가 되기 직전 달아오르는 자리다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel")
	FLinearColor EdgeColor = FLinearColor(1.0f, 0.45f, 0.18f, 1.0f);

	// 경계 두께. 마스크 값 기준의 정규화 폭이라 0~1 이다. 크면 넓게 달아오르고 작으면 칼금처럼 얇다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel", meta=(ClampMin="0.0", ClampMax="1.0"))
	float EdgeWidth = 0.08f;

	// 경계 발광 세기(Emissive 배수). 1 을 넘겨야 블룸이 붙는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel", meta=(ClampMin="0.0"))
	float EdgeGlow = 6.0f;

	// 어느 방향으로 깎여 나가는가(오브젝트 로컬). 기본은 발끝에서 머리 쪽이다.
	//
	// 왜 로컬인가: 월드 축을 쓰면 여러 시체가 같은 높이에서 동시에 깎인다. 쓰러진 자세는 개체마다
	//   다르므로 각자의 몸을 기준으로 재야 한 구가 하나의 사건으로 읽힌다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel")
	FVector AxisLocal = FVector(0.0f, 0.0f, 1.0f);

	// 노이즈 밀도. 이 값이 파편의 크기감을 정한다 - 작으면 큰 덩어리로 부서지고 크면 고운 가루가 된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel", meta=(ClampMin="0.0"))
	float NoiseScale = 4.0f;

	// 경계 근처 정점을 축 방향으로 미는 거리(cm). 사라지기 직전에 몸에서 떨어져 나가는 인상을 만든다.
	// 0 이면 제자리에서 그냥 지워진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel", meta=(ClampMin="0.0"))
	float Push = 12.0f;

	// 날아가는 파편·먼지. 비면 디졸브만 돈다.
	//
	// 왜 비어도 되는가: 이 연출의 1단계는 머티리얼만으로 완결된다. 파티클은 얹는 층이지 전제가 아니다.
	//   없는 것을 깨진 것으로 취급하면 자산이 준비되기 전까지 사망 연출 전체가 멈춘다(규칙 27).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel")
	TObjectPtr<UNiagaraSystem> DebrisEffect;

	// 위 파편 시스템의 사용자 파라미터 이름. 여기에 죽은 적의 스켈레탈 메시 컴포넌트를 꽂는다.
	//
	// 왜 필요한가: 파편이 몸에서 나오는 것으로 보이려면 파티클이 「그 순간의 그 자세」 표면에서
	//   태어나야 한다. 래그돌로 쓰러진 자세는 자산이 모르므로 런타임에 건네는 수밖에 없다.
	//   UNiagaraFunctionLibrary::OverrideSystemUserVariableSkeletalMeshComponent 가 그 통로다.
	// 규약: NS 쪽에 같은 이름의 Skeletal Mesh 형 User 파라미터가 있어야 하고, 그것을
	//   Skeletal Mesh Location 계열 모듈이 소비해야 한다. 이름이 어긋나면 조용히 무동작이므로
	//   런타임이 꽂은 사실을 로그로 남긴다.
	// 비우면 꽂지 않는다 - 메시를 안 쓰고 위치에서 그냥 터지는 파편 시스템도 쓸 수 있어야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel")
	FName DebrisMeshParameterName = TEXT("SkeletalMesh");

	// 파편에 쓸 재질. 런타임에 이것으로 MID 를 만들어 죽는 몬스터의 텍스처를 꽂는다.
	//
	// 왜 자산이 들고 있나: Niagara 컴포넌트는 재질 슬롯이 0개라 거기서 꺼낼 수 없다(2026-09-20 실측
	//   `get_num_materials() == 0`). 그렇다고 코드에 경로를 박으면 규칙 26 의 하드코딩이다.
	//   DebrisEffect 와 짝이므로 같은 자산에 둔다 - 둘은 언제나 함께 바뀐다.
	// 비어 있으면 색 주입을 건너뛰고 Niagara 렌더러의 재질이 그대로 쓰인다(무해한 축소).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel")
	TObjectPtr<UMaterialInterface> DebrisMaterial;

	// 파편 Niagara 의 재질 User 파라미터 이름. 런타임에 그 몬스터의 텍스처를 꽂은 MID 를 여기로 넘긴다.
	//   DebrisMeshParameterName 과 같은 이유로 자산에 둔다 - 이름이 어긋나면 엔진이 조용히 무시하므로
	//   리터럴을 코드에 흩뿌리면 어디서 틀렸는지 못 찾는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel")
	FName DebrisMaterialParameterName = TEXT("DebrisMaterial");
};
