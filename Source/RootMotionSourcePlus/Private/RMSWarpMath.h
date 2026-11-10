// Copyright JTW Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RMSTypes.h"

/**
 * 动画 Warping 的纯函数。
 *
 * 思路：动画根位移是一条从当前动画位置出发的曲线。设“剩余动画位移”为 R（当前动画时间到区间终点的总根位移，
 * 世界空间），而“当前位置到目标”的向量为 D。Warping 要找一个变换 T，使 T(R) = D，然后把每帧的动画位移
 * 也经 T 变换。这里取 T 为“绕垂直轴的旋转加整体缩放”的相似变换，在水平面内旋转、沿三维等比缩放，
 * 因此在俯仰角一致时 T(R) = D 恒成立（可直接测试），且不引入旧实现的剪切。
 *
 * 全部为无状态函数。每帧都用“当前实际位置”重算 T，被阻挡后下一帧自动找补（Guaranteed 语义）；
 * Incremental 语义下改用固定的 T（由窗口起点一次算出），不随实际位置变化。
 */
namespace RMSWarp
{
	/** 缩放系数上下限。目标在身后或剩余动画位移几乎为 0 时用来避免爆炸或反向。 */
	struct FLimits
	{
		float MinScale = 0.1f;
		float MaxScale = 4.0f;
	};

	/** 一次变换：水平旋转（弧度，绕世界 Z）加整体缩放。 */
	struct FSimilarity
	{
		float YawRadians = 0.0f;
		float Scale = 1.0f;
		/** 是否为恒等（剩余动画位移为 0 等无法求解的情形） */
		bool bIdentity = true;

		FVector Apply(const FVector& V) const;
	};

	/**
	 * 求变换 T，使 T(Remaining) = ToTarget（在限幅允许的范围内）。
	 * Remaining 为 0（或几乎为 0）时返回恒等变换，不会除零。
	 * 缩放系数被限幅后，T(Remaining) 与 ToTarget 方向一致但长度不同——这是有意的，
	 * 超出限幅说明动画与目标相差过大，宁可到不了也不扭曲动画。
	 * 已知边界：Remaining 与 ToTarget 俯仰角不同时，相似变换保证长度与水平方向，但 Z 分量对不上。
	 */
	FSimilarity Solve(const FVector& Remaining, const FVector& ToTarget, const FLimits& Limits);

	/**
	 * 把用户给的目标段整理为连续、有序、不越界的段列表：
	 * - 段按 WindowStart 排序，裁剪到 [RangeStart, RangeEnd]，丢弃长度为 0 或完全越界的段；
	 * - 与前一段重叠的部分让给前一段（后一段的起点推迟到前一段终点），不会出现两段同时生效；
	 * - 段与段之间、首段之前、末段之后的间隙补成 bWarp=false 的“原样播放段”，使整个区间被完整覆盖；
	 * - 间隙段的 Target 不使用（置为 ZeroVector），因为它们不 Warping。
	 * 返回的列表首段起点恒为 RangeStart、末段终点恒为 RangeEnd；区间长度为 0 时返回空列表。
	 * 这是旧实现里越界访问 Windows[-1] 与间隙段目标取零向量瞬移的修复点。
	 */
	void NormalizeSegments(const TArray<FRMSWarpSegment>& Input, float RangeStart, float RangeEnd, TArray<FRMSWarpSegment>& Output);

	/** 动画时间所属的段下标；恰在段边界时归入后一段（末点归入最后一段）。没有段时返回 INDEX_NONE。 */
	int32 FindSegmentIndex(const TArray<FRMSWarpSegment>& Segments, float AnimTime);

	/**
	 * 本帧位移 = T(本帧动画位移)。
	 * AnimDelta 与 Remaining 必须在同一空间（世界空间）。
	 */
	FVector WarpDelta(const FVector& AnimDelta, const FSimilarity& Transform);
}
