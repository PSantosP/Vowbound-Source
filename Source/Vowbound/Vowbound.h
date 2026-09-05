// VowBound: The Oath of the Scalpel
// Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Vowbound 로그 카테고리
DECLARE_LOG_CATEGORY_EXTERN(LogVB, Log, All);

// Shipping 빌드에서 완전 제거되는 디버그 로그 매크로
#if !UE_BUILD_SHIPPING
#define VB_LOG(Verbosity, Format, ...) \
	UE_LOG(LogVB, Verbosity, TEXT(Format), ##__VA_ARGS__);
#define VB_CLOG(Condition, Verbosity, Format, ...) \
	UE_CLOG(Condition, LogVB, Verbosity, TEXT(Format), ##__VA_ARGS__);
#else
#define VB_LOG(...)
#define VB_CLOG(...)
#endif
