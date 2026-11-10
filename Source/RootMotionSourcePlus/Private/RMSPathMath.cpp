// Copyright JTW Studio 2026. All Rights Reserved.

#include "RMSPathMath.h"

FVector RMSPath::CatmullRom(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, float T)
{
	const double T2 = static_cast<double>(T) * T;
	const double T3 = T2 * T;
	return 0.5 * ((2.0 * P1)
		+ (-P0 + P2) * T
		+ (2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3) * T2
		+ (-P0 + 3.0 * P1 - 3.0 * P2 + P3) * T3);
}

void RMSPath::BuildPolyline(const TArray<FVector>& Points, bool bSmooth, TArray<FVector>& OutPolyline)
{
	OutPolyline.Reset();

	// 去掉相邻重合点：重合点会产生零长度线段，样条里还会让切线退化
	TArray<FVector> Clean;
	Clean.Reserve(FMath::Min(Points.Num(), MaxPoints));
	for (const FVector& Point : Points)
	{
		if (Clean.Num() >= MaxPoints)
		{
			break;
		}
		if (Clean.Num() == 0 || !Clean.Last().Equals(Point, UE_KINDA_SMALL_NUMBER))
		{
			Clean.Add(Point);
		}
	}

	if (!bSmooth || Clean.Num() < 3)
	{
		OutPolyline = MoveTemp(Clean);
		return;
	}

	// 端点处复制首尾点作为虚拟邻点，使曲线经过全部原始点且端点切线合理
	OutPolyline.Reserve((Clean.Num() - 1) * SamplesPerSegment + 1);
	for (int32 Segment = 0; Segment < Clean.Num() - 1; ++Segment)
	{
		const FVector& P0 = Clean[FMath::Max(Segment - 1, 0)];
		const FVector& P1 = Clean[Segment];
		const FVector& P2 = Clean[Segment + 1];
		const FVector& P3 = Clean[FMath::Min(Segment + 2, Clean.Num() - 1)];
		for (int32 Sample = 0; Sample < SamplesPerSegment; ++Sample)
		{
			OutPolyline.Add(CatmullRom(P0, P1, P2, P3, static_cast<float>(Sample) / SamplesPerSegment));
		}
	}
	OutPolyline.Add(Clean.Last());
}

void RMSPath::BuildCumulativeLength(const TArray<FVector>& Polyline, TArray<float>& OutCumulative)
{
	OutCumulative.Reset();
	OutCumulative.Reserve(Polyline.Num());
	float Accum = 0.0f;
	for (int32 Index = 0; Index < Polyline.Num(); ++Index)
	{
		if (Index > 0)
		{
			Accum += static_cast<float>(FVector::Dist(Polyline[Index - 1], Polyline[Index]));
		}
		OutCumulative.Add(Accum);
	}
}

float RMSPath::TotalLength(const TArray<float>& Cumulative)
{
	return Cumulative.Num() > 0 ? Cumulative.Last() : 0.0f;
}

FVector RMSPath::SampleAtDistance(const TArray<FVector>& Polyline, const TArray<float>& Cumulative, float Distance)
{
	if (Polyline.Num() == 0 || Cumulative.Num() != Polyline.Num())
	{
		return FVector::ZeroVector;
	}
	const float Total = TotalLength(Cumulative);
	if (Polyline.Num() == 1 || Total <= UE_SMALL_NUMBER)
	{
		return Polyline[0];
	}

	const float D = FMath::Clamp(Distance, 0.0f, Total);

	// 二分找到 D 所在线段：Cumulative[Lo] <= D <= Cumulative[Lo + 1]
	int32 Lo = 0;
	int32 Hi = Cumulative.Num() - 1;
	while (Hi - Lo > 1)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (Cumulative[Mid] <= D)
		{
			Lo = Mid;
		}
		else
		{
			Hi = Mid;
		}
	}

	const float SegmentLength = Cumulative[Hi] - Cumulative[Lo];
	if (SegmentLength <= UE_SMALL_NUMBER)
	{
		return Polyline[Lo];
	}
	const float Alpha = (D - Cumulative[Lo]) / SegmentLength;
	return FMath::Lerp(Polyline[Lo], Polyline[Hi], static_cast<double>(Alpha));
}
