// Copyright JTW Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RMSTypes.h"

class URMSMotionSubsystem;
class UWorld;

#if !UE_BUILD_SHIPPING

/** 调试设施。整体在 Shipping 下不编译。 */
namespace RMSDebug
{
	/** 记录一条非终态事件（开始、失败等） */
	void RecordEvent(int32 HandleId, FName DisplayName, const TCHAR* EventName, const UWorld* World);

	/** 记录终态事件，包含结束原因 */
	void RecordEnd(int32 HandleId, FName DisplayName, ERMSMotionEndReason Reason, const UWorld* World);

	/** 每帧由子系统调用：按 RMS.Debug 级别绘制轨迹与屏幕面板 */
	void Draw(const URMSMotionSubsystem* Subsystem);

	/** 当前生效的调试级别 */
	int32 GetLevel();
}

#endif
