// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "VBInteractableComponent.generated.h"

class UVBInteractableConfig;
struct FVBRuleContext;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVBInteractedSignature, AActor*, InteractingActor);
// bInstant = 애니 없이 끝 자세로 맞춰라(세이브 로드). 평소 상호작용·복제 도착은 false.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVBActivationChangedSignature, bool, bActivated, bool, bInstant);

/**
 * «나는 상호작용 대상이다». 문·레버 같은 월드 액터에 붙인다.
 *
 * 무엇을 드나: 설정(자산)과 켜짐 여부, 그리고 완료 신호. 완료했을 때 무엇을 하는지는 소유 액터가 정한다 -
 *   OnInteracted 를 구독한다. 그래서 문 BP 를 리페어런트하지 않고 붙이기만 하면 되고,
 *   한 액터에 대상 둘(문 열기 / 잠그기)도 둘 수 있다.
 * 왜 SceneComponent 인가: 컴포넌트의 위치가 곧 «어디서 닿나» 와 «프롬프트가 어디 뜨나» 의 앵커다.
 *   문 손잡이 자리에 놓으면 거리·시야 판정과 프롬프트가 그 자리를 쓴다(user 결정: 프롬프트는 대상 위치에 뜬다).
 * 목록: BeginPlay 에 UVBInteractableSubsystem 에 등록하고 EndPlay 에 뺀다.
 * 권한: 완료 신호(NotifyInteracted)는 서버에서만 온다 - UVBGA_Interact 가 서버에서 부른다.
 *   켜짐 여부는 복제된다. 클라의 프롬프트가 꺼진 대상을 고르지 않게 하려는 것이다.
 */
UCLASS(ClassGroup=(Vowbound), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBInteractableComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UVBInteractableComponent();

	// 미할당이면 CDO 를 돌려준다 - null 이 아니다. 대신 BeginPlay 가 경고를 남긴다.
	const UVBInteractableConfig* GetConfig() const;

	bool IsInteractionEnabled() const { return bInteractionEnabled; }

	// 서버에서만. 잠긴 문, 이미 쓴 일회용 대상 등.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Vowbound|Interaction")
	void SetInteractionEnabled(bool bEnabled);

	// 프롬프트에 나올 행동 이름. 덮어쓴 것이 있으면 그것, 없으면 설정의 ActionText.
	FText GetActionText() const;

	// 대상이 상태에 따라 행동 이름을 바꾼다(열린 문 = 「닫기」). 빈 텍스트면 설정 값으로 돌아간다.
	// 복제한다 - 프롬프트는 클라에서 그려지므로 서버에서만 바꾸면 클라가 옛 이름을 띄운다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Vowbound|Interaction")
	void SetActionTextOverride(const FText& InText);

	// UVBGA_Interact 가 서버에서 완료 시 부른다(자동 대상은 인터랙터가). 완료 길이 모두 여기로 모인다.
	// 조건 칸(UseConditions)이 거짓이면 아무것도 안 하고 false - 동작 도중에 조건이 바뀐 경우다.
	bool NotifyInteracted(AActor* InteractingActor);

	// 조건 칸 판정(.sw/prologue-opening 14). 비면 참. 서버에서 부른다 - 클라에서 부르면 클라가 가진 상태로만 판정한다.
	bool PassesUseConditions(const AActor* InteractingActor) const;

	// 시도가 조건에 걸려 거절됐을 때(UVBGA_Interact, 서버): 거절 효과를 적용한다. 화면 글은 GA 가 인터랙터로 보낸다.
	void ApplyRefused(AActor* InteractingActor);

	const FText& GetRefusedText() const { return RefusedText; }

	// 완료. 서버에서만 온다.
	UPROPERTY(BlueprintAssignable, Category="Vowbound|Interaction")
	FVBInteractedSignature OnInteracted;

	// 완료 처리가 다 끝났다(서버) - 소유 액터의 OnInteracted -> 완료 사건 -> 완료 효과 다음. 서랍장이 내용물을 여기서 준다(.sw/narrative 14.5 W1):
	//   아이템 사건이 완료 사건보다 먼저 가면 «열어라 -> 안의 것을 구하라» 퀘스트의 둘째 단계가 서기 전에 지나간다(14.4 와 같은 순서 규칙).
	UPROPERTY(BlueprintAssignable, Category="Vowbound|Interaction")
	FVBInteractedSignature OnCompletionApplied;

	// 켜짐 상태(문 열림·스위치 켜짐). 두 상태를 오가는 대상의 상태는 여기 하나에 산다 -
	//   소품 액터가 따로 들면 주인이 둘이 된다(규칙 26). 저장(3단계)도 이 값을 읽고 쓴다.
	bool IsActivated() const { return bActivated; }

	// 서버에서만. 바뀌면 OnActivationChanged 가 울린다(서버는 여기서, 클라는 복제 도착 때).
	// bInstant 는 세이브 로드용 - 소품이 애니 없이 끝 자세로 간다. [MP 구멍] 클라에는 bInstant 가 안 간다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Vowbound|Interaction")
	void SetActivated(bool bNewActivated, bool bInstant = false);

	// 세이브 키. 저장하지 않는 대상이면 NAME_None.
	// 기본은 소유 액터의 이름이다 - 레벨에 놓인 액터의 이름은 로드마다 같아서 손으로 적을 것이 없다.
	//   런타임 스폰 액터는 이름이 매번 달라 이 키로 다시 찾을 수 없다 - 그런 대상은 SaveId 를 적거나 저장을 끈다(SetSaveState).
	//   (종전 주석은 «저장되지 않는다» 였으나 사실은 그 이름으로 저장되고 다시 쓰이지 않을 뿐이었다 - 2026-10-03 바로잡음.)
	FName GetSaveKey() const;

	// 런타임에 만든 대상(떨군 줍기 - AVBWorldItem::SpawnDrop)이 저장을 끈다. 이름이 매번 달라 켜짐 저장에 다시 쓰이지 않을 키가 쌓이기 때문이다.
	//   BeginPlay 전(지연 스폰 중)에 부른다 - 등록 때 대기 상태를 찾는 데도 이 키를 쓴다.
	void SetSaveState(bool bInSaveState) { bSaveState = bInSaveState; }

	// 상태가 «바뀌었을 때» 만 울린다 - 시작 상태는 안 울린다. 소유 액터는 BeginPlay 에서 IsActivated 를 읽어
	//   끝 자세로 바로 맞춘다. 왜: 컴포넌트 BeginPlay 가 소유 액터의 구독보다 먼저 돌아 시작 신호를 놓친다.
	UPROPERTY(BlueprintAssignable, Category="Vowbound|Interaction")
	FVBActivationChangedSignature OnActivationChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction")
	TObjectPtr<UVBInteractableConfig> Config;

	// 배치 시 꺼 둘 수 있다(잠긴 문으로 시작).
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category="Vowbound|Interaction")
	bool bInteractionEnabled = true;

	// 배치 시 켜진 채(열린 문으로) 시작할 수 있다.
	UPROPERTY(EditAnywhere, ReplicatedUsing=OnRep_Activated, BlueprintReadOnly, Category="Vowbound|Interaction")
	bool bActivated = false;

	UFUNCTION()
	void OnRep_Activated();

	// 켜짐 상태를 세이브에 남기는가. 한 번 쓰고 마는 대상(저장 포인트 등)은 끈다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction|Save")
	bool bSaveState = true;

	// 비어 있으면 소유 액터 이름이 키다(GetSaveKey).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction|Save")
	FName SaveId;

	// 이 대상이 «무엇인가»(예: Interaction.Door.Ward). 비어 있지 않으면 완료 때 Event.Interaction.Completed 를 이 태그들로 방송한다 -
	//   퀘스트의 «병동 문을 열어라» 가 듣는다(.sw/narrative 12.4). 놓인 자리마다 다르므로 인스턴스 칸이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction|Quest", meta=(Categories="Interaction"))
	FGameplayTagContainer EventTags;

	// 완료 때 적용할 효과(서버, 위 사건 뒤) - 상자의 내용물(«보상표 주기»), 열면 켜지는 사실 같은 «이 대상을 끝내면 생기는 일» (.sw/narrative 14.4).
	//   부른 플레이어 = 상호작용한 폰의 PlayerState, 상대 = 이 대상의 소유 액터. 놓인 자리마다 다르므로 인스턴스 칸이다.
	//   완료마다 돈다 - 한 번만 줄 것은 일회용 대상(AVBToggleProp::bOneShot)에 단다. 여닫는 대상에 달았다고 경고하지 않는 이유:
	//   이 컴포넌트는 소유 액터가 일회용인지 모르고, 사실 켜기처럼 여러 번 돌아도 맞는 효과가 있어 경고가 거짓으로 울린다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction|Quest", meta=(BaseStruct="/Script/Vowbound.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> CompletionEffects;

	// 이 대상을 쓸 수 있는 조건(전부 참). 비면 언제나 쓸 수 있다. 시작(UVBGA_Interact)과 완료(NotifyInteracted) 둘 다 본다 -
	//   동작 도중에 거짓이 되면 동작은 끝나도 완료 신호·효과가 안 난다. 자동 대상은 거짓인 동안 «들어옴» 으로 안 친다.
	//   프롬프트는 안 바꾼다 - 설비는 보이고 시도 때 답한다(03 Flow 5절, .sw/prologue-opening 14.3). 예: 제어함 = 첫 조우 해소 사실.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction|Quest", meta=(BaseStruct="/Script/Vowbound.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> UseConditions;

	// 시도가 거절될 때 HUD 알림 줄에 띄울 한 줄. 비면 글 없이 거절한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction|Quest")
	FText RefusedText;

	// 거절 때 서버 효과. 시도마다 돈다 - 한 번만 할 혼잣말은 «혼잣말» 의 조건(사실 거짓) + «사실 바꾸기» 로 만든다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction|Quest", meta=(BaseStruct="/Script/Vowbound.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> RefusedEffects;

private:
	// 조건·효과의 문맥: 부른 플레이어 = 상호작용한 폰의 PlayerState, 상대 = 이 대상의 소유 액터. 완료·거절·조건이 같은 문맥을 쓴다.
	FVBRuleContext MakeRuleContext(const AActor* InteractingActor) const;

	UPROPERTY(Replicated)
	FText ActionTextOverride;
};
