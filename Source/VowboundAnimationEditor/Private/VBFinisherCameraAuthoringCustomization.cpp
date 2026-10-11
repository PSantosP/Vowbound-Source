// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "VBFinisherCameraAuthoringCustomization.h"

#include "VBFinisherCameraAuthoringSession.h"
#include "VBFinisherCameraAuthoringSubsystem.h"
#include "Data/VBFinisherConfig.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "VBFinisherCameraAuthoring"

namespace
{
	using FSessionAction = void (UVBFinisherCameraAuthoringSession::*)();

	struct FButtonSpec
	{
		FText          Label;
		FText          ToolTip;
		FSessionAction Action;
	};

	// 버튼 한 줄. 엔진의 CallInEditor 줄과 같은 모양이다(PropertyCustomizationHelpers 의 SWrapBox, 간격 5·3).
	// 버튼은 되돌리기 기록을 열지 않는다(2026-10-03 user 결정 - 미리보기는 기록 밖). 자산을 바꾸는 세 동작
	//   (키 추가·담기·반영)은 세션 함수 안에서 스스로 기록을 연다. 미리보기 버튼까지 감싸면 «미리보기 끄기» 뒤
	//   Ctrl+Z 가 세션 모르게 대역을 되살려 주인 없는 액터가 레벨에 남는다(.sw/structure-refactor/design.md 7.4).
	TSharedRef<SWidget> MakeButtonStrip(const TWeakObjectPtr<UVBFinisherCameraAuthoringSession>& WeakSession,
	                                    TConstArrayView<FButtonSpec> Buttons)
	{
		TSharedRef<SWrapBox> Strip = SNew(SWrapBox).PreferredSize(2000).UseAllottedSize(true);
		for (const FButtonSpec& Spec : Buttons)
		{
			Strip->AddSlot()
			.Padding(0.0f, 0.0f, 5.0f, 3.0f)
			[
				SNew(SButton)
				.Text(Spec.Label)
				.ToolTipText(Spec.ToolTip)
				.OnClicked_Lambda([WeakSession, Action = Spec.Action]()
				{
					// 세션이나 자산이 사라졌으면 아무것도 안 한다. 패널이 낡은 채 남아 있는 경우다.
					UVBFinisherCameraAuthoringSession* Session = WeakSession.Get();
					if (Session && Session->IsAlive())
					{
						(Session->*Action)();
					}
					return FReply::Handled();
				})
			];
		}
		return Strip;
	}
}

TSharedRef<IDetailCustomization> FVBFinisherCameraAuthoringCustomization::MakeInstance()
{
	return MakeShared<FVBFinisherCameraAuthoringCustomization>();
}

void FVBFinisherCameraAuthoringCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	// 카테고리 이름과 표시 이름은 종전(CallInEditor 시절) 그대로다 - user 가 익힌 자리다.
	//   칸과 버튼을 같은 카테고리 빌더에 넣으므로, CallInEditor 시절의 «같은 칸이 패널에 두 번» 사고
	//   (CLAUDE.md 코딩 표준의 CallInEditor 예외)는 구조상 생기지 않는다.
	IDetailCategoryBuilder& Main = DetailBuilder.EditCategory(FName(TEXT("Vowbound 처형 카메라 저작")));
	IDetailCategoryBuilder& Aux  = DetailBuilder.EditCategory(FName(TEXT("Vowbound 처형 카메라 저작 (보조)")));

	// 저작 도구는 자산 하나에 대해서만 뜻이 있다 - 키 번호·미리보기 시각이 자산마다 다르다.
	//   클래스 기본값(CDO) 패널에는 세션을 만들지 않는다 - 거기서 저작할 처형은 없다.
	UVBFinisherConfig* Config = Objects.Num() == 1 ? Cast<UVBFinisherConfig>(Objects[0].Get()) : nullptr;
	if (Config && Config->HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}
	UVBFinisherCameraAuthoringSubsystem* Subsystem =
		GEditor ? GEditor->GetEditorSubsystem<UVBFinisherCameraAuthoringSubsystem>() : nullptr;
	UVBFinisherCameraAuthoringSession* Session =
		(Config && Subsystem) ? Subsystem->FindOrCreateSession(Config) : nullptr;
	if (!Session)
	{
		// 조용히 비워 두면 «도구가 사라졌다» 로 읽힌다. 왜 없는지 말한다 - 여럿을 골랐을 때와 그 밖을 가른다.
		const FText Why = Objects.Num() != 1
			? LOCTEXT("OneAssetOnlyText", "처형 설정 자산을 하나만 골랐을 때 저작 도구가 나온다.")
			: LOCTEXT("NoSessionText", "저작 도구를 띄우지 못했다(에디터 서브시스템이 없다). 자산을 다시 열어 보라.");
		Main.AddCustomRow(LOCTEXT("OneAssetOnly", "처형 카메라 저작"))
		[
			SNew(STextBlock)
			.Text(Why)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];
		return;
	}

	const TWeakObjectPtr<UVBFinisherCameraAuthoringSession> WeakSession(Session);

	// 버튼 순서는 종전과 같다. 엔진은 CallInEditor 버튼을 카테고리 안에서 함수 이름 순으로 놓았다
	//   (PropertyCustomizationHelpers 의 GetCallInEditorFunctionsForClassInternal 정렬).
	const FButtonSpec MainButtons[] = {
		{ LOCTEXT("AddKey", "지금 화면으로 키 추가"),
		  LOCTEXT("AddKeyTip", "지금 뷰포트 화면 그대로 키를 하나 만든다. 시각은 「미리보기 시각」. 같은 시각의 키는 덮어쓴다."),
		  &UVBFinisherCameraAuthoringSession::AddKeyFromViewport },
		{ LOCTEXT("Pose", "선택한 액터를 이 시각 자세로"),
		  LOCTEXT("PoseTip", "선택한 액터 자리에 대역을 세워 이 시각의 처형 자세를 잡는다. 상대역도 저작된 자리에 선다."),
		  &UVBFinisherCameraAuthoringSession::PosePreviewActors },
		{ LOCTEXT("PreviewKey", "이 키 보기 (선택한 액터 기준)"),
		  LOCTEXT("PreviewKeyTip", "「다룰 키」가 잡는 그림을 지금 뷰포트로 보여준다. 처형당할 액터를 먼저 선택한다."),
		  &UVBFinisherCameraAuthoringSession::PreviewKey },
		{ LOCTEXT("TogglePlay", "재생 / 일시정지"),
		  LOCTEXT("TogglePlayTip", "미리보기를 재생하거나 멈춘다. 끝에 닿으면 처음으로 돌아간다. 시각을 끌면 멈춘다."),
		  &UVBFinisherCameraAuthoringSession::TogglePlay },
	};
	const FButtonSpec AuxButtons[] = {
		{ LOCTEXT("Capture", "이 키에 담기 (선택한 액터 기준)"),
		  LOCTEXT("CaptureTip", "지금 뷰포트 구도를 「다룰 키」에 담는다. 시각과 블렌드는 안 건드린다."),
		  &UVBFinisherCameraAuthoringSession::CaptureKeyFromViewport },
		{ LOCTEXT("Clear", "미리보기 끄기"),
		  LOCTEXT("ClearTip", "세운 대역·꺼낸 카메라를 지우고 숨긴 배치 액터를 다시 보이게 한다."),
		  &UVBFinisherCameraAuthoringSession::ClearPosePreview },
		{ LOCTEXT("Replace", "상대역 자리 다시 잡기"),
		  LOCTEXT("ReplaceTip", "손으로 옮긴 상대역을 씬 자산에 저작된 자리로 되돌린다."),
		  &UVBFinisherCameraAuthoringSession::ReplacePartner },
		{ LOCTEXT("Spawn", "키를 카메라로 꺼내기"),
		  LOCTEXT("SpawnTip", "키마다 레벨에 카메라를 하나씩 세운다. 붙잡고 옮기면 그 키에 바로 반영된다."),
		  &UVBFinisherCameraAuthoringSession::SpawnKeyCameras },
		{ LOCTEXT("Sync", "카메라들을 키에 반영"),
		  LOCTEXT("SyncTip", "꺼낸 카메라를 지금 상태 그대로 키에 반영한다. FOV 처럼 이동이 아닌 변경용이다."),
		  &UVBFinisherCameraAuthoringSession::SyncKeysFromCameras },
	};

	// 버튼 줄이 칸보다 위에 온다 - 종전에도 그랬다(엔진이 CallInEditor 줄을 커스터마이즈된 줄로 먼저 넣었다).
	Main.AddCustomRow(LOCTEXT("MainButtons", "처형 카메라 저작"))
	[
		MakeButtonStrip(WeakSession, MainButtons)
	];

	// 세션의 칸을 이 자산의 카테고리에 끼운다. 고치면 세션 자신의 PostEditChangeProperty 가 받는다.
	const TArray<UObject*> SessionObjects{ Session };
	Main.AddExternalObjectProperty(SessionObjects, GET_MEMBER_NAME_CHECKED(UVBFinisherCameraAuthoringSession, EditorKeyIndex));
	Main.AddExternalObjectProperty(SessionObjects, GET_MEMBER_NAME_CHECKED(UVBFinisherCameraAuthoringSession, EditorPreviewTime));
	Main.AddExternalObjectProperty(SessionObjects, GET_MEMBER_NAME_CHECKED(UVBFinisherCameraAuthoringSession, bEditorPlaying));
	Main.AddExternalObjectProperty(SessionObjects, GET_MEMBER_NAME_CHECKED(UVBFinisherCameraAuthoringSession, bEditorFollowCamera));

	Aux.AddCustomRow(LOCTEXT("AuxButtons", "처형 카메라 저작 (보조)"))
	[
		MakeButtonStrip(WeakSession, AuxButtons)
	];
}

#undef LOCTEXT_NAMESPACE
