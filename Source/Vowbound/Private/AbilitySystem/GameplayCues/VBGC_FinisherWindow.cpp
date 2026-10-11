// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/GameplayCues/VBGC_FinisherWindow.h"

#include "CharacterCommon/VBHitFlashComponent.h"
#include "Components/SkeletalMeshComponent.h"  // 외곽선용 스텐실을 찍는다
#include "Vowbound/Vowbound.h"

bool UVBGC_FinisherWindow::OnActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	UVBHitFlashComponent* Flash = MyTarget ? MyTarget->FindComponentByClass<UVBHitFlashComponent>() : nullptr;
	if (!Flash)
	{
		// 조용히 넘어가면 「처형 표시가 안 뜬다」의 원인을 못 찾는다. 말은 한 번만 한다(Verbose).
		VB_LOG(Verbose, "VBGC_FinisherWindow: %s 에 UVBHitFlashComponent 없음 - 발광 스킵",
		       *GetNameSafe(MyTarget));
		return false;
	}

	if (!GlowProfile.OverlayMaterial)
	{
		// 저작이 비어 있으면 아무 일도 안 일어난다. 그 침묵이 「기능이 없다」로 읽히므로 소리 내어 밝힌다.
		VB_LOG(Warning, "VBGC_FinisherWindow: 발광 프로필에 OverlayMaterial 이 없다 - "
		                "GCN_FinisherWindow 클래스 기본값에서 저작해라");
		return false;
	}

	Flash->BeginGlow(GlowProfile);
	SetOutline(MyTarget, /*bEnable=*/true);
	return true;
}

void UVBGC_FinisherWindow::SetOutline(AActor* Target, bool bEnable) const
{
	if (!Target || OutlineStencilValue <= 0)
	{
		return;
	}

	// 메시가 여럿일 수 있다(무기·장식). 실루엣은 «보이는 것 전부» 라 하나만 찍으면 선이 끊긴다.
	TArray<USkeletalMeshComponent*> Meshes;
	Target->GetComponents(Meshes);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (!Mesh)
		{
			continue;
		}
		Mesh->SetRenderCustomDepth(bEnable);
		if (bEnable)
		{
			Mesh->SetCustomDepthStencilValue(OutlineStencilValue);
		}
	}
}

bool UVBGC_FinisherWindow::OnRemove_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	if (UVBHitFlashComponent* Flash = MyTarget ? MyTarget->FindComponentByClass<UVBHitFlashComponent>() : nullptr)
	{
		Flash->EndGlow();
	}

	// 발광이 없는 대상이어도 외곽선은 껐어야 한다 - 위 if 밖에 둔다.
	//   켜는 쪽은 발광 실패 시 일찍 돌아가지만, 끄는 쪽이 조건부면 켜진 채로 남는다.
	SetOutline(MyTarget, /*bEnable=*/false);
	return true;
}
