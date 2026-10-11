using UnrealBuildTool;

public class VowboundAnimationEditor : ModuleRules
{
	public VowboundAnimationEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "AnimGraph", "AnimGraphRuntime", "Vowbound" });
		PrivateDependencyModuleNames.Add("GameplayCameras"); // Camera asset/director authoring and readback.
		PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "BlueprintGraph", "Chooser", "PoseSearch", "AssetTools", "AssetRegistry", "ContextualAnimation", "MotionWarping", "StateTreeEditorModule", "StateTreeModule", "SmartObjectsModule", "GameplayInteractionsModule", "ProxyTable", "EnhancedInput", "InputCore", "GameplayTags", "PropertyBindingUtils" });

		// 처형 카메라 저작 도구(2026-10-02 구조 정리 1b - 런타임 자산의 CallInEditor 에서 옮겨 왔다).
		//   PropertyEditor·Slate·SlateCore = 디테일 패널 커스터마이제이션, EditorSubsystem = 저작 세션의 주인.
		PrivateDependencyModuleNames.AddRange(new[] { "PropertyEditor", "Slate", "SlateCore", "EditorSubsystem" });
	}
}
