// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

using UnrealBuildTool;

public class VowboundTests : ModuleRules
{
	public VowboundTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.Add("AnimationWarpingRuntime");

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",

			// AFunctionalTest — Developer 모듈. 본 모듈이 UncookedOnly(cooked/Shipping 제외)라
			// Developer 모듈 링크 제약과 충돌하지 않음 (Design_2026-06-05_DEBT-004 E10/E16).
			"FunctionalTesting",

			// 진짜 키 경로 주입 — StartContinuousInputInjectionForAction (런타임 API, E15)
			"EnhancedInput",

			// FGameplayTag (VBGameplayTags 단위 테스트)
			"GameplayTags",

			// FGameplayAttribute (AVBPlayerState::SavedAttributes 세이브 키 계약 테스트)
			"GameplayAbilities",

			// 런타임 모듈에서 옮겨 온 시험이 직접 쓰는 모듈 (2026-10-02, 구조 정리 1a).
			// Vowbound 가 공개 의존으로 이미 끌고 오지만, 시험이 직접 include·생성하는 모듈은 여기 적는다 -
			// 그래야 Vowbound 의 의존이 바뀌어도 시험이 조용히 깨지지 않는다.
			"GameplayCameras", // UGameplayCameraComponent, UCameraAsset (VBEnvironmentCameraPolicyTests)
			"InputCore",       // EKeys (VBTargetLockPolicyTests)

			// 검증 대상 게임 모듈 (VOWBOUND_API export 확인 완료, E20)
			"Vowbound",
		});
	}
}
