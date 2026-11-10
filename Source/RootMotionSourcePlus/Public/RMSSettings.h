// Copyright JTW Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RMSTypes.h"
#include "RMSSettings.generated.h"

/** Project Settings -> Plugins -> JTW Root Motion Source Plus */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "JTW Root Motion Source Plus"))
class ROOTMOTIONSOURCEPLUS_API URMSSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	URMSSettings();

	static const URMSSettings* Get();

	/** 默认调试级别：0 关，1 轨迹，2 轨迹加面板，3 全量。控制台 RMS.Debug 可运行时覆盖 */
	UPROPERTY(Config, EditAnywhere, Category = "RMS", meta = (ClampMin = "0", ClampMax = "3"))
	int32 DefaultDebugLevel = 0;

	/** 轨迹绘制的采样段数 */
	UPROPERTY(Config, EditAnywhere, Category = "RMS", meta = (ClampMin = "4", ClampMax = "64"))
	int32 TrajectorySamples = 16;

	/** 每个运动保留的事件历史条数 */
	UPROPERTY(Config, EditAnywhere, Category = "RMS", meta = (ClampMin = "16", ClampMax = "1024"))
	int32 EventHistoryCapacity = 128;

	/**
	 * 锁定轴：位移在对应轴上不产生速度（横版项目常设 Y）。
	 * 两端使用同一份项目设置，因此不参与网络序列化。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "RMS")
	bool bLockX = false;

	UPROPERTY(Config, EditAnywhere, Category = "RMS")
	bool bLockY = false;

	UPROPERTY(Config, EditAnywhere, Category = "RMS")
	bool bLockZ = false;

	FVector GetLockedAxes() const { return FVector(bLockX ? 1.0 : 0.0, bLockY ? 1.0 : 0.0, bLockZ ? 1.0 : 0.0); }

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};
