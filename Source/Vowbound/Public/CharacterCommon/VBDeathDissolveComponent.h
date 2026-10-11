// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Containers/Ticker.h" // FTSTicker::FDelegateHandle
#include "Engine/EngineTypes.h" // FTimerHandle
#include "VBDeathDissolveComponent.generated.h"

class UNiagaraComponent;
class USkeletalMeshComponent;
class UVBDeathDissolveConfig;

/**
 * UVBDeathDissolveComponent
 * 시체가 깎여 사라지는 연출의 상태 소유자.
 *
 * 무엇: 사망 정리 시각에 맞춰, 죽은 몸의 표면에서 가루 파티클을 띄우고 원본 메시를 숨긴다.
 *   파티클은 메시의 복제본이라 형태를 그대로 들고 있으므로 «몸이 가루가 됐다» 로 읽힌다.
 *   파편은 월드에 띄운다 - 붙이면 소유자가 적 액터가 되어 시체와 함께 파괴된다(근거는 SpawnDebris 주석).
 *
 * 왜 머티리얼로 깎지 않는가: 그 경로는 몬스터마다 마스터 머티리얼에 그래프를 주입해야 해서
 *   새 몬스터에서 조용히 빠진다(2026-09-20 보스 트롤이 그랬다). 숨기기는 자산 작업을 안 요구한다.
 *
 * 왜 컴포넌트인가: 예약 핸들과 숨긴 메시라는 인스턴스 상태를 누군가 들어야 한다. 액터에 두면
 *   사망 연출과 무관한 코드가 그 멤버를 보게 되고, 플레이어에도 같은 연출이 필요해지면
 *   두 액터에 같은 필드가 복제된다. UVBHitFlashComponent 가 같은 이유로 같은 모양이다.
 *
 * 왜 tick 이 없는가: CPU 가 할 일은 시작 순간에 파편을 띄우는 것과, 몇 프레임 뒤 메시를 숨기는 것뿐이다.
 *   뒤엣것은 코어 티커(FTSTicker)에 한 번 걸어 두고 끝낸다 - 액터 틱을 켜지 않으므로
 *   AVBEnemyBase 의 bCanEverTick 을 건드리지 않는다.
 *
 * 왜 설정을 멤버로 안 들고 인자로 받는가: 사망 수치의 홈은 UVBEnemyConfig 다. 여기에 또 하나를
 *   두면 같은 연출의 값이 두 곳에 생기고, 정리 지연(저쪽)과 디졸브 길이(이쪽)의 관계를
 *   검사할 수 있는 자리가 사라진다. 이 컴포넌트는 데이터의 주인이 아니라 실행자다.
 *
 * 왜 소유자를 ACharacter 로만 보는가: 플레이어(AVBCharacter)와 적(AVBEnemyBase)은 공통 VB 베이스가
 *   없다. 구체 클래스를 캐스트하면 이 컴포넌트가 한쪽 전용이 된다.
 *
 * 부작용: 대상 스켈레탈 메시의 가시성을 끈다(ResetDissolve 가 되돌린다). 머티리얼은 안 건드린다.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBDeathDissolveComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBDeathDissolveComponent();

	// 정리 시각에 맞춰 끝나도록 디졸브 시작을 역산해 예약한다.
	//   SecondsUntilCleanup = 지금부터 액터가 제거되기까지 남은 시간(초).
	//   Config 가 null 이거나 길이가 0 이하면 아무것도 하지 않는다.
	//
	// 왜 「시작 지연」이 아니라 「남은 시간」을 받는가: 호출자가 아는 것이 그것이기 때문이다.
	//   시작 지연을 인자로 두면 호출자가 빼기를 하게 되고, 그 빼기가 정리 타이머의 식과 어긋나는
	//   순간 시체가 다 사라지기 전에 없어지거나 투명한 채로 남는다. 뺄셈의 주인을 한쪽으로 모은다.
	void ScheduleDissolve(float SecondsUntilCleanup, UVBDeathDissolveConfig* Config);

	// UVBDeathDissolveSettings::ResolveConfig 로 넘겨주기만 한다. 판단의 홈은 그쪽이다(2026-10-02 이동, 구조 정리 1a).
	//
	// 왜 아직 남아 있나: 남은 호출자가 AVBEnemyBase::PreviewDeathDissolve 하나인데, 그 파일에 다른 갈래의
	//   미커밋 작업이 있어 이번 정리에서는 include 줄 말고는 손대지 않았다. 그 파일을 고칠 때 호출을
	//   UVBDeathDissolveSettings::ResolveConfig 로 바꾸고 이 함수를 지운다. 새 호출자는 Settings 쪽을 부른다.
	static UVBDeathDissolveConfig* ResolveConfig(UVBDeathDissolveConfig* Explicit);

	// 진행 중인 디졸브를 취소하고 메시를 원래 모습으로 되돌린다.
	//   되돌리는 것 셋 - 아직 안 터진 시작 예약, 아직 안 터진 숨기기 예약, 그리고 날아가던 파편.
	//   셋 다 안 거두면 「되돌렸는데 잠깐 뒤 다시 사라진다」·「가루는 계속 난다」로 나타난다.
	// 쓰는 곳: 시험 재생(AVBEnemyBase::ResetDeathDissolvePreview)과 재생 직전의 초기화.
	//   실제 사망 경로는 이 함수를 안 부른다 - 그쪽은 되돌릴 일이 없다.
	void ResetDissolve();

private:
	// 예약이 만료되면 여기서 실제로 심는다. 즉시 시작인 경우 ScheduleDissolve 가 직접 부른다.
	void BeginDissolve();

	// 소유자를 ACharacter 로만 보고 GetMesh() 를 돌려준다.
	USkeletalMeshComponent* ResolveTargetMesh() const;

	// 파편 파티클을 띄우고 표면 샘플링용 메시를 꽂는다. 설정이 비어 있으면 아무것도 하지 않는다.
	void SpawnDebris(USkeletalMeshComponent* TargetMesh);


	// 죽는 메시에서 파편에 쓸 기본색 텍스처를 찾는다. 못 찾으면 null 을 준다(그 경우 재질 기본값이 남는다).
	//
	// 왜 슬롯 0 이 아닌가: 삼각형이 가장 많은 섹션의 재질을 쓴다. 트롤은 슬롯 0 이 이빨이라
	//   슬롯 0 을 쓰면 몸 전체가 이빨 색이 된다(2026-09-20 실측).
	// 왜 이름으로 안 찾나: 마네킹은 머티리얼 인스턴스라 「Base Texture」 파라미터로 읽히지만
	//   트롤은 평범한 머티리얼이라 파라미터가 없다. 이름에 기대면 절반만 된다.
	static UTexture* ResolveDebrisTexture(const USkeletalMeshComponent* TargetMesh);

	// 시작을 기다리는 동안 설정을 붙들어 둔다. DebrisEffect 참조가 들어 있어 GC 대상이다.
	UPROPERTY()
	TObjectPtr<UVBDeathDissolveConfig> PendingConfig;

	// 잘림을 반영한 확정 길이(실초). 설정값이 남은 시간보다 길면 남은 시간으로 줄인다.
	float PendingDuration = 0.0f;

	// 숨긴 메시. 되돌리기가 다시 보이게 하려면 무엇을 숨겼는지 알아야 한다.
	//   약한 참조인 이유는 아래 SpawnedDebris 와 같다 - 이 컴포넌트가 그 메시의 수명을 잡지 않는다.
	TWeakObjectPtr<USkeletalMeshComponent> HiddenMesh;

	// 띄운 파편을 «지켜보기만» 한다. 소유하지 않는다는 사실을 타입으로 못박는다 -
	//   이 시스템은 월드가 Outer 라 우리 수명과 무관하게 살고, 제 수명이 끝나면 스스로 사라진다.
	//   UPROPERTY 로 들면 GC 가 우리를 따라 붙들게 되어 «누가 이걸 살려 두는가» 가 흐려진다.
	// 쓰는 곳: ResetDissolve 가 시험 재생을 물릴 때 아직 날아가는 것을 함께 거둔다.
	TWeakObjectPtr<UNiagaraComponent> SpawnedDebris;

	// 파편이 올라온 뒤 메시를 숨기는 예약. 되돌리기가 이것을 거둬야 하므로 들고 있어야 한다.
	//   월드 타이머가 아니라 코어 티커인 이유는 BeginDissolve 주석에 있다(에디터 월드에서도 돈다).
	FTSTicker::FDelegateHandle HideTickerHandle;

	FTimerHandle StartTimerHandle;
};
