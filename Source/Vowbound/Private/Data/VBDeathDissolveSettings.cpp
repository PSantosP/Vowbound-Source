// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Data/VBDeathDissolveSettings.h"

#include "Data/VBDeathDissolveConfig.h"

UVBDeathDissolveConfig* UVBDeathDissolveSettings::ResolveConfig(UVBDeathDissolveConfig* Explicit)
{
	// 명시된 것이 이긴다. 적 설정의 DeathDissolve 는 «이 적만 다르게» 를 뜻한다.
	if (Explicit)
	{
		return Explicit;
	}

	// 아무도 안 정했으면 프로젝트 기본으로 떨어진다.
	//
	// 왜 (2026-09-20, user 지시 「모든 몬스터 다 되게끔 구조를 만들었어야지」): 종전에는 적 설정이
	//   비어 있으면 이 연출이 그냥 안 났다. 새 몬스터의 기본이 «없음» 이라 아무 경고 없이 조용히 빠진다 -
	//   실제로 보스 트롤이 그랬다. 여기서 떨어뜨리면 배선의 뜻이 «켜기» 에서 «덮어쓰기» 로 뒤집힌다.
	// 끄는 법: 프로젝트 설정을 비우면 프로젝트 전체가 꺼진다. 코드를 안 고치고 끌 수 있어야 한다.
	return GetDefault<UVBDeathDissolveSettings>()->DefaultConfig.LoadSynchronous();
}
