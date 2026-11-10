// Copyright JTW Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 多点路径的纯函数。路径按弧长匀速参数化：
 * 先在路径上取固定数量的采样点，累计每段弧长，再按“已走过的弧长”反查位置。
 * 全部是无状态函数，同一份点列在任意时刻都得到同一个位置，因此回放与校正一致。
 */
namespace RMSPath
{
	/** 单段样条的采样数。折线不使用。 */
	constexpr int32 SamplesPerSegment = 16;

	/** 路径点上限，防止恶意或误填的超大数组撑爆网络包。 */
	constexpr int32 MaxPoints = 64;

	/** 把用户点列整理成可用的采样路径：去掉相邻重合点，折线保持原样，样条做 Catmull-Rom 加密。 */
	void BuildPolyline(const TArray<FVector>& Points, bool bSmooth, TArray<FVector>& OutPolyline);

	/** 每个采样点的累计弧长，首项为 0；输出长度等于输入长度。 */
	void BuildCumulativeLength(const TArray<FVector>& Polyline, TArray<float>& OutCumulative);

	/** 路径总弧长。 */
	float TotalLength(const TArray<float>& Cumulative);

	/**
	 * 按弧长取位置。Distance 会被钳制到 [0, 总长]。
	 * 空路径返回 ZeroVector；单点或总长为 0 返回首点。
	 */
	FVector SampleAtDistance(const TArray<FVector>& Polyline, const TArray<float>& Cumulative, float Distance);

	/** 单段 Catmull-Rom（均匀参数化），P0..P3 为相邻四点，T 在 [0,1] 内取 P1 到 P2 之间的点。 */
	FVector CatmullRom(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, float T);
}
