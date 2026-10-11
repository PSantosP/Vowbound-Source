#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PoseSearch/PoseSearchHistory.h"
#include "Animation/VBGASPCMCSelectionTypes.h"
#include "VBGASPLocomotionStateLibrary.generated.h"

class UAnimInstance;
class UChooserTable;

/** Native state callbacks; the AnimGraph, tagged nodes and Chooser authoring remain assets. */
UCLASS(meta=(BlueprintThreadSafe))
class VOWBOUND_API UVBGASPLocomotionStateLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Native three-context contract. Pose history always belongs to LinkedInstance. */
	static bool EvaluateCMCAnimation(UAnimInstance* LinkedInstance, UAnimInstance* ContextInstance,
		UChooserTable* Chooser, UAnimationAsset*& Animation, FVBGASPCMCChooserOutput& Output);
	static bool IsValidCMCSelection(const UAnimationAsset* Animation, const FVBGASPCMCChooserOutput& Output);
	static bool IsCMCPoseHistoryReady(UAnimInstance* Instance);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP")
	static void UpdateCMCSelection(UAnimInstance* Instance);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP")
	static void ConsumeCMCForceBlend(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP")
	static FVBGASPCMCBlendInputs GetCMCBlendInputs(UAnimInstance* Instance);
	/** Pure serial operation, also used by the runtime node dispatch. */
	static bool ConsumeCMCForceRequest(FVBGASPCMCState& State);
	/** Authored state names, rather than user-defined enum ordinals, define playback roles. */
	static EVBGASPCMCPhase GetCMCPhase(FName AuthoredState);
	static EVBGASPCMCPhase GetCMCSelectionPhase(FName AuthoredState, float VerticalSpeed);
	static void ApplyCMCWeaponPhase(FVBGASPCMCWeaponQuery& Query, FName AuthoredState, EVBGASPCMCPhase Phase);
	static TArray<FName> ProjectCMCPlaybackTags(const TArray<FName>& PlaybackTags, const TArray<FName>& DatabaseTags);
	static bool ShouldReleaseCMCTransition(EVBGASPCMCPhase Phase, bool bLooping,
		bool bToLoopNotify, float TimeRemaining, float BlendTime);
	/** 피벗 대체 래치의 한 걸음(순수). 피벗 원판정이 거짓이거나 래치 때와 자세가 다르면 끈다, 피벗 중에 전이가 반복 클립으로 대체됐으면 켠다, 그 밖에는 그대로.
	 *  원판정으로 푸는 이유: 막힌 판정(IsPivoting)으로 풀면 래치가 자기 자신을 영영 붙든다. */
	static bool StepPivotFallbackLatch(bool bLatched, bool bFellBackToLoop, bool bRawPivoting, bool bStanceChanged);
	/** 실제 링크 인스턴스에서 래치를 한 번 갱신한다(UpdateCMCSelection 이 매 갱신 머리에서 부른다) - 지금 자세와 피벗 원판정을 읽어 StepPivotFallbackLatch 로 푼다. */
	static void RefreshPivotFallbackLatch(UAnimInstance* Instance, FVBGASPCMCState& State);
	/** 래치의 주인(경로 무장/맨손·무기 풀)이 바뀌었나 - 바뀌었으면 SelectNativeCMC 가 래치를 지운다. */
	static bool HasPivotFallbackOwnerChanged(const FVBGASPCMCState& State);

	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP")
	static void SetBlendStackAnimFromChooser(UAnimInstance* Instance, uint8 StateMachineState, bool ForceBlend, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP")
	static void OnStateEntry_IdleLoop(UAnimInstance* Instance, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP")
	static void OnStateEntry_TransitionToIdle(UAnimInstance* Instance, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP")
	static void OnStateEntry_LocomotionLoop(UAnimInstance* Instance, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP")
	static void OnStateEntry_TransitionToLocomotion(UAnimInstance* Instance, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP")
	static void OnStateEntry_InAirLoop(UAnimInstance* Instance, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP")
	static void OnStateEntry_TransitionToInAir(UAnimInstance* Instance, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP")
	static void OnStateEntry_IdleBreak(UAnimInstance* Instance, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP")
	static void OnStateEntry_SlideLoop(UAnimInstance* Instance, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP")
	static void OnStateEntry_TransitionToSlide(UAnimInstance* Instance, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP")
	static void OnUpdate_TransitionToLocomotion(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP")
	static bool IsAnimationAlmostComplete(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP")
	static bool PlayLand(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP")
	static bool PlayMovingLand(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP")
	static FPoseHistoryReference Get_PoseHistoryReference(UAnimInstance* Instance);
};
