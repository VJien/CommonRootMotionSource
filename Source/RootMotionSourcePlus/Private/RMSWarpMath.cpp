// Copyright JTW Studio 2026. All Rights Reserved.

#include "RMSWarpMath.h"

FVector RMSWarp::FSimilarity::Apply(const FVector& V) const
{
	if (bIdentity)
	{
		return V;
	}
	const float C = FMath::Cos(YawRadians);
	const float S = FMath::Sin(YawRadians);
	// 绕 Z 轴旋转 Yaw，Z 分量只缩放不旋转
	return FVector((V.X * C - V.Y * S) * Scale, (V.X * S + V.Y * C) * Scale, V.Z * Scale);
}

RMSWarp::FSimilarity RMSWarp::Solve(const FVector& Remaining, const FVector& ToTarget, const FLimits& Limits)
{
	FSimilarity Result;

	const double RemainingLength = Remaining.Size();
	if (RemainingLength <= 1.0)
	{
		// 剩余动画位移几乎为 0：没有可缩放的东西，保持恒等
		return Result;
	}

	const double TargetLength = ToTarget.Size();
	const float RawScale = static_cast<float>(TargetLength / RemainingLength);
	Result.Scale = FMath::Clamp(RawScale, Limits.MinScale, Limits.MaxScale);

	// 水平旋转角：从 Remaining 的水平方向转到 ToTarget 的水平方向
	const FVector RemainingFlat(Remaining.X, Remaining.Y, 0.0);
	const FVector TargetFlat(ToTarget.X, ToTarget.Y, 0.0);
	if (RemainingFlat.SizeSquared() > 1.0 && TargetFlat.SizeSquared() > 1.0)
	{
		const double RemainingYaw = FMath::Atan2(RemainingFlat.Y, RemainingFlat.X);
		const double TargetYaw = FMath::Atan2(TargetFlat.Y, TargetFlat.X);
		Result.YawRadians = static_cast<float>(FMath::UnwindRadians(TargetYaw - RemainingYaw));
	}

	Result.bIdentity = false;
	return Result;
}

void RMSWarp::NormalizeSegments(const TArray<FRMSWarpSegment>& Input, float RangeStart, float RangeEnd, TArray<FRMSWarpSegment>& Output)
{
	Output.Reset();
	if (RangeEnd - RangeStart <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	// 先裁剪并排序，丢弃长度为 0 或完全越界的段
	TArray<FRMSWarpSegment> Sorted;
	Sorted.Reserve(Input.Num());
	for (const FRMSWarpSegment& Segment : Input)
	{
		FRMSWarpSegment Clipped = Segment;
		Clipped.WindowStart = FMath::Clamp(Segment.WindowStart, RangeStart, RangeEnd);
		Clipped.WindowEnd = FMath::Clamp(Segment.WindowEnd, RangeStart, RangeEnd);
		if (Clipped.WindowEnd - Clipped.WindowStart > UE_KINDA_SMALL_NUMBER)
		{
			Sorted.Add(Clipped);
		}
	}
	Sorted.Sort([](const FRMSWarpSegment& A, const FRMSWarpSegment& B) { return A.WindowStart < B.WindowStart; });

	auto AddGap = [&Output](float From, float To)
	{
		if (To - From > UE_KINDA_SMALL_NUMBER)
		{
			FRMSWarpSegment Gap;
			Gap.WindowStart = From;
			Gap.WindowEnd = To;
			Gap.Target = FVector::ZeroVector;
			Gap.bWarp = false;
			// 间隙段既不 Warping 位移也不转朝向：旋转字段必须清掉，
			// 否则用户误留的目标朝向会在间隙里生效，出现“该段应该原样播放却在转”的怪异行为
			Gap.bWarpRotation = false;
			Gap.TargetRotation = FRotator::ZeroRotator;
			Gap.RotationCurve = nullptr;
			Output.Add(Gap);
		}
	};

	float Cursor = RangeStart;
	for (FRMSWarpSegment& Segment : Sorted)
	{
		// 与前一段重叠的部分让给前一段
		Segment.WindowStart = FMath::Max(Segment.WindowStart, Cursor);
		if (Segment.WindowEnd - Segment.WindowStart <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}
		AddGap(Cursor, Segment.WindowStart);
		Output.Add(Segment);
		Cursor = Segment.WindowEnd;
	}
	AddGap(Cursor, RangeEnd);
}

int32 RMSWarp::FindSegmentIndex(const TArray<FRMSWarpSegment>& Segments, float AnimTime)
{
	if (Segments.Num() == 0)
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		// 段边界归入后一段；最后一段包含终点
		const bool bLast = Index == Segments.Num() - 1;
		if (AnimTime >= Segments[Index].WindowStart && (AnimTime < Segments[Index].WindowEnd || (bLast && AnimTime <= Segments[Index].WindowEnd)))
		{
			return Index;
		}
	}
	// 早于首段或晚于末段：就近归入首段或末段，保证任何时刻都有段可用
	return AnimTime < Segments[0].WindowStart ? 0 : Segments.Num() - 1;
}

FVector RMSWarp::WarpDelta(const FVector& AnimDelta, const FSimilarity& Transform)
{
	return Transform.Apply(AnimDelta);
}
