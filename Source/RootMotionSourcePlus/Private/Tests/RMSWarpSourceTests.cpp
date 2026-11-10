// Copyright JTW Studio 2026. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RMSMotionSource.h"
#include "RMSPathMath.h"
#include "RMSWarpMath.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"

namespace
{
	FRMSWarpSegment MakeSeg(float Start, float End, const FVector& Target, bool bWarp = true)
	{
		FRMSWarpSegment S;
		S.WindowStart = Start;
		S.WindowEnd = End;
		S.Target = Target;
		S.bWarp = bWarp;
		return S;
	}

	FRMSMotionSource MakeWarpSource()
	{
		FRMSMotionSource S;
		S.Duration = 1.0f;
		S.Params.Duration = 1.0f;
		S.AnimRangeStart = 0.0f;
		S.AnimRangeEnd = 1.0f;
		S.Warp.MinScale = 0.2f;
		S.Warp.MaxScale = 3.0f;
		S.Warp.Segments = { MakeSeg(0.0f, 0.3f, FVector::ZeroVector, false), MakeSeg(0.3f, 0.8f, FVector(400, 100, 0)), MakeSeg(0.8f, 1.0f, FVector::ZeroVector, false) };
		return S;
	}
}

/** Warping 在 Source 层的序列化、匹配和累计行为。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSWarpSourceTest, "RMS.WarpSource", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSWarpSourceTest::RunTest(const FString& Parameters)
{
	// 序列化往返：目标段与缩放限幅逐项一致
	const FRMSMotionSource Original = MakeWarpSource();
	FRMSMotionSource Copy = Original;
	FNetBitWriter Writer(nullptr, 1024 * 8);
	bool bOk = false;
	Copy.NetSerialize(Writer, nullptr, bOk);
	TestTrue(TEXT("Warping Source 序列化成功"), bOk);
	FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
	FRMSMotionSource Loaded;
	bool bLoadOk = false;
	Loaded.NetSerialize(Reader, nullptr, bLoadOk);
	TestTrue(TEXT("Warping Source 读取成功"), bLoadOk);

	if (TestEqual(TEXT("目标段数量一致"), Loaded.Warp.Segments.Num(), Original.Warp.Segments.Num()))
	{
		for (int32 Index = 0; Index < Original.Warp.Segments.Num(); ++Index)
		{
			const FRMSWarpSegment& A = Original.Warp.Segments[Index];
			const FRMSWarpSegment& B = Loaded.Warp.Segments[Index];
			TestEqual(TEXT("窗口起点一致"), B.WindowStart, A.WindowStart, 0.0001f);
			TestEqual(TEXT("窗口终点一致"), B.WindowEnd, A.WindowEnd, 0.0001f);
			TestTrue(TEXT("目标一致"), B.Target.Equals(A.Target, 0.01));
			TestEqual(TEXT("是否 Warping 一致"), B.bWarp, A.bWarp);
		}
	}
	TestEqual(TEXT("缩放下限一致"), Loaded.Warp.MinScale, Original.Warp.MinScale, 0.0001f);
	TestEqual(TEXT("缩放上限一致"), Loaded.Warp.MaxScale, Original.Warp.MaxScale, 0.0001f);
	TestTrue(TEXT("往返后 Matches"), Loaded.Matches(&Original));

	// Matches 对每个 Warping 字段单改敏感（旧实现的 MultiTargets 只发 TriggerDatas，当前段没发）
	auto ExpectDifferent = [&](const TCHAR* What, TFunctionRef<void(FRMSMotionSource&)> Mutate)
	{
		FRMSMotionSource Other = MakeWarpSource();
		Mutate(Other);
		TestFalse(What, Original.Matches(&Other));
	};
	ExpectDifferent(TEXT("只改窗口目标"), [](FRMSMotionSource& S) { S.Warp.Segments[1].Target.X += 40.0; });
	ExpectDifferent(TEXT("只改窗口起点"), [](FRMSMotionSource& S) { S.Warp.Segments[1].WindowStart = 0.35f; });
	ExpectDifferent(TEXT("只改窗口终点"), [](FRMSMotionSource& S) { S.Warp.Segments[1].WindowEnd = 0.7f; });
	ExpectDifferent(TEXT("只改某段是否 Warping"), [](FRMSMotionSource& S) { S.Warp.Segments[1].bWarp = false; });
	ExpectDifferent(TEXT("少一个段"), [](FRMSMotionSource& S) { S.Warp.Segments.Pop(); });
	ExpectDifferent(TEXT("只改缩放上限"), [](FRMSMotionSource& S) { S.Warp.MaxScale = 2.0f; });
	ExpectDifferent(TEXT("只改缩放下限"), [](FRMSMotionSource& S) { S.Warp.MinScale = 0.5f; });

	// 间隙段的目标不参与匹配（它们不 Warping，Target 无意义）
	FRMSMotionSource GapTargetChanged = MakeWarpSource();
	GapTargetChanged.Warp.Segments[0].Target = FVector(9999, 9999, 9999);
	TestTrue(TEXT("间隙段的 Target 不影响匹配"), Original.Matches(&GapTargetChanged));

	// 非法段数被拒绝：手工构造段数超限的包
	{
		FRMSMotionSource Base = MakeWarpSource();
		FNetBitWriter Hostile(nullptr, 1024 * 8);
		bool bBaseOk = false;
		Base.FRootMotionSource::NetSerialize(Hostile, nullptr, bBaseOk);
		FVector Start = Base.StartLocation;
		FVector Target = Base.TargetLocation;
		Hostile << Start;
		Hostile << Target;
		int32 PointCount = 0;
		Hostile << PointCount;
		// P3 起力段位于路径点之后、跟随之前：种类、忽略 Z、牵引目标标志、方向、强度、停止距离、最后已知目标、曲线标志、目标标志
		uint8 ForceKind = 0, bForceIgnoreZ = 1, bPullHasTarget = 0;
		Hostile << ForceKind << bForceIgnoreZ << bPullHasTarget;
		FVector ForceDir = FVector::ForwardVector;
		float ForceStrength = 0.0f, ForceStop = 0.0f;
		FVector PullKnown = FVector::ZeroVector;
		Hostile << ForceDir << ForceStrength << ForceStop << PullKnown;
		uint8 bForceCurve = 0, bPullTarget = 0;
		Hostile << bForceCurve << bPullTarget;
		uint8 bFollow = 0, Policy = 0, bLost = 0;
		Hostile << bFollow << Policy << bLost;
		FVector Offset = FVector::ZeroVector;
		Hostile << Offset;
		int32 HostileSegments = RMSPath::MaxPoints + 500;
		Hostile << HostileSegments;

		AddExpectedError(TEXT("Warping 段数量非法"), EAutomationExpectedErrorFlags::Contains, 1);
		FNetBitReader HostileReader(nullptr, Hostile.GetData(), Hostile.GetNumBits());
		FRMSMotionSource Victim;
		bool bVictimOk = true;
		TestFalse(TEXT("段数超过上限时 NetSerialize 返回失败"), Victim.NetSerialize(HostileReader, nullptr, bVictimOk));
		TestEqual(TEXT("失败后不得分配巨量内存"), Victim.Warp.Segments.Num(), 0);
	}

	// 逐段累计：三段首尾相接，每段的变换只作用于该段的动画位移，
	// 整条走完后总位移 = 各段变换后位移之和，且最后一个 Warping 段把角色带到该段目标
	{
		const FVector Start(0, 0, 0);
		// 目标距间隙末尾约 233，动画自身前进 100，所需缩放约 2.3，在上限 3.0 之内
		const FVector SegmentTarget(300, 100, 0);
		// 假设动画前 0.3 秒原样前进 90，Warping 段 0.3→0.8 动画自身前进 100，之后 0.8→1.0 原样前进 60
		const FVector GapA(90, 0, 0);
		const FVector WarpedAnim(100, 0, 0);
		const FVector GapB(60, 0, 0);

		const FVector AfterGapA = Start + GapA;
		const RMSWarp::FLimits Limits{ 0.2f, 3.0f };
		const RMSWarp::FSimilarity T = RMSWarp::Solve(WarpedAnim, SegmentTarget - AfterGapA, Limits);
		const FVector AfterWarp = AfterGapA + RMSWarp::WarpDelta(WarpedAnim, T);
		TestTrue(TEXT("Warping 段结束时恰好落在该段目标上"), AfterWarp.Equals(SegmentTarget, 0.1));

		const FVector Final = AfterWarp + GapB;
		TestTrue(TEXT("间隙段原样叠加：末段不会被拉回目标或瞬移"), Final.Equals(SegmentTarget + GapB, 0.1));

		// 超出缩放上限时到不了目标：这是有意的（宁可到不了也不过度扭曲动画），固定为测试
		const FVector FarTarget(400, 100, 0);
		const RMSWarp::FSimilarity Clamped = RMSWarp::Solve(WarpedAnim, FarTarget - AfterGapA, Limits);
		TestEqual(TEXT("目标过远：缩放被限幅到上限"), Clamped.Scale, Limits.MaxScale, 0.0001f);
		const FVector Reached = AfterGapA + RMSWarp::WarpDelta(WarpedAnim, Clamped);
		TestFalse(TEXT("目标过远：到不了目标点"), Reached.Equals(FarTarget, 1.0));
		TestTrue(TEXT("目标过远：仍朝目标方向前进，没有反向"), FVector::DotProduct(Reached - AfterGapA, FarTarget - AfterGapA) > 0.0);
	}
	return true;
}

#endif
