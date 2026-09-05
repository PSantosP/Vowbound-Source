// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h" // FGameplayTag 를 값으로 받는다 - 전방선언 불가
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBAbilitySystemStatics.generated.h"

class UAbilitySystemComponent;
struct FGameplayAbilitySpec;

/**
 * GAS 상태 질의의 공용 홈. 상태를 갖지 않는 정적 헬퍼만 둔다.
 *
 * 왜 생겼나(2026-08-13 전수 감사): "이 액터가 죽었는가"가 6곳에 각자 구현돼 있었고,
 *   ASC 를 얻는 방법조차 네 갈래였다 - 멤버 캐시 / 멤버 직접 /
 *   UAbilitySystemGlobals::GetAbilitySystemComponentFromActor / Cast<IAbilitySystemInterface> /
 *   UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent.
 *   사망 의미가 바뀌는 날(예: '다운' 중간 상태 추가) 여섯 파일을 찾아다녀야 하고,
 *   일부만 고치면 AI 는 시체를 계속 노리는데 근접 타겟 필터는 걸러내는 식으로 조용히 갈라진다.
 *   판정과 획득을 각각 한 곳으로 모은다.
 */
UCLASS()
class VOWBOUND_API UVBAbilitySystemStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// 사망 판정의 유일한 구현. ASC 를 이미 들고 있는 호출처가 쓴다(멤버 캐시 등).
	// ASC 가 null 이면 false - 사망 개념이 없는 대상은 죽지 않은 것으로 본다.
	static bool IsDead(const UAbilitySystemComponent* ASC);

	// 액터만 있는 호출처가 쓴다. ASC 획득까지 여기가 책임진다 -
	// 호출처가 각자 획득 방법을 고르면 그 방법이 갈리고 사망 판정도 함께 갈린다.
	// 획득은 엔진 정본(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor)을 쓴다:
	//   IAbilitySystemInterface 를 먼저 보고, 없으면 컴포넌트를 찾는다(UE5.8 기본값 LookForComponent=true).
	UFUNCTION(BlueprintPure, Category="Vowbound|GAS")
	static bool IsActorDead(const AActor* Actor);

	// 공격 중 판정의 유일한 구현. ASC 가 null 이면 false.
	// 왜 여기로 뽑았는가: 같은 인라인 질의가 세 곳(플레이어 콤보 입력 라우팅 2곳, 적 토큰 안전망 1곳)이
	//   됐다. IsDead 가 6곳까지 자란 뒤에야 이 클래스가 생겼으므로(FIND-087) 같은 길을 다시 가지 않는다.
	// State.Combat.Attacking 은 계약이 걸린 태그다. 적 공격 GA 의 ActivationOwnedTags 이고,
	//   그 하강 에지가 어택 토큰 반납의 유일한 경로다. 의미가 바뀌면 토큰 회계가 함께 바뀐다.
	static bool IsAttacking(const UAbilitySystemComponent* ASC);

	// 예고(텔레그래프) 중 판정. ASC 가 null 이면 false.
	// 왜 이 질의가 필요한가: 플레이어가 힘든 것은 맞는 횟수가 아니라 동시에 읽어야 하는 개수다.
	//   예고 둘이 겹치면 어느 쪽을 피할지 못 정한다. 그래서 코디네이터가 "누군가 예고 중이면
	//   다음 토큰을 주지 않는다"로 간격을 만든다 - 그 간격의 길이는 아키타입의 TelegraphDuration 이
	//   자동으로 정한다(전역 초 단위 노브를 두면 그 값과 홈이 겹친다).
	static bool IsTelegraphing(const UAbilitySystemComponent* ASC);

	// 이 타격이 아군 오사인가. true 면 호출부는 그 대상을 건너뛴다.
	//
	// 정책의 유일한 홈이다 - 호출부가 "Friendly 면 skip" 을 각자 쓰면 그 판단이 공격 경로 수만큼 복제된다.
	// 판정 자체는 엔진 정본에 위임한다(FGenericTeamId::GetAttitude) -
	//   attitude 를 손으로 계산하는 코드를 새로 만들지 않는다는 규율(reuse-catalog EVBTeam)을 지킨다.
	//
	// Neutral 은 통과시킨다(= 오사가 아니다). 실제 문제는 "같은 편을 때린다" 하나이고,
	//   팀 정보가 없다는 이유로 데미지를 죽이면 IGenericTeamAgentInterface 를 구현하지 않는 대상
	//   (파괴 가능 오브젝트 등)이 조용히 무적이 된다. 부재를 이유로 한 거부는 조용한 실패를 만든다.
	// 엔진 의미론(직접 확인): AIInterfaces.cpp::DefaultTeamAttitudeSolver 는 A != B ? Hostile : Friendly
	//   뿐이고 NoTeam(255) 특례가 없다. Neutral 은 A 가 IGenericTeamAgentInterface 를 구현하지 않을 때만 나온다.
	UFUNCTION(BlueprintPure, Category="Vowbound|GAS")
	static bool IsFriendlyFire(const AActor* Attacker, const AActor* Target);

	// InputTag 로 활성화 가능한 어빌리티 스펙을 찾는다. 없으면 nullptr.
	// 왜 공용 홈인가: BT 태스크와 전투 컴포넌트가 같은 순회를 각자 돌게 되기 때문이다.
	// 순회 중 FScopedAbilityListLock 을 건다 - 순회 도중 부여/제거되면 배열이 재할당된다(FIND-087 ②).
	//
	// 반환 포인터의 수명 계약: ASC 의 ActivatableAbilities 배열 원소를 가리킨다. 어빌리티 부여/제거가
	//   일어나면 재할당으로 무효가 되므로, 호출부는 반환 직후 같은 프레임에 Handle/Ability 만 읽고 버릴 것.
	//   보관하지 말 것.
	// ASC 가 const 가 아닌 이유: FScopedAbilityListLock 생성자가 비-const 참조를 요구한다(엔진 GameplayAbilitySpec.h).
	static const FGameplayAbilitySpec* FindActivatableAbilitySpecByInputTag(
			UAbilitySystemComponent* ASC, FGameplayTag InputTag);
};
