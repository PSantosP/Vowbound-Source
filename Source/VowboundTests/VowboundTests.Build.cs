// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

using UnrealBuildTool;

public class VowboundTests : ModuleRules
{
	public VowboundTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

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

			// 검증 대상 게임 모듈 (VOWBOUND_API export 확인 완료, E20)
			"Vowbound",
		});
	}
}
