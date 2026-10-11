// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VBToggleProp.generated.h"

class UAnimSequence;
class USkeletalMeshComponent;
class UVBInteractableComponent;

class UStaticMesh;
class UStaticMeshComponent;

// 선택한 소품 애니와 서버 시작 시각을 같이 보낸다. 발차기/뒷면 동작도 원격에서 같은 것을 재생한다.
USTRUCT()
struct FVBPropPlayback
{
	GENERATED_BODY()
	UPROPERTY()
	TObjectPtr<UAnimSequence> Animation;
	UPROPERTY()
	double ServerStartTime = 0.0;
	UPROPERTY()
	float StartPosition = 0.0f;
	UPROPERTY()
	float PlayRate = 1.0f;
	UPROPERTY()
	bool bSnapToEnd = true;
};

// 대리 메시 한 조각. 메시가 없는 팩 소품(밸브·게이트 등)을 기본 도형으로 대신할 때 쓴다(user 「대충 만들어」 2026-09-26).
// 조각을 데이터로 두는 이유: BP 에 컴포넌트를 붙이는 저작은 파이썬으로 하면 인스턴스에 안 실리는 함정이 있고(tool-traps
//   «BP CDO 를 python 으로 바꿀 땐»), 조각 목록은 레벨 인스턴스에서 바로 고칠 수 있어야 대충 맞추기가 된다.
USTRUCT(BlueprintType)
struct FVBPropProxyPart
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction")
	TObjectPtr<UStaticMesh> Mesh;

	// 소품 루트 기준. «닫힌 자세(소품 애니 0초)» 에서의 자리다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction")
	FTransform Transform;

	// 비어 있으면 고정. 이름이 있으면 소품 애니의 그 뼈를 따라 움직인다(닫힌 자세에서 뼈와의 차이를 유지).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction")
	FName FollowBone;
};

/**
 * 켜짐/꺼짐 두 상태를 오가는 소품 - 문·스위치·밸브·게이트·찬장·바닥 스위치. 설계 .sw/interaction/design.md 8절.
 *
 * 상태의 주인은 Interactable 컴포넌트(bActivated)다. 이 액터는 그 상태를 «보여 준다» -
 *   상태가 바뀌면 설정의 몸 동작 중 맞는 쪽의 소품 애니를 튼다. 시작 상태는 애니 끝 자세로 바로 맞춘다.
 * 보여 주는 길 둘:
 *   PropMesh 에 스켈레탈 메시가 있으면(팩 문·스위치) 그 메시가 소품 애니를 직접 재생한다.
 *   없으면 ProxyParts 의 대리 메시가 소품 애니의 뼈를 따라간다(팩에 메시가 없는 넷 - user 「대충 만들어」 2026-09-26).
 *     조각은 «닫힌 자세(애니 0초)» 에서 보기 좋은 자리에 놓는다 - 그 자리와 뼈의 차이를 잡아 두고 뼈를 따라간다.
 * 연결: LinkedProps 는 이 소품이 바뀔 때 같은 상태로 맞춘다(스위치가 문을 연다). 사람이 없으니 앞쪽 소품 애니를 쓴다.
 * 권한: 상태 변경·연결은 서버에서만. 애니는 선택된 재생 정보와 서버 시각을 복제해서 맞춘다.
 */
UCLASS()
class VOWBOUND_API AVBToggleProp : public AActor
{
	GENERATED_BODY()

public:
	AVBToggleProp();

	// 실제 표시 중인 소품의 뼈/소켓. 대리 메시도 같은 소품 애니의 현재 시각을 사용한다.
	bool GetInteractionBoneTransform(FName BoneName, FTransform& OutWorldTransform) const;
	void SetInteractionPlayRate(float Rate);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction")
	TObjectPtr<USkeletalMeshComponent> PropMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction")
	TObjectPtr<UVBInteractableComponent> Interactable;

	// 대리 메시 조각들. PropMesh 에 스켈레탈 메시가 있으면 보통 비워 둔다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction")
	TArray<FVBPropProxyPart> ProxyParts;

	virtual void OnConstruction(const FTransform& Transform) override;

	// 켜져 있을 때 프롬프트에 나올 이름(「닫기」「끄기」). 꺼져 있을 때는 설정의 ActionText.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction")
	FText DeactivateActionText;

	// 한 번 켜면 끝인가. 그러면 켜진 뒤 상호작용이 꺼진다 - 다 쓰였을 때(IsOneShotSpent)만.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction")
	bool bOneShot = false;

	// 일회용이 다 쓰였나. 바탕 소품은 켜지는 순간 다 쓰인다(늘 참). 서랍장(AVBLootContainer)은 «남은 몫이 없다» 로 덮는다 -
	//   남은 몫이 있으면 열린 채 잠기지 않아 닫았다 다시 열 수 있다(.sw/narrative 14.5 W1).
	//   잠그는 손은 이 소품 하나다(RefreshOneShotLock) - 파생은 판단만 덮는다. 잠금을 쓰는 주체가 둘이 되지 않게(규칙 26).
	virtual bool IsOneShotSpent() const { return true; }

	// 서버에서만. 일회용이고 켜져 있고 다 쓰였으면 상호작용을 끈다. 끄기만 하고 켜지 않는다(배치 때 잠근 대상을 풀지 않게).
	//   켜짐이 바뀔 때 이 소품이 부르고, 파생은 다 쓰였나가 바뀔 때 부른다.
	void RefreshOneShotLock();

	// 이 소품과 함께 켜지고 꺼지는 소품들(스위치 -> 문·게이트).
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Vowbound|Interaction")
	TArray<TObjectPtr<AVBToggleProp>> LinkedProps;

private:
	UFUNCTION()
	void HandleInteracted(AActor* InteractingActor);

	UFUNCTION()
	void HandleActivationChanged(bool bActivated, bool bInstant);

	// 서버: 상태에 딸린 것들(프롬프트 이름·일회용·연결)을 맞춘다.
	// bInstant 면 연결 소품도 애니 없이 맞춘다(세이브 로드).
	void ApplyServerSideEffects(bool bActivated, bool bInstant);

	// 대리 메시 조각을 (다시) 짓는다.
	void BuildProxyParts();

	// 소품 애니를 튼다. bSnapToEnd 면 재생 없이 끝 자세로 맞춘다(시작 상태).
	void PlayPropAnimation(UAnimSequence* Animation, bool bSnapToEnd, float StartPosition = 0.0f);

	UPROPERTY(ReplicatedUsing=OnRep_Playback)
	FVBPropPlayback Playback;

	UFUNCTION()
	void OnRep_Playback();

	// 뼈 따라가기 - 대리 메시 갈래.
	void ApplyBoneParts(float Time);
	FTransform SampleBoneComponentSpace(FName BoneName, float Time) const;

	// Interactor에 보관한 동작 우선, 없으면 앞뒤 판정. 사람이 없으면(연결) 앞쪽.
	UAnimSequence* ResolvePropAnimation(bool bActivated, const AActor* Interactor) const;

	// 대리 메시 갈래의 재생 상태.
	UPROPERTY()
	TObjectPtr<UAnimSequence> BonePartAnimation;
	float BonePartTime = 0.0f;

	// OnConstruction 이 만든 조각 컴포넌트. 다시 지을 때 지우려고 들고 있다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> ProxyComponents;

	// 뼈를 따라가는 조각 -> (컴포넌트, 닫힌 자세에서 뼈에 대한 상대 변환)
	struct FResolvedPart
	{
		TWeakObjectPtr<USceneComponent> Component;
		FName BoneName;
		FTransform BoneToComponent;
	};
	TArray<FResolvedPart> ResolvedParts;

	// 서버에서 확정 순간 선택된 동작을 읽는 용도. 원격 표시는 Playback으로 전달한다.
	TWeakObjectPtr<AActor> LastInteractor;
};
