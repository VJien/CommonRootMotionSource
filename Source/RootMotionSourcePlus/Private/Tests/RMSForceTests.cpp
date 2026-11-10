// Copyright JTW Studio 2026. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RMSMotionMath.h"
#include "Curves/CurveFloat.h"

/**
 * 力类的纯函数。覆盖旧 LinearConstraint “速度与帧率成反比”“曲线未判空”“目标重合产生 NaN”。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSForceTest, "RMS.Force", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSForceTest::RunTest(const FString& Parameters)
{
	// 恒力：方向被归一，速度 = 方向 × 强度
	{
		const FVector V = RMSMath::ComputeConstantForceVelocity(FVector(3, 4, 0), 500.0f);
		TestTrue(TEXT("恒力：方向归一后乘强度"), V.Equals(FVector(300, 400, 0), 0.01));
		TestEqual(TEXT("恒力：速度大小等于强度"), static_cast<float>(V.Size()), 500.0f, 0.01f);
		TestTrue(TEXT("恒力：方向为零返回零向量"), RMSMath::ComputeConstantForceVelocity(FVector::ZeroVector, 500.0f).IsZero());
		TestFalse(TEXT("恒力：方向为零无 NaN"), RMSMath::ComputeConstantForceVelocity(FVector::ZeroVector, 500.0f).ContainsNaN());
		TestTrue(TEXT("恒力：负强度被钳到 0"), RMSMath::ComputeConstantForceVelocity(FVector(1, 0, 0), -50.0f).IsZero());
		TestEqual(TEXT("恒力：强度被钳到上限"), static_cast<float>(RMSMath::ComputeConstantForceVelocity(FVector(1, 0, 0), 1.0e9f).Size()), RMSMath::MaxForceStrength, 0.1f);
	}

	// 与帧率无关（针对旧实现“每 tick 位移再除 dt”）：同样模拟 1 秒，不同步长下总位移相同
	{
		auto Integrate = [](float Step)
		{
			FVector Position = FVector::ZeroVector;
			const int32 Frames = FMath::RoundToInt(1.0f / Step);
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Position += RMSMath::ComputeConstantForceVelocity(FVector(1, 0, 0), 600.0f) * Step;
			}
			return Position;
		};
		const FVector At60 = Integrate(1.0f / 60.0f);
		const FVector At20 = Integrate(1.0f / 20.0f);
		const FVector At120 = Integrate(1.0f / 120.0f);
		TestEqual(TEXT("恒力：60 帧与 20 帧总位移一致"), static_cast<float>(At60.X), static_cast<float>(At20.X), 0.5f);
		TestEqual(TEXT("恒力：60 帧与 120 帧总位移一致"), static_cast<float>(At60.X), static_cast<float>(At120.X), 0.5f);
		TestEqual(TEXT("恒力：1 秒 600 厘米/秒位移 600"), static_cast<float>(At60.X), 600.0f, 0.5f);
	}

	// 牵引：基础方向与大小
	{
		const FVector V = RMSMath::ComputePullVelocity(FVector::ZeroVector, FVector(1000, 0, 0), 400.0f, 100.0f, true, nullptr);
		TestTrue(TEXT("牵引：朝目标，速度等于强度"), V.Equals(FVector(400, 0, 0), 0.01));
	}

	// 停止距离：边界与内侧为 0
	{
		TestTrue(TEXT("牵引：恰在停止距离上为 0"), RMSMath::ComputePullVelocity(FVector::ZeroVector, FVector(100, 0, 0), 400.0f, 100.0f, true, nullptr).IsZero());
		TestTrue(TEXT("牵引：停止距离内为 0"), RMSMath::ComputePullVelocity(FVector::ZeroVector, FVector(50, 0, 0), 400.0f, 100.0f, true, nullptr).IsZero());
		TestFalse(TEXT("牵引：刚出停止距离有速度"), RMSMath::ComputePullVelocity(FVector::ZeroVector, FVector(100.5, 0, 0), 400.0f, 100.0f, true, nullptr).IsZero());
	}

	// 目标重合：不产生 NaN，停止距离为 0 时也一样
	{
		const FVector Same = RMSMath::ComputePullVelocity(FVector(5, 5, 5), FVector(5, 5, 5), 400.0f, 0.0f, false, nullptr);
		TestTrue(TEXT("牵引：目标重合为 0"), Same.IsZero());
		TestFalse(TEXT("牵引：目标重合无 NaN"), Same.ContainsNaN());
	}

	// 忽略 Z：只取水平方向与水平距离
	{
		const FVector V = RMSMath::ComputePullVelocity(FVector::ZeroVector, FVector(300, 0, 900), 400.0f, 100.0f, true, nullptr);
		TestTrue(TEXT("牵引忽略 Z：速度没有 Z 分量"), FMath::IsNearlyZero(V.Z, 0.0001));
		TestTrue(TEXT("牵引忽略 Z：水平朝目标"), V.Equals(FVector(400, 0, 0), 0.01));

		// 目标正上方：水平距离为 0，不动
		const FVector Above = RMSMath::ComputePullVelocity(FVector::ZeroVector, FVector(0, 0, 900), 400.0f, 100.0f, true, nullptr);
		TestTrue(TEXT("牵引忽略 Z：目标在正上方不动"), Above.IsZero());

		// 不忽略 Z：三维方向
		const FVector Full = RMSMath::ComputePullVelocity(FVector::ZeroVector, FVector(0, 0, 900), 400.0f, 100.0f, false, nullptr);
		TestTrue(TEXT("牵引不忽略 Z：朝正上方"), Full.Equals(FVector(0, 0, 400), 0.01));
	}

	// 距离曲线：倍率随距离变化；曲线值为负被钳到 0；曲线为空等同恒定
	{
		UCurveFloat* Curve = NewObject<UCurveFloat>(GetTransientPackage());
		Curve->FloatCurve.AddKey(0.0f, 0.0f);
		Curve->FloatCurve.AddKey(1000.0f, 1.0f);

		const FVector Near = RMSMath::ComputePullVelocity(FVector::ZeroVector, FVector(250, 0, 0), 400.0f, 100.0f, true, Curve);
		const FVector Far = RMSMath::ComputePullVelocity(FVector::ZeroVector, FVector(1000, 0, 0), 400.0f, 100.0f, true, Curve);
		TestEqual(TEXT("距离曲线：250 处倍率 0.25"), static_cast<float>(Near.X), 100.0f, 0.5f);
		TestEqual(TEXT("距离曲线：1000 处倍率 1"), static_cast<float>(Far.X), 400.0f, 0.5f);
		TestTrue(TEXT("距离曲线：越远越快"), Far.X > Near.X);

		UCurveFloat* Negative = NewObject<UCurveFloat>(GetTransientPackage());
		Negative->FloatCurve.AddKey(0.0f, -5.0f);
		Negative->FloatCurve.AddKey(1000.0f, -5.0f);
		TestTrue(TEXT("距离曲线：负值被钳到 0，不反向"), RMSMath::ComputePullVelocity(FVector::ZeroVector, FVector(500, 0, 0), 400.0f, 100.0f, true, Negative).IsZero());
	}

	// 参数越界被钳制
	{
		TestEqual(TEXT("牵引：强度被钳到上限"), static_cast<float>(RMSMath::ComputePullVelocity(FVector::ZeroVector, FVector(1000, 0, 0), 1.0e9f, 100.0f, true, nullptr).X), RMSMath::MaxForceStrength, 0.1f);
		TestTrue(TEXT("牵引：负强度不反向"), RMSMath::ComputePullVelocity(FVector::ZeroVector, FVector(1000, 0, 0), -400.0f, 100.0f, true, nullptr).IsZero());
		// 负停止距离按 0 处理
		TestFalse(TEXT("牵引：负停止距离按 0 处理"), RMSMath::ComputePullVelocity(FVector::ZeroVector, FVector(10, 0, 0), 400.0f, -50.0f, true, nullptr).IsZero());
	}
	return true;
}

#endif
