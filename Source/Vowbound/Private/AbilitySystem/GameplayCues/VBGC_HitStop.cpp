// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/GameplayCues/VBGC_HitStop.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "TimeControl/VBTimeDilationSubsystem.h"
#include "Vowbound/Vowbound.h"

namespace
{
	// 히트스톱 요청의 안정적 handle. 같은 handle 재요청 = 자기 자신 교체(중첩 아님) →
	// 겹친 히트스톱이 서로 덮어쓰지 않고 하나로 coalesce 되어 duration 이 연장된다.
	const FName GHitStopHandle(TEXT("HitStop"));
}

// 생성자에서 GameplayCueTag 를 세우지 않는다. 태그는 GCN_HitStop BP 의 Class Defaults 가 소유한다.
//
// 왜: 네이티브 부모가 태그를 들면 BP 자식 저장 시 UAbilitySystemGlobals::DeriveGameplayCueTagFromClass 가
// "부모와 태그가 같다"를 감지해 자산명(GCN_HitStop)에서 태그를 유도하려 하고, 유도에 실패하면
// GameplayCueName 을 갱신하지 않은 채 return 한다. GameplayCueName 은 에셋 레지스트리 검색 키라
// 비어 있으면 BuildCuesToAddToGlobalSet 이 그 BP 를 건너뛰고, 큐는 아무 에러 없이 죽는다.
//
// 실측(2026-08-03): 생성자에 태그가 있는 이 클래스는 BP 자식에서 gameplayCueName=None 이었고,
// 생성자에 태그가 없는 UVBGC_HitFlash 는 정상 유도됐다. 그 한 줄이 유일한 차이였다.
//
// 애초에 네이티브 태그만으로는 등록 자체가 안 된다: InitObjectLibrary 는 BP 에셋만 스캔하므로
// (GameplayCueManager.cpp::UGameplayCueManager::LoadBlueprintAssetDataFromPaths) 네이티브 GCN 은 큐 세트에 들어갈 경로가 없다. BP 자식이 필수 부품이다.
UVBGC_HitStop::UVBGC_HitStop()
{
}

bool UVBGC_HitStop::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	// 여기에 NM_DedicatedServer 스킵 가드를 넣지 말 것. 큐는 데디 서버에서 실행되지 않는다
	// (GameplayCueRunOnDedicatedServer=0, FIND-038 에서 확정 — 그래서 히트리액트를 큐에서 GA 로 옮겼다).
	// UVBWeaponStateComponent::MulticastPlayWeaponChangeActing 에 그 가드가 있는 것은 그쪽이 Multicast RPC 라
	// 데디 서버에서 실제로 실행되기 때문이고, 기구가 다르다. 도달 불가 방어는 심지 않는다.
	// GameplayCueRunOnDedicatedServer 를 켜는 날이 오면 그때 이 판단을 다시 한다.
	UWorld* World = MyTarget ? MyTarget->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}
	
	// 큐 파라미터가 히트스톱 값의 유일한 출처다. RawMagnitude=Duration, NormalizedMagnitude=TimeDilation.
	// 값이 없으면 디폴트로 때우지 않고 거부한다. 채워 넣으면 두 가지가 동시에 나빠진다.
	//   하나, UVBAttackConfig 말고 여기에도 튜닝값이 생겨 두 홈이 조용히 갈라진다.
	//   둘, 배선이 끊긴 사실이 화면상 정상 동작으로 가려져 영영 드러나지 않는다.
	// 크기가 없는 큐는 튜닝 상황이 아니라 배선 버그다 - 큐를 쏘는 경로는 config 값을 싣는 곳 하나뿐이다.
	// Duration=0 을 흘리면 특히 위험하다. PushTimed 가 Push 로 위임해 만료 타이머 없는 무기한 슬로모가
	//   걸리고, Pop 호출부가 없어 그 상태에서 복구되지 않는다.
	if (Parameters.RawMagnitude <= 0.0f || Parameters.NormalizedMagnitude <= 0.0f)
	{
		VB_LOG(Warning, "VBGC_HitStop: 큐에 히트스톱 크기가 없다 (Duration=%.3f Dilation=%.3f) - 배선 오류라 스킵",
		       Parameters.RawMagnitude, Parameters.NormalizedMagnitude);
		return false;
	}

	const float Duration     = Parameters.RawMagnitude;
	const float TimeDilation = Parameters.NormalizedMagnitude;
	
	// 조정 계층에 위임한다. 직접 SetGlobalTimeDilation + 로컬 복원타이머는 히트스톱이 겹칠 때
	// 서로 덮어써(첫 복원이 시간을 1.0 으로 스냅 → 둘째 슬로모가 끊김) '팝'이 났다(clobbering).
	// 이제 handle 기반 요청 → min-wins 해소 → 조정계층이 real-time 만료로 복원을 단일 소유한다.
	// 각 클라 로컬 실행은 그대로(서브시스템도 per-world) — SP/host 거동 보존.
	if (UVBTimeDilationSubsystem* TimeSubsystem = UVBTimeDilationSubsystem::Get(World))
	{
		TimeSubsystem->PushTimed(GHitStopHandle, TimeDilation, Duration);
	}
	else
	{
		VB_LOG(Warning, "VBGC_HitStop: TimeDilationSubsystem 없음 — 히트스톱 스킵");
	}

	return true;
}
