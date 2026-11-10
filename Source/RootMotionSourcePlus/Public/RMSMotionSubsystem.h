// Copyright JTW Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RMSTypes.h"
#include "RMSMotionSubsystem.generated.h"

class UCharacterMovementComponent;
struct FRMSMotionSource;

/** 某个运动的结束通知（C++ 原生委托）。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FRMSMotionEndedNative, FRMSMotionHandle, ERMSMotionEndReason);

/** 登记表中的一项活动运动。 */
struct FRMSActiveMotion
{
	FRMSMotionHandle Handle;
	TWeakObjectPtr<UCharacterMovementComponent> Movement;
	TSharedPtr<FRMSMotionSource> Source;
	uint16 RootMotionSourceId = 0;
	FName InstanceName = NAME_None;
	/** 请求被发出时的世界时间，仅调试使用 */
	double StartWorldTime = 0.0;
	/** 调试显示名；取实例名，为空时显示 (unnamed) */
	FName DisplayName = NAME_None;
};

/**
 * 世界级登记表：保存 Handle 到活动运动的映射、判断结束并分发事件。
 * 不参与复制；联网依赖两端各自调用 PlayMotion。
 */
UCLASS()
class ROOTMOTIONSOURCEPLUS_API URMSMotionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static URMSMotionSubsystem* Get(const UWorld* World);

	/** 登记一个已加入 CMC 的 Source，返回 Handle。 */
	FRMSMotionHandle Register(UCharacterMovementComponent* Movement, const TSharedPtr<FRMSMotionSource>& Source, uint16 RootMotionSourceId, FName DisplayName);

	/** 主动结束一个运动；返回是否真的结束了某个活动运动。 */
	bool Stop(FRMSMotionHandle Handle, ERMSMotionEndReason Reason);

	bool IsActive(FRMSMotionHandle Handle) const;
	int32 CountActiveFor(const UCharacterMovementComponent* Movement) const;

	/** 结束某个 CMC 上的全部活动运动，返回结束的数量。无限时长的力只能这样或 Stop 单个结束。 */
	int32 StopAllFor(const UCharacterMovementComponent* Movement, ERMSMotionEndReason Reason);

	/** 查找同一 CMC 上同名的活动运动（InstanceName 为 None 时不查找）。 */
	FRMSMotionHandle FindActiveByName(const UCharacterMovementComponent* Movement, FName InstanceName) const;

	/** 分配一个在本 World 内唯一的 Handle。 */
	FRMSMotionHandle AllocateHandle();

	/** 分发一次“未真正开始”的失败，供 Library 在请求非法时通知订阅方。 */
	void NotifyFailed(FRMSMotionHandle Handle);

	FRMSMotionEndedNative& OnMotionEnded() { return MotionEnded; }

	const TMap<int32, FRMSActiveMotion>& GetActiveMotions() const { return Active; }

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return Active.Num() > 0; }
	virtual TStatId GetStatId() const override;

	virtual void Deinitialize() override;

private:
	void Finish(int32 HandleId, ERMSMotionEndReason Reason);

	TMap<int32, FRMSActiveMotion> Active;
	FRMSMotionEndedNative MotionEnded;
	int32 NextHandleId = 1;
};
