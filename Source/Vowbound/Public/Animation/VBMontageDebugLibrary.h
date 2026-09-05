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

	// 몽타주 첫 슬롯 트랙의 슬롯명을 재저작 (DefaultSlot→UpperBody 등). 콘텐츠 툴 — python 은 SlotAnimTracks 미노출이라 C++ 경유.
	// 반환: 성공(트랙 존재+변경) true. MarkPackageDirty 후 호출측이 에셋별 강제 save 필요(save_all only_if_dirty 스킵 트랩).
	UFUNCTION(BlueprintCallable, Category="Vowbound|Debug")
	static bool SetMontageSlotName(UAnimMontage* Montage, FName NewSlotName);

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

	// 빈/끊긴 montage 의 첫 슬롯 세그먼트에 anim 재연결 (RootMotion 복구). 성공 시 true.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Debug")
	static bool SetMontageSegmentAnim(UAnimMontage* Montage, UAnimSequenceBase* Anim);

	// PoseSearchBranchIn notify 의 Database 참조 경로(reflection). MotionMatch 가 검색 DB 를 찾는 링크.
	// "NULL-Database"=notify 있으나 DB 미설정, "no-BranchIn-notify"=notify 자체 없음. 정상이면 PSD 경로.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Debug")
	static FString GetMontageBranchInDatabasePath(UAnimMontage* Montage);

	// 비어있으면 notify 없음. PoseSearchBranchIn notify 의 Database 를 지정 PSD 로 설정(reflection). 성공 시 true.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Debug")
	static bool SetMontageBranchInDatabase(UAnimMontage* Montage, UObject* Database);

	// MotionWarping notify 의 RootMotionModifier.bWarpTranslation 설정(reflection). 적용된 notify 수 반환(0=notify 없음).
	// 왜: SkewWarp 의 translation 워프는 이 플래그로 하드 게이트됨(false=위치워프 무효). 공격 접근 간격(FIND-035)에 필수.
	//     notify 는 protected 접근이라 python 에서 못 세팅 → 이 함수 경유.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Debug")
	static int32 SetMontageWarpTranslation(UAnimMontage* Montage, bool bEnable);
};
