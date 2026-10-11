// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VBFinisherCamera.generated.h"

class UCameraComponent;
class UVBFinisherConfig;

/**
 * AVBFinisherCamera — 처형 한 판을 비추는 카메라 액터 (DEC-019 4/4)
 *
 * 무엇: 뷰타깃이 되어 설정 자산의 카메라 키를 따라 움직인다. 한 번의 처형에 하나 살고 끝나면 죽는다.
 *
 * 왜 액터인가: user 결정이다 - 스프링암을 값으로 미는 확장은 「카메라가 계속 뒤에 있게 할 수는 없어」로
 * 탈락했고, 시퀀서는 결말이 정해진 장면의 도구라 «누를 수도 안 누를 수도, 죽을 수도 안 죽을 수도»
 * 있는 이 흐름과 안 맞는다. 뷰타깃 전환은 엔진이 블렌드까지 책임지는 정규 경로다.
 *
 * 복제하지 않는다. 카메라는 보는 사람의 것이라 기계마다 다르다. 누가 이것을 세울지는
 * UVBFinisherDirectorSubsystem 이 「내 뷰타깃이 이 장면의 참가자인가」로 정한다.
 *
 * 시간축: 월드 시간이 아니라 씬 재생 시간 이다(`FContextualAnimSceneBinding::GetAnimMontageTime`).
 * 처형에는 슬로우가 걸리는데 월드 델타로 키를 진행하면 카메라도 같이 느려져 연출이 무너진다.
 *
 * 전제: Setup 에 넘기는 앵커 액터와 설정이 유효해야 한다. 판정은 부르는 쪽(서브시스템)이 한다.
 * 부작용: 없음. 뷰타깃 전환과 복귀는 서브시스템이 든다 - 이 액터는 «어디에 서 있을지» 만 안다.
 * 그 둘을 여기 넣으면 카메라가 플레이어 컨트롤러를 알게 되고, 분할 화면에서 누구의 것인지가 흐려진다.
 */
UCLASS()
class VOWBOUND_API AVBFinisherCamera : public AActor
{
	GENERATED_BODY()

public:
	AVBFinisherCamera();

	/**
	 * 이 카메라가 따라갈 것을 넘긴다. 스폰 직후 한 번 부른다.
	 *
	 * InAnchor: 카메라 키의 상대 좌표 기준(설정의 CameraAnchorRole 이 가리키는 액터).
	 * InTimeSource: 씬 재생 시간을 읽을 액터. 참가자면 누구든 같은 값이라 앵커와 같아도 된다.
	 */
	void Setup(const UVBFinisherConfig* InConfig, AActor* InAnchor, AActor* InTimeSource);

	virtual void Tick(float DeltaSeconds) override;

	// 지금 이 카메라의 회전. 복귀 직전에 컨트롤 회전을 여기 맞춘다 - 안 맞추면 폰으로 돌아가는
	// 순간 스프링암이 옛 회전으로 스냅한다. yaw 만이 아니라 pitch 도 넘긴다.
	FRotator GetViewRotation() const;

private:
	// 씬 재생 시간을 초 단위로. 못 읽으면 음수를 돌려준다(장면이 안 돌거나 바인딩이 없는 경우).
	float GetSceneTime() const;

	UPROPERTY()
	TObjectPtr<UCameraComponent> CameraComponent;

	UPROPERTY()
	TObjectPtr<const UVBFinisherConfig> Config;

	// 약참조다. 처형은 대상을 죽일 수 있고, 죽은 액터를 강참조로 붙들면 그 시체가 안 사라진다.
	TWeakObjectPtr<AActor> Anchor;
	TWeakObjectPtr<AActor> TimeSource;

	// 앵커가 사라진 뒤에도 마지막 자리를 유지하기 위한 기록. 없으면 원점으로 튄다.
	FTransform LastGoodWorld = FTransform::Identity;
	bool bHasLastGood = false;
};
