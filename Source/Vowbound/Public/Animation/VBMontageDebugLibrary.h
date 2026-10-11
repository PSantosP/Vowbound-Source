// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBMontageDebugLibrary.generated.h"

class UAnimMontage;

/**
 * UVBMontageDebugLibrary
 * 진단용 — UE 5.7 python reflection 이 막은 UAnimMontage 내부(SlotAnimTracks/Notifies)를
 * C++ public 멤버/공개 API 로 읽어 execute_python_script 에서 호출 가능하게 노출.
 * 트래버설 T-pose 원인(몽타주 슬롯명) 진단에 사용.
 *
 * 읽기만 한다. 고치는 함수(Set*) 다섯은 2026-10-02 구조 정리 1b 에서 에디터 모듈의 UVBMontageAuthoring 으로 옮겼다 -
 *   자산을 고치는 것은 에디터의 일이고, 여기 있던 동안 WITH_EDITOR 가드 없이 MarkPackageDirty 를 불렀다.
 */
UCLASS()
class VOWBOUND_API UVBMontageDebugLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// 몽타주 첫 슬롯 트랙의 슬롯명 (SlotAnimTracks[0].SlotName). 트랙 없으면 NAME_None.
	// LOAD-BEARING (진단용이 아니다): VBWeaponStateComponent 가 매 무기교체마다 호출해 상체/전신 게이트를 결정한다(FIND-053).
	//   "Debug" 카테고리지만 런타임 전투 로직에 배선됨 — 미사용 진단툴로 오인해 제거 금지.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Debug")
	static FName GetMontageSlotName(UAnimMontage* Montage);

	// 몽타주 슬롯 트랙 개수.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Debug")
	static int32 GetMontageSlotCount(UAnimMontage* Montage);

	// 몽타주 Notify 들의 요약 문자열 (이름 @시간 [class]). MotionWarping notify 유무 확인용.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Debug")
	static TArray<FString> GetMontageNotifySummary(UAnimMontage* Montage);

	// 몽타주 전체 구간 RootMotion 누적 translation. Z≈0 이면 수직 RootMotion 없음(리타겟/임포트 손실).
	UFUNCTION(BlueprintCallable, Category="Vowbound|Debug")
	static FVector GetMontageRootMotionDelta(UAnimMontage* Montage);

	// 몽타주 첫 슬롯의 source AnimSequence 정보 (이름/enableRM/forceRootLock). RootMotion 0 원인 진단.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Debug")
	static FString GetMontageRootMotionInfo(UAnimMontage* Montage);

	// MotionWarping notify 의 RootMotionModifier(SkewWarp) 모든 bool/enum 프로퍼티 덤프. Z 워프 무시 플래그 진단.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Debug")
	static FString GetMontageWarpModifierInfo(UAnimMontage* Montage);

	// 첫 슬롯 세그먼트의 source anim 의 실제 경로(없으면 NULL). 끊긴 참조 진단.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Debug")
	static FString GetMontageSegmentAnimPath(UAnimMontage* Montage);

	// PoseSearchBranchIn notify 의 Database 참조 경로(reflection). MotionMatch 가 검색 DB 를 찾는 링크.
	// "NULL-Database"=notify 있으나 DB 미설정, "no-BranchIn-notify"=notify 자체 없음. 정상이면 PSD 경로.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Debug")
	static FString GetMontageBranchInDatabasePath(UAnimMontage* Montage);

	// 섹션의 다음 섹션 이름. NAME_None 이면 그 섹션에서 몽타주가 멈춘다(콤보는 GA 가 명시적으로 점프한다).
	// FCompositeSection 은 python 에 안 나와 있어 여기서 읽는다(2026-09-06, 연타 병사 몽타주 저작). 쓰는 쪽은 UVBMontageAuthoring.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Debug|Montage")
	static FName GetMontageSectionNext(UAnimMontage* Montage, FName SectionName);
};
