// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VBFinisherDirectorSubsystem.generated.h"

class AVBFinisherCamera;
class APlayerController;
class UContextualAnimSceneActorComponent;
class UVBFinisherConfig;
class UVBFinisherComponent;

/**
 * UVBFinisherDirectorSubsystem — 처형 연출을 «보는 사람» 쪽에서 세운다 (DEC-019 4/4)
 *
 * 무엇: 처형 장면이 시작되면 이 기계의 로컬 플레이어들을 훑어, 뷰타깃이 그 장면의 참가자인 사람에게만
 * 전용 카메라를 세우고 슬로우를 건다. 끝나면 되돌린다.
 *
 * 왜 「누가 조종하나」가 아니라 「내 뷰타깃이 참가자인가」인가.
 * 처음 설계는 「공격자를 로컬 조종하는 기계」였다. 그것은 리플레이에서 절대 안 걸린다 -
 * 리플레이 뷰어는 아무 폰도 조종하지 않는다. 관전자도 같다. 즉 지금 싱글이라 맞는 조건이지
 * 구조가 아니었다(2026-09-13 user 지적).
 * 질문이 둘인데 하나로 눌러 붙인 것이 원인이다 - 「장면이 섰다」는 액터의 사실이고 복제되지만,
 * 「내가 그걸 볼 것인가」는 뷰어의 사실이고 기계마다 다르다. 뷰타깃으로 물으면 싱글·원격 클라·
 * 관전·분할 화면·리플레이가 같은 한 줄로 갈린다.
 * 이것은 리플레이 기능을 짓는 것이 아니다. 없는 것을 위해 짓지 않는다(규칙 26).
 * 뷰어 정체성을 네트워크 소유권에 «용접하지 않는» 것뿐이고 비용이 0 이다.
 * 그리고 「리플레이에서 된다」고는 말하지 않는다 - 지금 그것을 돌려볼 수단이 없다.
 *
 * 왜 컴포넌트가 아닌가: 컴포넌트는 그 액터의 것인데 이 판단은 뷰어의 것이다. 공격자에 달면
 * 피해자를 따라가는 관전자가 못 보고, 양쪽에 달면 한 장면에 주인이 둘이 된다(규칙 26).
 * 왜 UVBFinisherComponent 가 아닌가: 그 헤더가 스스로 「카메라와 슬로우는 여기 없다 - 그것은
 * 연출이고 이 컴포넌트는 장면이다」라고 선언해 두었다. 그 선이 정확히 이 경우를 위한 것이다.
 *
 * 진입점: UVBFinisherComponent::OnConfigArrived (복제 도착 신호) 하나뿐이다. 등록 배선을 만들지 않는다 -
 * 참가 후보를 미리 모아 두면 새 적 클래스가 그 등록을 빠뜨릴 자리가 생긴다.
 * 두 참가자는 장면이 서면 플러그인의 바인딩이 알려준다.
 *
 * 전제: 서버·클라 어디서나 산다. 로컬 플레이어가 없으면(데디 서버) 아무 일도 안 한다.
 * 부작용: 뷰타깃 전환, 카메라 액터 스폰·파괴, 전역 시간 배율 요청(로컬 한정).
 */
UCLASS()
class VOWBOUND_API UVBFinisherDirectorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// 게임 월드에서만 산다. 에디터 프리뷰 월드에 카메라를 세울 이유가 없다.
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	// 설정이 이 기계에 도착했다. 장면이 아직이면 기다렸다가, 이미 서 있으면 그 자리에서 시작한다.
	void HandleConfigArrived(AActor* Attacker, const UVBFinisherConfig* InConfig);

	// 플러그인이 「이 액터가 장면에 들어갔다」를 알려준다. 설정 도착과 도착 순서가 안 정해져 있어
	// 둘 다 필요하다 - 늦게 오는 쪽이 방아쇠가 된다.
	UFUNCTION()
	void HandleJoinedScene(UContextualAnimSceneActorComponent* SceneActorComponent);

	UFUNCTION()
	void HandleLeftScene(UContextualAnimSceneActorComponent* SceneActorComponent);

	// 두 신호가 다 모였는지 보고, 모였으면 카메라를 세운다. 두 번 불려도 한 번만 선다.
	void TryBegin();

	// 카메라와 슬로우를 거둔다. 끝나는 경로가 여럿이라(장면 종료·설정 비움·월드 종료) 한 곳에 모은다.
	void End();

	// 이 기계에서 그 장면을 보고 있는 로컬 플레이어들. 뷰타깃이 참가자인 사람만 담긴다.
	void CollectViewingControllers(const TArray<AActor*>& Participants,
	                              TArray<APlayerController*>& OutControllers) const;

	// 설정의 CameraAnchorRole 이 가리키는 액터. 못 찾으면 nullptr 이고 카메라를 세우지 않는다.
	AActor* ResolveAnchor(const UVBFinisherComponent* Finisher) const;

	// 지금 진행 중인 처형의 공격자. 여기에 설정과 장면이 매달려 있다.
	TWeakObjectPtr<AActor> PendingAttacker;

	UPROPERTY()
	TObjectPtr<const UVBFinisherConfig> PendingConfig = nullptr;

	// 우리가 구독을 걸어 둔 씬 컴포넌트. 끊을 때 같은 것을 찾아야 하므로 들고 있는다.
	TWeakObjectPtr<UContextualAnimSceneActorComponent> WatchedScene;

	// 세운 카메라와, 복귀시킬 컨트롤러들. 분할 화면이면 사람마다 하나씩이다.
	UPROPERTY()
	TArray<TObjectPtr<AVBFinisherCamera>> ActiveCameras;

	TArray<TWeakObjectPtr<APlayerController>> ActiveControllers;

	// 시간 배율 요청을 걸었는가. 안 걸었는데 Pop 하면 남의 요청을 지울 수 있다.
	bool bPushedTimeDilation = false;

	// 한 장면에 한 번만 세운다. TryBegin 이 두 신호 양쪽에서 불리기 때문이다.
	bool bRunning = false;
};
