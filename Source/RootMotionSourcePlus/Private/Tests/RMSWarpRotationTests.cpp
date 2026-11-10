// Copyright JTW Studio 2026. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RMSMotionMath.h"
#include "RMSMotionSource.h"
#include "RMSWarpMath.h"

/** 段级朝向 Warping 的纯数学：进度比例、增量方向、曲线控制完成时机、段末精确收敛。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSWarpRotationTest, "RMS.WarpRotation", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSWarpRotationTest::RunTest(const FString& Parameters)
{
	// 线性（无曲线）：整段 0→1 时间里，每帧消耗的剩余误差比例之和为 1，末帧一次清空
	{
		const float Span = 1.0f;
		float Consumed = 0.0f;
		float Remaining = 1.0f;
		const int32 Steps = 10;
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const float From = Step * Span / Steps;
			const float To = (Step + 1) * Span / Steps;
			const float Fraction = RMSMath::ComputeRotationFraction(nullptr, 0.0f, Span, From, To);
			Consumed += Remaining * Fraction;
			Remaining -= Remaining * Fraction;
		}
		TestEqual(TEXT("线性：全程消耗满 1.0"), Consumed, 1.0f, 0.001f);
		TestEqual(TEXT("线性：末帧后剩余为 0"), Remaining, 0.0f, 0.001f);
	}

	// 线性曲线 = 匀角速度：每帧“转过的绝对角度”相等，尽管“消耗剩余误差的比例”逐帧增大。
	// 这是按比例趋近的必然结果：剩余误差越来越小，要转同样角度就必须消耗更大比例
	{
		const float TotalYaw = 90.0f;
		const int32 Steps = 10;
		FQuat Actor = FQuat::Identity;
		float PreviousYaw = 0.0f;
		float FirstStepDegrees = -1.0f;
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const float From = Step / static_cast<float>(Steps);
			const float To = (Step + 1) / static_cast<float>(Steps);
			const float Fraction = RMSMath::ComputeRotationFraction(nullptr, 0.0f, 1.0f, From, To);
			Actor = RMSMath::ComputeYawRotationDelta(Actor, TotalYaw, Fraction) * Actor;
			const float NowYaw = static_cast<float>(Actor.Rotator().Yaw);
			const float StepDegrees = NowYaw - PreviousYaw;
			if (Step == 0)
			{
				FirstStepDegrees = StepDegrees;
			}
			else if (Step < Steps - 1)
			{
				TestEqual(*FString::Printf(TEXT("线性：第 %d 帧与首帧转角相同"), Step), StepDegrees, FirstStepDegrees, 0.01f);
			}
			PreviousYaw = NowYaw;
		}
		TestEqual(TEXT("线性：末帧精确到位"), static_cast<double>(Actor.Rotator().Yaw), 90.0, 0.05);
		TestEqual(TEXT("线性：每帧转角为总角度的 1/10"), FirstStepDegrees, 9.0f, 0.01f);
	}

	// 段末（To 进度到 1）返回 1：无论此前欠了多少，段结束一定精确到位
	{
		const float Last = RMSMath::ComputeRotationFraction(nullptr, 0.0f, 1.0f, 0.95f, 1.0f);
		TestEqual(TEXT("段末比例恒为 1"), Last, 1.0f, 0.0001f);
	}

	// 增量方向：Fraction=1 时把角色精确转到目标 Yaw
	{
		const FQuat Delta = RMSMath::ComputeYawRotationDelta(FQuat::Identity, 90.0f, 1.0f);
		TestEqual(TEXT("Fraction=1 精确转到目标 Yaw"), static_cast<double>(Delta.Rotator().Yaw), 90.0, 0.01);

		// Fraction=0.5 只转一半
		const FQuat Half = RMSMath::ComputeYawRotationDelta(FQuat::Identity, 90.0f, 0.5f);
		TestEqual(TEXT("Fraction=0.5 转一半"), static_cast<double>(Half.Rotator().Yaw), 45.0, 0.01);

		// Fraction=0 完全不转
		TestTrue(TEXT("Fraction=0 不转"), RMSMath::ComputeYawRotationDelta(FQuat::Identity, 90.0f, 0.0f).IsIdentity());
	}

	// 只绕世界 Z：目标只有 Yaw 时，输出增量不含俯仰/翻滚
	{
		const FQuat Tilted = FRotator(20.0, 0.0, 15.0).Quaternion();
		const FQuat Delta = RMSMath::ComputeYawRotationDelta(Tilted, 90.0f, 1.0f);
		const FRotator DeltaRot = Delta.Rotator();
		TestEqual(TEXT("增量不含俯仰"), DeltaRot.Pitch, 0.0, 0.01);
		TestEqual(TEXT("增量不含翻滚"), DeltaRot.Roll, 0.0, 0.01);
	}

	// 跨 ±180 走最短弧
	{
		const FQuat From179 = FRotator(0.0, 179.0, 0.0).Quaternion();
		const FQuat Delta = RMSMath::ComputeYawRotationDelta(From179, -179.0f, 1.0f);
		TestTrue(TEXT("跨 180 度走最短弧，转角不超过 180"), FMath::Abs(Delta.Rotator().Yaw) <= 180.0);
		// 179 → -179 应该正转 2 度（+2 → 181 == -179）
		TestEqual(TEXT("跨 180 度只转 2 度"), static_cast<double>(Delta.Rotator().Yaw), 2.0, 0.05);
	}

	// 模拟引擎合成：多帧后朝向精确落在目标，且中途被外力偏转后仍能收回
	{
		const float TargetYaw = 90.0f;
		FQuat Actor = FQuat::Identity;
		const int32 Steps = 8;
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const float From = Step / static_cast<float>(Steps);
			const float To = (Step + 1) / static_cast<float>(Steps);
			const float Fraction = RMSMath::ComputeRotationFraction(nullptr, 0.0f, 1.0f, From, To);
			Actor = RMSMath::ComputeYawRotationDelta(Actor, TargetYaw, Fraction) * Actor;
		}
		TestEqual(TEXT("多帧合成后精确落在目标朝向"), static_cast<double>(Actor.Rotator().Yaw), static_cast<double>(TargetYaw), 0.05);

		// 第 3 帧被外力扭偏 40 度，后续帧必须把它收回目标
		FQuat Disturbed = FQuat::Identity;
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const float From = Step / static_cast<float>(Steps);
			const float To = (Step + 1) / static_cast<float>(Steps);
			const float Fraction = RMSMath::ComputeRotationFraction(nullptr, 0.0f, 1.0f, From, To);
			Disturbed = RMSMath::ComputeYawRotationDelta(Disturbed, TargetYaw, Fraction) * Disturbed;
			if (Step == 2)
			{
				Disturbed = FQuat(FVector::UpVector, FMath::DegreesToRadians(40.0f)) * Disturbed;
			}
		}
		TestEqual(TEXT("中途被扭偏后仍收回目标朝向"), static_cast<double>(Disturbed.Rotator().Yaw), static_cast<double>(TargetYaw), 0.05);
	}

	// 跨多段：一帧内跑完两段，最终朝向等于第二段的目标
	{
		const FRMSWarpSegment SegmentA = [] { FRMSWarpSegment S; S.WindowStart = 0.0f; S.WindowEnd = 0.5f; S.bWarpRotation = true; S.TargetRotation = FRotator(0.0, 45.0, 0.0); return S; }();
		const FRMSWarpSegment SegmentB = [] { FRMSWarpSegment S; S.WindowStart = 0.5f; S.WindowEnd = 1.0f; S.bWarpRotation = true; S.TargetRotation = FRotator(0.0, -135.0, 0.0); return S; }();

		FQuat Working = FQuat::Identity;
		FQuat Accumulated = FQuat::Identity;
		for (const FRMSWarpSegment& Segment : { SegmentA, SegmentB })
		{
			const float Fraction = RMSMath::ComputeRotationFraction(Segment.RotationCurve, Segment.WindowStart, Segment.WindowEnd, Segment.WindowStart, Segment.WindowEnd);
			const FQuat Step = RMSMath::ComputeYawRotationDelta(Working, Segment.TargetRotation.Yaw, Fraction);
			Accumulated = Step * Accumulated;
			Working = Step * Working;
		}
		TestEqual(TEXT("一帧跨两段后落到第二段目标"), static_cast<double>(Accumulated.Rotator().Yaw), -135.0, 0.05);
	}

	return true;
}

/** 朝向 Warping 的字段进出序列化与匹配。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSWarpRotationSerializationTest, "RMS.WarpRotationSerialization", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSWarpRotationSerializationTest::RunTest(const FString& Parameters)
{
	auto MakeSource = []
	{
		FRMSMotionSource S;
		S.Duration = 1.0f;
		S.StartLocation = FVector(0, 0, 0);
		S.TargetLocation = FVector(100, 0, 0);
		S.Anim.AnimStart = 0.0f;
		S.Anim.AnimEnd = 1.0f;
		S.Anim.Rate = 1.0f;
		S.AnimRangeStart = 0.0f;
		S.AnimRangeEnd = 1.0f;

		FRMSWarpSegment Rotated;
		Rotated.WindowStart = 0.2f;
		Rotated.WindowEnd = 0.8f;
		Rotated.Target = FVector(300, 100, 0);
		Rotated.bWarp = true;
		Rotated.bWarpRotation = true;
		Rotated.TargetRotation = FRotator(0.0, 135.0, 0.0);
		Rotated.RotationCurve = nullptr;
		S.Warp.Segments.Add(Rotated);
		return S;
	};

	// 往返：旋转开关与目标 Yaw 一致，且往返后 Matches
	{
		const FRMSMotionSource Original = MakeSource();
		FRMSMotionSource Copy = Original;
		FNetBitWriter Writer(nullptr, 1024 * 8);
		bool bOk = false;
		Copy.NetSerialize(Writer, nullptr, bOk);
		TestTrue(TEXT("旋转 Warping：序列化成功"), bOk);

		FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
		FRMSMotionSource Loaded;
		bool bLoadOk = false;
		Loaded.NetSerialize(Reader, nullptr, bLoadOk);
		TestTrue(TEXT("旋转 Warping：读取成功"), bLoadOk);
		TestEqual(TEXT("段数一致"), Loaded.Warp.Segments.Num(), 1);
		TestTrue(TEXT("旋转开关一致"), Loaded.Warp.Segments[0].bWarpRotation);
		TestEqual(TEXT("目标 Yaw 一致"), Loaded.Warp.Segments[0].TargetRotation.Yaw, 135.0, 0.01);
		TestTrue(TEXT("往返后 Matches"), Loaded.Matches(&Original));
	}

	// 只同步 Yaw：俯仰/翻滚不上网，接收端一律清零
	{
		FRMSMotionSource WithPitch = MakeSource();
		WithPitch.Warp.Segments[0].TargetRotation = FRotator(30.0, 135.0, 20.0);
		FRMSMotionSource Copy = WithPitch;
		FNetBitWriter Writer(nullptr, 1024 * 8);
		bool bOk = false;
		Copy.NetSerialize(Writer, nullptr, bOk);
		FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
		FRMSMotionSource Loaded;
		bool bLoadOk = false;
		Loaded.NetSerialize(Reader, nullptr, bLoadOk);
		TestEqual(TEXT("俯仰不上网"), Loaded.Warp.Segments[0].TargetRotation.Pitch, 0.0, 0.01);
		TestEqual(TEXT("翻滚不上网"), Loaded.Warp.Segments[0].TargetRotation.Roll, 0.0, 0.01);
		TestEqual(TEXT("Yaw 正常同步"), Loaded.Warp.Segments[0].TargetRotation.Yaw, 135.0, 0.01);
	}

	// Matches 敏感性：改了旋转开关、目标 Yaw 或曲线都必须判定为不同
	{
		const FRMSMotionSource Original = MakeSource();

		FRMSMotionSource Toggled = Original;
		Toggled.Warp.Segments[0].bWarpRotation = false;
		TestFalse(TEXT("旋转开关变化导致不匹配"), Original.Matches(&Toggled));

		FRMSMotionSource YawChanged = Original;
		YawChanged.Warp.Segments[0].TargetRotation = FRotator(0.0, 45.0, 0.0);
		TestFalse(TEXT("目标 Yaw 变化导致不匹配"), Original.Matches(&YawChanged));

		// bWarpRotation 为 false 时，TargetYaw 与曲线都不参与匹配（该字段本就不生效）
		FRMSMotionSource BothOff = Original;
		BothOff.Warp.Segments[0].bWarpRotation = false;
		BothOff.Warp.Segments[0].TargetRotation = FRotator(0.0, 45.0, 0.0);
		FRMSMotionSource BothOffRef = Original;
		BothOffRef.Warp.Segments[0].bWarpRotation = false;
		TestTrue(TEXT("均未启用旋转时目标 Yaw 不参与匹配"), BothOff.Matches(&BothOffRef));
	}

	// 间隙段必须清掉旋转字段：用户误填的目标朝向不得在间隙里生效
	{
		TArray<FRMSWarpSegment> Input;
		FRMSWarpSegment Rotated;
		Rotated.WindowStart = 0.4f;
		Rotated.WindowEnd = 0.6f;
		Rotated.Target = FVector(100, 0, 0);
		Rotated.bWarp = true;
		Rotated.bWarpRotation = true;
		Rotated.TargetRotation = FRotator(0.0, 90.0, 0.0);
		Input.Add(Rotated);

		TArray<FRMSWarpSegment> Output;
		RMSWarp::NormalizeSegments(Input, 0.0f, 1.0f, Output);
		TestEqual(TEXT("补出前后两个间隙段"), Output.Num(), 3);
		TestFalse(TEXT("首间隙段不转"), Output[0].bWarpRotation);
		TestFalse(TEXT("末间隙段不转"), Output[2].bWarpRotation);
		TestEqual(TEXT("间隙段目标朝向被清零"), Output[0].TargetRotation.Yaw, 0.0, 0.001);
		TestTrue(TEXT("中间段保留旋转"), Output[1].bWarpRotation);
	}

	return true;
}

#endif
