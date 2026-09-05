// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Data/VBHitFlashConfig.h"

#include "Materials/MaterialInterface.h"

const FVBHitFlashProfile& UVBHitFlashConfig::ResolveProfile(const FGameplayTag& CueTag) const
{
	// 1) 정확 일치.
	// 2) 없으면 부모 태그를 위로 순회 — 자식 태그(...HitFlash.Infection)가 부모 GCN 으로 라우팅되는
	//    엔진 거동과 데이터 폴백을 같은 모양으로 맞춘다. 태그 계층이 곧 데이터 계층이 된다.
	// OverlayMaterial 이 빈 엔트리는 '미등록'으로 취급한다 — 부분 할당된 엔트리가 무동작을 유발하지 않도록
	//   (UVBHitReactConfig::ResolveMontage 의 폴백 관례 확장).
	for (FGameplayTag Tag = CueTag; Tag.IsValid(); Tag = Tag.RequestDirectParent())
	{
		if (const FVBHitFlashProfile* Profile = TagOverrides.Find(Tag))
		{
			if (Profile->OverlayMaterial)
			{
				return *Profile;
			}
		}
	}

	// 3) 최종 폴백. DefaultProfile.OverlayMaterial 마저 비어 있으면 소비자(컴포넌트)가 무동작으로 빠진다
	//    — 크래시 대신 무연출이 옳다(플래시는 순수 코스메틱이라 게임플레이에 영향이 없다).
	return DefaultProfile;
}
