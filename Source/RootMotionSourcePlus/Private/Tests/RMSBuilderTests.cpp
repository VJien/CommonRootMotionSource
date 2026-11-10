// Copyright JTW Studio 2026. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RMSMotionLibrary.h"
#include "RMSMotionMath.h"
#include "RMSWarpMath.h"

/**
 * 蓝图便捷构造函数的产出。
 * 这些函数是为了让蓝图不必手拼嵌套结构体，所以测试重点在“产出的 Request 是否自洽”，
 * 而不是重新验证运动行为（那由 RMS.World.* 覆盖）。
 *
 * 全部构造函数都以显式 StartLocation 为起点、不接受角色指针：
 * 联网时两端各读本机位置会把起点固化成不同值，Matches 就会判定不一致、服务器校正失效。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSBuilderTest, "RMS.Builder", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSBuilderTest::RunTest(const FString& Parameters)
{
	const FVector Origin(100, 200, 50);

	// 直线：起点、终点与参数直接落地
	{
		const FRMSMotionRequest R = URMSMotionLibrary::MakeStraightRequest(Origin, FVector(700, 200, 50), 0.4f, ERMSEase::EaseIn, ERMSReachMode::Incremental, ERMSRotationMode::None, FRotator::ZeroRotator, TEXT("S"));
		TestTrue(TEXT("直线：起点等于传入值"), R.StartLocation.Equals(Origin, 0.001));
		TestTrue(TEXT("直线：终点"), R.TargetLocation.Equals(FVector(700, 200, 50), 0.001));
		TestEqual(TEXT("直线：时长"), R.Params.Duration, 0.4f, 0.0001f);
		TestEqual(TEXT("直线：种类"), static_cast<uint8>(R.Params.PathKind), static_cast<uint8>(ERMSPathKind::Straight));
		TestEqual(TEXT("直线：缓动"), static_cast<uint8>(R.Params.Ease), static_cast<uint8>(ERMSEase::EaseIn));
		TestEqual(TEXT("直线：到达模式"), static_cast<uint8>(R.Params.ReachMode), static_cast<uint8>(ERMSReachMode::Incremental));
		TestEqual(TEXT("直线：实例名"), R.InstanceName, FName(TEXT("S")));
		TestFalse(TEXT("直线：不是动画运动"), R.Anim.Animation != nullptr);
		TestFalse(TEXT("直线：不是力"), R.Force.IsForce());
	}

	// 抛物线：种类与顶点高度
	{
		const FRMSMotionRequest R = URMSMotionLibrary::MakeParabolaRequest(Origin, FVector(500, 0, 100), 220.0f, 0.6f);
		TestEqual(TEXT("抛物线：种类"), static_cast<uint8>(R.Params.PathKind), static_cast<uint8>(ERMSPathKind::Parabola));
		TestEqual(TEXT("抛物线：顶点高度"), R.Params.ArcHeight, 220.0f, 0.0001f);
		TestEqual(TEXT("抛物线：匀速"), static_cast<uint8>(R.Params.Ease), static_cast<uint8>(ERMSEase::Linear));
	}

	// 折线/样条：终点取最后一个点；空数组退化为原地
	{
		const TArray<FVector> Points = { FVector(200, 0, 0), FVector(400, 100, 0), FVector(600, 0, 0) };
		const FRMSMotionRequest R = URMSMotionLibrary::MakePathRequest(Origin, Points, ERMSPathKind::Spline, 0.7f);
		TestEqual(TEXT("样条：路径点数量"), R.PathPoints.Num(), 3);
		TestTrue(TEXT("样条：终点等于最后一个路径点"), R.TargetLocation.Equals(FVector(600, 0, 0), 0.001));
		TestEqual(TEXT("样条：种类"), static_cast<uint8>(R.Params.PathKind), static_cast<uint8>(ERMSPathKind::Spline));
		TestEqual(TEXT("样条：被挡就停"), static_cast<uint8>(R.Params.ReachMode), static_cast<uint8>(ERMSReachMode::Incremental));

		const FRMSMotionRequest Empty = URMSMotionLibrary::MakePathRequest(Origin, {}, ERMSPathKind::Polyline, 0.5f);
		TestEqual(TEXT("空路径点：没有路径点"), Empty.PathPoints.Num(), 0);
		TestTrue(TEXT("空路径点：终点等于起点"), Empty.TargetLocation.Equals(Origin, 0.001));
	}

	// 跟随：目标、偏移、丢失策略、无限时长
	{
		AActor* FakeTarget = nullptr;
		const FRMSMotionRequest R = URMSMotionLibrary::MakeFollowRequest(Origin, FakeTarget, FVector(-100, 0, 0), -1.0f, ERMSFollowLostPolicy::End, ERMSReachMode::Guaranteed);
		TestEqual(TEXT("跟随：偏移"), R.Follow.WorldOffset.Y, 0.0);
		TestEqual(TEXT("跟随：丢失策略"), static_cast<uint8>(R.Follow.LostPolicy), static_cast<uint8>(ERMSFollowLostPolicy::End));
		TestTrue(TEXT("跟随：负时长保留为无限"), R.Params.Duration < 0.0f);
		TestEqual(TEXT("跟随：到达模式"), static_cast<uint8>(R.Params.ReachMode), static_cast<uint8>(ERMSReachMode::Guaranteed));
		TestTrue(TEXT("跟随：无目标时终点等于起点"), R.TargetLocation.Equals(Origin, 0.001));
	}

	// 恒力与牵引：种类、数值钳制、无限时长
	{
		const FRMSMotionRequest C = URMSMotionLibrary::MakeConstantForceRequest(Origin, FVector(0, 1, 0), 1.0e9f, -1.0f);
		TestEqual(TEXT("恒力：种类"), static_cast<uint8>(C.Force.Kind), static_cast<uint8>(ERMSForceKind::Constant));
		TestEqual(TEXT("恒力：强度被钳到上限"), C.Force.Strength, RMSMath::MaxForceStrength, 0.01f);

		// 上限之内的值不被改动
		const FRMSMotionRequest C2 = URMSMotionLibrary::MakeConstantForceRequest(Origin, FVector(0, 1, 0), 12345.0f, -1.0f);
		TestEqual(TEXT("恒力：上限内的强度原样保留"), C2.Force.Strength, 12345.0f, 0.01f);
		TestTrue(TEXT("恒力：无限时长"), C.Params.Duration < 0.0f);

		const FRMSMotionRequest P = URMSMotionLibrary::MakePullRequest(Origin, nullptr, -50.0f, -10.0f, true, -1.0f);
		TestEqual(TEXT("牵引：种类"), static_cast<uint8>(P.Force.Kind), static_cast<uint8>(ERMSForceKind::Pull));
		TestEqual(TEXT("牵引：负强度钳到 0"), P.Force.Strength, 0.0f, 0.0001f);
		TestEqual(TEXT("牵引：负停止距离钳到 0"), P.Force.StopDistance, 0.0f, 0.0001f);
		TestTrue(TEXT("牵引：默认忽略 Z"), P.Force.bIgnoreZ);
	}

	// 动画：速率钳制、终点 -1 保留语义
	{
		UAnimSequenceBase* NoAnim = nullptr;
		const FRMSMotionRequest R = URMSMotionLibrary::MakeAnimationRequest(Origin, NoAnim, 0.0f, 0.2f, -1.0f, true, TEXT("A"));
		TestEqual(TEXT("动画：速率 0 被钳到下限"), R.Anim.Rate, RMSMath::MinRate, 0.0001f);
		TestEqual(TEXT("动画：起点"), R.Anim.AnimStart, 0.2f, 0.0001f);
		TestEqual(TEXT("动画：终点 -1"), R.Anim.AnimEnd, -1.0f, 0.0001f);
		TestTrue(TEXT("动画：忽略 Z"), R.Anim.bIgnoreZ);
		TestTrue(TEXT("动画：无资产时不产生 Warp 段"), R.Warp.Segments.Num() == 0);
	}

	// 动画 Warping：无资产时也必须自洽（不产生段、不崩溃），并明确要求到达
	{
		const FRMSMotionRequest R = URMSMotionLibrary::MakeWarpedAnimationRequest(Origin, nullptr, FVector(800, 0, 0), 1.0f);
		TestTrue(TEXT("动画 Warping：无资产时没有段"), R.Warp.Segments.Num() == 0);
		TestEqual(TEXT("动画 Warping：强制到达"), static_cast<uint8>(R.Params.ReachMode), static_cast<uint8>(ERMSReachMode::Guaranteed));
		TestEqual(TEXT("动画 Warping：结束速度清零"), static_cast<uint8>(R.Params.FinishVelocity), static_cast<uint8>(ERMSFinishVelocity::Zero));
		TestTrue(TEXT("动画 Warping：目标点写入"), R.TargetLocation.Equals(FVector(800, 0, 0), 0.001));
	}

	return true;
}

/** 朝向 Warping 的便捷开关：无资产时的退化路径必须自洽。有资产路径见 RMS.RotationApi。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSBuilderFacingTest, "RMS.Builder.FaceTarget", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSBuilderFacingTest::RunTest(const FString& Parameters)
{
	// 没有资产就产生不了段，此时即使要求朝向也只能返回自洽的空 Request
	const FRMSMotionRequest NoAnim = URMSMotionLibrary::MakeWarpedAnimationRequest(FVector::ZeroVector, nullptr, FVector(100, 0, 0), 1.0f, 0.0f, -1.0f, ERMSRotationMode::FaceMotion);
	TestTrue(TEXT("无资产时即使要求朝向也不产生段"), NoAnim.Warp.Segments.Num() == 0);
	TestTrue(TEXT("无资产时不崩溃且返回自洽 Request"), NoAnim.TargetLocation.Equals(FVector(100, 0, 0), 0.001));
	return true;
}

/**
 * 朝向 API 的蓝图入口：构造器参数、WithRotation、WithWarpRotation 三条路都要真的生效。
 * 用手工填的 Warp 段模拟“已有动画 Request”，避免依赖动画资产。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSRotationApiTest, "RMS.RotationApi", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSRotationApiTest::RunTest(const FString& Parameters)
{
	const FVector Start(0, 0, 0);
	const FVector Target(300, 400, 0);   // 水平方向 Yaw = 53.13°

	// 1) MakeStraightRequest 的旋转参数直接落地
	{
		const FRMSMotionRequest R = URMSMotionLibrary::MakeStraightRequest(Start, Target, 0.3f, ERMSEase::Linear,
			ERMSReachMode::Guaranteed, ERMSRotationMode::FaceTarget, FRotator(0.0, 90.0, 0.0));
		TestEqual(TEXT("直线：朝向模式落地"), static_cast<uint8>(R.Params.RotationMode), static_cast<uint8>(ERMSRotationMode::FaceTarget));
		TestEqual(TEXT("直线：目标 Yaw 落地"), R.Params.TargetRotation.Yaw, 90.0, 0.01);
		TestTrue(TEXT("直线：默认不带曲线"), R.Params.RotationCurve == nullptr);
	}

	// 2) MakeDashRequest 默认 FaceMotion（预设行为不变），可改成 FaceTarget
	{
		const FRMSMotionRequest Default = URMSMotionLibrary::MakeDashRequest(Start, FVector(1, 0, 0), 600.0f, 0.3f);
		TestEqual(TEXT("冲刺：默认朝运动方向"), static_cast<uint8>(Default.Params.RotationMode), static_cast<uint8>(ERMSRotationMode::FaceMotion));

		const FRMSMotionRequest Locked = URMSMotionLibrary::MakeDashRequest(Start, FVector(1, 0, 0), 600.0f, 0.3f,
			ERMSRotationMode::FaceTarget, FRotator(0.0, 45.0, 0.0));
		TestEqual(TEXT("冲刺：可锁朝向"), static_cast<uint8>(Locked.Params.RotationMode), static_cast<uint8>(ERMSRotationMode::FaceTarget));
		TestEqual(TEXT("冲刺：锁定的目标 Yaw"), Locked.Params.TargetRotation.Yaw, 45.0, 0.01);
	}

	// 3) WithRotation 同时改位移参数与 Warp 段，避免“参数设了但动画不转”
	{
		FRMSMotionRequest Request;
		Request.StartLocation = Start;
		Request.Anim.Animation = nullptr;   // 只是占位，下面手动造段
		FRMSWarpSegment Segment;
		Segment.WindowStart = 0.0f;
		Segment.WindowEnd = 1.0f;
		Segment.Target = Target;
		Segment.bWarp = true;
		Request.Warp.Segments.Add(Segment);

		const FRMSMotionRequest Faced = URMSMotionLibrary::WithRotation(Request, ERMSRotationMode::FaceMotion);
		TestEqual(TEXT("WithRotation：位移模式同步"), static_cast<uint8>(Faced.Params.RotationMode), static_cast<uint8>(ERMSRotationMode::FaceMotion));
		TestTrue(TEXT("WithRotation：段开启旋转"), Faced.Warp.Segments[0].bWarpRotation);
		// 起点→目标 的水平方向
		TestEqual(TEXT("WithRotation：段朝向等于目标方向"), Faced.Warp.Segments[0].TargetRotation.Yaw, Target.Rotation().Yaw, 0.01);

		// 改回 None 必须清干净，不能残留上次的目标朝向
		const FRMSMotionRequest Cleared = URMSMotionLibrary::WithRotation(Faced, ERMSRotationMode::None);
		TestFalse(TEXT("WithRotation：None 关闭段旋转"), Cleared.Warp.Segments[0].bWarpRotation);
		TestEqual(TEXT("WithRotation：None 清空目标朝向"), Cleared.Warp.Segments[0].TargetRotation.Yaw, 0.0, 0.001);
		TestTrue(TEXT("WithRotation：None 清空曲线"), Cleared.Warp.Segments[0].RotationCurve == nullptr);

		// FaceTarget 用显式角度，不等于运动方向
		const FRMSMotionRequest Fixed = URMSMotionLibrary::WithRotation(Request, ERMSRotationMode::FaceTarget, FRotator(0.0, -90.0, 0.0));
		TestEqual(TEXT("WithRotation：FaceTarget 用显式角度"), Fixed.Warp.Segments[0].TargetRotation.Yaw, -90.0, 0.01);
	}

	// 4) 间隙段（bWarp=false）不得被施加旋转
	{
		FRMSMotionRequest Request;
		Request.StartLocation = Start;
		FRMSWarpSegment Gap;
		Gap.WindowStart = 0.0f;
		Gap.WindowEnd = 0.5f;
		Gap.bWarp = false;
		Request.Warp.Segments.Add(Gap);

		const FRMSMotionRequest R = URMSMotionLibrary::WithWarpRotation(Request, ERMSRotationMode::FaceMotion);
		TestFalse(TEXT("间隙段不转"), R.Warp.Segments[0].bWarpRotation);
		TestEqual(TEXT("间隙段目标朝向保持零"), R.Warp.Segments[0].TargetRotation.Yaw, 0.0, 0.001);
	}

	// 5) WithWarpRotation 没段时必须警告并原样返回，不崩溃
	{
		FRMSMotionRequest Empty;
		AddExpectedError(TEXT("Request 没有 Warp 段"), EAutomationExpectedErrorFlags::Contains, 1);
		const FRMSMotionRequest R = URMSMotionLibrary::WithWarpRotation(Empty, ERMSRotationMode::FaceMotion);
		TestEqual(TEXT("无段时原样返回"), R.Warp.Segments.Num(), 0);
	}

	// 6) 纯垂直位移（水平投影重合）没有朝向可言，必须关掉旋转而不是造一个任意方向
	{
		FRMSMotionRequest Request;
		Request.StartLocation = Start;
		FRMSWarpSegment Segment;
		Segment.WindowStart = 0.0f;
		Segment.WindowEnd = 1.0f;
		Segment.Target = FVector(0, 0, 500);   // 正上方
		Segment.bWarp = true;
		Request.Warp.Segments.Add(Segment);

		const FRMSMotionRequest R = URMSMotionLibrary::WithWarpRotation(Request, ERMSRotationMode::FaceMotion);
		TestFalse(TEXT("纯垂直位移不产生朝向"), R.Warp.Segments[0].bWarpRotation);
	}

	return true;
}

#endif
