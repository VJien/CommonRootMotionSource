// Copyright JTW Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RMSTypes.h"

class UCurveFloat;
class UCurveVector;

/**
 * 一条运动的几何形状，Apply 时由请求一次性构造，之后只读。
 * 全部由 (种类, 起点, 终点, 路径点, 弧线高度) 纯函数推出，不含随时间累加的状态。
 */
struct FRMSMotionShape
{
	ERMSPathKind Kind = ERMSPathKind::Straight;
	FVector Start = FVector::ZeroVector;
	FVector Target = FVector::ZeroVector;
	float ArcHeight = 0.0f;

	/** 折线或样条的采样点（含起点）与累计弧长；其他种类为空 */
	TArray<FVector> Polyline;
	TArray<float> Cumulative;
};

/** 无 UObject 状态、无 World 依赖的纯函数，便于自动化测试。 */
namespace RMSMath
{
	/** 时长下限（秒）。低于它的值一律钳到此值，避免除零。 */
	constexpr float MinDuration = 0.01f;

	float ClampDuration(float Duration);

	/** 内置缓动：输入输出都在 0～1。 */
	float ApplyEase(ERMSEase Ease, float Alpha);

	/** 时间进度（0～1）映射为位移进度；有自定义曲线用曲线，否则用 Ease。 */
	float EvaluateProgress(const FRMSMotionParams& Params, float TimeAlpha);

	/** 正弦弧线高度：半个正弦，起终点为 0。 */
	float EvaluateArc(float ArcHeight, float TimeAlpha);

	/**
	 * 真抛物线高度：h(u) = 4 * Peak * u * (1 - u)，u 为位移进度 0～1。
	 * 峰值恰为 Peak，起终点为 0；与位移进度而非时间相关，因此缓动会同时改变水平与垂直节奏，轨迹形状不变。
	 */
	float EvaluateParabola(float Peak, float ProgressAlpha);

	/** 构造形状：折线与样条在此构建弧长表。点列为空时退化为直线。 */
	void BuildShape(ERMSPathKind Kind, const FVector& Start, const FVector& Target, const TArray<FVector>& PathPoints, float ArcHeight, FRMSMotionShape& OutShape);

	/** 形状上的终点：折线与样条取最后一点，其余取 Target。 */
	FVector ShapeEndpoint(const FRMSMotionShape& Shape);

	/** 位移进度（0～1，已过缓动）对应的形状位置。 */
	FVector EvaluateShape(const FRMSMotionShape& Shape, float ProgressAlpha);

	/** 起终点或路径上，Actor 在 TimeAlpha 时刻的期望位置（含缓动）。 */
	FVector EvaluatePosition(const FRMSMotionParams& Params, const FRMSMotionShape& Shape, float TimeAlpha);

	/**
	 * 朝向空间到世界空间的偏移转换。朝向空间约定：X = 起点到终点的水平前进方向，Y = 其右侧（Z 向上时的右手系），Z = 世界上方。
	 * 起终点水平投影重合（纯垂直运动）时没有定义前进方向，退化为世界 X 轴。
	 * 偏移只转换这一次，调用方不得再旋转（旧实现在此二次旋转导致路径扭曲）。
	 */
	FVector OffsetToWorld(const FVector& LocalOffset, const FVector& Start, const FVector& End);

	/** 在位移进度处采样偏移曲线并转到世界空间；曲线为空返回零向量。 */
	FVector EvaluateOffset(const UCurveVector* Curve, float ProgressAlpha, const FVector& Start, const FVector& End);

	/**
	 * 本帧朝向增量。引擎把 Source 输出的旋转当作增量：新朝向 = 输出旋转 * 当前朝向，
	 * 所以必须输出“从当前朝向转到期望朝向”的相对旋转，而不是绝对朝向。
	 * 水平速度过小时返回单位旋转（不转动）。
	 */
	FQuat ComputeFacingDelta(const FQuat& CurrentRotation, const FVector& Velocity);

	/**
	 * 段内旋转进度：时间进度 t(0~1) → 旋转进度 s(0~1)。
	 * 曲线为空则线性。返回值被钳制到 0~1，因此 s(1) 恒为 1——段结束时一定转到目标朝向。
	 */
	float EvaluateRotationProgress(const UCurveFloat* RotationCurve, float TimeAlpha);

	/**
	 * 本帧应消耗掉的“剩余朝向误差”比例。位移运动与动画 Warping 共用。
	 *
	 * 引擎把 Source 输出的旋转当作增量（新朝向 = 增量 * 当前朝向），所以不能直接给绝对朝向。
	 * 设段内旋转进度为 s，当前剩余误差为 R。若角色一直走在计划上，则 R 与 (1 - s) 成正比，
	 * 本帧应消耗的误差就是 R * (s(To) - s(From)) / (1 - s(From))，即下面的返回值。
	 *
	 * 这样做的效果：
	 * - 曲线把 s 压在前段（0～0.6 恒为 0）时，比例恒为 0，前段完全不转；
	 * - s(To) 到 1 时返回 1，段结束时强制精确转到目标朝向，不受中途外力偏转影响；
	 * - 中途被别的逻辑转了朝向，下一帧比例不变，但基准是当前实际朝向，会自动收回来。
	 *
	 * FromTime / ToTime 为段内时间（位移运动用真实时间，动画运动用动画时间）。
	 * 段长为 0 时，到了段终点返回 1（立即转到位），否则返回 0。
	 */
	float ComputeRotationFraction(const UCurveFloat* RotationCurve, float SegmentStart, float SegmentEnd, float FromTime, float ToTime);

	/**
	 * 朝目标 Yaw 转动“剩余误差 × Fraction”的旋转增量，只绕世界 Z 轴、走最短弧。
	 * Fraction 为 1 时增量恰好把角色转到 TargetYaw。
	 */
	FQuat ComputeYawRotationDelta(const FQuat& CurrentRotation, float TargetYaw, float Fraction);

	/** 动画播放速率下限。 */
	constexpr float MinRate = 0.01f;

	float ClampRate(float Rate);

	/**
	 * 解析动画有效终点：AnimEnd 小于等于 0 或超过动画长度时取动画长度；结果不小于 AnimStart。
	 * 返回的区间长度即动画“有效时长”，可为 0（此时回放立即完成）。
	 */
	void ResolveAnimRange(float PlayLength, float AnimStart, float AnimEnd, float& OutStart, float& OutEnd);

	/** 动画回放需要的真实时长：(终点 - 起点) / Rate，不小于 MinDuration。 */
	float AnimRealDuration(float RangeStart, float RangeEnd, float Rate);

	/**
	 * 真实已播放时间 → 动画时间。动画时间 = 起点 + 真实时间 * Rate，并钳制在 [起点, 终点]。
	 * 旧实现写成 (起点 + 真实时间) * Rate，起点非 0 时错乱。
	 */
	float RealTimeToAnimTime(float RangeStart, float RangeEnd, float Rate, float RealTime);

	/** 忽略 Z（可选），在动画根位移的位移分量上应用。 */
	FVector FilterAnimTranslation(const FVector& Translation, bool bIgnoreZ);

	/**
	 * 把“动画组件空间的根位移”转为世界空间位移。
	 * 组件空间的位移要先经 ComponentRotation 旋转到世界，ComponentRotation 取 Apply 时 Mesh 的世界旋转，
	 * 由调用方在 Apply 时取值并序列化，运行时不得再读当前姿态（两端可能不一致）。
	 */
	FVector AnimDeltaToWorld(const FVector& ComponentSpaceDelta, const FQuat& ComponentRotation);

	/**
	 * 预设：由方向、距离、时长构造 Params 与终点。全部是纯函数，方向为零或距离接近 0 时终点等于起点，不产生 NaN。
	 * 预设的默认值是插件作者的建议，调用方可以在返回后继续修改。
	 */
	void PresetDash(const FVector& Start, const FVector& Direction, float Distance, float Duration, FRMSMotionParams& OutParams, FVector& OutTarget);
	void PresetKnockback(const FVector& Start, const FVector& Direction, float Distance, float Duration, float Lift, FRMSMotionParams& OutParams, FVector& OutTarget);
	void PresetJump(const FVector& Start, const FVector& Target, float ApexHeight, float Duration, FRMSMotionParams& OutParams);

	/** 强度上限（厘米/秒），防止误填极大值或恶意包造成瞬移。 */
	constexpr float MaxForceStrength = 100000.0f;

	/** 停止距离上限（厘米）。 */
	constexpr float MaxStopDistance = 100000.0f;

	/** 恒力速度：方向归一后乘强度。方向为零返回零向量。与帧率无关。 */
	FVector ComputeConstantForceVelocity(const FVector& Direction, float Strength);

	/**
	 * 牵引速度：朝目标的方向 × 强度 × 距离曲线(距离)。
	 * - 距离小于等于 StopDistance 时为 0；
	 * - 曲线为空时倍率恒为 1；曲线值被钳到非负；
	 * - bIgnoreZ 时只取水平方向与水平距离；
	 * - 目标与角色重合（距离为 0）时为 0，不产生 NaN。
	 */
	FVector ComputePullVelocity(const FVector& From, const FVector& Target, float Strength, float StopDistance, bool bIgnoreZ, const UCurveFloat* StrengthOverDistance);

	/** 按轴向锁定把向量对应分量置零：LockedAxes 的 X/Y/Z 非 0 表示该轴被锁定。 */
	FVector ApplyAxisLock(const FVector& Vector, const FVector& LockedAxes);

	/**
	 * 计算本帧速度（厘米/秒）。
	 * - Guaranteed：目标时刻位置减当前实际位置，被挡后下一帧会补回来。
	 * - Incremental：只取目标时刻与当前时刻两个期望位置之差，与角色实际位置无关。
	 * StepTime 是 CMC 本帧移动时间，SimTime 是本 Source 本帧推进的模拟时间（末帧可能更短）。
	 */
	FVector ComputeVelocity(const FRMSMotionParams& Params, const FRMSMotionShape& Shape, float Duration,
		float CurrentTime, float SimTime, float StepTime, const FVector& ActualLocation);
}
