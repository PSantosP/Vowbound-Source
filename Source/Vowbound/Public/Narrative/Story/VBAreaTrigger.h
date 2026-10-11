// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Player/VBPlayerPreparationBinding.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "VBAreaTrigger.generated.h"

class APawn;
class APlayerController;
class UBoxComponent;
class UPrimitiveComponent;
class UVBPlayerReadinessComponent;
class UVBStoryStateComponent;

DECLARE_MULTICAST_DELEGATE(FVBOnAreaOccupancyChanged);

namespace VBAreaTrigger
{
	// 한 플레이어의 «만족»(안에 있다 + 조건 참)이 바뀌었을 때 발화하는가. 순수 함수라 단위 시험이 잰다.
	//   만족이 거짓 -> 참으로 바뀔 때만 발화한다. 한 번만 영역은 이미 발화했으면(사실이 켜져 있으면) 다시 안 한다.
	VOWBOUND_API bool ShouldFire(bool bWasSatisfied, bool bNowSatisfied, bool bOnce, bool bOnceAlreadyFired);
}

/**
 * 영역 진입 - 플레이어가 상자 안에 있고 조건이 참이 되면 사건을 내고 효과를 적용한다. 설계 .sw/prologue-opening 3.3·8.3.
 *
 * 왜 상호작용 Auto 가 아닌가: Auto 는 다시 들어올 때마다 발화하고, 반경이 설정 자산 하나에 묶이고(위치마다 자산이 는다),
 *   구 거리라 복도·방 모양에 안 맞는다.
 * 왜 겹침 신호에서 바로 발화하지 않는가: 이어하기는 폰을 PlayerStart 에 낳은 «뒤에» 저장(사실)을 적용한다(AVBGameMode::HandleStartingNewPlayer).
 *   시작 지점의 영역이 겹침 신호에서 바로 발화하면 저장이 들어오기 전의 사실로 판정해 깨어남 효과를 다시 건다.
 *   그래서 신호(겹침·사실 바뀜·시작)는 «다음 틱에 다시 보라» 만 남기고, 판정은 그때의 겹침과 사실로 한다.
 *   물리 겹침만으로 준비 완료를 추측하지 않는다. 일회 영역은 bRequirePlayerPreparation을 켜서 현재 준비 API까지 확인한다.
 * 한 번만: OnceFact 를 적으면 그 사실이 «이미 발화함» 이다. 사실은 저장되므로 이어하기가 다시 걸지 않는다.
 *   사실은 효과(FVBEffect_SetFact)로 켠다 - 사실을 바꾸는 길을 하나로 둔다(UVBStoryStateComponent 주석).
 * 순서는 상호작용 완료와 같다: 사실 -> 사건 -> 효과. 효과가 낸 사건이 먼저 가면 «들어가라 -> 그 안의 것을 구하라» 의 둘째 단계가 서기 전에 지나간다.
 * 서버에서만 판정한다. 클라에는 아무 일도 없다(결과는 사실·퀘스트 복제로 간다).
 */
UCLASS()
class VOWBOUND_API AVBAreaTrigger : public AActor
{
	GENERATED_BODY()

public:
	AVBAreaTrigger();

	// 현재 물리 겹침만 읽는다. 준비·조건·OnceFact·지난 발화와 독립이며 저장하지 않는다.
	bool IsPawnInside(const APawn* Pawn) const;

	// 겹침 입력이 바뀌었을 수 있다(서버). 구독자는 현재 폰으로 다시 조회하며 승인 bool을 캐시하지 않는다.
	FVBOnAreaOccupancyChanged OnOccupancyChanged;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 영역. 크기와 회전은 배치에서 정한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Story")
	TObjectPtr<UBoxComponent> Area;

	// 전부 참이어야 발화한다. 비면 들어오기만 하면 된다. 안에 있는 동안 참이 되어도 발화한다(사실이 바뀌면 다시 본다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story", meta=(BaseStruct="/Script/Vowbound.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> Conditions;

	// 발화할 때 적용한다(사건 뒤).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story", meta=(BaseStruct="/Script/Vowbound.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> Effects;

	// 사건(Event.Area.Entered)의 대상 태그. 퀘스트 목표가 여기에 질의를 건다. 비면 사건을 내지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story")
	FGameplayTagContainer EventTags;

	// 적으면 한 번만 발화한다 - 이 사실이 켜져 있으면 «이미 발화함». 비우면 들어올 때마다(만족이 새로 될 때마다) 발화한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story", meta=(Categories="Story"))
	FGameplayTag OnceFact;

	// 일회 영역만 사용한다. 준비 전에는 edge/OnceFact/사건/효과를 소진하지 않고, 준비 통지 뒤 현재 점유를 다시 본다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story")
	bool bRequirePlayerPreparation = false;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBEncounterConsumerTestAccess;
#endif
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	                        int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	// 다음 틱에 한 번 판정한다. 같은 틱의 신호 여럿은 하나로 합친다.
	void RequestEvaluate();
	void Evaluate();
	bool IsSatisfiedFor(APawn& Pawn);
	bool TryFire(APawn& Pawn);
	void RefreshPlayerBindings();
	void DisconnectPlayerBindings();

	// 플레이어 폰마다 지난 판정의 만족. 거짓 -> 참 일 때만 발화한다.
	TMap<TWeakObjectPtr<APawn>, bool> LastSatisfied;

	TWeakObjectPtr<UVBStoryStateComponent> BoundStory;
	FDelegateHandle StoryChangedHandle;
	bool bEvaluatePending = false;
	bool bWarnedNoStory = false;
	bool bWarnedMissingOnce = false;
	FVBPlayerPreparationBinding PlayerBinding;
	bool bEnding = false;
};
