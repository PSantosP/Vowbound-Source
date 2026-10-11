// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h" // FGameplayTag 를 값으로 받는다 - 전방선언 불가
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Data/VBReachRules.h"            // 기본 인자라 완전한 타입이 필요하다.
                                         // 설정 자산(VBMedicalConfig.h)이 아니라 이 구조체만 끌어온다 -
                                         // 이 헤더를 15개 파일이 포함하므로 GameplayEffect.h 까지
                                         // 딸려 가면 안 된다.
#include "VBAbilitySystemStatics.generated.h"

class UAbilitySystemComponent;
struct FGameplayAbilitySpec;

/**
 * GAS 상태 질의의 공용 홈. 상태를 갖지 않는 정적 헬퍼만 둔다.
 *
 * 왜 생겼나(2026-08-13 전수 감사): "이 액터가 죽었는가"가 6곳에 각자 구현돼 있었고,
 * ASC 를 얻는 방법조차 네 갈래였다 - 멤버 캐시 / 멤버 직접 /
 * UAbilitySystemGlobals::GetAbilitySystemComponentFromActor / Cast<IAbilitySystemInterface> /
 * UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent.
 * 사망 의미가 바뀌는 날(예: '다운' 중간 상태 추가) 여섯 파일을 찾아다녀야 하고,
 * 일부만 고치면 AI 는 시체를 계속 노리는데 근접 타겟 필터는 걸러내는 식으로 조용히 갈라진다.
 * 판정과 획득을 각각 한 곳으로 모은다.
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
	// IAbilitySystemInterface 를 먼저 보고, 없으면 컴포넌트를 찾는다(UE5.8 기본값 LookForComponent=true).
	UFUNCTION(BlueprintPure, Category="Vowbound|GAS")
	static bool IsActorDead(const AActor* Actor);

	// 공격 중 판정의 유일한 구현. ASC 가 null 이면 false.
	// 왜 여기로 뽑았는가: 같은 인라인 질의가 세 곳(플레이어 콤보 입력 라우팅 2곳, 적 토큰 안전망 1곳)이
	// 됐다. IsDead 가 6곳까지 자란 뒤에야 이 클래스가 생겼으므로(FIND-087) 같은 길을 다시 가지 않는다.
	// State.Combat.Attacking 은 계약이 걸린 태그다. 적 공격 GA 의 ActivationOwnedTags 이고,
	// 그 하강 에지가 어택 토큰 반납의 유일한 경로다. 의미가 바뀌면 토큰 회계가 함께 바뀐다.
	static bool IsAttacking(const UAbilitySystemComponent* ASC);

	// 예고(텔레그래프) 중 판정. ASC 가 null 이면 false.
	// 왜 이 질의가 필요한가: 플레이어가 힘든 것은 맞는 횟수가 아니라 동시에 읽어야 하는 개수다.
	// 예고 둘이 겹치면 어느 쪽을 피할지 못 정한다. 그래서 코디네이터가 "누군가 예고 중이면
	// 다음 토큰을 주지 않는다"로 간격을 만든다 - 그 간격의 길이는 아키타입의 TelegraphDuration 이
	// 자동으로 정한다(전역 초 단위 노브를 두면 그 값과 홈이 겹친다).
	static bool IsTelegraphing(const UAbilitySystemComponent* ASC);

	// 처형 창이 열려 있는가. ASC 가 null 이면 false.
	// 태그의 단독 생산자는 UVBGA_FinisherWindow 이고(2026-09-29 부터 켜질 때가 아니라 창이 «열릴 때» 붙인다), 읽는 쪽은 여기를 거친다.
	// 왜 그 GA 의 static 이 아니라 여기인가: 「이 ASC 가 상태 X 인가」의 홈이 이미 이 클래스다
	// (IsDead / IsAttacking / IsTelegraphing). 질의마다 홈을 달리 두면 다음 사람이 어디를 볼지 못 정한다.
	static bool IsInFinisherWindow(const UAbilitySystemComponent* ASC);

	// 액터판. 소비자가 대개 대상 «액터» 만 들고 있어서 매번 ASC 를 뽑는 줄이 붙는다(IsActorDead 와 같은 이유).
	UFUNCTION(BlueprintPure, Category="Vowbound|GAS")
	static bool IsActorInFinisherWindow(const AActor* Actor);

	// 처형 창 GA 가 켜져 있는가(반응이 끝나기를 기다리는 중 + 열림) - State.Combat.FinisherWindowArmed. ASC 가 null 이면 false.
	// 위 IsInFinisherWindow(열림만)와 가른다 - 처형이 걸리는지는 «열림» 이 답하고, «이제부터 창의 몫이다» 는 이것이 답한다.
	// 소비자 둘 = UVBGA_Stagger 의 기절 게이지 소진 거절, UVBBTService_UpdateCombat 의 포커스 게이트(대기·창 동안 플레이어 쪽으로 돌려 세우지 않는다).
	//   액터판은 두지 않는다 - 두 소비자가 모두 ASC 를 이미 들고 있다(BT 서비스는 세 질의에 ASC 하나를 쓴다).
	static bool IsFinisherWindowArmed(const UAbilitySystemComponent* ASC);

	// 경직(Poise 파탄) 중인가 - State.Combat.Staggered. ASC 가 null 이면 false.
	// 그로기(다운)도 여기 걸린다 - UVBGA_Knockdown 이 UVBGA_Stagger 의 파생이라 같은 태그를 함께 든다(design .sw/boss/design.md §14.3).
	// 첫 소비자 = UVBBTService_UpdateCombat 의 포커스 게이트(2026-09-28, user 「엎어진 보스가 나를 향해 계속 회전한다」).
	//   BT 데코레이터가 같은 태그로 이동·공격 분기를 닫지만 그것은 자산 안의 판정이고, 코드 쪽 질의의 홈은 여기 하나다.
	static bool IsStaggered(const UAbilitySystemComponent* ASC);

	// 액터판(IsActorDead 와 같은 이유).
	UFUNCTION(BlueprintPure, Category="Vowbound|GAS")
	static bool IsActorStaggered(const AActor* Actor);

	// 그로기(다운)로 엎드려 있는가 - State.Combat.Downed. ASC 가 null 이면 false. 생산자는 UVBGA_Knockdown 하나(루즈태그, 서버).
	// Downed 는 언제나 Staggered 와 함께 선다(Knockdown 이 Stagger 파생) - «경직 중인가» 만 알면 되는 소비자는 IsStaggered 를 쓴다.
	//   이 질의는 «엎드린 몸» 이 다른 판단을 바꾸는 자리용이다(사망 몽타주·페이즈 연출·넉백·반격 감시). 2026-09-29 인라인 여섯 곳을 여기로 모았다.
	static bool IsDowned(const UAbilitySystemComponent* ASC);

	UFUNCTION(BlueprintPure, Category="Vowbound|GAS")
	static bool IsActorDowned(const AActor* Actor);

	// 처형을 걸 수 있는 대상을 찾는다. 없으면 nullptr.
	//
	// 자격 세 가지 — ①처형 창이 열려 있다(IsActorInFinisherWindow) ②MaxRange 안이다
	//   ③대상이 처형을 허용한다(UVBEnemyConfig::bAllowFinisher. 보스가 여기서 빠진다).
	// 고르는 순서: 락온 대상이 자격을 갖췄으면 그것, 아니면 전방 원뿔에서 찾는다.
	//   필터를 점수 경쟁 «안» 에 넣으므로 「자격 없는 최상위 후보」가 뒤를 가리지 않는다.
	//
	// 왜 어빌리티가 아니라 여기인가(2026-09-19): 소비자가 셋이 됐다 —
	//   UVBGA_Execute · UVBGA_Purify(둘 다 UVBGA_MedicalAbilityBase::FindMedicalTarget 경유)와
	//   UVBGA_LightAttack(공격 버튼으로 처형에 들어가는 새 경로, user 결정 2026-09-19
	//   「공격 버튼만 눌러도 바로 자연스럽게 처형 되게끔」). 셋째가 붙는 순간 판정이 어빌리티
	//   계층에 갇혀 있으면 안 된다(규칙 12). 「이 액터가 상태 X 인가」의 홈이 이미 이 클래스다.
	// MaxRange 를 인자로 받는 이유: 사거리는 어빌리티 설정(UVBMedicalConfig::MedicalRange)의 것이고
	//   여기로 옮기면 같은 값이 두 곳에 산다. 판정만 가져오고 값은 부르는 쪽에 남긴다.
	// Rules 는 «그 자리에서 짝 애니가 성립하나» 를 본다(높이·야우·시야). 기본값은 GASP 실측치다 -
	//   설정을 안 만든 호출자가 조용히 무검사로 떨어지지 않게 하려는 것이다(규칙 27).
	static AActor* FindFinisherTarget(const AActor* Attacker, float MaxRange,
	                                  const FVBReachRules& Rules = FVBReachRules());

	// From 이 그 자리에서 TargetLocation 에 «닿는가» - 높이차 · 방향각 · 시야(FVBReachRules).
	//
	// 왜 따로 뺐나(2026-09-23): 처형 대상 고르기와 환경 상호작용이 같은 질문을 한다. 한쪽에 람다로 두면
	//   다른 쪽이 복제하게 된다. 거리는 여기서 보지 않는다 - 사거리는 기능마다 다른 값의 홈을 가진다.
	// FacingYaw 를 인자로 받는 이유: 기준 방향이 기능마다 다르다. 처형은 몸 방향(짝 애니가 몸 기준으로
	//   저작됐다), 상호작용은 카메라 방향(플레이어가 «보고 있는 것» 을 고른다).
	// IgnoredActor 는 시야 스윕에서 뺀다 - 대상 자신이 자기를 가리면 안 된다.
	static bool PassesReachRules(const AActor* From, float FacingYaw, const FVector& TargetLocation,
	                             const AActor* IgnoredActor, const FVBReachRules& Rules);

	// 이 액터가 «처형을 거는 중» 인가. 위 IsActorInFinisherWindow(당하는 쪽)와 짝이다.
	//
	// 왜 있나: 처형 중에는 다른 애니메이션이 이 몸에 재생되면 안 된다(user 결정 2026-09-13).
	// GA 인 생산자는 UVBGA_Execute 의 차단 목록과 반응 GA 의 자기 차단이 맡고, GA 가 아닌
	// 진입점(무기 소환·납도, 트래버설)은 이 질의로 스스로 물러난다.
	// 왜 진입점마다 조건을 따로 쓰지 않나: 그러면 그것이 곧 다음 결함이다. 같은 질문은 같은 함수로
	// 묻게 해야 소유자가 늘 때 한 곳만 고친다(2026-09-13 TIP 게이트를 뒤집으며 얻은 판단과 같다).
	UFUNCTION(BlueprintPure, Category="Vowbound|GAS")
	static bool IsActorFinishing(const AActor* Actor);

	// 이 타격이 아군 오사인가. true 면 호출부는 그 대상을 건너뛴다.
	//
	// 정책의 유일한 홈이다 - 호출부가 "Friendly 면 skip" 을 각자 쓰면 그 판단이 공격 경로 수만큼 복제된다.
	// 판정 자체는 엔진 정본에 위임한다(FGenericTeamId::GetAttitude) -
	// attitude 를 손으로 계산하는 코드를 새로 만들지 않는다는 규율(reuse-catalog EVBTeam)을 지킨다.
	//
	// Neutral 은 통과시킨다(= 오사가 아니다). 실제 문제는 "같은 편을 때린다" 하나이고,
	// 팀 정보가 없다는 이유로 데미지를 죽이면 IGenericTeamAgentInterface 를 구현하지 않는 대상
	// (파괴 가능 오브젝트 등)이 조용히 무적이 된다. 부재를 이유로 한 거부는 조용한 실패를 만든다.
	// 엔진 의미론(직접 확인): AIInterfaces.cpp::DefaultTeamAttitudeSolver 는 A != B ? Hostile : Friendly
	// 뿐이고 NoTeam(255) 특례가 없다. Neutral 은 A 가 IGenericTeamAgentInterface 를 구현하지 않을 때만 나온다.
	UFUNCTION(BlueprintPure, Category="Vowbound|GAS")
	static bool IsFriendlyFire(const AActor* Attacker, const AActor* Target);

	// InputTag 로 활성화 가능한 어빌리티 스펙을 찾는다. 없으면 nullptr.
	// 왜 공용 홈인가: BT 태스크와 전투 컴포넌트가 같은 순회를 각자 돌게 되기 때문이다.
	// 순회 중 FScopedAbilityListLock 을 건다 - 순회 도중 부여/제거되면 배열이 재할당된다(FIND-087 ②).
	//
	// 반환 포인터의 수명 계약: ASC 의 ActivatableAbilities 배열 원소를 가리킨다. 어빌리티 부여/제거가
	// 일어나면 재할당으로 무효가 되므로, 호출부는 반환 직후 같은 프레임에 Handle/Ability 만 읽고 버릴 것.
	// 보관하지 말 것.
	// ASC 가 const 가 아닌 이유: FScopedAbilityListLock 생성자가 비-const 참조를 요구한다(엔진 GameplayAbilitySpec.h).
	static const FGameplayAbilitySpec* FindActivatableAbilitySpecByInputTag(
			UAbilitySystemComponent* ASC, FGameplayTag InputTag);
};
