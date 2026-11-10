// Copyright JTW Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/CancellableAsyncAction.h"
#include "RMSTypes.h"
#include "RMSAsyncAction_PlayMotion.generated.h"

class ACharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRMSPlayMotionDelegate, FRMSMotionHandle, Handle, ERMSMotionEndReason, Reason);

/** 蓝图异步节点：播放运动并等待结束。只调用 Library 并订阅子系统事件。 */
UCLASS()
class ROOTMOTIONSOURCEPLUS_API URMSAsyncAction_PlayMotion : public UCancellableAsyncAction
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "RMS", meta = (BlueprintInternalUseOnly = "true", DisplayName = "Play Motion And Wait"))
	static URMSAsyncAction_PlayMotion* PlayMotionAndWait(ACharacter* Character, FRMSMotionRequest Request);

	/** 正常播放完成 */
	UPROPERTY(BlueprintAssignable, Category = "RMS")
	FRMSPlayMotionDelegate OnCompleted;

	/** 被停止、被替换或角色销毁 */
	UPROPERTY(BlueprintAssignable, Category = "RMS")
	FRMSPlayMotionDelegate OnInterrupted;

	/** 请求非法或被 Block 策略拒绝 */
	UPROPERTY(BlueprintAssignable, Category = "RMS")
	FRMSPlayMotionDelegate OnFailed;

	virtual void Activate() override;
	virtual void Cancel() override;

private:
	void HandleEnded(FRMSMotionHandle EndedHandle, ERMSMotionEndReason Reason);
	void Unbind();

	TWeakObjectPtr<ACharacter> TargetCharacter;
	FRMSMotionRequest PendingRequest;
	FRMSMotionHandle ActiveHandle;
	FDelegateHandle EndedBinding;
};
