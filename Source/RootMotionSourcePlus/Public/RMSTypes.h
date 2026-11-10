// Copyright JTW Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/RootMotionSource.h"
#include "RMSTypes.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRMS, Log, All);

class UCurveFloat;
class UCurveVector;
class UAnimSequenceBase;
class AActor;

/** 一次运动的结束原因。 */
UENUM(BlueprintType)
enum class ERMSMotionEndReason : uint8
{
	/** 正常播放到时长结束 */
	Completed,
	/** 被 StopMotion、角色销毁或外部移除打断 */
	Interrupted,
	/** 被同名新运动替换 */
	Replaced,
	/** 请求非法或被 Block 策略拒绝，未真正开始 */
	Failed
};

/** 同名运动已存在时的处理方式。 */
UENUM(BlueprintType)
enum class ERMSConflictPolicy : uint8
{
	/** 允许叠加，不检查同名 */
	Allow,
	/** 先结束同名旧运动（原因 Replaced），再开始新运动 */
	Replace,
	/** 同名存在时拒绝新请求（原因 Failed） */
	Block
};

/** 运动结束时对角色速度的处理。 */
UENUM(BlueprintType)
enum class ERMSFinishVelocity : uint8
{
	/** 保持运动最后一帧给出的速度 */
	Keep,
	/** 速度清零 */
	Zero,
	/** 水平速度限制到指定值 */
	Clamp
};

/** 运动期间的朝向策略。只对位移运动（直线、折线、样条、抛物线、跟随）生效；动画运动用 Warp 段的朝向开关。 */
UENUM(BlueprintType)
enum class ERMSRotationMode : uint8
{
	/** 不修改朝向 */
	None,
	/** 朝向运动的水平方向（朝哪走朝哪） */
	FaceMotion,
	/**
	 * 转向固定目标：运动过程中转到 Params.TargetRotation。
	 * 与 FaceMotion 的区别是方向由你指定，不必等于运动方向——例如横向闪避时仍面朝敌人。
	 */
	FaceTarget
};

/** 内置缓动，覆盖绝大多数需求；需要特殊形状时再提供自定义曲线。 */
UENUM(BlueprintType)
enum class ERMSEase : uint8
{
	/** 匀速 */
	Linear,
	/** 先慢后快 */
	EaseIn,
	/** 先快后慢 */
	EaseOut,
	/** 两头慢中间快 */
	EaseInOut
};

/**
 * 到达模式：运动被阻挡（墙体、碰撞）之后如何处理。
 * 与引擎的 ERootMotionAccumulateMode（与角色已有速度如何合成）是两件不同的事。
 */
UENUM(BlueprintType)
enum class ERMSReachMode : uint8
{
	/** 强制到达：每帧补到“此刻应在的位置”，被挡后会找补，最终一定到达目标点 */
	Guaranteed,
	/** 逐帧递进：每帧只走这一帧应走的位移，被挡的部分直接丢弃，不保证到达目标点 */
	Incremental
};

/** 运动的路径种类。 */
UENUM(BlueprintType)
enum class ERMSPathKind : uint8
{
	/** 起点到终点的直线 */
	Straight,
	/** 沿多个路径点的折线 */
	Polyline,
	/** 经过全部路径点的平滑样条（Catmull-Rom） */
	Spline,
	/** 起点到终点的抛物线，高度由 ArcHeight 给出 */
	Parabola
};

/**
 * 动画根位移回放的参数。动画资产经 PackageMap 传输；对端解析不到时该 Source 被放弃。
 * 动画时间 = AnimStart + 已播放真实时间 * Rate，到 AnimEnd 结束。
 */
USTRUCT(BlueprintType)
struct ROOTMOTIONSOURCEPLUS_API FRMSAnimationParams
{
	GENERATED_BODY()

	/** 要回放根位移的动画（Sequence 或 Montage）；为空则本运动不是动画运动 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	TObjectPtr<UAnimSequenceBase> Animation = nullptr;

	/** 动画起点（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS", meta = (ClampMin = "0.0"))
	float AnimStart = 0.0f;

	/** 动画终点（秒）；小于等于 0 表示动画末尾 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	float AnimEnd = -1.0f;

	/** 播放速率，低于下限会被钳制 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS", meta = (ClampMin = "0.01"))
	float Rate = 1.0f;

	/** 忽略根位移的 Z 分量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	bool bIgnoreZ = false;

	bool IsValid() const { return Animation != nullptr; }
};

/** 跟随目标丢失（被销毁或失效）时的行为。 */
UENUM(BlueprintType)
enum class ERMSFollowLostPolicy : uint8
{
	/** 继续飞向最后一次已知位置，时间到即结束 */
	GoToLastKnown,
	/** 立即结束（原因 Interrupted） */
	End
};

/**
 * 跟随 Actor 的动态目标。目标位置每帧由服务器与客户端各自读取目标 Actor 的位置，
 * 并经 UpdateStateFrom 由服务器向客户端校正。Actor 引用走网络对象引用，不使用裸指针。
 */
USTRUCT(BlueprintType)
struct ROOTMOTIONSOURCEPLUS_API FRMSFollowParams
{
	GENERATED_BODY()

	/** 要跟随的目标 Actor；为空则本运动不是跟随运动 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	TObjectPtr<AActor> TargetActor = nullptr;

	/** 相对目标 Actor 的世界空间偏移（例如停在目标前方 100） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	FVector WorldOffset = FVector::ZeroVector;

	/** 目标丢失时的行为 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	ERMSFollowLostPolicy LostPolicy = ERMSFollowLostPolicy::GoToLastKnown;

	bool IsValid() const { return TargetActor != nullptr; }
};

/** Warping 的一个目标段：在动画区间 [WindowStart, WindowEnd] 内，把剩余动画位移对齐到 Target。 */
USTRUCT(BlueprintType)
struct ROOTMOTIONSOURCEPLUS_API FRMSWarpSegment
{
	GENERATED_BODY()

	/** 本段起点（动画时间，秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	float WindowStart = 0.0f;

	/** 本段终点（动画时间，秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	float WindowEnd = 0.0f;

	/** 本段结束时角色应处于的世界位置（**胶囊中心**） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	FVector Target = FVector::ZeroVector;

	/** 为 false 时本段只播放原始动画根位移，不做 Warping（用于窗口之间的间隙） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	bool bWarp = true;

	/** 为 true 时本段同时做朝向 Warping：本段结束时角色朝向 TargetRotation（只取 Yaw） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	bool bWarpRotation = false;

	/** 本段结束时应达到的世界朝向（仅 bWarpRotation 为 true 时使用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	FRotator TargetRotation = FRotator::ZeroRotator;

	/**
	 * 本段内的旋转完成时机曲线（横轴 段内时间进度 0～1，纵轴 旋转进度 0～1）。留空 = 线性。
	 * 例：0～0.6 为 0、0.6～1 升到 1 = 前段不转、末段集中转完（挥砍发力）。
	 * 无论曲线如何，段结束时旋转都精确等于 TargetRotation。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS|Advanced")
	TObjectPtr<UCurveFloat> RotationCurve = nullptr;
};

/** 动画 Warping 参数。依附在动画回放（Anim.Animation 非空）之上。 */
USTRUCT(BlueprintType)
struct ROOTMOTIONSOURCEPLUS_API FRMSWarpParams
{
	GENERATED_BODY()

	/** 目标段，按时间顺序排列、互不重叠。为空表示不做 Warping，只回放动画 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	TArray<FRMSWarpSegment> Segments;

	/** 缩放下限。目标太近时动画位移被压缩，不会低于此值 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS", meta = (ClampMin = "0.01"))
	float MinScale = 0.1f;

	/** 缩放上限。目标太远时动画位移被放大，不会超过此值（超过则到不了目标） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS", meta = (ClampMin = "0.01"))
	float MaxScale = 4.0f;

	bool IsValid() const { return Segments.Num() > 0; }
};

/** 力的种类。 */
UENUM(BlueprintType)
enum class ERMSForceKind : uint8
{
	/** 不是力：按位移到目标处理（默认） */
	None,
	/** 恒力：沿固定方向持续施加速度 */
	Constant,
	/** 牵引：朝目标 Actor 持续施加吸引速度 */
	Pull
};

/**
 * 力的参数。力没有终点，所以 ReachMode 对力类无意义，会被忽略。
 * 速度单位是厘米/秒，与帧率无关。Duration 小于 0 表示无限时长，只能 StopMotion 结束。
 */
USTRUCT(BlueprintType)
struct ROOTMOTIONSOURCEPLUS_API FRMSForceParams
{
	GENERATED_BODY()

	/** 力的种类；None 表示本请求不是力 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	ERMSForceKind Kind = ERMSForceKind::None;

	/** 恒力的方向（世界空间，会被归一）。牵引不使用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	FVector Direction = FVector::ForwardVector;

	/** 强度：恒力的速度，或牵引在距离曲线为 1 时的速度（厘米/秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS", meta = (ClampMin = "0.0"))
	float Strength = 500.0f;

	/** 牵引：距目标小于该距离时速度为 0（厘米） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS", meta = (ClampMin = "0.0"))
	float StopDistance = 100.0f;

	/** 牵引：只取水平方向。地面角色通常为真，空中牵引需要关掉 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	bool bIgnoreZ = true;

	/** 牵引：距离（厘米）到强度倍率的曲线；留空表示恒定 1 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS|Advanced")
	TObjectPtr<UCurveFloat> StrengthOverDistance = nullptr;

	/** 牵引的目标 Actor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	TObjectPtr<AActor> PullTarget = nullptr;

	bool IsForce() const { return Kind != ERMSForceKind::None; }
};

/** 运动句柄，只在创建它的 World 内有效；0 为无效。 */
USTRUCT(BlueprintType)
struct ROOTMOTIONSOURCEPLUS_API FRMSMotionHandle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RMS")
	int32 Id = 0;

	bool IsValid() const { return Id != 0; }
	bool operator==(const FRMSMotionHandle& Other) const { return Id == Other.Id; }
	friend uint32 GetTypeHash(const FRMSMotionHandle& Handle) { return ::GetTypeHash(Handle.Id); }
};

/**
 * 运动的全部“怎么动”参数，纯数据。
 * 序列化时直接发送数值；只有设置了自定义曲线才会经 PackageMap 传输曲线对象引用。
 */
USTRUCT(BlueprintType)
struct ROOTMOTIONSOURCEPLUS_API FRMSMotionParams
{
	GENERATED_BODY()

	/** 时长（秒），低于下限会被钳到下限 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	float Duration = 0.5f;

	/** 到达模式 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	ERMSReachMode ReachMode = ERMSReachMode::Guaranteed;

	/** 位移缓动；设置了自定义曲线时忽略 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	ERMSEase Ease = ERMSEase::Linear;

	/** 抬升高度（厘米）：沿时间做半个正弦弧线，起终点处为 0；0 表示无 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	float ArcHeight = 0.0f;

	/**
	 * 朝向策略。FaceMotion 与 FaceTarget 都是“整段运动匀速转到目标”，不是每帧瞬时对齐。
	 * 注意：引擎只对 AccumulateMode = Override 应用旋转；Additive 时旋转会被静默忽略。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	ERMSRotationMode RotationMode = ERMSRotationMode::None;

	/**
	 * 转向目标：RotationMode = FaceTarget 时角色在运动过程中转到这个世界朝向（只取 Yaw）。
	 * 例：横向闪避时仍面朝敌人，就把这里填敌人的方向。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS", meta = (EditCondition = "RotationMode == ERMSRotationMode::FaceTarget"))
	FRotator TargetRotation = FRotator::ZeroRotator;

	/**
	 * 转向完成时机曲线（横轴运动时间进度 0～1，纵轴旋转进度 0～1）。留空 = 线性（整段匀速转）。
	 * 仅 RotationMode 为 FaceMotion / FaceTarget 时使用。
	 * 例：0～0.6 为 0、0.6～1 升到 1 = 前段不转、末段集中转完。
	 * 无论曲线如何，运动结束时朝向都精确等于目标。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS|Advanced", meta = (EditCondition = "RotationMode != ERMSRotationMode::None"))
	TObjectPtr<UCurveFloat> RotationCurve = nullptr;

	/** 结束速度策略 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	ERMSFinishVelocity FinishVelocity = ERMSFinishVelocity::Zero;

	/** Clamp 策略的水平速度上限（厘米/秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS", meta = (EditCondition = "FinishVelocity == ERMSFinishVelocity::Clamp"))
	float FinishClampSpeed = 0.0f;

	/** 与角色已有速度的合成方式（引擎枚举：Override 覆盖，Additive 叠加） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	ERootMotionAccumulateMode AccumulateMode = ERootMotionAccumulateMode::Override;

	/** 路径种类 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	ERMSPathKind PathKind = ERMSPathKind::Straight;

	/**
	 * 可选：路径偏移曲线（横轴位移进度 0～1，纵轴为偏移向量，单位厘米）。留空表示无偏移。
	 * 偏移在“朝向空间”中给出：X 为沿起点到终点的水平前进方向，Y 为其右侧，Z 为世界上方。
	 * 偏移在进度 0 与 1 处应为 0，否则起终点会被偏移，插件不会自动归零。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS|Advanced")
	TObjectPtr<UCurveVector> OffsetCurve = nullptr;

	/** 可选：自定义位移曲线（横轴时间 0～1，纵轴位移 0～1）。留空使用 Ease */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS|Advanced")
	TObjectPtr<UCurveFloat> CustomProgressCurve = nullptr;

	bool HasCustomCurve() const { return CustomProgressCurve != nullptr; }
	bool HasOffsetCurve() const { return OffsetCurve != nullptr; }
};

/** 一次运动播放的运行时参数。 */
USTRUCT(BlueprintType)
struct ROOTMOTIONSOURCEPLUS_API FRMSMotionRequest
{
	GENERATED_BODY()

	/** 运动参数；直接内嵌，不需要任何资产 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	FRMSMotionParams Params;

	/**
	 * 起点（世界空间）。**语义是角色胶囊中心**，等于 ACharacter::GetActorLocation()，不是脚底。
	 * 脚底与该点相差一个胶囊半高（默认 88 厘米）。射线打地面的命中点必须先用 FootToCenter 转换。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	FVector StartLocation = FVector::ZeroVector;

	/**
	 * 终点（世界空间）。**语义同样是胶囊中心**。
	 * Polyline 与 Spline 时被忽略，终点取路径最后一点。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	FVector TargetLocation = FVector::ZeroVector;

	/** 力。Kind 非 None 时本请求是力，位移相关字段被忽略；Duration 小于 0 表示无限时长 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	FRMSForceParams Force;

	/** 跟随 Actor。TargetActor 非空时终点由目标 Actor 的实时位置加偏移决定，TargetLocation 仅作初值 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	FRMSFollowParams Follow;

	/** 动画根位移回放参数。Animation 非空时本请求按动画根位移运动，路径相关字段被忽略 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	FRMSAnimationParams Anim;

	/** 动画 Warping。Segments 非空时，动画根位移被对齐到各目标段 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	FRMSWarpParams Warp;

	/** 路径点（世界空间，不含起点，**胶囊中心**）。仅 Polyline 与 Spline 使用，最多 64 个 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	TArray<FVector> PathPoints;

	/** 实例名：同名冲突判断与 CMC 匹配服务器校正都依赖它 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	FName InstanceName = NAME_None;

	/** 优先级，数值大者在 CMC 中后处理、覆盖较低者 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	int32 Priority = 0;

	/** 同名冲突策略 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RMS")
	ERMSConflictPolicy ConflictPolicy = ERMSConflictPolicy::Replace;
};
