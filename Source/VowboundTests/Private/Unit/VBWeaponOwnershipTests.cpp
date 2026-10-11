#include "Misc/AutomationTest.h"
#include "Character/VBWeaponStateComponent.h"

// 가진 무기 판단. 설계 .sw/prologue-opening 3.1·10절.
// 꺼내는 관문(ServerSummonWeapon)과 공격 막기(UVBGA_MeleeAttackBase::CanActivateAbility)는 이 두 함수의 답을 그대로 쓴다.
// 실제 발도 연기·복제는 PIE 가 본다.

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponOwnershipTest, "Vowbound.Unit.Weapon.Ownership",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWeaponOwnershipTest::RunTest(const FString& Parameters)
{
	using namespace VBWeaponOwnership;
	const EVBWeaponType None = EVBWeaponType::None;
	const EVBWeaponType Katana = EVBWeaponType::Katana;
	const EVBWeaponType BigSword = EVBWeaponType::BigSword;
	const EVBWeaponType Fighter = EVBWeaponType::Fighter;
	const TArray<EVBWeaponType> Known = {Katana, BigSword, Fighter};

	// 재발도: 기존 순서(선택 -> 마지막 -> 기본)를 «가진 것» 안에서 지킨다.
	TestEqual(TEXT("nothing owned -> None (attack and summon stay blocked)"), PickResummon(None, None, Katana, {}), None);
	TestEqual(TEXT("all owned keeps the old order: default"), PickResummon(None, None, Katana, Known), Katana);
	TestEqual(TEXT("last used wins over default"), PickResummon(None, BigSword, Katana, Known), BigSword);
	TestEqual(TEXT("current wins over last used"), PickResummon(Fighter, BigSword, Katana, Known), Fighter);
	TestEqual(TEXT("default not owned -> first owned"), PickResummon(None, None, Katana, {BigSword}), BigSword);
	TestEqual(TEXT("last used no longer owned is skipped"), PickResummon(None, Fighter, Katana, {Katana}), Katana);

	// 주기: 결과 셋(설계 10절). 획득 효과는 NewlyOwned 일 때만 건다.
	TArray<EVBWeaponType> Owned;
	TestEqual(TEXT("first pickup is new"), Grant(Owned, Known, Katana), EVBWeaponGrantResult::NewlyOwned);
	TestEqual(TEXT("second pickup of the same weapon is already owned"), Grant(Owned, Known, Katana), EVBWeaponGrantResult::AlreadyOwned);
	TestEqual(TEXT("already owned does not duplicate"), Owned.Num(), 1);
	TestEqual(TEXT("None fails"), Grant(Owned, Known, None), EVBWeaponGrantResult::Failed);
	TestEqual(TEXT("unknown weapon (no summon data) fails"), Grant(Owned, Known, EVBWeaponType::Magic), EVBWeaponGrantResult::Failed);
	TestEqual(TEXT("failed grant leaves the list alone"), Owned.Num(), 1);
	return true;
}
#endif
