// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBMontageAuthoring.generated.h"

class UAnimMontage;
class UAnimSequenceBase;
class USkeleton;

/**
 * 몽타주를 «고치는» 콘텐츠 도구. 파이썬(execute_python_script)에서 부른다 -
 *   python 이 SlotAnimTracks·Notifies·CompositeSections 를 못 건드려서 C++ 를 거친다.
 *
 * 왜 에디터 모듈에 있나 (2026-10-02, 구조 정리 1b): 종전에는 런타임 모듈의 UVBMontageDebugLibrary 에
 *   읽는 함수와 섞여 있었고, WITH_EDITOR 가드 없이 MarkPackageDirty 를 불렀다. 자산을 고치는 것은
 *   에디터의 일이라 여기로 옮겼다. 읽는 함수(Get*)는 런타임이 쓰므로 그쪽에 남았다
 *   (GetMontageSlotName 은 VBWeaponStateComponent 가 매 무기교체마다 부른다).
 * 파이썬 이름이 바뀌었다: unreal.VBMontageDebugLibrary.set_* -> unreal.VBMontageAuthoring.set_*.
 * 저장은 부르는 쪽이 한다. MarkPackageDirty 만 하므로 저장을 따로 해야 디스크에 남는다
 *   (save_all only_if_dirty 가 건너뛰는 경우가 있다 - tool-traps).
 */
UCLASS()
class VOWBOUNDANIMATIONEDITOR_API UVBMontageAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// 몽타주 첫 슬롯 트랙의 슬롯명을 재저작 (DefaultSlot -> UpperBody 등). 반환: 트랙이 있어 바꿨으면 true.
	UFUNCTION(BlueprintCallable, Category="Vowbound|MontageAuthoring")
	static bool SetMontageSlotName(UAnimMontage* Montage, FName NewSlotName);

	// 빈/끊긴 몽타주의 첫 슬롯 세그먼트에 애니를 다시 잇는다(RootMotion 복구). 성공 시 true.
	UFUNCTION(BlueprintCallable, Category="Vowbound|MontageAuthoring")
	static bool SetMontageSegmentAnim(UAnimMontage* Montage, UAnimSequenceBase* Anim);

	// PoseSearchBranchIn 노티의 Database 를 지정 PSD 로 설정(reflection). 성공 시 true.
	UFUNCTION(BlueprintCallable, Category="Vowbound|MontageAuthoring")
	static bool SetMontageBranchInDatabase(UAnimMontage* Montage, UObject* Database);

	// MotionWarping 노티의 RootMotionModifier.bWarpTranslation 설정(reflection). 적용된 노티 수를 준다(0=노티 없음).
	// 왜: SkewWarp 의 translation 워프는 이 플래그로 하드 게이트된다(false=위치 워프 무효). 공격 접근 간격(FIND-035)에 필수.
	//   노티는 protected 접근이라 python 에서 못 세팅한다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|MontageAuthoring")
	static int32 SetMontageWarpTranslation(UAnimMontage* Montage, bool bEnable);

	// 섹션의 다음 섹션 이름을 바꾼다. FCompositeSection 은 python 에 안 나와 있다(2026-09-06, 연타 병사 몽타주 저작).
	UFUNCTION(BlueprintCallable, Category="Vowbound|MontageAuthoring")
	static bool SetMontageSectionNext(UAnimMontage* Montage, FName SectionName, FName NextSectionName);

	// 첫 슬롯 첫 세그먼트의 원료 구간·배속을 바꾸고 몽타주 길이를 다시 잰다(2026-10-06, 거즈 사용 몽타주).
	//   python 이 세그먼트를 고쳐도 길이는 C++ 에서만 다시 잰다(UAnimMontage::CalculateSequenceLength). 노티는 몽타주 시각 그대로 남으니 다시 놓는 것은 부르는 쪽 몫이다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|MontageAuthoring")
	static bool SetMontageSegmentRange(UAnimMontage* Montage, float AnimStartTime, float AnimEndTime, float PlayRate);

	// 골격에 몽타주 슬롯을 등록하고 그룹을 정한다(2026-10-06). 같은 그룹의 몽타주끼리 서로 끊으므로 그룹이 곧 «무엇이 무엇을 끊나» 다.
	//   종전에는 Anim Slot Manager 에서 손으로 했다(UpperBody, 2026-07-23). 그룹이 없으면 거절한다 - 새 그룹을 만들지 않는다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|MontageAuthoring")
	static bool RegisterSkeletonSlot(USkeleton* Skeleton, FName SlotName, FName GroupName);
};
