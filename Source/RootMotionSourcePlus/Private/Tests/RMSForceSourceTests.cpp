// Copyright JTW Studio 2026. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RMSMotionSource.h"
#include "RMSMotionMath.h"
#include "RMSPathMath.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"

namespace
{
	FRMSMotionSource MakeForceSource(ERMSForceKind Kind)
	{
		FRMSMotionSource S;
		S.Duration = 2.0f;
		S.Params.Duration = 2.0f;
		S.Force.Kind = Kind;
		S.Force.Direction = FVector(0, 1, 0);
		S.Force.Strength = 450.0f;
		S.Force.StopDistance = 120.0f;
		S.Force.bIgnoreZ = true;
		S.PullLastKnownTarget = FVector(800, 100, 0);
		S.bPullHasTarget = Kind == ERMSForceKind::Pull;
		return S;
	}
}

/** 力在 Source 层：序列化、Matches、无限时长与非法包。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSForceSourceTest, "RMS.ForceSource", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSForceSourceTest::RunTest(const FString& Parameters)
{
	// 序列化往返：恒力与牵引各一遍
	for (const ERMSForceKind Kind : { ERMSForceKind::Constant, ERMSForceKind::Pull })
	{
		const FRMSMotionSource Original = MakeForceSource(Kind);
		FRMSMotionSource Copy = Original;
		FNetBitWriter Writer(nullptr, 1024 * 8);
		bool bOk = false;
		// 牵引目标与距离曲线都为空，不需要 PackageMap
		Copy.NetSerialize(Writer, nullptr, bOk);
		TestTrue(TEXT("力 Source 序列化成功"), bOk);

		FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
		FRMSMotionSource Loaded;
		bool bLoadOk = false;
		Loaded.NetSerialize(Reader, nullptr, bLoadOk);
		TestTrue(TEXT("力 Source 读取成功"), bLoadOk);
		TestEqual(TEXT("力种类一致"), static_cast<uint8>(Loaded.Force.Kind), static_cast<uint8>(Kind));
		TestTrue(TEXT("方向一致"), Loaded.Force.Direction.Equals(Original.Force.Direction, 0.001));
		TestEqual(TEXT("强度一致"), Loaded.Force.Strength, Original.Force.Strength, 0.001f);
		TestEqual(TEXT("停止距离一致"), Loaded.Force.StopDistance, Original.Force.StopDistance, 0.001f);
		TestEqual(TEXT("忽略 Z 一致"), Loaded.Force.bIgnoreZ, Original.Force.bIgnoreZ);
		TestTrue(TEXT("牵引最后已知目标一致"), Loaded.PullLastKnownTarget.Equals(Original.PullLastKnownTarget, 0.01));
		TestEqual(TEXT("是否有牵引目标一致"), Loaded.bPullHasTarget, Original.bPullHasTarget);
		TestTrue(TEXT("往返后 Matches"), Loaded.Matches(&Original));
	}

	// Matches：每个力字段单改敏感
	const FRMSMotionSource Base = MakeForceSource(ERMSForceKind::Pull);
	auto ExpectDifferent = [&](const TCHAR* What, TFunctionRef<void(FRMSMotionSource&)> Mutate)
	{
		FRMSMotionSource Other = MakeForceSource(ERMSForceKind::Pull);
		Mutate(Other);
		TestFalse(What, Base.Matches(&Other));
	};
	ExpectDifferent(TEXT("只改力种类"), [](FRMSMotionSource& S) { S.Force.Kind = ERMSForceKind::Constant; });
	ExpectDifferent(TEXT("只改方向"), [](FRMSMotionSource& S) { S.Force.Direction = FVector(1, 0, 0); });
	ExpectDifferent(TEXT("只改强度"), [](FRMSMotionSource& S) { S.Force.Strength = 900.0f; });
	ExpectDifferent(TEXT("只改停止距离"), [](FRMSMotionSource& S) { S.Force.StopDistance = 300.0f; });
	ExpectDifferent(TEXT("只改忽略 Z"), [](FRMSMotionSource& S) { S.Force.bIgnoreZ = false; });
	ExpectDifferent(TEXT("无限时长与有限时长不同"), [](FRMSMotionSource& S) { S.Duration = -1.0f; S.Params.Duration = -1.0f; });

	// 牵引目标的位置随时间变化，不参与匹配
	FRMSMotionSource TargetMoved = MakeForceSource(ERMSForceKind::Pull);
	TargetMoved.PullLastKnownTarget = FVector(5000, 5000, 0);
	TestTrue(TEXT("牵引：目标位置不同仍判为同一个 Source"), Base.Matches(&TargetMoved));

	// UpdateStateFrom：牵引目标位置从权威端同步，不改其他力字段
	{
		FRMSMotionSource Local = MakeForceSource(ERMSForceKind::Pull);
		FRMSMotionSource Authoritative = MakeForceSource(ERMSForceKind::Pull);
		Authoritative.PullLastKnownTarget = FVector(1234, 56, 0);
		TestTrue(TEXT("UpdateStateFrom 成功"), Local.UpdateStateFrom(&Authoritative, false));
		TestTrue(TEXT("牵引目标位置被同步"), Local.PullLastKnownTarget.Equals(FVector(1234, 56, 0), 0.01));
		TestEqual(TEXT("强度不被改写"), Local.Force.Strength, 450.0f, 0.001f);
	}

	// 无限时长：Duration 为负，引擎不会超时
	{
		FRMSMotionSource Infinite = MakeForceSource(ERMSForceKind::Constant);
		Infinite.Duration = -1.0f;
		TestTrue(TEXT("Duration 为负视为无限时长"), Infinite.IsInfinite());
		TestFalse(TEXT("无限时长：引擎不启用超时"), Infinite.IsTimeOutEnabled());
		Infinite.SetTime(1.0e6f);
		TestFalse(TEXT("无限时长：时间再长也不会 Finished"), Infinite.Status.HasFlag(ERootMotionSourceStatusFlags::Finished));

		FRMSMotionSource Finite = MakeForceSource(ERMSForceKind::Constant);
		TestFalse(TEXT("有限时长不是无限"), Finite.IsInfinite());
		TestTrue(TEXT("有限时长：引擎启用超时"), Finite.IsTimeOutEnabled());
		Finite.SetTime(3.0f);
		TestTrue(TEXT("有限时长：超过时长后 Finished"), Finite.Status.HasFlag(ERootMotionSourceStatusFlags::Finished));

		// 无限时长往返后仍为无限
		FNetBitWriter Writer(nullptr, 1024 * 8);
		bool bOk = false;
		Infinite.NetSerialize(Writer, nullptr, bOk);
		FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
		FRMSMotionSource Loaded;
		bool bLoadOk = false;
		Loaded.NetSerialize(Reader, nullptr, bLoadOk);
		TestTrue(TEXT("无限时长往返后仍为无限"), Loaded.IsInfinite());
	}

	// 非法包：构造“种类字节越界”的包，接收端必须拒绝而不是崩溃
	{
		FRMSMotionSource Base2 = MakeForceSource(ERMSForceKind::Constant);
		FNetBitWriter Hostile(nullptr, 1024 * 8);
		bool bBaseOk = false;
		Base2.FRootMotionSource::NetSerialize(Hostile, nullptr, bBaseOk);
		FVector Start = Base2.StartLocation;
		FVector Target = Base2.TargetLocation;
		Hostile << Start;
		Hostile << Target;
		// 字节顺序必须与 NetSerialize 一致：基类字段、起点、终点、路径点数量、力段……
		// 力段在 Follow 与 Warping 段之前，因此这里不需要写 Warping 段数量
		int32 PointCount = 0;
		Hostile << PointCount;
		// 力段：先写非法的种类字节，后面按序写满，保证包长度完整
		uint8 BadKind = 99, bIgnoreZ = 1, bHasTarget = 0;
		Hostile << BadKind << bIgnoreZ << bHasTarget;
		FVector Dir = FVector::ForwardVector;
		float Strength = 100.0f, Stop = 10.0f;
		FVector Known = FVector::ZeroVector;
		Hostile << Dir << Strength << Stop << Known;
		uint8 bCurve = 0, bPull = 0;
		Hostile << bCurve << bPull;

		AddExpectedError(TEXT("力种类非法"), EAutomationExpectedErrorFlags::Contains, 1);
		FNetBitReader HostileReader(nullptr, Hostile.GetData(), Hostile.GetNumBits());
		FRMSMotionSource Victim;
		bool bVictimOk = true;
		TestFalse(TEXT("力种类越界被拒绝"), Victim.NetSerialize(HostileReader, nullptr, bVictimOk));
	}
	return true;
}

#endif
