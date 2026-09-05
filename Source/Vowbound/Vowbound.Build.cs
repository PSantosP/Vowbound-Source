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

			                                     // GAS dependencies
			                                     "GameplayAbilities",
			                                     "GameplayTasks",
			                                     "GameplayTags",

			                                     // Niagara
			                                     "Niagara",
			                                     
			                                     // Debugger
			                                     "GameplayDebugger",
			                                     
			                                     // AI + Navigation
			                                     "AIModule",
			                                     "NavigationSystem",
			                                     
			                                     "CommonInput",

			                                     // UVBGameFlowSettings 의 부모 UDeveloperSettings. Project Settings 노출 + ini 영속화.
			                                     "DeveloperSettings",
			                                     
												 "MotionWarping",

												 "MotionTrajectory",

												 // GASP OffsetRootBone — EOffsetRootBoneMode enum + AnimGraph 노드.
												 "AnimationWarpingRuntime",

												 // GASP Motion Matching — UPoseSearchDatabase, EPoseSearchInterruptMode, MotionMatchingAnimNodeLibrary.
												 "PoseSearch",

												 // GASP Chooser-based PSD selection — UChooserTable, UChooserFunctionLibrary::EvaluateChooserMulti.
												 // Update_MotionMatching 함수가 매 frame chooser 평가 후 결과 PSD array 를 SetDatabasesToSearch 에 전달.
												 "Chooser",
		                                     });

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}