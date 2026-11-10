// Copyright JTW Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RMSTypes.h"
#include "RMSMotionLibrary.generated.h"

class ACharacter;
class AActor;
class UAnimSequenceBase;

/** 公共入口。对任意 ACharacter 可用，角色上不需要挂任何组件。 */
UCLASS()
class ROOTMOTIONSOURCEPLUS_API URMSMotionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 开始一次运动。联网时需要在服务器与预测客户端两端以相同参数各调用一次。
	 * 参数非法或被 Block 策略拒绝时返回无效 Handle 并写 Warning。
	 */
	UFUNCTION(BlueprintCallable, Category = "RMS")
	static FRMSMotionHandle PlayMotion(ACharacter* Character, const FRMSMotionRequest& Request);

	/** 停止一个运动（结束原因 Interrupted）。返回是否停止了活动运动。 */
	UFUNCTION(BlueprintCallable, Category = "RMS", meta = (WorldContext = "WorldContextObject"))
	static bool StopMotion(const UObject* WorldContextObject, FRMSMotionHandle Handle);

	/**
	 * 停止该角色身上的全部运动（结束原因 Interrupted），返回停止的数量。
	 * 无限时长的力（Duration 小于 0）不会自己结束，用它或 StopMotion 清理。
	 */
	UFUNCTION(BlueprintCallable, Category = "RMS")
	static int32 StopAllMotions(ACharacter* Character);

	UFUNCTION(BlueprintPure, Category = "RMS", meta = (WorldContext = "WorldContextObject"))
	static bool IsMotionActive(const UObject* WorldContextObject, FRMSMotionHandle Handle);

	UFUNCTION(BlueprintPure, Category = "RMS")
	static int32 GetActiveMotionCount(const ACharacter* Character);

	// ---------------------------------------------------------------- 位置语义换算
	//
	// 全部世界坐标的语义都是「角色胶囊中心」，等于 ACharacter::GetActorLocation()。
	// 脚底与该点相差一个胶囊半高。以下函数用于在两种语义之间显式换算。

	/** 脚底点 → 胶囊中心。射线打地面得到的命中点必须经此转换后才能作为 StartLocation / TargetLocation。 */
	UFUNCTION(BlueprintPure, Category = "RMS|Location")
	static FVector FootToCenter(const ACharacter* Character, FVector FootLocation);

	/** 胶囊中心 → 脚底点。直接传任意世界点时，用角色半高做一次换算。 */
	UFUNCTION(BlueprintPure, Category = "RMS|Location")
	static FVector CenterToFoot(const ACharacter* Character, FVector CenterLocation);

	/** 角色当前的脚底点。等价于 GetActorLocation() 减去胶囊半高。 */
	UFUNCTION(BlueprintPure, Category = "RMS|Location")
	static FVector GetFootLocation(const ACharacter* Character);

	/** 角色当前的胶囊中心。与 ACharacter::GetActorLocation() 等价，提供它是为了让语义在蓝图里自解释。 */
	UFUNCTION(BlueprintPure, Category = "RMS|Location")
	static FVector GetCenterLocation(const ACharacter* Character);

	// ---------------------------------------------------------------- 预设与便捷构造
	//
	// 全部以显式 StartLocation 作为起点，不接受角色指针：联网时两端各读本机位置会把起点固化成
	// 不同值，导致 Matches 判定不一致、服务器校正失效。起点必须由一端确定后显式传入（通常取
	// Get Center Location）。返回值可直接改字段，再交给 PlayMotion。

	/**
	 * 预设：冲刺。从 StartLocation 沿方向位移，先快后慢，被挡就停（Incremental），结束时速度清零。
	 * @param RotationMode 默认 FaceMotion（朝运动方向）。想锁朝向冲刺（如横向闪避仍面朝敌人）就传
	 *                     FaceTarget 并把 TargetRotation 设为敌人方向；传 None 则完全不改朝向。
	 */
	UFUNCTION(BlueprintPure, Category = "RMS|Presets")
	static FRMSMotionRequest MakeDashRequest(FVector StartLocation, FVector Direction, float Distance = 600.0f, float Duration = 0.3f,
		ERMSRotationMode RotationMode = ERMSRotationMode::FaceMotion, FRotator TargetRotation = FRotator::ZeroRotator, FName InstanceName = NAME_None);

	/** 预设：击退。沿方向位移并抬升 Lift，被挡就停，结束时速度清零，不改变朝向。 */
	UFUNCTION(BlueprintPure, Category = "RMS|Presets")
	static FRMSMotionRequest MakeKnockbackRequest(FVector StartLocation, FVector Direction, float Distance = 400.0f, float Duration = 0.4f, float Lift = 60.0f, FName InstanceName = NAME_None);

	/** 预设：跳跃。抛物线精确落到 Target，ApexHeight 为顶点高度，强制到达，朝向运动方向，结束时速度清零（保证落点精确）。 */
	UFUNCTION(BlueprintPure, Category = "RMS|Presets")
	static FRMSMotionRequest MakeJumpRequest(FVector StartLocation, FVector Target, float ApexHeight = 150.0f, float Duration = 0.7f,
		ERMSRotationMode RotationMode = ERMSRotationMode::FaceMotion, FRotator TargetRotation = FRotator::ZeroRotator, FName InstanceName = NAME_None);

	/** 便捷构造：从 StartLocation 沿方向位移指定距离，位移参数由调用方给全。 */
	UFUNCTION(BlueprintPure, Category = "RMS")
	static FRMSMotionRequest MakeDirectionalRequest(FVector StartLocation, FVector Direction, float Distance, const FRMSMotionParams& Params, FName InstanceName = NAME_None);

	/**
	 * 便捷构造：动画根位移对齐到终点（单段 Warping，窗口覆盖整段动画）。
	 * 这是最常用的动画位移用法：动画自己播、位移由插件接管、最后精确落到 Target。
	 * Rate 要与你播放动画的速率一致，否则位移和动作对不上。
	 *
	 * @param RotationMode  朝向怎么处理（动画运动用这个，不看 Params.RotationMode）：
	 *                      None = 不动朝向；FaceMotion = 段末朝 StartLocation→Target 的水平方向（最常用）；
	 *                      FaceTarget = 段末朝 TargetRotation。
	 * @param TargetRotation 仅 FaceTarget 使用，只取 Yaw。
	 * @param RotationCurve 转向完成时机曲线（横轴段内时间 0～1，纵轴旋转进度 0～1）。留空 = 整段匀速转。
	 *                      例：0～0.6 为 0、0.6～1 升到 1 = 前段不转、末段集中转完（挥砍发力）。
	 *                      无论曲线如何，段结束时朝向都精确等于目标。
	 */
	UFUNCTION(BlueprintPure, Category = "RMS|Presets")
	static FRMSMotionRequest MakeWarpedAnimationRequest(FVector StartLocation, UAnimSequenceBase* Animation, FVector Target,
		float Rate = 1.0f, float AnimStart = 0.0f, float AnimEnd = -1.0f,
		ERMSRotationMode RotationMode = ERMSRotationMode::FaceMotion, FRotator TargetRotation = FRotator::ZeroRotator,
		UCurveFloat* RotationCurve = nullptr, FName InstanceName = NAME_None);

	/**
	 * 便捷构造：只回放动画根位移，不做 Warping。
	 * 先用它验证“这个动画的根位移能被读到”；能读到再加 Warping。
	 */
	UFUNCTION(BlueprintPure, Category = "RMS|Presets")
	static FRMSMotionRequest MakeAnimationRequest(FVector StartLocation, UAnimSequenceBase* Animation,
		float Rate = 1.0f, float AnimStart = 0.0f, float AnimEnd = -1.0f, bool bIgnoreZ = false, FName InstanceName = NAME_None);

	/**
	 * 便捷构造：折线或样条。
	 * Point 数组是路径点（不含起点），终点自动取最后一个点。
	 */
	UFUNCTION(BlueprintPure, Category = "RMS|Presets")
	static FRMSMotionRequest MakePathRequest(FVector StartLocation, const TArray<FVector>& PathPoints,
		ERMSPathKind Kind = ERMSPathKind::Spline, float Duration = 0.6f, FName InstanceName = NAME_None);

	/**
	 * 便捷构造：直线加目标，时长、缓动与朝向由调用方给。
	 * @param RotationMode 默认 None（不改朝向）。FaceMotion = 朝运动方向，FaceTarget = 转到 TargetRotation。
	 */
	UFUNCTION(BlueprintPure, Category = "RMS|Presets")
	static FRMSMotionRequest MakeStraightRequest(FVector StartLocation, FVector Target, float Duration = 0.3f,
		ERMSEase Ease = ERMSEase::EaseOut, ERMSReachMode ReachMode = ERMSReachMode::Guaranteed,
		ERMSRotationMode RotationMode = ERMSRotationMode::None, FRotator TargetRotation = FRotator::ZeroRotator, FName InstanceName = NAME_None);

	/** 便捷构造：抛物线到目标，ApexHeight 为顶点高度。朝向参数同 MakeStraightRequest。 */
	UFUNCTION(BlueprintPure, Category = "RMS|Presets")
	static FRMSMotionRequest MakeParabolaRequest(FVector StartLocation, FVector Target, float ApexHeight = 150.0f,
		float Duration = 0.6f, ERMSReachMode ReachMode = ERMSReachMode::Guaranteed,
		ERMSRotationMode RotationMode = ERMSRotationMode::None, FRotator TargetRotation = FRotator::ZeroRotator, FName InstanceName = NAME_None);

	/** 便捷构造：跟随目标 Actor。Duration 小于 0 表示无限（需 StopMotion 结束）。 */
	UFUNCTION(BlueprintPure, Category = "RMS|Presets")
	static FRMSMotionRequest MakeFollowRequest(FVector StartLocation, AActor* TargetActor, FVector WorldOffset,
		float Duration = 0.6f, ERMSFollowLostPolicy LostPolicy = ERMSFollowLostPolicy::GoToLastKnown,
		ERMSReachMode ReachMode = ERMSReachMode::Guaranteed,
		ERMSRotationMode RotationMode = ERMSRotationMode::None, FRotator TargetRotation = FRotator::ZeroRotator, FName InstanceName = NAME_None);

	/** 便捷构造：恒力。Duration 小于 0 表示无限（需 StopMotion / StopAllMotions 结束）。 */
	UFUNCTION(BlueprintPure, Category = "RMS|Presets")
	static FRMSMotionRequest MakeConstantForceRequest(FVector StartLocation, FVector Direction,
		float Strength = 500.0f, float Duration = -1.0f, FName InstanceName = NAME_None);

	/** 便捷构造：牵引。Duration 小于 0 表示无限（需 StopMotion / StopAllMotions 结束）。 */
	UFUNCTION(BlueprintPure, Category = "RMS|Presets")
	static FRMSMotionRequest MakePullRequest(FVector StartLocation, AActor* TargetActor,
		float Strength = 500.0f, float StopDistance = 150.0f, bool bIgnoreZ = true, float Duration = -1.0f, FName InstanceName = NAME_None);

	// ---------------------------------------------------------------- 旋转

	/**
	 * 给任意 Request 设置朝向。便捷节点已经有旋转参数；这个函数用于在构造之后追加或修改，
	 * 例如先 Make Focus 拿到一个 Request，再统一改成锁朝向。
	 *
	 * 位移运动改的是 Params.RotationMode；动画运动（Anim.Animation 非空且已有 Warp 段）会改为
	 * 给每一段设置朝向，这样动画位移与朝向的开关保持一致，不会出现"参数设了但动画不转"。
	 *
	 * @param RotationCurve 转向完成时机曲线，留空 = 整段匀速转。
	 */
	UFUNCTION(BlueprintPure, Category = "RMS|Rotation")
	static FRMSMotionRequest WithRotation(const FRMSMotionRequest& Request, ERMSRotationMode RotationMode,
		FRotator TargetRotation = FRotator::ZeroRotator, UCurveFloat* RotationCurve = nullptr);

	/**
	 * 给动画 Request 的每个 Warp 段设置朝向。
	 * 与 WithRotation 的区别：这个明确只作用于动画段，不碰 Params，且能覆盖多段 Warping 的每一段。
	 */
	UFUNCTION(BlueprintPure, Category = "RMS|Rotation")
	static FRMSMotionRequest WithWarpRotation(const FRMSMotionRequest& Request, ERMSRotationMode RotationMode,
		FRotator TargetRotation = FRotator::ZeroRotator, UCurveFloat* RotationCurve = nullptr);
};
