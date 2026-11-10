// Copyright JTW Studio 2026. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RMSMotionMath.h"
#include "RMSPathMath.h"
#include "RMSWarpMath.h"
#include "RMSMotionSource.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"
#include "Curves/CurveVector.h"

namespace
{
	/** 构造一个固定参数的 Source；不含自定义曲线，对象引用路径由联网手测覆盖 */
	FRMSMotionSource MakeSource()
	{
		FRMSMotionSource Source;
		Source.StartLocation = FVector(100.0, 200.0, 300.0);
		Source.TargetLocation = FVector(700.0, 200.0, 300.0);
		Source.Duration = 0.5f;
		Source.InstanceName = TEXT("Test");
		Source.Params.Duration = 0.5f;
		Source.Params.RotationMode = ERMSRotationMode::FaceMotion;
		Source.Params.Ease = ERMSEase::EaseInOut;
		Source.Params.ArcHeight = 40.0f;
		Source.Params.ReachMode = ERMSReachMode::Incremental;
		Source.Params.PathKind = ERMSPathKind::Spline;
		Source.PathPoints = { FVector(300, 250, 300), FVector(500, -100, 320), FVector(700, 200, 300) };
		return Source;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSMathTest, "RMS.Math", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSMathTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("时长钳到下限"), RMSMath::ClampDuration(0.0f), RMSMath::MinDuration);
	TestEqual(TEXT("时长钳到下限（负数）"), RMSMath::ClampDuration(-3.0f), RMSMath::MinDuration);
	TestEqual(TEXT("合法时长不变"), RMSMath::ClampDuration(0.7f), 0.7f);

	for (const ERMSEase Ease : { ERMSEase::Linear, ERMSEase::EaseIn, ERMSEase::EaseOut, ERMSEase::EaseInOut })
	{
		TestEqual(TEXT("缓动起点为 0"), RMSMath::ApplyEase(Ease, 0.0f), 0.0f);
		TestEqual(TEXT("缓动终点为 1"), RMSMath::ApplyEase(Ease, 1.0f), 1.0f);
	}
	TestTrue(TEXT("EaseIn 前半程慢于线性"), RMSMath::ApplyEase(ERMSEase::EaseIn, 0.5f) < 0.5f);
	TestTrue(TEXT("EaseOut 前半程快于线性"), RMSMath::ApplyEase(ERMSEase::EaseOut, 0.5f) > 0.5f);
	TestEqual(TEXT("EaseInOut 中点为 0.5"), RMSMath::ApplyEase(ERMSEase::EaseInOut, 0.5f), 0.5f);
	TestEqual(TEXT("越界输入被钳制"), RMSMath::ApplyEase(ERMSEase::Linear, 2.0f), 1.0f);

	TestEqual(TEXT("弧线起点为 0"), RMSMath::EvaluateArc(100.0f, 0.0f), 0.0f, 0.001f);
	TestEqual(TEXT("弧线终点为 0"), RMSMath::EvaluateArc(100.0f, 1.0f), 0.0f, 0.001f);
	TestEqual(TEXT("弧线中点为峰值"), RMSMath::EvaluateArc(100.0f, 0.5f), 100.0f, 0.001f);

	FRMSMotionParams Linear;
	Linear.Ease = ERMSEase::Linear;
	FRMSMotionShape Line;
	RMSMath::BuildShape(ERMSPathKind::Straight, FVector(0, 0, 0), FVector(100, 0, 0), {}, 0.0f, Line);
	const FVector Mid = RMSMath::EvaluatePosition(Linear, Line, 0.5f);
	TestEqual(TEXT("中点 X"), Mid.X, 50.0);

	const FVector Locked = RMSMath::ApplyAxisLock(FVector(1, 2, 3), FVector(0, 1, 0));
	TestEqual(TEXT("锁定 Y 后 Y 为 0"), Locked.Y, 0.0);
	TestEqual(TEXT("未锁定轴保留 X"), Locked.X, 1.0);
	TestEqual(TEXT("未锁定轴保留 Z"), Locked.Z, 3.0);
	return true;
}

/** 多点路径：覆盖旧 PathMoveTo“第 3 段起位置全错”“空路径永不结束”“按点序号插值速度不均”三类缺陷。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSPathTest, "RMS.Path", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSPathTest::RunTest(const FString& Parameters)
{
	// 四段折线，段长悬殊（100、1000、100、1000），用于检验按弧长而不是按点序号参数化
	const TArray<FVector> Points = { FVector(100, 0, 0), FVector(100, 1000, 0), FVector(200, 1000, 0), FVector(200, 2000, 0) };
	FRMSMotionShape Shape;
	RMSMath::BuildShape(ERMSPathKind::Polyline, FVector::ZeroVector, FVector::ZeroVector, Points, 0.0f, Shape);
	const float Total = RMSPath::TotalLength(Shape.Cumulative);
	TestEqual(TEXT("总弧长 = 100+1000+100+1000"), Total, 2200.0f, 0.01f);

	TestTrue(TEXT("进度 0 在起点"), RMSMath::EvaluateShape(Shape, 0.0f).Equals(FVector::ZeroVector, 0.01));
	TestTrue(TEXT("进度 1 精确落在最后一个路径点"), RMSMath::EvaluateShape(Shape, 1.0f).Equals(FVector(200, 2000, 0), 0.01));

	// 各段边界：第 3 段起位置曾经全错
	TestTrue(TEXT("第 1 段终点"), RMSMath::EvaluateShape(Shape, 100.0f / Total).Equals(FVector(100, 0, 0), 0.05));
	TestTrue(TEXT("第 2 段终点"), RMSMath::EvaluateShape(Shape, 1100.0f / Total).Equals(FVector(100, 1000, 0), 0.05));
	TestTrue(TEXT("第 3 段终点"), RMSMath::EvaluateShape(Shape, 1200.0f / Total).Equals(FVector(200, 1000, 0), 0.05));
	TestTrue(TEXT("第 3 段中点"), RMSMath::EvaluateShape(Shape, 1150.0f / Total).Equals(FVector(150, 1000, 0), 0.05));

	// 匀速：同一直线段内等进度间隔 → 等位移
	const FVector A = RMSMath::EvaluateShape(Shape, 0.10f);
	const FVector B = RMSMath::EvaluateShape(Shape, 0.20f);
	const FVector C = RMSMath::EvaluateShape(Shape, 0.30f);
	TestEqual(TEXT("同一直线段内等进度间隔，等位移"), static_cast<float>(FVector::Dist(A, B)), static_cast<float>(FVector::Dist(B, C)), 0.5f);

	// 退化输入：绝不崩溃、绝不挂起
	FRMSMotionShape Empty;
	RMSMath::BuildShape(ERMSPathKind::Polyline, FVector(5, 6, 7), FVector(50, 60, 70), {}, 0.0f, Empty);
	TestEqual(TEXT("空路径点退化为直线"), static_cast<uint8>(Empty.Kind), static_cast<uint8>(ERMSPathKind::Straight));
	TestTrue(TEXT("空路径点：终点取 Target"), RMSMath::ShapeEndpoint(Empty).Equals(FVector(50, 60, 70), 0.01));

	FRMSMotionShape Same;
	RMSMath::BuildShape(ERMSPathKind::Polyline, FVector(1, 1, 1), FVector::ZeroVector, { FVector(1, 1, 1), FVector(1, 1, 1) }, 0.0f, Same);
	TestTrue(TEXT("全部点重合：长度为 0，返回该点，不崩溃"), RMSMath::EvaluateShape(Same, 0.5f).Equals(FVector(1, 1, 1), 0.01));

	TestTrue(TEXT("空数组采样返回零向量"), RMSPath::SampleAtDistance({}, {}, 10.0f).IsZero());
	TestTrue(TEXT("距离越界被钳制到端点"), RMSPath::SampleAtDistance(Shape.Polyline, Shape.Cumulative, 1.0e9f).Equals(FVector(200, 2000, 0), 0.01));
	TestTrue(TEXT("负距离被钳制到起点"), RMSPath::SampleAtDistance(Shape.Polyline, Shape.Cumulative, -50.0f).Equals(FVector::ZeroVector, 0.01));

	// 超过上限的点列被截断，不撑爆
	TArray<FVector> Many;
	for (int32 Index = 1; Index <= 200; ++Index)
	{
		Many.Add(FVector(Index * 10.0, 0, 0));
	}
	FRMSMotionShape Big;
	RMSMath::BuildShape(ERMSPathKind::Polyline, FVector::ZeroVector, FVector::ZeroVector, Many, 0.0f, Big);
	TestTrue(TEXT("点数被限制在上限内"), Big.Polyline.Num() <= RMSPath::MaxPoints);

	// 样条：必须经过所有原始点
	const TArray<FVector> SplinePts = { FVector(300, 200, 0), FVector(600, -200, 0), FVector(900, 0, 0) };
	FRMSMotionShape Spline;
	RMSMath::BuildShape(ERMSPathKind::Spline, FVector::ZeroVector, FVector::ZeroVector, SplinePts, 0.0f, Spline);
	for (const FVector& Pt : SplinePts)
	{
		float Best = TNumericLimits<float>::Max();
		for (const FVector& Sample : Spline.Polyline)
		{
			Best = FMath::Min(Best, static_cast<float>(FVector::Dist(Sample, Pt)));
		}
		TestTrue(TEXT("样条经过每个原始路径点"), Best < 0.01f);
	}
	TestTrue(TEXT("样条终点精确"), RMSMath::EvaluateShape(Spline, 1.0f).Equals(FVector(900, 0, 0), 0.01));
	TestTrue(TEXT("样条起点精确"), RMSMath::EvaluateShape(Spline, 0.0f).Equals(FVector::ZeroVector, 0.01));
	return true;
}

/** 解析抛物线：覆盖旧 Parabola/JumpV2 “同高时高度恒为 0”“Segment=0 除零”“终点 Z 被忽略”。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSParabolaTest, "RMS.Parabola", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSParabolaTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("起点高度 0"), RMSMath::EvaluateParabola(120.0f, 0.0f), 0.0f, 0.001f);
	TestEqual(TEXT("终点高度 0"), RMSMath::EvaluateParabola(120.0f, 1.0f), 0.0f, 0.001f);
	TestEqual(TEXT("中点恰为峰值"), RMSMath::EvaluateParabola(120.0f, 0.5f), 120.0f, 0.001f);
	TestEqual(TEXT("关于中点对称"), RMSMath::EvaluateParabola(120.0f, 0.2f), RMSMath::EvaluateParabola(120.0f, 0.8f), 0.001f);
	TestEqual(TEXT("越界进度被钳制"), RMSMath::EvaluateParabola(120.0f, 5.0f), 0.0f, 0.001f);

	// 起终点同高：旧实现高度恒为 0，新实现必须有弧线
	FRMSMotionShape Level;
	RMSMath::BuildShape(ERMSPathKind::Parabola, FVector(0, 0, 100), FVector(1000, 0, 100), {}, 150.0f, Level);
	TestEqual(TEXT("同高时中点高于两端"), RMSMath::EvaluateShape(Level, 0.5f).Z, 250.0, 0.01);

	// 终点高于起点：终点 Z 必须精确，不被忽略
	FRMSMotionShape Up;
	RMSMath::BuildShape(ERMSPathKind::Parabola, FVector(0, 0, 0), FVector(1000, 0, 300), {}, 100.0f, Up);
	TestTrue(TEXT("终点 Z 精确"), RMSMath::EvaluateShape(Up, 1.0f).Equals(FVector(1000, 0, 300), 0.01));
	TestTrue(TEXT("起点精确"), RMSMath::EvaluateShape(Up, 0.0f).Equals(FVector::ZeroVector, 0.01));
	TestEqual(TEXT("中点 Z = 直线中点 + 峰值"), RMSMath::EvaluateShape(Up, 0.5f).Z, 250.0, 0.01);

	// 抛物线自带高度，不得再叠加 Params.ArcHeight 的正弦弧线
	FRMSMotionParams Params;
	Params.Ease = ERMSEase::Linear;
	Params.ArcHeight = 999.0f;
	TestEqual(TEXT("抛物线形状下 Params.ArcHeight 不叠加"), RMSMath::EvaluatePosition(Params, Level, 0.5f).Z, 250.0, 0.01);
	return true;
}

/** 偏移曲线：覆盖旧实现“偏移被二次旋转导致路径扭曲”与“偏移曲线没有序列化导致远端退化成直线”。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSOffsetTest, "RMS.Offset", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSOffsetTest::RunTest(const FString& Parameters)
{
	// 朝向空间约定：X = 前进，Y = 右侧，Z = 世界上方
	// 朝 +X 前进：右侧应为 +Y
	TestTrue(TEXT("朝 +X：前进偏移沿 +X"), RMSMath::OffsetToWorld(FVector(10, 0, 0), FVector::ZeroVector, FVector(1000, 0, 0)).Equals(FVector(10, 0, 0), 0.001));
	const FVector RightWhenFacingX = RMSMath::OffsetToWorld(FVector(0, 10, 0), FVector::ZeroVector, FVector(1000, 0, 0));
	// 引擎对“角色朝向 +X 时的右侧”的定义是权威：必须与 FRotationMatrix 的 Y 轴一致
	TestTrue(TEXT("朝 +X：Y 偏移方向与引擎的右向量一致"), RightWhenFacingX.GetSafeNormal().Equals(FRotationMatrix(FRotator::ZeroRotator).GetScaledAxis(EAxis::Y), 0.001));
	const FVector RightWhenFacingY = RMSMath::OffsetToWorld(FVector(0, 10, 0), FVector::ZeroVector, FVector(0, 1000, 0));
	TestTrue(TEXT("朝 +Y：Y 偏移方向与引擎的右向量一致"), RightWhenFacingY.GetSafeNormal().Equals(FRotationMatrix(FRotator(0, 90, 0)).GetScaledAxis(EAxis::Y), 0.001));
	TestEqual(TEXT("朝 +X：Y 偏移在水平面内且垂直于前进方向"), static_cast<float>(FVector::DotProduct(RightWhenFacingX, FVector(1, 0, 0))), 0.0f, 0.001f);
	TestEqual(TEXT("朝 +X：Y 偏移长度保持"), static_cast<float>(RightWhenFacingX.Size()), 10.0f, 0.001f);
	TestEqual(TEXT("朝 +X：Y 偏移不进入 Z"), static_cast<float>(RightWhenFacingX.Z), 0.0f, 0.001f);

	// Z 偏移始终是世界上方，与朝向无关
	TestTrue(TEXT("Z 偏移与朝向无关"), RMSMath::OffsetToWorld(FVector(0, 0, 25), FVector::ZeroVector, FVector(0, 1000, 0)).Equals(FVector(0, 0, 25), 0.001));

	// 旋转一致性：同一偏移在朝 +X 与朝 +Y 时，应是同一向量绕 Z 轴转 90 度，且只转一次
	const FVector AlongX = RMSMath::OffsetToWorld(FVector(10, 4, 7), FVector::ZeroVector, FVector(1000, 0, 0));
	const FVector AlongY = RMSMath::OffsetToWorld(FVector(10, 4, 7), FVector::ZeroVector, FVector(0, 1000, 0));
	const FVector RotatedOnce = FRotator(0, 90, 0).RotateVector(AlongX);
	TestTrue(TEXT("朝向变化 90 度，偏移只旋转一次（不得二次旋转）"), RotatedOnce.Equals(AlongY, 0.01));

	// 起终点水平投影重合（纯垂直运动）：退化为世界 X，不得产生 NaN
	const FVector Vertical = RMSMath::OffsetToWorld(FVector(10, 4, 7), FVector(5, 5, 0), FVector(5, 5, 500));
	TestFalse(TEXT("纯垂直运动不产生 NaN"), Vertical.ContainsNaN());
	TestTrue(TEXT("纯垂直运动退化为世界 X 前进"), Vertical.Equals(RMSMath::OffsetToWorld(FVector(10, 4, 7), FVector::ZeroVector, FVector(1000, 0, 0)), 0.001));

	// 空曲线：无偏移
	TestTrue(TEXT("空曲线偏移为零"), RMSMath::EvaluateOffset(nullptr, 0.5f, FVector::ZeroVector, FVector(1000, 0, 0)).IsZero());

	// 带真实曲线：偏移叠加到路径上，并且起终点不受影响
	UCurveVector* Curve = NewObject<UCurveVector>(GetTransientPackage());
	Curve->FloatCurves[1].AddKey(0.0f, 0.0f);
	Curve->FloatCurves[1].AddKey(0.5f, 100.0f);
	Curve->FloatCurves[1].AddKey(1.0f, 0.0f);
	FRMSMotionParams Params;
	Params.Ease = ERMSEase::Linear;
	Params.OffsetCurve = Curve;
	FRMSMotionShape Shape;
	RMSMath::BuildShape(ERMSPathKind::Straight, FVector::ZeroVector, FVector(1000, 0, 0), {}, 0.0f, Shape);
	TestTrue(TEXT("曲线偏移：起点不受影响"), RMSMath::EvaluatePosition(Params, Shape, 0.0f).Equals(FVector::ZeroVector, 0.01));
	TestTrue(TEXT("曲线偏移：终点不受影响"), RMSMath::EvaluatePosition(Params, Shape, 1.0f).Equals(FVector(1000, 0, 0), 0.01));
	const FVector MidPos = RMSMath::EvaluatePosition(Params, Shape, 0.5f);
	TestEqual(TEXT("曲线偏移：中点仍在前进方向 500"), static_cast<float>(MidPos.X), 500.0f, 0.01f);
	TestEqual(TEXT("曲线偏移：中点侧向偏移 100"), static_cast<float>(FMath::Abs(MidPos.Y)), 100.0f, 0.01f);

	// 偏移同样作用于多点路径与抛物线
	FRMSMotionShape Poly;
	RMSMath::BuildShape(ERMSPathKind::Polyline, FVector::ZeroVector, FVector::ZeroVector, { FVector(500, 0, 0), FVector(1000, 0, 0) }, 0.0f, Poly);
	TestEqual(TEXT("多点路径叠加偏移"), static_cast<float>(FMath::Abs(RMSMath::EvaluatePosition(Params, Poly, 0.5f).Y)), 100.0f, 0.01f);
	return true;
}

/** 动画回放的时间与空间约定：覆盖旧 SimpleAnimation “时间公式错”“Rate 除零”“终点 -1 被当 0”。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSAnimTimeTest, "RMS.AnimTime", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSAnimTimeTest::RunTest(const FString& Parameters)
{
	// 区间解析：-1 表示到动画末尾，不是 0
	float Start = 0.0f, End = 0.0f;
	RMSMath::ResolveAnimRange(2.0f, 0.0f, -1.0f, Start, End);
	TestEqual(TEXT("终点 -1 解析为动画长度"), End, 2.0f, 0.0001f);
	RMSMath::ResolveAnimRange(2.0f, 0.5f, 0.0f, Start, End);
	TestEqual(TEXT("终点 0 同样解析为动画长度"), End, 2.0f, 0.0001f);
	RMSMath::ResolveAnimRange(2.0f, 0.5f, 99.0f, Start, End);
	TestEqual(TEXT("终点超出动画长度被钳制"), End, 2.0f, 0.0001f);
	RMSMath::ResolveAnimRange(2.0f, 5.0f, -1.0f, Start, End);
	TestTrue(TEXT("起点超出长度：区间退化为长度 0，不出现负区间"), End >= Start && Start <= 2.0f);
	RMSMath::ResolveAnimRange(2.0f, 1.5f, 1.0f, Start, End);
	TestTrue(TEXT("终点早于起点：钳到起点，区间长度 0"), FMath::IsNearlyEqual(End, Start, 0.0001f));

	// 时间公式：动画时间 = 起点 + 真实时间 * Rate（旧实现为 (起点 + 真实时间) * Rate）
	TestEqual(TEXT("起点 1.0 速率 2.0：真实 0.25 秒 → 动画 1.5"), RMSMath::RealTimeToAnimTime(1.0f, 3.0f, 2.0f, 0.25f), 1.5f, 0.0001f);
	TestEqual(TEXT("速率 1.0 起点 0：真实时间即动画时间"), RMSMath::RealTimeToAnimTime(0.0f, 3.0f, 1.0f, 0.7f), 0.7f, 0.0001f);
	TestEqual(TEXT("超过终点被钳制"), RMSMath::RealTimeToAnimTime(0.0f, 1.0f, 1.0f, 5.0f), 1.0f, 0.0001f);
	TestEqual(TEXT("负真实时间被钳制到起点"), RMSMath::RealTimeToAnimTime(0.5f, 1.0f, 1.0f, -3.0f), 0.5f, 0.0001f);

	// 速率为 0 或负数：被钳制，绝不除零
	TestEqual(TEXT("速率 0 被钳制"), RMSMath::ClampRate(0.0f), RMSMath::MinRate, 0.0001f);
	TestEqual(TEXT("速率负数被钳制"), RMSMath::ClampRate(-2.0f), RMSMath::MinRate, 0.0001f);
	const float DurationAtZeroRate = RMSMath::AnimRealDuration(0.0f, 1.0f, 0.0f);
	TestTrue(TEXT("速率 0 时的真实时长有限"), FMath::IsFinite(DurationAtZeroRate) && DurationAtZeroRate > 0.0f);
	TestEqual(TEXT("速率 2.0 真实时长减半"), RMSMath::AnimRealDuration(0.0f, 1.0f, 2.0f), 0.5f, 0.0001f);
	TestEqual(TEXT("区间长度 0 的真实时长取下限，不为 0"), RMSMath::AnimRealDuration(1.0f, 1.0f, 1.0f), RMSMath::MinDuration, 0.0001f);

	// 播放结束时的动画时间恰为区间终点：总位移不丢段
	const float EndAnim = RMSMath::RealTimeToAnimTime(0.5f, 1.5f, 2.0f, RMSMath::AnimRealDuration(0.5f, 1.5f, 2.0f));
	TestEqual(TEXT("播完真实时长后动画时间恰到终点"), EndAnim, 1.5f, 0.0001f);

	// 空间：组件空间位移按 Apply 时的旋转转到世界
	const FQuat Yaw90 = FRotator(0, 90, 0).Quaternion();
	TestTrue(TEXT("朝向 +Y 的角色，动画向前 100 → 世界 +Y 100"), RMSMath::AnimDeltaToWorld(FVector(100, 0, 0), Yaw90).Equals(FVector(0, 100, 0), 0.01));
	TestTrue(TEXT("忽略 Z"), RMSMath::FilterAnimTranslation(FVector(1, 2, 3), true).Equals(FVector(1, 2, 0), 0.0001));
	TestTrue(TEXT("不忽略 Z 时原样"), RMSMath::FilterAnimTranslation(FVector(1, 2, 3), false).Equals(FVector(1, 2, 3), 0.0001));

	// 序列化：动画区间、旋转与速率往返一致；动画资产为空时不要求 PackageMap
	FRMSMotionSource Source;
	Source.StartLocation = FVector(10, 20, 30);
	Source.TargetLocation = FVector(10, 20, 30);
	Source.Duration = 0.5f;
	Source.Params.Duration = 0.5f;
	Source.AnimRangeStart = 0.25f;
	Source.AnimRangeEnd = 1.75f;
	Source.AnimComponentRotation = Yaw90;
	Source.Anim.Rate = 1.5f;
	Source.Anim.bIgnoreZ = true;
	FNetBitWriter Writer(nullptr, 1024 * 8);
	bool bOk = false;
	Source.NetSerialize(Writer, nullptr, bOk);
	TestTrue(TEXT("无动画资产时序列化成功"), bOk);
	FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
	FRMSMotionSource Loaded;
	bool bLoadOk = false;
	Loaded.NetSerialize(Reader, nullptr, bLoadOk);
	TestTrue(TEXT("无动画资产时读取成功"), bLoadOk);
	TestEqual(TEXT("动画起点一致"), Loaded.AnimRangeStart, 0.25f, 0.0001f);
	TestEqual(TEXT("动画终点一致"), Loaded.AnimRangeEnd, 1.75f, 0.0001f);
	TestEqual(TEXT("速率一致"), Loaded.Anim.Rate, 1.5f, 0.0001f);
	TestTrue(TEXT("忽略 Z 标志一致"), Loaded.Anim.bIgnoreZ);
	TestTrue(TEXT("组件旋转一致"), Loaded.AnimComponentRotation.Equals(Yaw90, 0.0001));

	// Matches 对每个动画字段单改敏感
	auto ExpectAnimDifferent = [&](const TCHAR* What, TFunctionRef<void(FRMSMotionSource&)> Mutate)
	{
		FRMSMotionSource Other = Source;
		Mutate(Other);
		TestFalse(What, Source.Matches(&Other));
	};
	ExpectAnimDifferent(TEXT("只改速率"), [](FRMSMotionSource& S) { S.Anim.Rate = 2.5f; });
	ExpectAnimDifferent(TEXT("只改忽略 Z"), [](FRMSMotionSource& S) { S.Anim.bIgnoreZ = false; });
	ExpectAnimDifferent(TEXT("只改动画起点"), [](FRMSMotionSource& S) { S.AnimRangeStart = 0.5f; });
	ExpectAnimDifferent(TEXT("只改动画终点"), [](FRMSMotionSource& S) { S.AnimRangeEnd = 1.0f; });
	ExpectAnimDifferent(TEXT("只改组件旋转"), [](FRMSMotionSource& S) { S.AnimComponentRotation = FQuat::Identity; });
	return true;
}

/**
 * 跟随目标：覆盖旧 MoveToActor / UpdateDynamicMoveToTarget 的缺陷——
 * 目标更新重置起点和剩余时长、Matches 因目标位置微小差异误判、目标 Actor 未解析时被当成普通运动。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSFollowTest, "RMS.Follow", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSFollowTest::RunTest(const FString& Parameters)
{
	auto MakeFollow = []()
	{
		FRMSMotionSource S;
		S.StartLocation = FVector(0, 0, 0);
		S.TargetLocation = FVector(1000, 0, 0);
		S.Duration = 1.0f;
		S.Params.Duration = 1.0f;
		S.Params.Ease = ERMSEase::Linear;
		S.bIsFollowing = true;
		S.Follow.WorldOffset = FVector(-100, 0, 0);
		return S;
	};

	// 目标位置变化不得让两个同源 Source 判为不同（旧实现因此被服务器校正丢弃）
	FRMSMotionSource Server = MakeFollow();
	FRMSMotionSource Client = MakeFollow();
	Server.TargetLocation = FVector(1000, 0, 0);
	Client.TargetLocation = FVector(1013, 4, 0);
	TestTrue(TEXT("跟随运动：两端目标位置存在微小差异时仍判为同一个 Source"), Server.Matches(&Client));

	// 但目标 Actor 身份、偏移、丢失策略任一不同，必须判为不同
	auto ExpectDifferent = [&](const TCHAR* What, TFunctionRef<void(FRMSMotionSource&)> Mutate)
	{
		FRMSMotionSource Other = MakeFollow();
		Mutate(Other);
		TestFalse(What, Server.Matches(&Other));
	};
	ExpectDifferent(TEXT("只改世界偏移"), [](FRMSMotionSource& S) { S.Follow.WorldOffset = FVector(-50, 0, 0); });
	ExpectDifferent(TEXT("只改丢失策略"), [](FRMSMotionSource& S) { S.Follow.LostPolicy = ERMSFollowLostPolicy::End; });
	ExpectDifferent(TEXT("跟随运动与普通运动不同"), [](FRMSMotionSource& S) { S.bIsFollowing = false; });

	// 普通运动仍然比较目标位置
	FRMSMotionSource PlainA = MakeFollow();
	FRMSMotionSource PlainB = MakeFollow();
	PlainA.bIsFollowing = false;
	PlainB.bIsFollowing = false;
	PlainB.TargetLocation.X += 50.0;
	TestFalse(TEXT("普通运动：目标位置不同仍判为不同"), PlainA.Matches(&PlainB));

	// UpdateStateFrom：权威端的目标位置同步过来；起点与剩余时长不被改写
	FRMSMotionSource Local = MakeFollow();
	Local.SetTime(0.4f);
	FRMSMotionSource Authoritative = MakeFollow();
	Authoritative.SetTime(0.4f);
	Authoritative.TargetLocation = FVector(1500, 200, 0);
	Authoritative.bFollowTargetLost = true;
	TestTrue(TEXT("UpdateStateFrom 成功"), Local.UpdateStateFrom(&Authoritative, false));
	TestTrue(TEXT("目标位置从权威端同步"), Local.TargetLocation.Equals(FVector(1500, 200, 0), 0.01));
	TestTrue(TEXT("丢失标志从权威端同步"), Local.bFollowTargetLost);
	TestTrue(TEXT("起点不被重置"), Local.StartLocation.Equals(FVector::ZeroVector, 0.01));
	TestEqual(TEXT("总时长不被改写"), Local.Duration, 1.0f, 0.0001f);
	TestEqual(TEXT("已播放时间不被重置"), Local.GetTime(), 0.4f, 0.0001f);

	// 非跟随运动不应被同步改写目标
	FRMSMotionSource PlainLocal = MakeFollow();
	PlainLocal.bIsFollowing = false;
	FRMSMotionSource PlainAuth = MakeFollow();
	PlainAuth.bIsFollowing = false;
	PlainAuth.TargetLocation = FVector(9999, 0, 0);
	PlainLocal.UpdateStateFrom(&PlainAuth, false);
	TestTrue(TEXT("非跟随运动：UpdateStateFrom 不改目标"), PlainLocal.TargetLocation.Equals(FVector(1000, 0, 0), 0.01));

	// 目标 Actor 在本端尚未解析（指针为空）时不当作丢失，也不放弃运动
	FRMSMotionSource Unresolved = MakeFollow();
	Unresolved.Follow.TargetActor = nullptr;
	TestTrue(TEXT("目标未解析：RefreshFollowTarget 返回 true，保留权威位置"), Unresolved.RefreshFollowTarget());
	TestFalse(TEXT("目标未解析：不被标记为丢失"), Unresolved.bFollowTargetLost);
	TestTrue(TEXT("目标未解析：目标位置保持不变"), Unresolved.TargetLocation.Equals(FVector(1000, 0, 0), 0.01));

	// 序列化往返：跟随标志、偏移、策略与丢失标志一致（目标 Actor 为空时不需要 PackageMap）
	FRMSMotionSource Source = MakeFollow();
	Source.Follow.LostPolicy = ERMSFollowLostPolicy::End;
	Source.bFollowTargetLost = true;
	FNetBitWriter Writer(nullptr, 1024 * 8);
	bool bOk = false;
	// bIsFollowing 为 true 且目标 Actor 为空：序列化需要 PackageMap，缺失时必须明确失败而不是崩溃
	AddExpectedError(TEXT("缺少 PackageMap，无法序列化跟随目标"), EAutomationExpectedErrorFlags::Contains, 1);
	Source.NetSerialize(Writer, nullptr, bOk);
	TestFalse(TEXT("跟随运动缺少 PackageMap 时明确失败"), bOk);
	return true;
}

/**
 * 动画 Warping 的数学核心：T(剩余动画位移) = 当前位置到目标。
 * 覆盖旧实现 FTransform 乘法顺序反、缩放无限幅、剩余位移为 0 时除零、目标在身后时负缩放。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSWarpTest, "RMS.Warp", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSWarpTest::RunTest(const FString& Parameters)
{
	const RMSWarp::FLimits Limits;

	// 同方向放大：动画前进 100，目标在 250 → 缩放 2.5
	{
		const RMSWarp::FSimilarity T = RMSWarp::Solve(FVector(100, 0, 0), FVector(250, 0, 0), Limits);
		TestTrue(TEXT("同方向放大：T(R) = D"), T.Apply(FVector(100, 0, 0)).Equals(FVector(250, 0, 0), 0.01));
		TestEqual(TEXT("同方向放大：缩放系数 2.5"), T.Scale, 2.5f, 0.0001f);
	}

	// 方向不同：动画朝 +X，目标朝 +Y → 旋转 90 度
	{
		const RMSWarp::FSimilarity T = RMSWarp::Solve(FVector(100, 0, 0), FVector(0, 100, 0), Limits);
		TestTrue(TEXT("旋转 90 度：T(R) = D"), T.Apply(FVector(100, 0, 0)).Equals(FVector(0, 100, 0), 0.01));
		// 动画中途的一半位移也应被同样旋转
		TestTrue(TEXT("旋转 90 度：一半位移同样旋转"), T.Apply(FVector(50, 0, 0)).Equals(FVector(0, 50, 0), 0.01));
	}

	// 目标在身后 180 度，但限幅允许：旋转 180 度，缩放为正，不出现负缩放
	{
		const RMSWarp::FSimilarity T = RMSWarp::Solve(FVector(100, 0, 0), FVector(-200, 0, 0), Limits);
		TestTrue(TEXT("目标在身后：缩放系数为正"), T.Scale > 0.0f);
		TestTrue(TEXT("目标在身后：T(R) = D"), T.Apply(FVector(100, 0, 0)).Equals(FVector(-200, 0, 0), 0.05));
	}

	// 任意水平方向与长度，T(R) = D 恒成立（在限幅内）
	for (const float Angle : { 0.0f, 33.0f, 90.0f, 135.0f, -70.0f, 179.0f, -179.0f })
	{
		const FVector R = FRotator(0, 17, 0).RotateVector(FVector(120, 0, 0));
		const FVector D = FRotator(0, Angle, 0).RotateVector(FVector(300, 0, 0));
		const RMSWarp::FSimilarity T = RMSWarp::Solve(R, D, Limits);
		TestTrue(*FString::Printf(TEXT("任意水平角 %.0f：T(R) = D"), Angle), T.Apply(R).Equals(D, 0.05));
	}

	// 带高度的位移：R 与 D 的俯仰角相同时 T(R)=D 成立（等比缩放保持俯仰）
	{
		const FVector R(100, 0, 50);
		const FVector D(0, 200, 100);
		const RMSWarp::FSimilarity T = RMSWarp::Solve(R, D, Limits);
		TestTrue(TEXT("相同俯仰角：T(R) = D"), T.Apply(R).Equals(D, 0.05));
	}

	// 已知边界：俯仰角不同时，相似变换只能保证长度与水平方向，Z 分量对不上。
	// 这里把边界固定成测试，避免日后被误当成 bug 或被悄悄改变语义
	{
		const FVector R(100, 0, 0);
		const FVector D(100, 0, 100);
		const RMSWarp::FSimilarity T = RMSWarp::Solve(R, D, Limits);
		const FVector Out = T.Apply(R);
		TestFalse(TEXT("已知边界：俯仰角不同时 T(R) 不等于 D"), Out.Equals(D, 1.0));
		TestEqual(TEXT("已知边界：长度仍与目标一致（在限幅内）"), static_cast<float>(Out.Size()), static_cast<float>(D.Size()), 0.05f);
	}

	// 缩放限幅：目标过远不会无限放大
	{
		const RMSWarp::FSimilarity T = RMSWarp::Solve(FVector(100, 0, 0), FVector(100000, 0, 0), Limits);
		TestEqual(TEXT("缩放被限幅到上限"), T.Scale, Limits.MaxScale, 0.0001f);
		const RMSWarp::FSimilarity Tiny = RMSWarp::Solve(FVector(100, 0, 0), FVector(0.5, 0, 0), Limits);
		TestEqual(TEXT("缩放被限幅到下限"), Tiny.Scale, Limits.MinScale, 0.0001f);
	}

	// 退化：剩余动画位移为 0 → 恒等变换，不除零，不产生 NaN
	{
		const RMSWarp::FSimilarity T = RMSWarp::Solve(FVector::ZeroVector, FVector(300, 0, 0), Limits);
		TestTrue(TEXT("剩余位移为 0：恒等变换"), T.bIdentity);
		TestFalse(TEXT("剩余位移为 0：无 NaN"), T.Apply(FVector(10, 20, 30)).ContainsNaN());
		TestTrue(TEXT("剩余位移为 0：原样通过"), T.Apply(FVector(10, 20, 30)).Equals(FVector(10, 20, 30), 0.0001));

		const RMSWarp::FSimilarity NearZero = RMSWarp::Solve(FVector(0.3, 0, 0), FVector(300, 0, 0), Limits);
		TestTrue(TEXT("剩余位移接近 0：恒等变换"), NearZero.bIdentity);
	}

	// 目标已经到达（D = 0）：缩放压到下限，不为 0，不反向
	{
		const RMSWarp::FSimilarity T = RMSWarp::Solve(FVector(100, 0, 0), FVector::ZeroVector, Limits);
		TestEqual(TEXT("已到达：缩放为下限"), T.Scale, Limits.MinScale, 0.0001f);
		TestTrue(TEXT("已到达：位移方向不反向"), T.Apply(FVector(100, 0, 0)).X >= 0.0);
	}

	// 纯垂直剩余位移：没有水平方向，不旋转
	{
		const RMSWarp::FSimilarity T = RMSWarp::Solve(FVector(0, 0, 100), FVector(0, 0, 250), Limits);
		TestEqual(TEXT("纯垂直：不旋转"), T.YawRadians, 0.0f, 0.0001f);
		TestTrue(TEXT("纯垂直：T(R) = D"), T.Apply(FVector(0, 0, 100)).Equals(FVector(0, 0, 250), 0.05));
	}

	// 逐帧累加：把剩余位移分成若干帧分别变换，总和等于 T(R)（线性性，保证分帧不丢位移）
	{
		const FVector R(300, 0, 0);
		const FVector D(0, 450, 0);
		const RMSWarp::FSimilarity T = RMSWarp::Solve(R, D, Limits);
		FVector Sum = FVector::ZeroVector;
		for (int32 Frame = 0; Frame < 6; ++Frame)
		{
			Sum += RMSWarp::WarpDelta(FVector(50, 0, 0), T);
		}
		TestTrue(TEXT("分 6 帧累加等于整体变换"), Sum.Equals(D, 0.05));
	}
	return true;
}

/** 朝向输出必须是增量：引擎按“输出 * 当前朝向”合成。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSFacingTest, "RMS.Facing", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSFacingTest::RunTest(const FString& Parameters)
{
	// 当前朝向 +X（偏航 0），运动朝 +Y：应转 90 度
	const FQuat Delta = RMSMath::ComputeFacingDelta(FQuat::Identity, FVector(0, 500, 0));
	TestEqual(TEXT("朝 +Y 需要转 90 度"), Delta.Rotator().Yaw, 90.0, 0.01);

	// 已经朝向运动方向时增量为单位旋转，不能再继续转
	const FQuat Facing90 = FRotator(0, 90, 0).Quaternion();
	const FQuat NoTurn = RMSMath::ComputeFacingDelta(Facing90, FVector(0, 500, 0));
	TestTrue(TEXT("已朝向运动方向时增量为 0"), FMath::IsNearlyZero(NoTurn.Rotator().Yaw, 0.01));

	// 模拟引擎合成：连续多帧后朝向收敛，而不是持续空转
	FQuat Actor = FQuat::Identity;
	for (int32 Frame = 0; Frame < 5; ++Frame)
	{
		Actor = RMSMath::ComputeFacingDelta(Actor, FVector(0, 500, 0)) * Actor;
	}
	TestEqual(TEXT("多帧合成后稳定在 90 度"), Actor.Rotator().Yaw, 90.0, 0.01);

	// 速度过小不转动
	TestTrue(TEXT("几乎不动时不转"), RMSMath::ComputeFacingDelta(FQuat::Identity, FVector(0.1, 0.1, 0)).IsIdentity());
	// 跨 180 度走最短路径
	const FQuat Wrap = RMSMath::ComputeFacingDelta(FRotator(0, 170, 0).Quaternion(), FVector(-500, -100, 0));
	TestTrue(TEXT("跨 180 度取最短路径，转角不超过 180"), FMath::Abs(Wrap.Rotator().Yaw) <= 180.0);
	return true;
}

/** 到达模式的核心语义：被挡住之后，强制模式会找补，逐帧模式不会。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSReachModeTest, "RMS.ReachMode", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSReachModeTest::RunTest(const FString& Parameters)
{
	FRMSMotionParams Params;
	Params.Ease = ERMSEase::Linear;
	const FVector Start(0, 0, 0);
	const FVector Target(1000, 0, 0);
	const float Duration = 1.0f;
	const float Step = 0.1f;
	FRMSMotionShape Shape;
	RMSMath::BuildShape(ERMSPathKind::Straight, Start, Target, {}, 0.0f, Shape);

	// 模拟 10 帧；第 3 到 5 帧角色被墙挡住，实际位置不动
	auto Simulate = [&](ERMSReachMode Mode, FVector& OutFinal)
	{
		Params.ReachMode = Mode;
		FVector Actual = Start;
		for (int32 Frame = 0; Frame < 10; ++Frame)
		{
			const float Time = Step * Frame;
			const FVector Velocity = RMSMath::ComputeVelocity(Params, Shape, Duration, Time, Step, Step, Actual);
			const bool bBlocked = Frame >= 2 && Frame <= 4;
			if (!bBlocked)
			{
				Actual += Velocity * Step;
			}
		}
		OutFinal = Actual;
	};

	FVector GuaranteedFinal, IncrementalFinal;
	Simulate(ERMSReachMode::Guaranteed, GuaranteedFinal);
	Simulate(ERMSReachMode::Incremental, IncrementalFinal);

	TestTrue(TEXT("强制到达：被挡几帧后仍精确到达目标点"), GuaranteedFinal.Equals(Target, 0.01));
	TestTrue(TEXT("逐帧递进：被挡的位移被丢弃，终点落后于目标点"), IncrementalFinal.X < Target.X - 1.0);
	TestTrue(TEXT("逐帧递进：丢弃量等于被挡帧的位移（3 帧 x 100）"), FMath::IsNearlyEqual(IncrementalFinal.X, 700.0, 0.01));

	// 无阻挡时两种模式结果一致
	auto SimulateFree = [&](ERMSReachMode Mode)
	{
		Params.ReachMode = Mode;
		FVector Actual = Start;
		for (int32 Frame = 0; Frame < 10; ++Frame)
		{
			Actual += RMSMath::ComputeVelocity(Params, Shape, Duration, Step * Frame, Step, Step, Actual) * Step;
		}
		return Actual;
	};
	TestTrue(TEXT("无阻挡时强制模式到达目标"), SimulateFree(ERMSReachMode::Guaranteed).Equals(Target, 0.01));
	TestTrue(TEXT("无阻挡时逐帧模式同样到达目标"), SimulateFree(ERMSReachMode::Incremental).Equals(Target, 0.01));

	// 多点路径下两种模式的差异同样成立：被挡后强制模式仍能到达最后一个路径点
	FRMSMotionShape Poly;
	RMSMath::BuildShape(ERMSPathKind::Polyline, Start, FVector::ZeroVector, { FVector(500, 300, 0), FVector(1000, 0, 0) }, 0.0f, Poly);
	auto SimulatePath = [&](ERMSReachMode Mode)
	{
		Params.ReachMode = Mode;
		FVector Actual = Start;
		for (int32 Frame = 0; Frame < 10; ++Frame)
		{
			const FVector Velocity = RMSMath::ComputeVelocity(Params, Poly, Duration, Step * Frame, Step, Step, Actual);
			if (!(Frame >= 3 && Frame <= 4))
			{
				Actual += Velocity * Step;
			}
		}
		return Actual;
	};
	TestTrue(TEXT("折线 + 强制到达：被挡后仍精确到达最后一点"), SimulatePath(ERMSReachMode::Guaranteed).Equals(FVector(1000, 0, 0), 0.05));
	TestFalse(TEXT("折线 + 逐帧递进：被挡后落在最后一点之前"), SimulatePath(ERMSReachMode::Incremental).Equals(FVector(1000, 0, 0), 1.0));

	// 末帧 SimTime 可能小于 StepTime：逐帧模式的速度必须按 StepTime 归一，不能按 SimTime
	Params.ReachMode = ERMSReachMode::Incremental;
	const FVector LastFrame = RMSMath::ComputeVelocity(Params, Shape, Duration, 0.95f, 0.05f, 0.1f, FVector::ZeroVector);
	TestTrue(TEXT("末帧只走半帧位移：速度按 StepTime 归一"), FMath::IsNearlyEqual(LastFrame.X * 0.1, 50.0, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSSourceSerializeTest, "RMS.Source.NetSerializeRoundTrip", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSSourceSerializeTest::RunTest(const FString& Parameters)
{
	FRMSMotionSource Original = MakeSource();
	Original.Priority = 3;
	Original.SetTime(0.2f);

	// 不含自定义曲线时不需要 PackageMap，整个 Source 是纯数据
	FNetBitWriter Writer(nullptr, 1024 * 8);
	bool bWriteOk = false;
	Original.NetSerialize(Writer, nullptr, bWriteOk);
	TestTrue(TEXT("写入成功"), bWriteOk);
	TestFalse(TEXT("写入无错误"), Writer.IsError());

	FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
	FRMSMotionSource Loaded;
	bool bReadOk = false;
	Loaded.NetSerialize(Reader, nullptr, bReadOk);
	TestTrue(TEXT("读取成功"), bReadOk);
	TestFalse(TEXT("读取无错误"), Reader.IsError());

	TestTrue(TEXT("起点一致"), Loaded.StartLocation.Equals(Original.StartLocation, 0.01));
	TestTrue(TEXT("终点一致"), Loaded.TargetLocation.Equals(Original.TargetLocation, 0.01));
	TestEqual(TEXT("基类时长一致"), Loaded.Duration, Original.Duration);
	TestEqual(TEXT("当前时间一致"), Loaded.GetTime(), Original.GetTime());
	TestEqual(TEXT("优先级一致"), Loaded.Priority, Original.Priority);
	TestEqual(TEXT("实例名一致"), Loaded.InstanceName, Original.InstanceName);
	TestEqual(TEXT("Params 时长一致"), Loaded.Params.Duration, Original.Params.Duration);
	TestEqual(TEXT("弧线高度一致"), Loaded.Params.ArcHeight, Original.Params.ArcHeight);
	TestEqual(TEXT("到达模式一致"), static_cast<uint8>(Loaded.Params.ReachMode), static_cast<uint8>(Original.Params.ReachMode));
	TestEqual(TEXT("缓动一致"), static_cast<uint8>(Loaded.Params.Ease), static_cast<uint8>(Original.Params.Ease));
	TestEqual(TEXT("路径种类一致"), static_cast<uint8>(Loaded.Params.PathKind), static_cast<uint8>(Original.Params.PathKind));
	TestEqual(TEXT("路径点数量一致"), Loaded.PathPoints.Num(), Original.PathPoints.Num());
	for (int32 Index = 0; Index < FMath::Min(Loaded.PathPoints.Num(), Original.PathPoints.Num()); ++Index)
	{
		TestTrue(TEXT("路径点逐个一致"), Loaded.PathPoints[Index].Equals(Original.PathPoints[Index], 0.01));
	}
	TestTrue(TEXT("形状终点一致"), Loaded.GetEndpoint().Equals(Original.GetEndpoint(), 0.01));
	TestEqual(TEXT("朝向策略一致"), static_cast<uint8>(Loaded.Params.RotationMode), static_cast<uint8>(Original.Params.RotationMode));
	TestEqual(TEXT("结束速度策略一致"), static_cast<uint8>(Loaded.Params.FinishVelocity), static_cast<uint8>(Original.Params.FinishVelocity));
	TestTrue(TEXT("往返后 Matches"), Loaded.Matches(&Original));

	// 往返后期望轨迹必须逐点一致，这才是两端行为一致的真正保证
	for (const float Time : { 0.0f, 0.1f, 0.25f, 0.4f, 0.5f })
	{
		TestTrue(TEXT("期望轨迹一致"), Loaded.EvaluateExpectedLocation(Time).Equals(Original.EvaluateExpectedLocation(Time), 0.01));
	}

	// 接收端必须拒绝非法的路径点数量，防止恶意包撑爆内存。
	// 手工构造一个包：先写合法的基类字段与两个位置，再把“路径点数量”写成超过上限的值。
	// 包长度完整，因此不会触发引擎 FBitReader 的越界 ensure，只会走本插件自己的校验。
	{
		FRMSMotionSource Base = MakeSource();
		FNetBitWriter HostileWriter(nullptr, 1024 * 8);
		bool bBaseOk = false;
		// 借用基类序列化写出前缀字段
		Base.FRootMotionSource::NetSerialize(HostileWriter, nullptr, bBaseOk);
		FVector StartCopy = Base.StartLocation;
		FVector TargetCopy = Base.TargetLocation;
		HostileWriter << StartCopy;
		HostileWriter << TargetCopy;
		int32 HostileCount = RMSPath::MaxPoints + 1000;
		HostileWriter << HostileCount;

		AddExpectedError(TEXT("路径点数量非法"), EAutomationExpectedErrorFlags::Contains, 1);
		FNetBitReader HostileReader(nullptr, HostileWriter.GetData(), HostileWriter.GetNumBits());
		FRMSMotionSource Victim;
		bool bVictimOk = true;
		const bool bResult = Victim.NetSerialize(HostileReader, nullptr, bVictimOk);
		TestFalse(TEXT("路径点数量超过上限时 NetSerialize 返回失败"), bResult);
		TestFalse(TEXT("bOutSuccess 为 false"), bVictimOk);
		TestEqual(TEXT("失败后不得分配巨量内存"), Victim.PathPoints.Num(), 0);
	}

	// 负数数量同样必须被拒绝
	{
		FRMSMotionSource Base = MakeSource();
		FNetBitWriter NegWriter(nullptr, 1024 * 8);
		bool bBaseOk = false;
		Base.FRootMotionSource::NetSerialize(NegWriter, nullptr, bBaseOk);
		FVector StartCopy = Base.StartLocation;
		FVector TargetCopy = Base.TargetLocation;
		NegWriter << StartCopy;
		NegWriter << TargetCopy;
		int32 NegativeCount = -5;
		NegWriter << NegativeCount;

		AddExpectedError(TEXT("路径点数量非法"), EAutomationExpectedErrorFlags::Contains, 1);
		FNetBitReader NegReader(nullptr, NegWriter.GetData(), NegWriter.GetNumBits());
		FRMSMotionSource Victim;
		bool bVictimOk = true;
		TestFalse(TEXT("负数数量被拒绝"), Victim.NetSerialize(NegReader, nullptr, bVictimOk));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSSourceMatchesTest, "RMS.Source.Matches", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSSourceMatchesTest::RunTest(const FString& Parameters)
{
	const FRMSMotionSource Base = MakeSource();
	TestTrue(TEXT("相同参数匹配"), Base.Matches(&Base));

	auto ExpectDifferent = [&](const TCHAR* What, TFunctionRef<void(FRMSMotionSource&)> Mutate)
	{
		FRMSMotionSource Other = MakeSource();
		Mutate(Other);
		TestFalse(What, Base.Matches(&Other));
	};

	ExpectDifferent(TEXT("只改终点"), [](FRMSMotionSource& S) { S.TargetLocation.X += 50.0; });
	ExpectDifferent(TEXT("只改起点"), [](FRMSMotionSource& S) { S.StartLocation.Z += 50.0; });
	ExpectDifferent(TEXT("只改基类时长"), [](FRMSMotionSource& S) { S.Duration = 1.0f; });
	ExpectDifferent(TEXT("只改 Params 时长"), [](FRMSMotionSource& S) { S.Params.Duration = 1.0f; });
	ExpectDifferent(TEXT("只改到达模式"), [](FRMSMotionSource& S) { S.Params.ReachMode = ERMSReachMode::Guaranteed; });
	ExpectDifferent(TEXT("只改缓动"), [](FRMSMotionSource& S) { S.Params.Ease = ERMSEase::Linear; });
	ExpectDifferent(TEXT("只改弧线高度"), [](FRMSMotionSource& S) { S.Params.ArcHeight = 80.0f; });
	ExpectDifferent(TEXT("只改朝向策略"), [](FRMSMotionSource& S) { S.Params.RotationMode = ERMSRotationMode::None; });
	ExpectDifferent(TEXT("只改结束速度策略"), [](FRMSMotionSource& S) { S.Params.FinishVelocity = ERMSFinishVelocity::Keep; });
	ExpectDifferent(TEXT("设置了偏移曲线"), [](FRMSMotionSource& S) { S.Params.OffsetCurve = NewObject<UCurveVector>(GetTransientPackage()); });
	ExpectDifferent(TEXT("只改路径种类"), [](FRMSMotionSource& S) { S.Params.PathKind = ERMSPathKind::Polyline; });
	ExpectDifferent(TEXT("只改第 1 个路径点"), [](FRMSMotionSource& S) { S.PathPoints[0].X += 40.0; });
	ExpectDifferent(TEXT("只改中间路径点"), [](FRMSMotionSource& S) { S.PathPoints[1].Z += 40.0; });
	ExpectDifferent(TEXT("只改最后一个路径点"), [](FRMSMotionSource& S) { S.PathPoints.Last().Y += 40.0; });
	ExpectDifferent(TEXT("少一个路径点"), [](FRMSMotionSource& S) { S.PathPoints.Pop(); });
	ExpectDifferent(TEXT("多一个路径点"), [](FRMSMotionSource& S) { S.PathPoints.Add(FVector(800, 0, 0)); });

	FRMSMotionSource TinyNoise = MakeSource();
	TinyNoise.TargetLocation.X += 0.01;
	TestTrue(TEXT("低于容差的浮点误差仍视为同一个 Source"), Base.Matches(&TinyNoise));
	return true;
}

#endif
