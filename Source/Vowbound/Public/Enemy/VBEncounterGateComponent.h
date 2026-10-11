// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Player/VBPlayerPreparationBinding.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "VBEncounterGateComponent.generated.h"

class APlayerState;
class APlayerController;
class APawn;
class AVBAreaTrigger;
class UVBPlayerReadinessComponent;
class UVBStoryStateComponent;
struct FOnAttributeChangeData;

DECLARE_MULTICAST_DELEGATE(FVBOnEncounterWoken);
DECLARE_MULTICAST_DELEGATE_OneParam(FVBOnEncounterHeldChanged, bool /*bHeld*/);

/** 보류 요청 하나의 처리. 판정만 따로 둔 것은 쓰러짐 경로를 ASC 없이 시험하려고다(VBAreaTrigger::ShouldFire 와 같은 모양). */
enum class EVBEncounterHoldAction : uint8
{
	Ignore,          // 같은 값이거나, 쓰러진 조우에 새 보류
	ApplyAndNotify,  // 태그·공격 취소 + 방송(AI 가 행동 트리를 멈추거나 잇는다)
	ApplySilently,   // 쓰러지기 전 걸린 보류를 걷는다 - 태그만, 방송 없음(죽은 적의 행동 트리를 잇지 않는다)
};

namespace VBEncounterHold
{
	VOWBOUND_API EVBEncounterHoldAction Decide(bool bHeldNow, bool bWantHeld, bool bDefeated);
}

/**
 * 배치한 적 하나의 «조우 문»: 나오는가 / 언제 깨어나는가 / 쓰러지면 무엇을 남기나. 설계 .sw/prologue-opening 3.2·8.1·8.2·10절.
 *
 * 왜 적 설정 자산이 아니라 배치에 붙이는 컴포넌트인가: 적 설정(UVBEnemyConfig)은 종류마다 하나다. «프롤로그의 첫 감염체» 와 다른 감염체는
 *   설정이 같고 조건만 다르다. 배치(레벨 인스턴스)에 이 컴포넌트를 붙이고 값을 적는다 - 붙이지 않은 적은 지금처럼 바로 깨어난다.
 * 깨어남의 주인은 AI 컨트롤러다(AVBAIController::OnPossess 가 행동 트리를 시작한다). 이 컴포넌트는 «깨어도 된다» 만 말하고,
 *   컨트롤러가 그 말을 듣고 시작한다. 그 전에는 행동 트리가 없어 지각이 칠판을 못 쓴다 - 보이지만 다가오지 않는다.
 * 나오는가(ExistConditions): 서버 BeginPlay 에서 한 번 본다. 이어하기는 월드 BeginPlay 전에 사실을 적용하므로(AVBGameMode::HandleStartingNewPlayer)
 *   «이미 쓰러뜨림» 사실이 여기서 보인다 - 적은 저장되지 않아 불러오면 다시 나오는데, 그것을 이 조건이 막는다(설계 8.1).
 * 깨어남(WakeConditions): 사실이 바뀔 때 다시 본다. 비면 바로 깬다. 보통 «전투 영역에 들어왔다» 사실 하나 -
 *   그 사실은 AVBAreaTrigger 가 «무기 가짐» 조건과 함께 켠다. 준비 opt-in은 현재 폰의 준비와 WakeArea의 실제 겹침도 읽는다.
 *   저장된 Engaged는 이력이며 현재 점유가 아니다. 영역 밖 복원은 준비 뒤 재접근해야 조건으로 깬다.
 * 맞으면 깬다(bWakeWhenDamaged): 무기 없이는 공격이 시작되지 않으므로(UVBGA_MeleeAttackBase::CanActivateAbility) 이 길은 언제나
 *   «무장한 플레이어가 먼저 건드린» 경우다 - 03 의 «선공 없음» 과 부딪히지 않는다. 그때 WokenEffects 로 접근 대사를 끊을 사실을 켠다.
 * 쓰러짐(DefeatedEffects): 체력이 0 이 되거나, 체력 0 없이 KillZ 아래로 떨어져 파괴되면 한 번. 조우 완료 사실을 켜는 자리다
 *   (퀘스트 목표는 그 사실을 조건으로 읽는다 - 사건 순서에 안 기댄다). 낙하도 세는 이유: 안 세면 그 사실이 영영 안 켜져 진행이 잠긴다.
 *   레벨 전환·스트리밍·종료·존재 조건으로 나감·관리상 Destroy 는 해소가 아니다(EndPlay 주석).
 * 보류(SetHeld): 시네마틱 장면 동안 이 조우를 멈춘다(.sw/cinematic-playback 10절 4). 적 ASC 에 State.Encounter.Held 로스태그를 붙여
 *   공격 GA 를 막고(UVBGA_EnemyMeleeAttack::ActivationBlockedTags), AI 컨트롤러가 OnHeldChanged 를 듣고 행동 트리를 멈춘다·잇는다.
 *   태그는 새 공격만 막으므로, 켜는 순간 이미 진행 중인 공격(Ability.Attack 계열)도 취소한다 - 판정 창이 열린 채 장면에 들어가지 않게(Codex 03 지적 10-04).
 *   깨어남과 따로다 - 보류 중에 깨어나도 행동 트리는 멈춘 채 시작한다. 거는 쪽은 규칙 효과 «조우 보류»(EncounterTags 로 찾는다).
 *   쓰러진 조우에는 보류를 새로 걸지 않고, 풀어도 행동 트리를 잇지 않는다 - 보류 해제가 «다시 움직임» 이 되면 죽은 적이 움직인다.
 * 조건의 «부른 플레이어» 는 첫 플레이어다. 여러 명이면 맞지 않는다 - 싱글(리슨 호스트) 전제이고, MP 하드닝(FIND-128)에서 다시 본다.
 * 서버에서만 판정한다.
 */
UCLASS(ClassGroup=(Vowbound), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBEncounterGateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBEncounterGateComponent();

	bool IsAwake() const { return bAwake; }
	bool IsHeld() const { return bHeld; }

	// 서버에서만. 같은 값이면 아무것도 안 한다.
	void SetHeld(bool bInHeld);

	const FGameplayTagContainer& GetEncounterTags() const { return EncounterTags; }

	// 보류가 바뀔 때(서버). AI 컨트롤러가 듣는다.
	FVBOnEncounterHeldChanged OnHeldChanged;

	// 깨어날 때 한 번(서버). AI 컨트롤러가 듣고 행동 트리를 시작한다.
	FVBOnEncounterWoken OnWoken;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 이 조우를 가리키는 꼬리표. 규칙 효과 «조우 보류» 가 이것으로 대상을 찾는다 - 같은 꼬리표의 적 여럿을 한 번에 묶는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Encounter")
	FGameplayTagContainer EncounterTags;

	// 거짓이면 이 적은 나오지 않는다(시작할 때 사라진다). 비면 늘 나온다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Encounter", meta=(BaseStruct="/Script/Vowbound.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> ExistConditions;

	// 전부 참이면 깨어난다. 비면 바로 깬다(붙이지 않은 것과 같다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Encounter", meta=(BaseStruct="/Script/Vowbound.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> WakeConditions;

	// 대기 중에 체력이 깎이면 깬다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Encounter")
	bool bWakeWhenDamaged = true;

	// 조건/피격 모두 현재 플레이어 준비를 확인한다. 기본 false로 기존 조우를 보존한다(싱글/리슨 호스트).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Encounter")
	bool bRequirePlayerPreparation = false;

	// 준비 opt-in의 조건 깨우기 영역. 준비된 선공은 기존 의미대로 이 영역 밖에서도 깨운다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Vowbound|Encounter", meta=(EditCondition="bRequirePlayerPreparation"))
	TObjectPtr<AVBAreaTrigger> WakeArea;

	// 깨어날 때 적용한다(조건으로 깨든 맞아서 깨든).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Encounter", meta=(BaseStruct="/Script/Vowbound.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> WokenEffects;

	// 체력이 0 이 될 때 한 번 적용한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Encounter", meta=(BaseStruct="/Script/Vowbound.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> DefeatedEffects;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBEncounterConsumerTestAccess;
#endif
	void EvaluateWake();
	void RequestEvaluateWake();
	void Wake(const TCHAR* Why, bool bFromDamage = false);
	void RefreshPlayerBindings();
	void DisconnectPlayerBindings();
	void HandleHealthChanged(const FOnAttributeChangeData& Data);
	// 해소 한 번(쓰러짐·사라짐). DefeatedEffects 를 적용한다.
	void ApplyDefeated(APlayerState* Killer, const TCHAR* Why);
	APlayerState* GetFirstPlayerState() const;

	bool bAwake = false;
	bool bHeld = false;
	bool bDefeated = false;
	// 존재 조건을 통과해 실제로 나왔다 - 스스로 사라진 경우(존재 조건 거짓)를 «해소» 로 세지 않으려고.
	bool bExisted = false;
	TWeakObjectPtr<UVBStoryStateComponent> BoundStory;
	TWeakObjectPtr<AVBAreaTrigger> BoundWakeArea;
	FDelegateHandle StoryChangedHandle;
	FDelegateHandle HealthChangedHandle;
	FDelegateHandle AreaOccupancyHandle;
	bool bEvaluatePending = false;
	FVBPlayerPreparationBinding PlayerBinding;
	bool HasValidWakeArea() const;
	bool bWarnedInvalidWakeArea = false;
	bool bEnding = false;
};
