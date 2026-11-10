// Copyright JTW Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RMSTypes.h"
#include "RMSAnimNotifyState_Warp.generated.h"

class UAnimSequenceBase;

/**
 * 动画 Warping 窗口标记。放在动画上，标出“在这段时间内把动画根位移对齐到某个目标”。
 * 本类没有运行时行为：窗口在 PlayMotion 时由 URMSWarpLibrary 读取动画上的标记生成，
 * 不依赖通知在播放时被触发，因此不受动画被打断、跳帧或网络延迟影响。
 */
UCLASS(meta = (DisplayName = "RMS Warp Window"))
class ROOTMOTIONSOURCEPLUS_API URMSAnimNotifyState_Warp : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	/** 窗口目标名。PlayMotion 时通过名字到位置的映射绑定到具体世界位置 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	FName TargetName = NAME_None;

	virtual FString GetNotifyName_Implementation() const override;
};

/** 窗口收集与目标绑定。 */
UCLASS()
class ROOTMOTIONSOURCEPLUS_API URMSWarpLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 从动画读取全部 RMS Warp 窗口，并按目标名映射到世界位置，生成 Warp 参数。
	 * 找不到映射的窗口会被忽略并写 Warning（该时间段按原样播放动画）。
	 * @param TargetNames 目标名数组，与 TargetLocations 一一对应。
	 */
	UFUNCTION(BlueprintCallable, Category = "RMS")
	static FRMSWarpParams BuildWarpFromAnimation(const UAnimSequenceBase* Animation, const TArray<FName>& TargetNames, const TArray<FVector>& TargetLocations);
};
