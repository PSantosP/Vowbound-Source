// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Shop/VBEconomySettings.h"

#include "Inventory/Shop/VBEconomyConfig.h"
#include "Vowbound/Vowbound.h"

const UVBEconomyConfig* UVBEconomySettings::LoadEconomy(bool bWarnIfMissing)
{
	const UVBEconomySettings* Settings = GetDefault<UVBEconomySettings>();
	const UVBEconomyConfig* Economy = Settings ? Settings->Economy.LoadSynchronous() : nullptr;
	if (!Economy && bWarnIfMissing)
	{
		VB_LOG(Warning, "경제 설정 자산이 없다(Project Settings > Vowbound Economy > Economy = %s) - 거래를 판정할 수 없다",
		       Settings ? *Settings->Economy.ToString() : TEXT("설정 없음"));
	}
	return Economy;
}
