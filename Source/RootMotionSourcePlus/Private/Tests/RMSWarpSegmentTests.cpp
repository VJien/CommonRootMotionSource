// Copyright JTW Studio 2026. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RMSWarpMath.h"

namespace
{
	FRMSWarpSegment MakeSegment(float Start, float End, const FVector& Target)
	{
		FRMSWarpSegment Segment;
		Segment.WindowStart = Start;
		Segment.WindowEnd = End;
		Segment.Target = Target;
		return Segment;
	}
}

/** 窗口整理：覆盖旧 MultiTargets “首窗口前有间隙访问 Windows[-1]”“间隙段目标取零向量瞬移”“重叠窗口靠排序截断”。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSWarpSegmentsTest, "RMS.WarpSegments", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSWarpSegmentsTest::RunTest(const FString& Parameters)
{
	// 整个区间必须被完整、无缝、不重叠地覆盖
	auto ExpectCovered = [this](const TArray<FRMSWarpSegment>& Out, float A, float B, const TCHAR* What)
	{
		if (Out.Num() == 0)
		{
			AddError(FString::Printf(TEXT("%s：输出为空"), What));
			return;
		}
		TestEqual(*FString::Printf(TEXT("%s：首段起点 = 区间起点"), What), Out[0].WindowStart, A, 0.0001f);
		TestEqual(*FString::Printf(TEXT("%s：末段终点 = 区间终点"), What), Out.Last().WindowEnd, B, 0.0001f);
		for (int32 Index = 1; Index < Out.Num(); ++Index)
		{
			TestEqual(*FString::Printf(TEXT("%s：第 %d 段与前一段无缝"), What, Index), Out[Index].WindowStart, Out[Index - 1].WindowEnd, 0.0001f);
		}
		for (int32 Index = 0; Index < Out.Num(); ++Index)
		{
			TestTrue(*FString::Printf(TEXT("%s：第 %d 段长度为正"), What, Index), Out[Index].WindowEnd > Out[Index].WindowStart);
		}
	};

	// 首窗口之前有间隙（旧实现在此访问 Windows[-1]）
	{
		TArray<FRMSWarpSegment> Out;
		RMSWarp::NormalizeSegments({ MakeSegment(0.4f, 0.8f, FVector(500, 0, 0)) }, 0.0f, 1.0f, Out);
		ExpectCovered(Out, 0.0f, 1.0f, TEXT("首窗口前有间隙"));
		if (TestEqual(TEXT("首窗口前有间隙：共 3 段（间隙、窗口、间隙）"), Out.Num(), 3))
		{
			TestFalse(TEXT("首段是间隙，不 Warping"), Out[0].bWarp);
			TestTrue(TEXT("窗口段 Warping"), Out[1].bWarp);
			TestFalse(TEXT("末段是间隙，不 Warping"), Out[2].bWarp);
			TestTrue(TEXT("窗口段保留原目标"), Out[1].Target.Equals(FVector(500, 0, 0), 0.01));
		}
	}

	// 多窗口、窗口间有间隙
	{
		TArray<FRMSWarpSegment> Out;
		RMSWarp::NormalizeSegments({ MakeSegment(0.2f, 0.4f, FVector(100, 0, 0)), MakeSegment(0.6f, 0.9f, FVector(300, 0, 0)) }, 0.0f, 1.0f, Out);
		ExpectCovered(Out, 0.0f, 1.0f, TEXT("两窗口"));
		TestEqual(TEXT("两窗口：共 5 段"), Out.Num(), 5);
	}

	// 乱序输入被排序
	{
		TArray<FRMSWarpSegment> Out;
		RMSWarp::NormalizeSegments({ MakeSegment(0.6f, 0.9f, FVector(300, 0, 0)), MakeSegment(0.2f, 0.4f, FVector(100, 0, 0)) }, 0.0f, 1.0f, Out);
		ExpectCovered(Out, 0.0f, 1.0f, TEXT("乱序"));
		if (Out.Num() >= 2)
		{
			TestTrue(TEXT("乱序：第一个 Warp 段是较早的窗口"), Out[1].bWarp && Out[1].Target.Equals(FVector(100, 0, 0), 0.01));
		}
	}

	// 重叠窗口：后一段让位，不会两段同时生效
	{
		TArray<FRMSWarpSegment> Out;
		RMSWarp::NormalizeSegments({ MakeSegment(0.2f, 0.6f, FVector(100, 0, 0)), MakeSegment(0.4f, 0.8f, FVector(300, 0, 0)) }, 0.0f, 1.0f, Out);
		ExpectCovered(Out, 0.0f, 1.0f, TEXT("重叠"));
		for (int32 Index = 1; Index < Out.Num(); ++Index)
		{
			TestTrue(TEXT("重叠：任意相邻段不重叠"), Out[Index].WindowStart >= Out[Index - 1].WindowEnd - 0.0001f);
		}
	}

	// 越界、零长度与反向窗口被丢弃，不崩溃
	{
		TArray<FRMSWarpSegment> Out;
		RMSWarp::NormalizeSegments({ MakeSegment(2.0f, 3.0f, FVector::ZeroVector), MakeSegment(0.5f, 0.5f, FVector::ZeroVector), MakeSegment(0.9f, 0.3f, FVector::ZeroVector) }, 0.0f, 1.0f, Out);
		ExpectCovered(Out, 0.0f, 1.0f, TEXT("全部窗口无效"));
		if (TestEqual(TEXT("全部窗口无效：退化为单个原样播放段"), Out.Num(), 1))
		{
			TestFalse(TEXT("全部窗口无效：该段不 Warping"), Out[0].bWarp);
		}
	}

	// 部分越界的窗口被裁剪
	{
		TArray<FRMSWarpSegment> Out;
		RMSWarp::NormalizeSegments({ MakeSegment(-5.0f, 0.5f, FVector(100, 0, 0)) }, 0.0f, 1.0f, Out);
		ExpectCovered(Out, 0.0f, 1.0f, TEXT("起点越界裁剪"));
		if (Out.Num() > 0)
		{
			TestEqual(TEXT("起点越界裁剪：窗口起点被钳到区间起点"), Out[0].WindowStart, 0.0f, 0.0001f);
			TestTrue(TEXT("起点越界裁剪：首段 Warping"), Out[0].bWarp);
		}
	}

	// 区间长度为 0：输出为空，不崩溃
	{
		TArray<FRMSWarpSegment> Out;
		RMSWarp::NormalizeSegments({ MakeSegment(0.0f, 1.0f, FVector::ZeroVector) }, 0.5f, 0.5f, Out);
		TestEqual(TEXT("区间长度 0：输出为空"), Out.Num(), 0);
	}

	// 空输入：整个区间一个原样播放段
	{
		TArray<FRMSWarpSegment> Out;
		RMSWarp::NormalizeSegments({}, 0.0f, 2.0f, Out);
		ExpectCovered(Out, 0.0f, 2.0f, TEXT("空输入"));
		TestEqual(TEXT("空输入：单段"), Out.Num(), 1);
	}

	// 查找所属段：边界归后一段，末点归最后一段，越界就近归入
	{
		TArray<FRMSWarpSegment> Out;
		RMSWarp::NormalizeSegments({ MakeSegment(0.2f, 0.4f, FVector(100, 0, 0)), MakeSegment(0.6f, 0.9f, FVector(300, 0, 0)) }, 0.0f, 1.0f, Out);
		TestEqual(TEXT("起点归第 0 段"), RMSWarp::FindSegmentIndex(Out, 0.0f), 0);
		TestEqual(TEXT("窗口中间"), RMSWarp::FindSegmentIndex(Out, 0.3f), 1);
		TestEqual(TEXT("边界归后一段"), RMSWarp::FindSegmentIndex(Out, 0.4f), 2);
		TestEqual(TEXT("终点归最后一段"), RMSWarp::FindSegmentIndex(Out, 1.0f), Out.Num() - 1);
		TestEqual(TEXT("早于区间就近归首段"), RMSWarp::FindSegmentIndex(Out, -1.0f), 0);
		TestEqual(TEXT("晚于区间就近归末段"), RMSWarp::FindSegmentIndex(Out, 9.0f), Out.Num() - 1);
		TestEqual(TEXT("无段返回 INDEX_NONE"), RMSWarp::FindSegmentIndex({}, 0.5f), static_cast<int32>(INDEX_NONE));
	}
	return true;
}

#endif
