// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VBDeathSignalSubsystem.generated.h"

/**
 * «이 액터가 죽었다» 를 같은 월드의 서버 쪽 구독자에게 알리는 좁은 신호 하나.
 *
 * 왜 있나: 종전에는 플레이어·적의 HandleDeath 가 AVBAIController::NotifyActorDied 를 직접 불렀고, 그 정적 함수가
 *   월드의 AI 컨트롤러를 전부 훑었다. 그래서 캐릭터 쪽이 AI 를 알아야 했다. 이제 두 액터는 이 신호만 알고
 *   AI 컨트롤러가 구독한다(구조 정리 2단계 2항, 설계 .sw/structure-refactor/phase2-2-death-notification.md A'안).
 * 무엇이 아닌가: 사망 판정(State.Dead)·처치 사건(Event.Enemy.Killed)·연출 복제의 주인이 아니다. 지연·대기열도 없다 -
 *   부르는 자리(HandleDeath 의 그 단계)에서 동기로 방송하고, 그 «단계» 가 계약이다. 태그 콜백으로 바꾸면 통지가
 *   능력 취소·처치 사건보다 앞당겨진다.
 * 왜 월드 서브시스템인가: PIE 의 여러 월드가 저절로 갈린다. 정적 전역 delegate 는 콜백마다 월드를 걸러야 한다.
 */
UCLASS()
class VOWBOUND_API UVBDeathSignalSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 서버에서만 방송한다. 클라이언트·null·월드 없음은 아무것도 안 한다. DeadActor 가 있는 월드의 구독자에게만 간다. */
	static void BroadcastActorDied(AActor* DeadActor);

	/**
	 * Owner 에 약하게 묶인다 - Owner 가 사라지면 해제 없이도 방송에서 빠진다(BeginPlay 전에 파괴돼 EndPlay 가 안 오는
	 *   구독자도 그래서 남지 않는다). 해제는 돌려받은 핸들로 이 서브시스템에서 한다.
	 * 방송 중에 더한 구독은 그 방송에 불리지 않는다(엔진 multicast 는 시작 때의 목록을 돈다). 지원 범위 밖으로 적었다.
	 */
	FDelegateHandle AddListener(const UObject* Owner, TFunction<void(AActor* DeadActor)> Callback);

	/**
	 * 멱등. 방송 중에 불러도 된다 - 엔진은 목록 압축만 방송 뒤로 미루고, 묶인 콜백(TFunction)은 그 자리에서 해제한다.
	 *   그래서 콜백 안에서 자기를 해제했다면 그 뒤로는 캡처한 값이나 this 를 더 쓰지 않는다.
	 */
	void RemoveListener(FDelegateHandle Handle);

	virtual void Deinitialize() override;

private:
	DECLARE_MULTICAST_DELEGATE_OneParam(FActorDiedSignal, AActor*);

	// private 이라 방송은 BroadcastActorDied 하나뿐이다 - 서버 가드를 우회할 길이 없다.
	FActorDiedSignal ActorDied;
};
