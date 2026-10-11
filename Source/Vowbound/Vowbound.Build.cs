// Vowbound: The Scalpel's Oath
// Copyright (c) 2025. All Rights Reserved.

using UnrealBuildTool;

public class Vowbound : ModuleRules
{
	public Vowbound(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		                                     {
			                                     "Core",
			                                     "CoreUObject",
			                                     "Engine",
			                                     "InputCore",
			                                     "EnhancedInput",
			                                     "StateTreeModule",
			                                     "GameplayStateTreeModule",
			                                     "UMG",

			                                     // FMargin/FSlateBrush 등 Slate 타입을 UPROPERTY 로 노출하려면 직접 의존이 필요하다.
			                                     // UMG 를 통한 전이 의존만으로는 리플렉션 심볼(Z_Construct_UScriptStruct_FMargin)이 링크되지 않는다.
			                                     "SlateCore",
			                                     // FAnchors(캔버스 앵커)는 SlateCore 가 아니라 Slate 에 있다. 터치 조작 배치(FVBTouchControlSlot)가
			                                     //   값으로 들어 리플렉션 심볼 Z_Construct_UScriptStruct_FAnchors 가 필요하다(2026-09-24 링크 오류 LNK2019).
			                                     "Slate",

			                                     // GAS dependencies
			                                     "GameplayAbilities",
			                                     "GameplayTasks",
			                                     "GameplayTags",

			                                     // FFastArraySerializer(인벤토리 복제, .sw/narrative 10.2). 전이 의존만으로는 링크가 보장되지 않는다(위 Slate 사례).
			                                     "NetCore",

			                                     // Niagara
			                                     "Niagara",

			                                     // 휘두름 궤적 띠(UVBSwingTrailComponent) - 엔진 플러그인, 기본 켜짐. .sw/weapon-trace 18절
			                                     "ProceduralMeshComponent",
			                                     
			                                     // Debugger
			                                     "GameplayDebugger",
			                                     
			                                     // AI + Navigation
			                                     "AIModule",
			                                     "NavigationSystem",
			                                     
			                                     "CommonInput",
			                                     // 화면 층(UCommonActivatableWidget·스택·버튼) - .sw/ui-foundation. 플러그인은 이미 켜져 있었다.
			                                     "CommonUI",

			                                     // UVBGameFlowSettings 의 부모 UDeveloperSettings. Project Settings 노출 + ini 영속화.
			                                     "DeveloperSettings",
			                                     
												 "MotionWarping",

												 "MotionTrajectory",

												 // GASP OffsetRootBone — EOffsetRootBoneMode enum + AnimGraph 노드.
												 "AnimationWarpingRuntime",
			                                     "AnimGraphRuntime",   // 적 ABP 노드 함수(USequencePlayerLibrary/UBlendSpacePlayerLibrary) - 2026-09-06,

												 // GASP Motion Matching — UPoseSearchDatabase, EPoseSearchInterruptMode, MotionMatchingAnimNodeLibrary.
												 "PoseSearch",

												 // GASP Chooser-based PSD selection — UChooserTable, UChooserFunctionLibrary::EvaluateChooserMulti.
												 // Update_MotionMatching 함수가 매 frame chooser 평가 후 결과 PSD array 를 SetDatabasesToSearch 에 전달.
												 "Chooser",

												 // 처형·피니셔의 두 몸 맞물림 — UContextualAnimSceneAsset(역할·클립·상대 정렬·섹션).
												 // 실험 단계 플러그인이라 접점을 UVBFinisherComponent 한 곳으로 모은다(DEC-019).
												 "ContextualAnimation",
		                                     });

		PrivateDependencyModuleNames.AddRange(new string[] { });
		PrivateDependencyModuleNames.Add("AnimationCore"); // 사다리 현재 포즈의 TwoBoneIK 풀이.
		PublicDependencyModuleNames.Add("PhysicsControl"); // GASP powered ragdoll configuration and runtime.
		PublicDependencyModuleNames.Add("GameplayCameras"); // Imported GASP camera asset/director/rigs.
		PublicDependencyModuleNames.Add("BlendStack"); // Native GASP animation-node decisions.
		PublicDependencyModuleNames.Add("VBCinematicPlayback"); // Game-facing rules consume the reusable playback core.
		PublicDependencyModuleNames.AddRange(new[] { "SmartObjectsModule", "GameplayInteractionsModule", "ProxyTable" });

		// UnrealEd 의존은 없다(2026-10-02 구조 정리 1b). 종전에는 처형 카메라 저작 도구가 UVBFinisherConfig 안에
		//   있어서 에디터 빌드에서만 UnrealEd 를 걸었다. 그 도구는 VowboundAnimationEditor 모듈로 옮겼다 -
		//   런타임 모듈은 에디터 모듈을 모른다. 에디터 전용 코드가 다시 필요하면 이 모듈이 아니라 그쪽에 둔다.
	}
}
