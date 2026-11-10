// Copyright JTW Studio 2026. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RMSMotionMath.h"

/** 预设：默认值符合计划 §3.1，退化输入不产生 NaN。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSPresetTest, "RMS.Preset", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSPresetTest::RunTest(const FString& Parameters)
{
	const FVector Start(100, 200, 50);

	// 冲刺：终点 = 起点 + 单位方向 * 距离，方向会被归一
	{
		FRMSMotionParams Params;
		FVector Target;
		RMSMath::PresetDash(Start, FVector(5, 0, 0), 600.0f, 0.3f, Params, Target);
		TestTrue(TEXT("冲刺：方向被归一，终点 = 起点 + 距离"), Target.Equals(FVector(700, 200, 50), 0.01));
		TestEqual(TEXT("冲刺：时长"), Params.Duration, 0.3f, 0.0001f);
		TestEqual(TEXT("冲刺：直线"), static_cast<uint8>(Params.PathKind), static_cast<uint8>(ERMSPathKind::Straight));
		TestEqual(TEXT("冲刺：被挡就停（Incremental）"), static_cast<uint8>(Params.ReachMode), static_cast<uint8>(ERMSReachMode::Incremental));
		TestEqual(TEXT("冲刺：先快后慢"), static_cast<uint8>(Params.Ease), static_cast<uint8>(ERMSEase::EaseOut));
		TestEqual(TEXT("冲刺：结束速度清零"), static_cast<uint8>(Params.FinishVelocity), static_cast<uint8>(ERMSFinishVelocity::Zero));
		TestEqual(TEXT("冲刺：朝向运动方向"), static_cast<uint8>(Params.RotationMode), static_cast<uint8>(ERMSRotationMode::FaceMotion));
	}

	// 击退：抬升高度生效，不改变朝向
	{
		FRMSMotionParams Params;
		FVector Target;
		RMSMath::PresetKnockback(Start, FVector(0, -1, 0), 400.0f, 0.4f, 60.0f, Params, Target);
		TestTrue(TEXT("击退：终点沿方向"), Target.Equals(FVector(100, -200, 50), 0.01));
		TestEqual(TEXT("击退：抬升高度"), Params.ArcHeight, 60.0f, 0.0001f);
		TestEqual(TEXT("击退：不改变朝向"), static_cast<uint8>(Params.RotationMode), static_cast<uint8>(ERMSRotationMode::None));
		TestEqual(TEXT("击退：被挡就停"), static_cast<uint8>(Params.ReachMode), static_cast<uint8>(ERMSReachMode::Incremental));

		// 负抬升被钳到 0
		FRMSMotionParams Neg;
		RMSMath::PresetKnockback(Start, FVector(1, 0, 0), 100.0f, 0.4f, -50.0f, Neg, Target);
		TestEqual(TEXT("击退：负抬升被钳到 0"), Neg.ArcHeight, 0.0f, 0.0001f);
	}

	// 跳跃：抛物线，强制到达，顶点高度由 ArcHeight 给出
	{
		FRMSMotionParams Params;
		RMSMath::PresetJump(Start, FVector(900, 200, 50), 150.0f, 0.7f, Params);
		TestEqual(TEXT("跳跃：抛物线"), static_cast<uint8>(Params.PathKind), static_cast<uint8>(ERMSPathKind::Parabola));
		TestEqual(TEXT("跳跃：顶点高度"), Params.ArcHeight, 150.0f, 0.0001f);
		TestEqual(TEXT("跳跃：强制到达"), static_cast<uint8>(Params.ReachMode), static_cast<uint8>(ERMSReachMode::Guaranteed));
		TestEqual(TEXT("跳跃：结束速度清零（Keep 会让角色冲过落点，真实角色测试发现）"), static_cast<uint8>(Params.FinishVelocity), static_cast<uint8>(ERMSFinishVelocity::Zero));

		// 端到端：用预设生成的 Params 走一遍形状，起点与终点精确，中点为峰值
		FRMSMotionShape Shape;
		RMSMath::BuildShape(Params.PathKind, Start, FVector(900, 200, 50), {}, Params.ArcHeight, Shape);
		TestTrue(TEXT("跳跃：起点精确"), RMSMath::EvaluatePosition(Params, Shape, 0.0f).Equals(Start, 0.01));
		TestTrue(TEXT("跳跃：精确落到目标"), RMSMath::EvaluatePosition(Params, Shape, 1.0f).Equals(FVector(900, 200, 50), 0.01));
		TestEqual(TEXT("跳跃：中点 Z = 起终点高度 + 顶点高度"), RMSMath::EvaluatePosition(Params, Shape, 0.5f).Z, 50.0 + 150.0, 0.01);
	}

	// 退化输入：方向为零、距离为 0 或负、时长非法，都不得产生 NaN
	{
		FRMSMotionParams Params;
		FVector Target;
		RMSMath::PresetDash(Start, FVector::ZeroVector, 600.0f, 0.3f, Params, Target);
		TestTrue(TEXT("方向为零：终点 = 起点"), Target.Equals(Start, 0.0001));
		TestFalse(TEXT("方向为零：无 NaN"), Target.ContainsNaN());

		RMSMath::PresetDash(Start, FVector(1, 0, 0), 0.0f, 0.3f, Params, Target);
		TestTrue(TEXT("距离为 0：终点 = 起点"), Target.Equals(Start, 0.0001));

		RMSMath::PresetDash(Start, FVector(1, 0, 0), -300.0f, 0.3f, Params, Target);
		TestTrue(TEXT("距离为负：钳到 0，终点 = 起点"), Target.Equals(Start, 0.0001));

		RMSMath::PresetDash(Start, FVector(1, 0, 0), 600.0f, 0.0f, Params, Target);
		TestEqual(TEXT("时长为 0：钳到下限"), Params.Duration, RMSMath::MinDuration, 0.0001f);

		RMSMath::PresetDash(Start, FVector(1, 0, 0), 600.0f, -5.0f, Params, Target);
		TestEqual(TEXT("时长为负：钳到下限"), Params.Duration, RMSMath::MinDuration, 0.0001f);
	}
	return true;
}

#endif
