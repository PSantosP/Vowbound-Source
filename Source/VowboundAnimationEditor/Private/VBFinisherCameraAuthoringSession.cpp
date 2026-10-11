// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "VBFinisherCameraAuthoringSession.h"

#include "Data/VBFinisherConfig.h"
#include "Editor.h"                            // GEditor, FEditorDelegates
#include "Engine/Selection.h"                  // 선택한 액터를 기준으로 삼는다
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimMontage.h"             // 씬 트랙이 몽타주라 풀어서 시퀀스를 꺼낸다
#include "Components/SkeletalMeshComponent.h"  // 미리보기 자세 세우기
#include "Components/SceneComponent.h"         // 상대역의 뿌리 - 캐릭터와 같은 층으로 짓는다
#include "Camera/CameraActor.h"                // 키를 붙잡고 옮길 수 있게 레벨로 꺼낸다
#include "Camera/CameraComponent.h"            // 꺼낸 카메라의 FOV 를 키와 맞춘다
#include "ContextualAnimSceneAsset.h"          // GetAnimTrack / 정렬 변환 / UContextualAnimRolesAsset
#include "ContextualAnimTypes.h"               // FContextualAnimTrack
#include "EditorViewportClient.h"              // ViewFOV - 화각까지 맞춰야 본 구도가 참이 된다
#include "ScopedTransaction.h"                 // 자산을 바꾸는 동작의 되돌리기 기록
#include "Subsystems/AssetEditorSubsystem.h"   // 창이 닫히는 순간을 잡는다
#include "Subsystems/UnrealEditorSubsystem.h"  // 레벨 뷰포트 카메라 읽기·쓰기
#include "UObject/UObjectGlobals.h"            // FCoreUObjectDelegates::OnObjectPropertyChanged

// 런타임의 LogVB 는 export 되지 않아 이 모듈에서 못 쓴다(Vowbound.h). 기능 카테고리는 reuse-catalog §1 이 허용한다.
DEFINE_LOG_CATEGORY_STATIC(LogVBFinisherAuthoring, Log, All);

#define LOCTEXT_NAMESPACE "VBFinisherCameraAuthoring"

namespace
{
	// 기준 액터는 «선택한 것» 이다. 키 좌표가 그 액터 기준 상대값이라 기준 없이는 뜻이 안 선다.
	//   자동으로 고르지 않는다 - 레벨에 후보가 여럿일 때 엉뚱한 것을 골라도 사람은 모른다.
	// 못 찾으면 그 자리에서 말한다 - 조용히 돌아가면 버튼이 고장난 것으로 읽힌다(규칙 27: 말밖에 못 하는 자리).
	AActor* VBSelectedAnchor()
	{
		AActor* Anchor = nullptr;
		if (GEditor)
		{
			if (USelection* Selected = GEditor->GetSelectedActors())
			{
				Anchor = Selected->GetTop<AActor>();
			}
		}
		if (!Anchor)
		{
			UE_LOG(LogVBFinisherAuthoring, Warning, TEXT("처형 카메라 저작: 기준 액터가 선택되지 않았다. "
			                                             "레벨에서 처형당할 액터(더미 등)를 클릭한 뒤 다시 눌러라"));
		}
		return Anchor;
	}

	// 「다룰 키」 버튼 둘이 같은 준비를 한다 - 다룰 키가 있는가, 기준 액터가 선택돼 있는가.
	bool VBGetAuthoringContext(const UVBFinisherConfig& Config, int32 KeyIndex,
	                           AActor*& OutAnchor, UUnrealEditorSubsystem*& OutEditorSub)
	{
		OutAnchor    = nullptr;
		OutEditorSub = nullptr;

		if (!Config.CameraKeys.IsValidIndex(KeyIndex))
		{
			UE_LOG(LogVBFinisherAuthoring, Warning, TEXT("처형 카메라 저작: 키 번호 %d 이 없다(키 %d개). 다룰 키 번호를 고쳐라"),
			       KeyIndex, Config.CameraKeys.Num());
			return false;
		}

		if (!GEditor)
		{
			return false;
		}

		OutAnchor = VBSelectedAnchor();
		if (!OutAnchor)
		{
			return false;
		}

		OutEditorSub = GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>();
		return OutEditorSub != nullptr;
	}

	// 활성 레벨 뷰포트. FOV 는 UUnrealEditorSubsystem 이 안 다뤄서 뷰포트 클라이언트를 직접 잡는다.
	// 없으면 nullptr - 그때는 FOV 만 못 맞추고 위치·방향은 그대로 된다(부분 실패를 통째 실패로 만들지 않는다).
	FEditorViewportClient* VBActiveViewportClient()
	{
		if (!GEditor)
		{
			return nullptr;
		}
		for (FEditorViewportClient* Client : GEditor->GetAllViewportClients())
		{
			if (Client && Client->IsPerspective() && Client->Viewport && Client->Viewport->HasFocus())
			{
				return Client;
			}
		}
		// 포커스가 없으면 첫 원근 뷰포트를 쓴다. 하나뿐인 경우가 대부분이다.
		for (FEditorViewportClient* Client : GEditor->GetAllViewportClients())
		{
			if (Client && Client->IsPerspective())
			{
				return Client;
			}
		}
		return nullptr;
	}

	// 키 좌표계는 액터의 위치와 «yaw» 만 쓴다. 런타임(AVBFinisherCamera::Tick)이 그렇게 푼다 -
	//   저작과 재생이 다른 좌표계를 쓰면 담은 그림과 나오는 그림이 어긋난다.
	FTransform VBAnchorFrame(const AActor& Anchor)
	{
		return FTransform(Anchor.GetActorQuat(), Anchor.GetActorLocation());
	}

	// 선택한 액터들을 순서대로 준다. 첫째가 앵커 역할이다.
	void VBGetSelectedActors(TArray<AActor*>& Out)
	{
		Out.Reset();
		if (!GEditor)
		{
			return;
		}
		if (USelection* Selected = GEditor->GetSelectedActors())
		{
			for (FSelectionIterator It(*Selected); It; ++It)
			{
				if (AActor* Actor = Cast<AActor>(*It))
				{
					Out.Add(Actor);
				}
			}
		}
	}

	// 씬 자산의 트랙은 «몽타주» 를 가리킨다(AM_Exec_*). 단일 노드 인스턴스는 몽타주를 못 튼다 -
	//   모드만 바뀌고 애니는 안 걸려서 기준 자세(팔 벌린 A 포즈)가 나온다(2026-09-13 실측: 모드는
	//   ANIMATION_SINGLE_NODE 인데 재생할 애니가 None 이었다).
	// 그래서 몽타주를 풀어 안의 시퀀스를 꺼내고, 시각도 그 시퀀스 기준으로 옮긴다.
	UAnimSequenceBase* VBResolvePlayable(UAnimSequenceBase* Animation, float InTime, float& OutTime)
	{
		OutTime = InTime;
		UAnimMontage* Montage = Cast<UAnimMontage>(Animation);
		if (!Montage)
		{
			return Animation;   // 이미 시퀀스다
		}

		if (Montage->SlotAnimTracks.Num() == 0 || Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() == 0)
		{
			return nullptr;
		}

		const FAnimSegment& Segment = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
		OutTime = Segment.ConvertTrackPosToAnimPos(InTime);
		return Segment.GetAnimReference();
	}

	// 한 액터를 그 클립의 그 시각 자세로 세운다. 에디터에서는 애니 평가가 기본으로 꺼져 있어
	//   켜 주지 않으면 포즈가 안 바뀐다 - 값은 들어가는데 화면이 그대로라 「안 먹는다」로 보인다.
	// 자세만 세운다. 액터도 메시도 «옮기지» 않는다 - 그것은 자리 잡기의 몫이고, 여기서 같이 하면
	//   주인이 둘이 되어 서로 덮어쓴다(2026-09-13 에 실제로 그랬다).
	void VBPoseActorAt(AActor& Actor, UAnimSequenceBase* Animation, float Time)
	{
		USkeletalMeshComponent* Mesh = Actor.FindComponentByClass<USkeletalMeshComponent>();
		if (!Mesh || !Animation)
		{
			return;
		}

		float              PlayTime = Time;
		UAnimSequenceBase* Playable = VBResolvePlayable(Animation, Time, PlayTime);
		if (!Playable)
		{
			UE_LOG(LogVBFinisherAuthoring, Warning, TEXT("처형 카메라 저작: %s 에서 재생할 시퀀스를 못 꺼냈다"),
			       *Animation->GetName());
			return;
		}

		// PlayAnimation 을 쓰면 안 된다. 그것은 SetAnimation 뒤에 «Play» 까지 하고
		//   (엔진 SkeletalMeshComponent.cpp::USkeletalMeshComponent::PlayAnimation),
		//   에디터 틱이 켜진 상태라 그 뒤로 애니가 «혼자 흘러간다»
		//   (2026-09-13 user: 「슬라이드하면 그냥 알아서 재생되고 막 그래」).
		// SetAnimation 은 자산만 걸고 SetPlaying(false) 를 함께 한다. 시간은 우리가 준다 -
		//   재생은 저작 도구의 타이머가 몰아야 시각 슬라이더와 한 값을 본다.
		Mesh->SetUpdateAnimationInEditor(true);
		Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Mesh->SetAnimation(Playable);
		Mesh->SetPosition(PlayTime, /*bFireNotifies=*/false);
		Mesh->TickAnimation(0.0f, false);
		Mesh->RefreshBoneTransforms();

		// 루트 이동을 «걷어내지 않는다». 한때 걷어냈는데 그것이 공격자를 1m 옮긴 원인이었다(2026-09-13).
		//   런타임은 루트모션이 액터를 옮기고 메시가 따라간다. 미리보기는 추출이 없어 이동이 포즈에 남는다.
		//   세계 좌표에서 몸이 있는 자리는 둘 다 «시작 + 이동» 으로 같다. 뺄 것이 없다.
	}
}

void UVBFinisherCameraAuthoringSession::Initialize(UVBFinisherConfig* InConfig)
{
	Config = InConfig;

	// 자산 변경 통지는 세션이 살아 있는 내내 듣는다. 미리보기 자원(재생·꺼낸 카메라·대역)과 수명이 다르다.
	//   1b 전에는 자산 자신의 PostEditChangeProperty 가 받아서 미리보기와 무관하게 «다룰 키 번호» 를 조였다.
	//   미리보기를 켤 때만 걸면, 처음 연 패널이나 미리보기를 끈 뒤 키를 지울 때 번호가 안 조여진다
	//   (2026-10-03 codex-claude_02 검토). 떼는 곳은 BeginDestroy 하나다.
	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(
		this, &UVBFinisherCameraAuthoringSession::OnObjectPropertyChanged);
}

bool UVBFinisherCameraAuthoringSession::EnsureConfigOrRelease()
{
	if (Config.IsValid())
	{
		return true;
	}
	// 자산이 사라졌다. 그냥 돌아가면 재생 타이머와 레벨의 임시 액터가 주인 없이 남는다.
	if (HasPreviewResources())
	{
		UE_LOG(LogVBFinisherAuthoring, Log, TEXT("처형 카메라 저작: 자산이 사라져 미리보기를 거둔다"));
		ReleasePreview();
	}
	return false;
}

bool UVBFinisherCameraAuthoringSession::HasPreviewResources() const
{
	return PlayTickerHandle.IsValid() || PreviewSelf.IsValid() || PreviewPartner.IsValid()
	    || !HiddenActors.IsEmpty() || !KeyCameras.IsEmpty();
}

void UVBFinisherCameraAuthoringSession::SyncKeyIndexToPreviewTime()
{
	const UVBFinisherConfig* Cfg = Config.Get();
	if (!Cfg || Cfg->CameraKeys.IsEmpty())
	{
		return;
	}

	int32 Best     = 0;
	float BestDist = FLT_MAX;
	for (int32 i = 0; i < Cfg->CameraKeys.Num(); ++i)
	{
		const float Dist = FMath::Abs(Cfg->CameraKeys[i].Time - EditorPreviewTime);
		if (Dist < BestDist)
		{
			BestDist = Dist;
			Best     = i;
		}
	}
	EditorKeyIndex = Best;
}

void UVBFinisherCameraAuthoringSession::OnObjectSelected(UObject* Selected)
{
	// 우리가 꺼낸 키 카메라를 고른 것만 본다. 레벨의 다른 액터를 골랐다고 번호가 움직이면
	//   사람이 잡아 둔 번호가 클릭 한 번에 날아간다.
	if (!EnsureConfigOrRelease())
	{
		return;
	}
	const UVBFinisherConfig* Cfg = Config.Get();
	const int32 Found = KeyCameras.IndexOfByPredicate(
		[Selected](const TWeakObjectPtr<AActor>& Ptr) { return Ptr.Get() == Selected; });
	if (!Cfg || Found == INDEX_NONE || !Cfg->CameraKeys.IsValidIndex(Found))
	{
		return;
	}

	EditorKeyIndex    = Found;
	EditorPreviewTime = Cfg->CameraKeys[Found].Time;
	UE_LOG(LogVBFinisherAuthoring, Log, TEXT("처형 카메라 저작: 키 %d (%.2f초) 을 골랐다"), Found, EditorPreviewTime);
}

void UVBFinisherCameraAuthoringSession::InvalidateKeyCameras()
{
	// 키 배열이 바뀌면 꺼내 둔 카메라와 번호가 어긋난다. 배열 순서가 곧 대응 관계이기 때문이다.
	//   어긋난 채로 두면 «조용히 틀린다» - 되돌려 쓰기가 카메라 N 의 자리를 지금의 키 N 에 넣는데,
	//   그것은 사람이 그 카메라로 잡던 키가 아니다. 그래서 값을 고치려 들지 않고 «없는 상태» 로 되돌린다.
	if (KeyCameras.IsEmpty())
	{
		return;
	}
	DestroyKeyCameras();
	UE_LOG(LogVBFinisherAuthoring, Log, TEXT("처형 카메라 저작: 키가 바뀌어 꺼내 둔 카메라를 지웠다. 필요하면 다시 꺼내라"));
}

void UVBFinisherCameraAuthoringSession::AddKeyFromViewport()
{
	// 되돌리기·다시하기 도중에는 아무것도 바꾸지 않는다. 맨 앞에서 본다 - 아래 «따라가기 끄기» 까지
	//   해 놓고 돌아가면 반쯤 한 상태가 남는다.
	if (GIsTransacting || !EnsureConfigOrRelease())
	{
		return;
	}
	UVBFinisherConfig* Cfg = Config.Get();

	AActor* Anchor = VBSelectedAnchor();
	if (!Anchor)
	{
		return;
	}

	UUnrealEditorSubsystem* EditorSub =
		GEditor ? GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>() : nullptr;
	if (!EditorSub)
	{
		return;
	}

	// 따라가기가 켜져 있으면 화면은 «이미 있는 키가 만든 그림» 이다. 그 상태로 담으면
	//   사람이 잡은 것이 아니라 자기 자신을 다시 담는다. 그래서 끄고, 껐다고 말한다.
	if (bEditorFollowCamera)
	{
		bEditorFollowCamera = false;
		UE_LOG(LogVBFinisherAuthoring, Log, TEXT("처형 카메라 저작: 「카메라 따라가기」를 껐다 - 켜진 채로는 화면이 "
		                                         "기존 키가 만든 그림이라 담을 것이 없다"));
	}

	FVector  CamLocation = FVector::ZeroVector;
	FRotator CamRotation = FRotator::ZeroRotator;
	EditorSub->GetLevelViewportCameraInfo(CamLocation, CamRotation);

	const FTransform Frame = VBAnchorFrame(*Anchor);

	// 볼 지점 규약은 「이 키에 담기」와 같다 - 보는 방향으로 기준 액터까지의 거리만큼 나간 점.
	//   기준 액터 원점을 그대로 쓰면 발밑을 보게 되어 담는 순간 그림이 내려간다.
	const float   Distance = FMath::Max(1.0f, FVector::Dist(CamLocation, Anchor->GetActorLocation()));
	const FVector LookAt   = CamLocation + CamRotation.Vector() * Distance;

	FVBFinisherCameraKey New;
	New.Time             = EditorPreviewTime;
	New.RelativeLocation = Frame.InverseTransformPosition(CamLocation);
	New.LookAtOffset     = Frame.InverseTransformPosition(LookAt);
	if (const FEditorViewportClient* Client = VBActiveViewportClient())
	{
		New.FOV = Client->ViewFOV;
	}

	// 되돌리기에 올린다. 버튼이 아니라 여기서 여는 이유: 자산을 바꾸는 동작은 어디서 부르든 무를 수 있어야 한다.
	const FScopedTransaction Transaction(LOCTEXT("AddKeyFromViewport", "처형 카메라: 지금 화면으로 키 추가"));
	Cfg->Modify();
	// 세션도 같은 기록에 넣는다 - 아래에서 «다룰 키 번호» 를 새 키로 옮긴다. 1b 전에는 번호가 자산에 있어
	//   자산의 Modify 하나로 함께 되돌아갔다. 빠뜨리면 Ctrl+Z 뒤 키는 줄었는데 번호는 범위 밖에 남는다
	//   (2026-10-03 codex-claude_02 검토).
	Modify();

	// 같은 시각이면 덮어쓴다. 길이 0 인 구간이 생기면 그 사이를 보간할 수가 없고,
	//   패널에서는 키 둘이 겹쳐 보여 어느 것을 고쳐야 하는지도 알 수 없다.
	int32 Index = INDEX_NONE;
	for (int32 i = 0; i < Cfg->CameraKeys.Num(); ++i)
	{
		if (FMath::IsNearlyEqual(Cfg->CameraKeys[i].Time, New.Time, 0.01f))
		{
			// 도달 방식과 블렌드 시간은 그 키가 쓰던 것을 지킨다 - 구도만 다시 잡는 것이다.
			New.Interp         = Cfg->CameraKeys[i].Interp;
			New.BlendTime      = Cfg->CameraKeys[i].BlendTime;
			Cfg->CameraKeys[i] = New;
			Index              = i;
			break;
		}
	}

	if (Index == INDEX_NONE)
	{
		// 시간 순으로 꽂는다. 배열 순서가 곧 재생 순서라 정렬이 깨지면 보간이 뒤로 간다.
		Index = 0;
		while (Index < Cfg->CameraKeys.Num() && Cfg->CameraKeys[Index].Time < New.Time)
		{
			++Index;
		}
		Cfg->CameraKeys.Insert(New, Index);
	}

	// 방금 만든 것을 「다룰 키」로 세운다. 다음 동작(보기·담기)이 그대로 이 키를 가리킨다.
	EditorKeyIndex = Index;
	Cfg->MarkPackageDirty();
	InvalidateKeyCameras();

	UE_LOG(LogVBFinisherAuthoring, Log, TEXT("처형 카메라 저작: %.2f초에 키 %d 를 화면 그대로 담았다 (총 %d개, FOV %.0f). "
	                                         "시각을 옮기고 화면을 다시 잡은 뒤 또 누르면 된다"),
	       New.Time, Index, Cfg->CameraKeys.Num(), New.FOV);
}

void UVBFinisherCameraAuthoringSession::SpawnKeyCameras()
{
	if (!EnsureConfigOrRelease())
	{
		return;
	}
	const UVBFinisherConfig* Cfg = Config.Get();

	AActor* Anchor = VBSelectedAnchor();
	if (!Anchor)
	{
		return;
	}
	if (Cfg->CameraKeys.IsEmpty())
	{
		UE_LOG(LogVBFinisherAuthoring, Warning, TEXT("처형 카메라 저작: 키가 하나도 없다. CameraKeys 에 항목을 먼저 추가해라"));
		return;
	}

	UWorld* World = Anchor->GetWorld();
	if (!World)
	{
		return;
	}

	DestroyKeyCameras();
	EnsureLifecycleHooks();

	// 기준을 지금 기억한다. 되돌려 쓸 때 선택에서 다시 찾으면 못 찾는다 -
	//   카메라를 옮기는 순간 선택된 것은 그 카메라다.
	KeyAnchor = Anchor;

	const FTransform Frame = VBAnchorFrame(*Anchor);
	KeyCameras.Reserve(Cfg->CameraKeys.Num());

	for (int32 Index = 0; Index < Cfg->CameraKeys.Num(); ++Index)
	{
		const FVBFinisherCameraKey& Key = Cfg->CameraKeys[Index];

		const FVector CamLocation = Frame.TransformPosition(Key.RelativeLocation);
		const FVector ToTarget    = Frame.TransformPosition(Key.LookAtOffset) - CamLocation;

		// Transient 로 세운다. 레벨에 저장되면 저작 도구가 콘텐츠를 더럽힌다.
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Params.Name = MakeUniqueObjectName(World->GetCurrentLevel(), ACameraActor::StaticClass(),
		                                   TEXT("VBFinisherKeyCamera"));

		ACameraActor* Cam = World->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(), CamLocation,
			ToTarget.IsNearlyZero() ? FRotator::ZeroRotator : ToTarget.Rotation(), Params);
		if (!Cam)
		{
			KeyCameras.Add(nullptr);
			continue;
		}

		Cam->SetActorLabel(FString::Printf(TEXT("[키 %d] %.2f초"), Index, Key.Time));
		if (UCameraComponent* CamComp = Cam->GetCameraComponent())
		{
			CamComp->SetFieldOfView(Key.FOV);
		}
		KeyCameras.Add(Cam);
	}

	// 움직임을 그 자리에서 받는다. 「저장」 단계를 두면 잊은 만큼 작업이 조용히 날아간다.
	if (GEngine && !ActorMovedHandle.IsValid())
	{
		ActorMovedHandle = GEngine->OnActorMoved().AddUObject(this, &UVBFinisherCameraAuthoringSession::OnActorMoved);
	}

	// 첫 카메라를 골라 둔다. 레벨 뷰포트가 고른 카메라의 그림을 구석에 띄우므로
	//   꺼내자마자 「이 키가 무엇을 찍고 있나」가 보인다.
	if (GEditor && KeyCameras.IsValidIndex(0) && KeyCameras[0].IsValid())
	{
		GEditor->SelectNone(/*bNoteSelectionChange=*/false, /*bDeselectBSPSurfs=*/true);
		GEditor->SelectActor(KeyCameras[0].Get(), /*bInSelected=*/true, /*bNotify=*/true);
	}

	UE_LOG(LogVBFinisherAuthoring, Log, TEXT("처형 카메라 저작: 키 %d개를 카메라로 꺼냈다. 붙잡고 옮기면 바로 반영된다 "
	                                         "(Ctrl+Shift+P 로 그 카메라를 직접 몰 수도 있다). 「미리보기 끄기」가 지운다"),
	       Cfg->CameraKeys.Num());
}

void UVBFinisherCameraAuthoringSession::OnActorMoved(AActor* Moved)
{
	// 되돌리기·다시하기가 카메라와 키를 함께 복원하는 중이면 키에 쓰지 않는다 - 쓰면 복원이 덮인다.
	//   엔진이 그때도 이 알림을 보내는지는 확인하지 않았다. 어느 쪽이든 여기서 막는다.
	if (bSyncingKeys || !Moved || GIsTransacting)
	{
		return;
	}

	// 우리가 세운 것만 본다. 레벨의 다른 액터가 움직였다고 키를 건드리면 안 된다.
	const bool bOurs = KeyCameras.ContainsByPredicate(
		[Moved](const TWeakObjectPtr<AActor>& Ptr) { return Ptr.Get() == Moved; });
	if (bOurs)
	{
		SyncKeysFromCameras();
	}
}

void UVBFinisherCameraAuthoringSession::SyncKeysFromCameras()
{
	if (!EnsureConfigOrRelease())
	{
		return;
	}
	UVBFinisherConfig* Cfg = Config.Get();

	if (KeyCameras.IsEmpty())
	{
		UE_LOG(LogVBFinisherAuthoring, Warning, TEXT("처형 카메라 저작: 꺼낸 카메라가 없다. 「키를 카메라로 꺼내기」를 먼저 눌러라"));
		return;
	}

	// 기준은 꺼낼 때 기억해 둔 것이다. 지금 선택을 보면 안 된다 - 카메라를 옮기는 순간
	//   선택된 것은 그 카메라라서, 선택으로 찾으면 실제 작업 중에는 언제나 못 찾는다.
	AActor* Anchor = KeyAnchor.Get();
	if (!Anchor)
	{
		// 기준이 사라지면 «조용히 아무것도 안 하는» 것이 제일 나쁘다 - 옮긴 것이 사라진 줄도 모른다.
		UE_LOG(LogVBFinisherAuthoring, Warning, TEXT("처형 카메라 저작: 기준 액터가 사라졌다(지워졌거나 레벨이 바뀌었다). "
		                                             "옮긴 것이 키에 안 들어갔다. 기준을 다시 고르고 「키를 카메라로 꺼내기」를 다시 눌러라"));
		return;
	}

	if (GIsTransacting)
	{
		return;
	}

	TGuardValue<bool> Guard(bSyncingKeys, true);
	const FTransform  Frame = VBAnchorFrame(*Anchor);

	// 카메라를 끌어 옮길 때는 엔진의 «액터 이동» 트랜잭션이 이미 열려 있고, 이 기록은 그 안에 합쳐진다.
	//   그래서 카메라 이동을 되돌리면 키도 같이 되돌아간다. 버튼으로 부르면 이것이 가장 바깥 기록이 된다.
	const FScopedTransaction Transaction(LOCTEXT("SyncKeysFromCameras", "처형 카메라: 카메라들을 키에 반영"));
	Cfg->Modify();

	int32 Written = 0;
	for (int32 Index = 0; Index < Cfg->CameraKeys.Num() && Index < KeyCameras.Num(); ++Index)
	{
		const ACameraActor* Cam = Cast<ACameraActor>(KeyCameras[Index].Get());
		if (!Cam)
		{
			continue;
		}

		FVBFinisherCameraKey& Key = Cfg->CameraKeys[Index];

		// 보는 «거리» 는 그 키가 쓰던 값을 유지한다. 매번 앵커까지로 다시 재면 카메라를 옮길 때마다
		//   보는 점이 따라 움직여서 사람이 잡은 구도가 무너진다. 거리가 0 인 새 키만 앵커까지로 떨어진다.
		const float StoredDistance = FVector::Dist(Key.LookAtOffset, Key.RelativeLocation);
		const FVector CamLocation  = Cam->GetActorLocation();
		const float   Distance     = StoredDistance > 1.0f
			                             ? StoredDistance
			                             : FMath::Max(1.0f, FVector::Dist(CamLocation, Anchor->GetActorLocation()));

		Key.RelativeLocation = Frame.InverseTransformPosition(CamLocation);
		Key.LookAtOffset     = Frame.InverseTransformPosition(
			CamLocation + Cam->GetActorRotation().Vector() * Distance);

		if (const UCameraComponent* CamComp = Cam->GetCameraComponent())
		{
			Key.FOV = CamComp->FieldOfView;
		}
		++Written;
	}

	Cfg->MarkPackageDirty();
	UE_LOG(LogVBFinisherAuthoring, Log, TEXT("처형 카메라 저작: 카메라 %d개를 키에 반영했다 (기준=%s)"),
	       Written, *Anchor->GetName());
}

void UVBFinisherCameraAuthoringSession::DestroyKeyCameras()
{
	if (GEngine && ActorMovedHandle.IsValid())
	{
		GEngine->OnActorMoved().Remove(ActorMovedHandle);
		ActorMovedHandle.Reset();
	}

	for (const TWeakObjectPtr<AActor>& Ptr : KeyCameras)
	{
		if (AActor* Cam = Ptr.Get())
		{
			Cam->Destroy();
		}
	}
	KeyCameras.Reset();
	KeyAnchor = nullptr;
}

void UVBFinisherCameraAuthoringSession::PreviewKey()
{
	if (!EnsureConfigOrRelease())
	{
		return;
	}
	const UVBFinisherConfig* Cfg = Config.Get();

	AActor*                 Anchor    = nullptr;
	UUnrealEditorSubsystem* EditorSub = nullptr;
	if (!VBGetAuthoringContext(*Cfg, EditorKeyIndex, Anchor, EditorSub))
	{
		return;
	}

	const FVBFinisherCameraKey& Key   = Cfg->CameraKeys[EditorKeyIndex];
	const FTransform            Frame = VBAnchorFrame(*Anchor);

	const FVector CamLocation = Frame.TransformPosition(Key.RelativeLocation);
	const FVector LookAt      = Frame.TransformPosition(Key.LookAtOffset);
	const FVector ToTarget    = LookAt - CamLocation;

	EditorSub->SetLevelViewportCameraInfo(
		CamLocation, ToTarget.IsNearlyZero() ? FRotator::ZeroRotator : ToTarget.Rotation());

	// FOV 도 맞춘다. 이게 없으면 화면이 실제보다 넓거나 좁게 보여서 「보기」가 거짓말을 하는 도구가 된다.
	bool bFOVApplied = false;
	if (FEditorViewportClient* Client = VBActiveViewportClient())
	{
		Client->ViewFOV = Key.FOV;
		Client->Invalidate();
		bFOVApplied = true;
	}

	UE_LOG(LogVBFinisherAuthoring, Log, TEXT("처형 카메라 저작: 키 %d 로 이동 (기준=%s, FOV %.0f%s)"),
	       EditorKeyIndex, *Anchor->GetName(), Key.FOV,
	       bFOVApplied ? TEXT("") : TEXT(" - 뷰포트를 못 찾아 FOV 는 못 맞췄다"));
}

void UVBFinisherCameraAuthoringSession::CaptureKeyFromViewport()
{
	if (!EnsureConfigOrRelease())
	{
		return;
	}
	UVBFinisherConfig* Cfg = Config.Get();

	AActor*                 Anchor    = nullptr;
	UUnrealEditorSubsystem* EditorSub = nullptr;
	if (!VBGetAuthoringContext(*Cfg, EditorKeyIndex, Anchor, EditorSub))
	{
		return;
	}

	FVector  CamLocation = FVector::ZeroVector;
	FRotator CamRotation = FRotator::ZeroRotator;
	EditorSub->GetLevelViewportCameraInfo(CamLocation, CamRotation);

	const FTransform Frame = VBAnchorFrame(*Anchor);

	// 볼 지점은 «보고 있는 방향으로 기준 액터까지의 거리만큼» 나간 점이다. 사람이 잡은 구도는 대개
	//   몸의 한 부분(가슴·머리)을 향하는데 원점은 발밑이라, 원점으로 담으면 담는 순간 그림이 내려간다.
	const float   Distance = FMath::Max(1.0f, FVector::Dist(CamLocation, Anchor->GetActorLocation()));
	const FVector LookAt   = CamLocation + CamRotation.Vector() * Distance;

	if (GIsTransacting)
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("CaptureKeyFromViewport", "처형 카메라: 이 키에 담기"));
	Cfg->Modify();

	FVBFinisherCameraKey& Key = Cfg->CameraKeys[EditorKeyIndex];
	Key.RelativeLocation = Frame.InverseTransformPosition(CamLocation);
	Key.LookAtOffset     = Frame.InverseTransformPosition(LookAt);

	// 화각도 구도의 일부다. 위치만 담고 FOV 를 놔두면 「담은 그림」과 「나오는 그림」이 갈린다.
	if (const FEditorViewportClient* Client = VBActiveViewportClient())
	{
		Key.FOV = Client->ViewFOV;
	}
	Cfg->MarkPackageDirty();

	UE_LOG(LogVBFinisherAuthoring, Log, TEXT("처형 카메라 저작: 키 %d 에 담았다 (기준=%s, 위치 %s, 볼 지점 %s, FOV %.0f)"),
	       EditorKeyIndex, *Anchor->GetName(),
	       *Key.RelativeLocation.ToCompactString(), *Key.LookAtOffset.ToCompactString(), Key.FOV);
}

AActor* UVBFinisherCameraAuthoringSession::EnsurePreviewActor(const AActor& Anchor, const FContextualAnimTrack& OtherTrack,
                                                              TWeakObjectPtr<AActor>& Slot, const TCHAR* Label,
                                                              bool bPreferAnchorMesh)
{
	if (AActor* Existing = Slot.Get())
	{
		return Existing;
	}

	const UVBFinisherConfig* Cfg = Config.Get();
	UWorld* World = Anchor.GetWorld();
	if (!Cfg || !World)
	{
		return nullptr;
	}

	// 메시를 어디서 구하나: 앵커 역할은 «그 액터가 실제로 쓰는 메시» 가 먼저다 - 그 자리에 선 것을
	//   대신 보여주는 것이므로 스켈레톤 기본 메시를 쓰면 화면에 다른 몸이 선다.
	//   상대역은 그 클립 스켈레톤의 미리보기 메시가 첫째다. 없으면 앵커의 메시를 빌린다
	//   (두 참가자가 같은 스켈레톤인 것이 이 갈래의 전제라 그림이 맞는다).
	USkeletalMesh* Mesh = nullptr;
	const USkeletalMeshComponent* AnchorMesh =
		const_cast<AActor&>(Anchor).FindComponentByClass<USkeletalMeshComponent>();

	if (bPreferAnchorMesh && AnchorMesh)
	{
		Mesh = AnchorMesh->GetSkeletalMeshAsset();
	}
	if (!Mesh)
	{
		if (const UAnimSequenceBase* Anim = OtherTrack.Animation)
		{
			if (USkeleton* Skeleton = Anim->GetSkeleton())
			{
				Mesh = Skeleton->GetPreviewMesh();
			}
		}
	}
	if (!Mesh && AnchorMesh)
	{
		Mesh = AnchorMesh->GetSkeletalMeshAsset();
	}
	if (!Mesh)
	{
		UE_LOG(LogVBFinisherAuthoring, Warning, TEXT("처형 카메라 저작: '%s' 의 메시를 못 구했다 - 스켈레톤 미리보기 메시도 없고 "
		                                             "선택한 액터에 스켈레탈 메시도 없다"), Label);
		return nullptr;
	}

	// Transient 로 스폰한다. 레벨에 저장되면 저작 도구가 콘텐츠를 더럽힌다 - 미리보기는 흔적을 남기면 안 된다.
	// RF_Transactional 은 뺀다(기본값에 들어 있다 - World.cpp::FActorSpawnParameters 생성자). 미리보기 대역은
	//   되돌리기 기록 밖이다(2026-10-03 user 결정). 시각 슬라이더가 끄는 동안 바깥 트랜잭션을 열어 두므로
	//   이 플래그가 남으면 첫 스크럽의 생성이 기록되고, 되돌리기가 세션 모르게 대역을 지우거나 되살린다.
	FActorSpawnParameters Params;
	Params.ObjectFlags = (Params.ObjectFlags | RF_Transient) & ~RF_Transactional;
	Params.Name = MakeUniqueObjectName(World->GetCurrentLevel(), AActor::StaticClass(),
	                                   TEXT("VBFinisherPreviewActor"));

	// 맨 AActor 에 «뿌리 + 그 밑의 메시» 로 짓는다. ASkeletalMeshActor 를 쓰면 안 된다 -
	//   그쪽은 메시가 곧 루트라(엔진 SkeletalMeshActor.cpp::ASkeletalMeshActor 생성자)
	//   자리를 잡는 순간 메시 보정(yaw -90, Z -88)이 통째로 지워져 상대역만 90도 틀어지고 88cm 떴다
	//   (2026-09-13 user: 「처형자가 올바르게 회전과 제대로 일단 안나오는 것 같은데」).
	// 캐릭터와 같은 층으로 지으면 두 참가자가 같은 계약을 따른다 - 자리는 액터가, 자세는 메시가 진다.
	AActor* Partner = World->SpawnActor<AActor>(AActor::StaticClass(), Anchor.GetActorTransform(), Params);
	if (!Partner)
	{
		return nullptr;
	}

	USceneComponent* Root = NewObject<USceneComponent>(Partner, TEXT("PreviewRoot"));
	Partner->SetRootComponent(Root);
	Root->RegisterComponent();

	USkeletalMeshComponent* PartnerMesh = NewObject<USkeletalMeshComponent>(Partner, TEXT("PreviewMesh"));
	PartnerMesh->SetupAttachment(Root);
	PartnerMesh->SetSkeletalMeshAsset(Mesh);
	PartnerMesh->RegisterComponent();

	// 메시를 액터 안에서 어디에 놓을지. 값은 씬 자산의 역할 정의가 든다 -
	//   런타임이 쓰는 것과 같은 출처여야 미리보기가 거짓말하지 않는다.
	if (const UContextualAnimRolesAsset* RolesAsset = Cfg->Scene ? Cfg->Scene->GetRolesAsset() : nullptr)
	{
		if (const FContextualAnimRoleDefinition* RoleDef = RolesAsset->FindRoleDefinitionByName(OtherTrack.Role))
		{
			PartnerMesh->SetRelativeTransform(RoleDef->MeshToComponent);
		}
	}

	Partner->SetActorLabel(FString::Printf(TEXT("[미리보기] %s"), Label));
	Slot = Partner;
	UE_LOG(LogVBFinisherAuthoring, Log, TEXT("처형 카메라 저작: '%s' 를 세웠다 (메시=%s). 「미리보기 끄기」로 지운다"),
	       Label, *Mesh->GetName());
	return Partner;
}

void UVBFinisherCameraAuthoringSession::PosePreviewActors()
{
	PosePreviewActorsInternal(/*bVerbose=*/true);
}

void UVBFinisherCameraAuthoringSession::PosePreviewActorsInternal(bool bVerbose)
{
	if (!EnsureConfigOrRelease())
	{
		return;
	}
	const UVBFinisherConfig* Cfg = Config.Get();

	// 슬라이더를 끄는 동안 매 틱 불린다. 그때 조용해야 한다 - 선택이 없다고 프레임마다 경고하면
	//   로그가 죽고, 그 로그가 죽으면 진짜 진단이 그 안에 묻힌다. 버튼으로 부를 때만 말한다.
	if (!Cfg->Scene)
	{
		UE_CLOG(bVerbose, LogVBFinisherAuthoring, Warning, TEXT("처형 카메라 저작: Scene 이 비어 있어 자세를 못 세운다"));
		return;
	}

	// 우리가 세운 미리보기 액터는 기준에서 뺀다. 그것을 기준으로 삼으면 자기 자신을 기준으로 자기를 놓는 꼴이
	//   되고 자리가 조금씩 흘러간다.
	TArray<AActor*> Selected;
	VBGetSelectedActors(Selected);
	Selected.RemoveAll([this](const AActor* A)
	{
		return A == PreviewSelf.Get() || A == PreviewPartner.Get()
		    || KeyCameras.ContainsByPredicate([A](const TWeakObjectPtr<AActor>& P) { return P.Get() == A; });
	});
	if (Selected.IsEmpty())
	{
		UE_CLOG(bVerbose, LogVBFinisherAuthoring, Warning, TEXT("처형 카메라 저작: 기준 액터를 선택하지 않았다. "
		                                                        "레벨에서 처형당할 액터를 클릭한 뒤 다시 눌러라 "
		                                                        "(미리보기로 세운 액터는 기준이 될 수 없다)"));
		return;
	}

	// 앵커 역할의 클립. 섹션·세트는 0 을 쓴다 - 두 역할 처형은 하나뿐이고,
	//   섹션이 여럿인 처형이 생기면 그때 섹션 번호도 저작 도구에 노출한다.
	const FContextualAnimTrack* AnchorTrack = Cfg->Scene->GetAnimTrack(0, 0, Cfg->CameraAnchorRole);
	if (!AnchorTrack || !AnchorTrack->Animation)
	{
		UE_CLOG(bVerbose, LogVBFinisherAuthoring, Warning, TEXT("처형 카메라 저작: 역할 '%s' 의 클립을 씬에서 못 찾았다"),
		        *Cfg->CameraAnchorRole.ToString());
		return;
	}

	EnsureLifecycleHooks();

	// 배치 액터는 «좌표 기준» 으로만 쓴다. 자세는 우리가 세운 액터가 잡는다 -
	//   배치 액터의 애니 모드를 바꾸면 PIE 가 그 상태를 복제해 게임에서 기준 포즈가 나온다.
	AActor* const Frame = Selected[0];
	AActor* SelfActor = EnsurePreviewActor(*Frame, *AnchorTrack, PreviewSelf,
	                                       *Cfg->CameraAnchorRole.ToString(), /*bPreferAnchorMesh=*/true);
	if (!SelfActor)
	{
		return;
	}
	SelfActor->SetActorTransform(Frame->GetActorTransform());

	// 겹쳐 보이면 구도를 못 잡는다. 저장되지 않는 임시 숨김이라 PIE 에도 안 따라간다.
	if (!Frame->IsTemporarilyHiddenInEditor())
	{
		Frame->SetIsTemporarilyHiddenInEditor(true);
		HiddenActors.AddUnique(Frame);
	}

	const float Time = FMath::Clamp(EditorPreviewTime, 0.0f, AnchorTrack->Animation->GetPlayLength());
	VBPoseActorAt(*SelfActor, AnchorTrack->Animation, Time);

	// 나머지 역할(대개 공격자)도 세운다. 간격까지 맞춰야 구도가 참이 된다.
	{
		FName OtherRole = NAME_None;
		for (const FName& Role : Cfg->Scene->GetRoles())
		{
			if (Role != Cfg->CameraAnchorRole)
			{
				OtherRole = Role;
				break;
			}
		}

		if (const FContextualAnimTrack* OtherTrack = OtherRole != NAME_None
			                                             ? Cfg->Scene->GetAnimTrack(0, 0, OtherRole)
			                                             : nullptr)
		{
			// 둘째를 직접 고르지 않았으면 우리가 세운다. 플레이어 캐릭터는 런타임에 소환되므로
			//   레벨에 놓고 고를 수가 없다 - 「둘 다 선택하라」는 지시가 성립하지 않는다.
			const bool bPartnerExisted = PreviewPartner.IsValid();
			AActor* Partner = EnsurePreviewActor(*Frame, *OtherTrack, PreviewPartner,
			                                     *OtherRole.ToString(), /*bPreferAnchorMesh=*/false);
			if (!Partner)
			{
				return;
			}

			// 자리는 «처음 한 번» 만 잡는다. 매번 덮어쓰면 사람이 손으로 옮겨도 다음 프레임에 되돌아간다.
			//   자동 계산은 씬 자산 값을 그대로 푼 것이라 그 값이 실제와 다르면 여기서도 다르다. 그럴 때
			//   사람이 눈으로 맞춘 자리가 곧 「씬 자산을 얼마나 고쳐야 하는가」의 답이 된다
			//   (2026-09-13 user: 「내가 수정할 수 있는건가?」). 다시 잡으려면 「상대역 자리 다시 잡기」.
			const bool bPlaceNow = !bPartnerExisted || bForcePlacePartner;
			bForcePlacePartner = false;
			if (bPlaceNow)
			{
				// 런타임(UVBFinisherComponent::StartFinisher)이 공격자를 놓는 것과 같은 함수를 쓴다.
				//   시각 0 을 쓴다 - 액터는 장면 내내 그 자리에 서 있고 몸이 움직이는 것은 포즈가 한다.
				//   정렬 트랙은 «워프 포인트 기준 경로» 라 시각을 넘겨 액터를 끌면 포즈의 이동과 겹친다(2026-09-13 검산).
				const FTransform RelToAnchor = Cfg->Scene->GetAlignmentTransformForRoleRelativeToOtherRole(
					0, 0, OtherRole, Cfg->CameraAnchorRole, 0.0f);

				if (!RelToAnchor.Equals(FTransform::Identity))
				{
					const FTransform AnchorFrame = VBAnchorFrame(*Frame);
					const FTransform Desired     = RelToAnchor * AnchorFrame;

					Partner->SetActorLocationAndRotation(
						Desired.GetLocation(), FRotator(0.0f, Desired.Rotator().Yaw, 0.0f));

					// 시킨 값과 실제 값을 나란히 찍는다. 상시 점검이다 - 둘이 다르면 자리를 잡은 뒤
					//   무언가가 또 옮겼다는 뜻이고, 그때는 여기가 아니라 그 «무언가» 를 봐야 한다.
					UE_CLOG(bVerbose, LogVBFinisherAuthoring, Log,
					        TEXT("처형 카메라 저작: 앵커 loc=%s yaw=%.1f | 시킴 loc=%s yaw=%.1f | 실제 loc=%s yaw=%.1f"),
					        *AnchorFrame.GetLocation().ToCompactString(), AnchorFrame.Rotator().Yaw,
					        *Desired.GetLocation().ToCompactString(), Desired.Rotator().Yaw,
					        *Partner->GetActorLocation().ToCompactString(), Partner->GetActorRotation().Yaw);
				}
			}

			VBPoseActorAt(*Partner, OtherTrack->Animation,
			              FMath::Clamp(EditorPreviewTime, 0.0f, OtherTrack->Animation->GetPlayLength()));
		}
	}

	// 뷰포트를 저작 중인 카메라에 태운다. 런타임과 같은 함수를 부른다 - 여기서 따로 계산하면
	//   「본 것」과 「나오는 것」이 갈리고, 갈린 것을 알아챌 방법이 없다.
	if (bEditorFollowCamera)
	{
		FVector RelLocation, RelLookAt;
		float   FOV = 90.0f;
		if (Cfg->EvaluateCameraAt(Time, RelLocation, RelLookAt, FOV))
		{
			const FTransform AnchorFrame = VBAnchorFrame(*Frame);
			const FVector    CamLocation = AnchorFrame.TransformPosition(RelLocation);
			const FVector    ToTarget    = AnchorFrame.TransformPosition(RelLookAt) - CamLocation;

			if (UUnrealEditorSubsystem* EditorSub =
			        GEditor ? GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>() : nullptr)
			{
				EditorSub->SetLevelViewportCameraInfo(
					CamLocation, ToTarget.IsNearlyZero() ? FRotator::ZeroRotator : ToTarget.Rotation());
			}
			if (FEditorViewportClient* Client = VBActiveViewportClient())
			{
				Client->ViewFOV = FOV;
				Client->Invalidate();
			}
		}
		else
		{
			// 조용히 안 따라가면 「따라가기가 고장났다」로 읽힌다. 말할 수밖에 없는 자리다.
			UE_CLOG(bVerbose, LogVBFinisherAuthoring, Warning, TEXT("처형 카메라 저작: 따라갈 카메라 키가 아직 없다. "
			                                                        "키를 하나 만들고 「이 키에 담기」로 채워라"));
		}
	}

	UE_CLOG(bVerbose, LogVBFinisherAuthoring, Log, TEXT("처형 카메라 저작: %.2f초 자세 (기준=%s, 클립 %.2f초)"),
	        Time, *Frame->GetName(), AnchorTrack->Animation->GetPlayLength());
}

void UVBFinisherCameraAuthoringSession::ReplacePartner()
{
	// 사람이 옮겨 놓은 자리를 버리고 저작 값으로 되돌린다. 「내가 옮긴 게 맞나」를 확인할 때 쓴다.
	bForcePlacePartner = true;
	PosePreviewActorsInternal(/*bVerbose=*/true);
}

int32 UVBFinisherCameraAuthoringSession::ReleasePreview()
{
	StopPlayTicker();      // 지운 액터를 타이머가 계속 찾지 않게 먼저 멈춘다
	DestroyKeyCameras();   // 꺼낸 카메라도 함께 지운다 - 레벨에 흔적을 남기지 않는다

	// 우리가 세운 것들은 지운다. 남겨 두면 다음에 레벨을 저장할 때 사람이 「이게 뭐지」를 묻는다.
	for (TWeakObjectPtr<AActor>* Slot : { &PreviewSelf, &PreviewPartner })
	{
		if (AActor* Actor = Slot->Get())
		{
			Actor->Destroy();
		}
		*Slot = nullptr;
	}

	// 배치 액터의 «애니 모드를 안 바꾸므로» 되돌릴 것도 없다 - 숨긴 것만 다시 보이게 하면 끝이다.
	int32 Shown = 0;
	for (const TWeakObjectPtr<AActor>& Ptr : HiddenActors)
	{
		if (AActor* Actor = Ptr.Get())
		{
			Actor->SetIsTemporarilyHiddenInEditor(false);
			++Shown;
		}
	}
	HiddenActors.Reset();

	EditorPreviewTime = 0.0f;
	ReleaseLifecycleHooks();
	return Shown;
}

void UVBFinisherCameraAuthoringSession::ClearPosePreview()
{
	const int32 Shown = ReleasePreview();
	UE_LOG(LogVBFinisherAuthoring, Log, TEXT("처형 카메라 저작: 미리보기 끔 (세운 것 지움, 숨겼던 %d개 다시 보임)"), Shown);
}

void UVBFinisherCameraAuthoringSession::EnsureLifecycleHooks()
{
	if (AssetClosedHandle.IsValid() || PieHandle.IsValid())
	{
		return;
	}

	if (UAssetEditorSubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr)
	{
		AssetClosedHandle =
			Sub->OnAssetClosedInEditor().AddUObject(this, &UVBFinisherCameraAuthoringSession::OnAssetClosed);
	}
	PieHandle = FEditorDelegates::PreBeginPIE.AddWeakLambda(this, [this](bool) { OnLevelGone(); });
	MapOpenedHandle = FEditorDelegates::OnMapOpened.AddWeakLambda(this, [this](const FString&, bool) { OnLevelGone(); });
	SelectionHandle = USelection::SelectObjectEvent.AddUObject(this, &UVBFinisherCameraAuthoringSession::OnObjectSelected);
	// 자산 변경 통지는 여기서 걸지 않는다 - Initialize 가 세션 수명 동안 건다.
}

void UVBFinisherCameraAuthoringSession::ReleaseLifecycleHooks()
{
	if (UAssetEditorSubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr)
	{
		Sub->OnAssetClosedInEditor().Remove(AssetClosedHandle);
	}
	AssetClosedHandle.Reset();

	FEditorDelegates::PreBeginPIE.Remove(PieHandle);
	PieHandle.Reset();
	FEditorDelegates::OnMapOpened.Remove(MapOpenedHandle);
	MapOpenedHandle.Reset();

	USelection::SelectObjectEvent.Remove(SelectionHandle);
	SelectionHandle.Reset();
}

void UVBFinisherCameraAuthoringSession::OnAssetClosed(UObject* Asset, IAssetEditorInstance* /*Instance*/)
{
	// 다른 자산의 창이 닫힌 것까지 우리 미리보기를 끄면 안 된다. 비교 대상은 세션이 아니라 자산이다.
	// 그 자산의 편집기가 하나라도 닫히면 거둔다(종전 그대로). «마지막 편집기만» 은 택하지 않았다 -
	//   엔진의 닫힘 알림이 목록에서 빼기 전에 나가서 판정이 까다롭고, 데이터 자산은 보통 편집기가 하나다.
	if (!Asset || Asset != Config.Get())
	{
		return;
	}
	UE_LOG(LogVBFinisherAuthoring, Log, TEXT("처형 카메라 저작: 창이 닫혀 미리보기를 초기화한다"));
	ReleasePreview();
}

void UVBFinisherCameraAuthoringSession::OnLevelGone()
{
	// PIE 로 들어가거나 레벨이 갈린다. 우리가 세운 액터는 곧 뜻을 잃고, 재생 타이머는
	//   없어진 액터를 매 프레임 찾는다. 둘 다 여기서 끝낸다. 타이머만 남은 상태도 센다.
	if (!HasPreviewResources())
	{
		return;
	}
	UE_LOG(LogVBFinisherAuthoring, Log, TEXT("처형 카메라 저작: 레벨이 바뀌어 미리보기를 초기화한다"));
	ReleasePreview();
}

void UVBFinisherCameraAuthoringSession::TogglePlay()
{
	if (!EnsureConfigOrRelease())
	{
		return;
	}

	if (PlayTickerHandle.IsValid())
	{
		StopPlayTicker();
		UE_LOG(LogVBFinisherAuthoring, Log, TEXT("처형 카메라 저작: 일시정지 (%.2f초). 이제 상대역을 손으로 옮겨도 유지된다"),
		       EditorPreviewTime);
		return;
	}

	// 타이머도 미리보기 자원이다. 붙이기 전에 창 닫기·PIE·맵 변경을 건다 - 종전에는 이것이 없어서
	//   재생부터 누르면 창을 닫아도 재생이 안 멈췄다(기존 결함 보완, 2026-10-02).
	EnsureLifecycleHooks();

	// 코어 티커에 붙는다. 에디터에서는 월드가 안 틱하므로 월드 타이머로는 시간이 안 흐른다.
	PlayTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UVBFinisherCameraAuthoringSession::PlayTick));
	bEditorPlaying = true;

	UE_LOG(LogVBFinisherAuthoring, Log, TEXT("처형 카메라 저작: 재생 시작 (반복). 슬라이더를 끌거나 이 버튼을 다시 누르면 멈춘다"));
}

void UVBFinisherCameraAuthoringSession::StopPlayTicker()
{
	if (PlayTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PlayTickerHandle);
		PlayTickerHandle.Reset();
	}
	bEditorPlaying = false;
}

bool UVBFinisherCameraAuthoringSession::PlayTick(float DeltaSeconds)
{
	const UVBFinisherConfig* Cfg = Config.Get();

	// 길이는 앵커 클립이 정한다. 자산이 사라졌거나 클립이 없으면 스스로 멈춘다 - 조용히 계속 도는 타이머는
	//   다음 사람이 못 찾는다. 스스로 멈출 때도 상태를 내린다. 안 내리면 패널은 「재생 중」이라 말하는데
	//   아무것도 안 도는, 사람이 못 푸는 상태가 된다. 자기 틱 안에서는 RemoveTicker 대신 false 를 돌려 뗀다.
	const FContextualAnimTrack* Track =
		(Cfg && Cfg->Scene) ? Cfg->Scene->GetAnimTrack(0, 0, Cfg->CameraAnchorRole) : nullptr;
	if (!Track || !Track->Animation)
	{
		PlayTickerHandle.Reset();
		bEditorPlaying = false;
		if (!Cfg)
		{
			ReleasePreview();   // 자산이 사라졌다 - 나머지 자원도 거둔다
		}
		else
		{
			UE_LOG(LogVBFinisherAuthoring, Warning, TEXT("처형 카메라 저작: 역할 '%s' 의 클립이 없어 재생을 멈춘다"),
			       *Cfg->CameraAnchorRole.ToString());
		}
		return false;
	}

	const float Length = Track->Animation->GetPlayLength();
	EditorPreviewTime += DeltaSeconds;
	if (EditorPreviewTime > Length)
	{
		EditorPreviewTime = 0.0f;   // 끝에 닿으면 처음으로. 돌려 놓고 그 위에서 구도를 잡는다
	}

	PosePreviewActorsInternal(/*bVerbose=*/false);
	return true;   // 계속 돈다
}

void UVBFinisherCameraAuthoringSession::OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	UVBFinisherConfig* Cfg = Config.Get();
	if (!Cfg || Object != Cfg)
	{
		return;
	}

	// 엔진이 키 배열을 바꾼 경우를 잡는다. 디테일 패널의 원소 메뉴와 배열 머리의 +·휴지통은 우리 코드를
	//   안 지나므로, 여기서 안 받으면 꺼내 둔 카메라가 조용히 어긋난 채로 남는다.
	// 값만 고친 것(시각 타이핑 등)에는 반응하지 않는다. 매번 세워 둔 카메라를 지우면 못 쓴다.
	bool bStructureChanged = false;
	if (Event.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UVBFinisherConfig, CameraKeys))
	{
		const EPropertyChangeType::Type Kind = Event.ChangeType;
		bStructureChanged = (Kind & (EPropertyChangeType::ArrayAdd | EPropertyChangeType::ArrayRemove
		                             | EPropertyChangeType::ArrayClear | EPropertyChangeType::ArrayMove
		                             | EPropertyChangeType::Duplicate)) != 0;
	}

	// 되돌리기·다시하기는 «속성 이름 없는» 통지로 와서 위 판정에 안 걸린다(UObject::PostEditUndo -> PostEditChange).
	//   그래서 개수로 한 번 더 본다 - 꺼낸 카메라 수와 키 수가 다르면 대응이 깨진 것이다.
	//   개수로 보므로 값만 되돌린 것은 카메라를 지우지 않는다(카메라 이동의 되돌리기와 부딪히지 않는다).
	//   순서만 바꾼 것의 되돌리기는 개수가 같아 못 잡는다 - 종전에도 못 잡던 자리다(기존 결함 보완, 2026-10-02).
	const bool bCountMismatch = !KeyCameras.IsEmpty() && KeyCameras.Num() != Cfg->CameraKeys.Num();

	const int32 ClampedIndex = FMath::Clamp(EditorKeyIndex, 0, FMath::Max(0, Cfg->CameraKeys.Num() - 1));
	if (bStructureChanged || bCountMismatch)
	{
		// 배열 변경이 연 기록에 세션도 넣는다 - 그래야 그 변경의 Ctrl+Z 가 번호도 옛 값으로 되돌린다.
		//   되돌리기·다시하기 도중에는 기록을 만들지 않는다(그때는 아래 범위 확인만 한다).
		if (ClampedIndex != EditorKeyIndex && !GIsTransacting)
		{
			Modify();
		}
		EditorKeyIndex = ClampedIndex;
		InvalidateKeyCameras();
	}
	else if (ClampedIndex != EditorKeyIndex)
	{
		// 되돌리기가 키를 줄였는데 번호는 그대로인 경우의 방어. 꺼낸 카메라가 없으면 개수 비교가 안 걸려
		//   여기까지 온다. 기록을 만들지 않고 범위 안으로만 넣는다.
		EditorKeyIndex = ClampedIndex;
	}
}

void UVBFinisherCameraAuthoringSession::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// 시각 슬라이더를 «끄는 동안» 자세가 따라오게 한다. 끝난 뒤에만 갱신하면 스크럽이 아니라
	//   찍어 보기가 된다. 이 칸은 디테일 패널에 외부 객체 칸으로 끼워져 있고, 슬라이더의 Interactive 변경도
	//   이 객체로 온다(엔진 PropertyNode 의 객체 통지 - 2026-10-02 codex-claude_02 확인).
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UVBFinisherCameraAuthoringSession, EditorPreviewTime))
	{
		// 손으로 끌면 재생을 멈춘다. 재생이 이어지면 놓는 즉시 다시 흘러가 버려서 「그 시각을 본다」가 성립하지 않는다.
		StopPlayTicker();

		// 「다룰 키」가 지금 보고 있는 시각을 따라온다(2026-09-13 user: 「그 키 지우기가 어떻게 선택하는건데」).
		SyncKeyIndexToPreviewTime();
		PosePreviewActorsInternal(/*bVerbose=*/false);
	}
}

void UVBFinisherCameraAuthoringSession::BeginDestroy()
{
	// 타이머가 죽은 객체를 부르지 않게 한다. 서브시스템이 내려가거나 GC 가 거두면 여기로 온다.
	ReleasePreview();
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
	PropertyChangedHandle.Reset();
	Super::BeginDestroy();
}

#undef LOCTEXT_NAMESPACE
