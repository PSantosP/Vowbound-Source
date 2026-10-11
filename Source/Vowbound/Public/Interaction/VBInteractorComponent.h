// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VBInteractorComponent.generated.h"

class UVBInteractableComponent;
struct FVBInteractionMotion;
class UVBPushableComponent;

// 대상과 동작 번호를 한 번에 복제해 서로 다른 상호작용의 조합을 읽지 않는다.
USTRUCT()
struct FVBEnvironmentMotionState
{
	GENERATED_BODY()
	UPROPERTY()
	TObjectPtr<UVBInteractableComponent> Target;
	UPROPERTY()
	uint8 MotionIndex = MAX_uint8;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVBInteractFocusChangedSignature, UVBInteractableComponent*, NewFocus);
DECLARE_MULTICAST_DELEGATE_OneParam(FVBOnInteractionRefused, const FText& /*RefusedText*/);

/**
 * 플레이어 쪽 상호작용. «지금 무엇을 가리키고 있나» 를 고르고, 홀드 진행을 프롬프트에 보여준다.
 *
 * 고르기(FindBestInteractable)는 서버와 클라가 같은 함수를 부른다.
 *   클라는 매 틱 불러 프롬프트를 띄우고(초점), 서버는 UVBGA_Interact 가 활성화될 때 한 번 불러 판정한다.
 *   클라가 고른 대상을 서버로 보내지 않는 이유: 받은 것을 믿으면 서버가 다시 재야 하고, 다시 잴 거면
 *   처음부터 서버가 고르는 것과 같다. 기준 방향(컨트롤 회전)은 서버에도 있으므로 같은 답이 나온다.
 *   경계에서 둘이 갈리면 프롬프트와 다른 대상이 반응할 수 있으나, 그 폭은 한 프레임의 카메라 차이다.
 * 초점은 표시용이다. 판정의 주인은 GA 다.
 */
UCLASS(ClassGroup=(Vowbound), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBInteractorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBInteractorComponent();

	// 지금 이 자리에서 닿는 대상 가운데 카메라 방향에 가장 가까운 것. 없으면 null.
	UVBInteractableComponent* FindBestInteractable() const;

	// 이 대상에 지금 닿는가. 홀드 완료 시점의 재확인에 쓴다 - 누르는 동안 걸어서 벗어났을 수 있다.
	bool CanReach(const UVBInteractableComponent* Interactable) const;

	UVBInteractableComponent* GetFocusedInteractable() const { return FocusedInteractable.Get(); }

	// 홀드 표시. UVBGA_Interact 가 로컬 인스턴스에서 부른다.
	void BeginHoldDisplay(float Duration);
	void EndHoldDisplay();

	// 0~1. 홀드 중이 아니면 음수.
	float GetHoldProgress() const;

	// 몸으로 하는 상호작용(설 자리로 옮기기 + 몽타주) 동안 캡슐 방향의 주인이 된다.
	// 왜: 몽타주는 제자리이고 이동은 루트모션 소스라, 그 사이 strafe 회전이 캡슐을 카메라 쪽으로 돌리면
	//   소품을 등지고 문을 연다. AVBCharacter 의 두 소유자 목록(DEBT-011)이 이 술어를 부른다.
	bool OwnsCapsuleFacing() const;

	// UVBGA_Interact 가 몸 동작 시작·끝에 부른다. 소유자의 회전 모드를 다시 평가시킨다.
	void BeginMotion(UVBInteractableComponent* Target, uint8 MotionIndex);
	void EndMotion();
	UVBInteractableComponent* GetMotionTarget() const { return MotionState.Target; }
	const FVBInteractionMotion* GetActiveMotion() const;
	uint8 GetMotionIndex() const { return MotionState.MotionIndex; }
	UVBPushableComponent* GetPushable() const;
	void SetPushInput(float Value);
	void RequestRepeatInteraction();

	// 초점이 바뀌었다. 로컬에서만 온다(HUD 가 구독).
	UPROPERTY(BlueprintAssignable, Category="Vowbound|Interaction")
	FVBInteractFocusChangedSignature OnFocusChanged;

	// 서버가 시도를 대상의 조건 칸으로 거절했다(.sw/prologue-opening 14.4). UVBGA_Interact 가 서버에서 부르고 주인 클라에서 돈다.
	UFUNCTION(Client, Reliable)
	void ClientInteractionRefused(const FText& RefusedText);

	// 위 거절이 주인 클라에 왔다. HUD 알림 줄이 듣는다.
	FVBOnInteractionRefused OnInteractionRefused;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// 소유 폰이 이 기계에서 조종되는가. 초점은 그 경우에만 고른다 - 서버의 남의 폰이나 시뮬레이트 프록시는 프롬프트가 없다.
	bool IsLocallyControlledOwner() const;

	void SetFocus(UVBInteractableComponent* NewFocus);

	// 서버: 자동 대상의 범위 출입을 보고, 들어오는 순간 한 번 완료를 알린다.
	void UpdateAutoInteractables();

	// 지금 범위 안에 있는 자동 대상. 나갔다 들어와야 다시 걸린다.
	TSet<TWeakObjectPtr<UVBInteractableComponent>> AutoInside;

	TWeakObjectPtr<UVBInteractableComponent> FocusedInteractable;

	// 홀드 표시 상태. 음수 = 홀드 중 아님.
	double HoldStartTime = -1.0;
	float  HoldDuration  = 0.0f;

	UPROPERTY(ReplicatedUsing=OnRep_MotionState)
	FVBEnvironmentMotionState MotionState;
	UFUNCTION()
	void OnRep_MotionState();
	UFUNCTION(Server, Reliable)
	void ServerRepeatInteraction();
	UFUNCTION(Server, Unreliable)
	void ServerPushInput(float Value);
	float LocalPushInput = 0.0f;
	double NextPushInputTime = 0.0;
};
