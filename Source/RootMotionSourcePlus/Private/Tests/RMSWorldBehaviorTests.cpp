// Copyright JTW Studio 2026. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RMSMotionLibrary.h"
#include "RMSMotionMath.h"
#include "RMSMotionSubsystem.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Tests/AutomationCommon.h"

namespace
{
	/** 带墙的测试世界：角色悬空飞行，前方 X=wallX 处有一堵阻挡碰撞的盒子墙。 */
	struct FRMSWallWorld
	{
		FTestWorldWrapper Wrapper;
		ACharacter* Character = nullptr;
		AActor* Wall = nullptr;

		bool Init(float WallX)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game))
			{
				return false;
			}
			UWorld* World = Wrapper.GetTestWorld();
			if (!World || !Wrapper.BeginPlayInTestWorld())
			{
				return false;
			}

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Character = World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FVector(0, 0, 1000), FRotator::ZeroRotator, Params);
			if (!Character || !Character->GetCharacterMovement())
			{
				return false;
			}
			UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
			Movement->bRunPhysicsWithNoController = true;
			Movement->GravityScale = 0.0f;
			Movement->SetMovementMode(MOVE_Flying);

			if (WallX > 0.0f)
			{
				Wall = World->SpawnActor<AActor>(AActor::StaticClass(), FVector(WallX, 0, 1000), FRotator::ZeroRotator, Params);
				if (!Wall)
				{
					return false;
				}
				UBoxComponent* Box = NewObject<UBoxComponent>(Wall, TEXT("WallBox"));
				Box->SetBoxExtent(FVector(20, 500, 500));
				Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
				Wall->SetRootComponent(Box);
				Box->RegisterComponent();
				Box->SetWorldLocation(FVector(WallX, 0, 1000));
			}
			return true;
		}

		void Tick(int32 Frames, float Step = 1.0f / 60.0f)
		{
			for (int32 Index = 0; Index < Frames; ++Index)
			{
				Wrapper.TickTestWorld(Step);
			}
		}
	};

	/** 在墙前停下后，把墙移走，看之后是否还会冲向原目标。返回 {撞墙时位置, 终点位置} */
	TPair<float, float> RunWallScenario(FAutomationTestBase& Test, ERMSReachMode Mode)
	{
		FRMSWallWorld T;
		if (!T.Init(300.0f))
		{
			Test.AddWarning(TEXT("无法创建带墙的测试世界，场景被跳过"));
			return TPair<float, float>(-1.0f, -1.0f);
		}

		FRMSMotionRequest Request;
		Request.Params.Duration = 1.0f;
		Request.Params.Ease = ERMSEase::Linear;
		Request.Params.ReachMode = Mode;
		Request.Params.FinishVelocity = ERMSFinishVelocity::Zero;
		Request.StartLocation = T.Character->GetActorLocation();
		Request.TargetLocation = Request.StartLocation + FVector(800, 0, 0);
		Request.InstanceName = TEXT("WallRun");
		URMSMotionLibrary::PlayMotion(T.Character, Request);

		// 先跑到约 0.6 秒：此时应当已经撞上 X=280 附近的墙面
		T.Tick(36);
		const float AtWall = static_cast<float>(T.Character->GetActorLocation().X);

		// 把墙移到很远处，之后继续跑完剩余时间
		if (T.Wall && T.Wall->GetRootComponent())
		{
			T.Wall->SetActorLocation(FVector(50000, 0, 1000));
		}
		T.Tick(40);
		const float Final = static_cast<float>(T.Character->GetActorLocation().X);
		return TPair<float, float>(AtWall, Final);
	}
}

/**
 * 到达模式的真实行为（用户明确要求的关键需求）：
 * 被墙挡住几帧之后，Guaranteed 会找补回目标点，Incremental 不会。
 * 之前只有纯函数模拟这一语义，这里首次用真实角色与真实碰撞验证。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSWorldReachModeTest, "RMS.World.ReachMode", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSWorldReachModeTest::RunTest(const FString& Parameters)
{
	const TPair<float, float> Guaranteed = RunWallScenario(*this, ERMSReachMode::Guaranteed);
	const TPair<float, float> Incremental = RunWallScenario(*this, ERMSReachMode::Incremental);
	if (Guaranteed.Key < 0.0f || Incremental.Key < 0.0f)
	{
		return true;
	}

	// 两种模式都应被墙挡住：撞墙时位置不超过墙面（墙中心 300，半厚 20，角色胶囊半径约 34，所以应停在 246 之前附近）
	TestTrue(TEXT("Guaranteed：被墙挡住，没有穿墙"), Guaranteed.Key < 300.0f);
	TestTrue(TEXT("Incremental：被墙挡住，没有穿墙"), Incremental.Key < 300.0f);

	// 墙移走之后：Guaranteed 找补，应回到目标点（800）附近；Incremental 不找补，终点落后
	TestTrue(TEXT("Guaranteed：墙移走后找补，最终到达目标点附近"), Guaranteed.Value > 760.0f);
	TestTrue(TEXT("Incremental：墙移走后不找补，终点明显落后于目标点"), Incremental.Value < 700.0f);
	TestTrue(TEXT("两种模式的终点差异明显（Guaranteed 更远）"), Guaranteed.Value > Incremental.Value + 100.0f);

	AddInfo(FString::Printf(TEXT("撞墙时位置：Guaranteed=%.1f Incremental=%.1f；最终位置：Guaranteed=%.1f Incremental=%.1f（目标 800）"), Guaranteed.Key, Incremental.Key, Guaranteed.Value, Incremental.Value));
	return true;
}

/** 牵引：真实角色被拉向目标 Actor，并在停止距离处停住。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSWorldPullTest, "RMS.World.Pull", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSWorldPullTest::RunTest(const FString& Parameters)
{
	FRMSWallWorld T;
	if (!T.Init(-1.0f))
	{
		AddWarning(TEXT("无法创建测试世界，牵引场景被跳过"));
		return true;
	}
	UWorld* World = T.Wrapper.GetTestWorld();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Target = World->SpawnActor<AActor>(AActor::StaticClass(), FVector(800, 0, 1000), FRotator::ZeroRotator, Params);
	if (!Target)
	{
		AddError(TEXT("无法生成牵引目标"));
		return true;
	}
	Target->SetActorEnableCollision(false);
	USceneComponent* Root = NewObject<USceneComponent>(Target, TEXT("Root"));
	Target->SetRootComponent(Root);
	Root->RegisterComponent();
	Target->SetActorLocation(FVector(800, 0, 1000));

	FRMSMotionRequest Request;
	Request.Params.Duration = -1.0f;
	Request.Params.FinishVelocity = ERMSFinishVelocity::Zero;
	Request.Force.Kind = ERMSForceKind::Pull;
	Request.Force.PullTarget = Target;
	Request.Force.Strength = 600.0f;
	Request.Force.StopDistance = 150.0f;
	Request.Force.bIgnoreZ = true;
	Request.StartLocation = T.Character->GetActorLocation();
	Request.InstanceName = TEXT("PullTest");
	const FRMSMotionHandle Handle = URMSMotionLibrary::PlayMotion(T.Character, Request);
	TestTrue(TEXT("牵引 Handle 有效"), Handle.IsValid());

	T.Tick(30);
	const float Mid = static_cast<float>(T.Character->GetActorLocation().X);
	TestTrue(TEXT("牵引：0.5 秒后已被拉动"), Mid > 100.0f);
	TestTrue(TEXT("牵引：还没到停止距离"), Mid < 650.0f);

	T.Tick(180);
	const float Settled = static_cast<float>(T.Character->GetActorLocation().X);
	const float DistanceToTarget = 800.0f - Settled;
	TestTrue(TEXT("牵引：最终停在停止距离附近而不是穿过目标"), DistanceToTarget > 100.0f && DistanceToTarget < 200.0f);
	TestTrue(TEXT("牵引：无限时长在到达后仍活动（速度为 0 但没结束）"), URMSMotionLibrary::IsMotionActive(T.Character, Handle));

	// 目标移动后角色继续被拉向新位置
	Target->SetActorLocation(FVector(1400, 0, 1000));
	T.Tick(120);
	const float AfterMove = static_cast<float>(T.Character->GetActorLocation().X);
	TestTrue(TEXT("牵引：目标移动后角色跟着被拉过去"), AfterMove > Settled + 400.0f);

	// 目标被销毁后不崩溃，沿用最后已知位置
	Target->Destroy();
	T.Tick(30);
	TestTrue(TEXT("牵引：目标销毁后运动仍然活动，不崩溃"), URMSMotionLibrary::IsMotionActive(T.Character, Handle));
	URMSMotionLibrary::StopAllMotions(T.Character);
	return true;
}

/**
 * 位移运动的朝向：FaceTarget 要在真实角色上真的转到目标 Yaw。
 * 这是“横向闪避仍面朝敌人”这类用法的核心，不能只验证字段落地。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSWorldRotationTest, "RMS.World.Rotation", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSWorldRotationTest::RunTest(const FString& Parameters)
{
	FRMSWallWorld T;
	if (!T.Init(-1.0f))
	{
		AddWarning(TEXT("无法创建测试世界，朝向用例被跳过"));
		return true;
	}

	// FaceTarget：沿 +X 位移，但朝向转到 +Y（90°）。方向与位移不同，正是锁朝向闪避的语义
	{
		T.Character->SetActorLocation(FVector(0, 0, 1000));
		T.Character->SetActorRotation(FRotator::ZeroRotator);
		const FRMSMotionRequest Request = URMSMotionLibrary::MakeStraightRequest(
			T.Character->GetActorLocation(), FVector(600, 0, 1000), 0.3f, ERMSEase::Linear, ERMSReachMode::Guaranteed,
			ERMSRotationMode::FaceTarget, FRotator(0.0, 90.0, 0.0), TEXT("FaceTarget"));
		URMSMotionLibrary::PlayMotion(T.Character, Request);
		T.Tick(40);

		const float FinalYaw = static_cast<float>(T.Character->GetActorRotation().Yaw);
		AddInfo(FString::Printf(TEXT("FaceTarget：位移到 X=%.1f，最终 Yaw=%.1f（目标 90）"),
			static_cast<float>(T.Character->GetActorLocation().X), FinalYaw));
		TestTrue(TEXT("FaceTarget：确实位移了"), FMath::Abs(static_cast<float>(T.Character->GetActorLocation().X) - 600.0f) < 40.0f);
		TestEqual(TEXT("FaceTarget：结束时朝向精确等于目标 Yaw"), FinalYaw, 90.0f, 0.5f);
	}

	// None：完全不改朝向
	{
		T.Character->SetActorLocation(FVector(0, 0, 1000));
		T.Character->SetActorRotation(FRotator(0.0, 30.0, 0.0));
		const FRMSMotionRequest Request = URMSMotionLibrary::MakeStraightRequest(
			T.Character->GetActorLocation(), FVector(600, 0, 1000), 0.3f, ERMSEase::Linear, ERMSReachMode::Guaranteed,
			ERMSRotationMode::None, FRotator(0.0, 90.0, 0.0), TEXT("NoRotation"));
		URMSMotionLibrary::PlayMotion(T.Character, Request);
		T.Tick(40);
		TestEqual(TEXT("None：朝向保持不变"), static_cast<float>(T.Character->GetActorRotation().Yaw), 30.0f, 0.5f);
	}

	// FaceMotion：沿 +Y 位移，朝向应转到 90°
	{
		T.Character->SetActorLocation(FVector(0, 0, 1000));
		T.Character->SetActorRotation(FRotator::ZeroRotator);
		const FRMSMotionRequest Request = URMSMotionLibrary::MakeStraightRequest(
			T.Character->GetActorLocation(), FVector(0, 600, 1000), 0.3f, ERMSEase::Linear, ERMSReachMode::Guaranteed,
			ERMSRotationMode::FaceMotion, FRotator::ZeroRotator, TEXT("FaceMotion"));
		URMSMotionLibrary::PlayMotion(T.Character, Request);
		T.Tick(40);
		TestEqual(TEXT("FaceMotion：朝向运动方向"), static_cast<float>(T.Character->GetActorRotation().Yaw), 90.0f, 2.0f);
	}

	return true;
}

/** 预设端到端：用 Make*Request 生成的请求直接在真实角色上跑。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSWorldPresetTest, "RMS.World.Preset", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSWorldPresetTest::RunTest(const FString& Parameters)
{
	FRMSWallWorld T;
	if (!T.Init(-1.0f))
	{
		AddWarning(TEXT("无法创建测试世界，预设场景被跳过"));
		return true;
	}

	// 冲刺：600 厘米 0.3 秒，结束后速度清零
	{
		T.Character->SetActorLocation(FVector(0, 0, 1000));
		const FRMSMotionRequest Dash = URMSMotionLibrary::MakeDashRequest(T.Character->GetActorLocation(), FVector(1, 0, 0), 600.0f, 0.3f, ERMSRotationMode::FaceMotion, FRotator::ZeroRotator, TEXT("PresetDash"));
		const FRMSMotionHandle Handle = URMSMotionLibrary::PlayMotion(T.Character, Dash);
		TestTrue(TEXT("冲刺 Handle 有效"), Handle.IsValid());
		T.Tick(40);
		TestTrue(TEXT("冲刺：到达约 600 厘米处"), FMath::Abs(static_cast<float>(T.Character->GetActorLocation().X) - 600.0f) < 40.0f);
		TestFalse(TEXT("冲刺：时长结束后运动已结束"), URMSMotionLibrary::IsMotionActive(T.Character, Handle));
	}

	// 跳跃：抛物线精确落到目标
	{
		T.Character->SetActorLocation(FVector(0, 0, 1000));
		const FRMSMotionRequest Jump = URMSMotionLibrary::MakeJumpRequest(T.Character->GetActorLocation(), FVector(500, 0, 1000), 150.0f, 0.6f, ERMSRotationMode::FaceMotion, FRotator::ZeroRotator, TEXT("PresetJump"));
		URMSMotionLibrary::PlayMotion(T.Character, Jump);
		float PeakZ = 0.0f;
		for (int32 Frame = 0; Frame < 50; ++Frame)
		{
			T.Tick(1);
			PeakZ = FMath::Max(PeakZ, static_cast<float>(T.Character->GetActorLocation().Z));
		}
		AddInfo(FString::Printf(TEXT("跳跃落点=%s 峰值Z=%.1f（目标 500,0,1000）"), *T.Character->GetActorLocation().ToCompactString(), PeakZ));
		TestTrue(TEXT("跳跃：精确落到目标附近"), FVector::Dist(T.Character->GetActorLocation(), FVector(500, 0, 1000)) < 40.0f);
		TestTrue(TEXT("跳跃：中途确实抬升到接近顶点高度"), PeakZ > 1000.0f + 110.0f);
	}

	// 击退：被推开
	{
		T.Character->SetActorLocation(FVector(0, 0, 1000));
		const FRMSMotionRequest Knock = URMSMotionLibrary::MakeKnockbackRequest(T.Character->GetActorLocation(), FVector(-1, 0, 0), 400.0f, 0.4f, 60.0f, TEXT("PresetKnock"));
		URMSMotionLibrary::PlayMotion(T.Character, Knock);
		T.Tick(40);
		TestTrue(TEXT("击退：被推开约 400 厘米"), FMath::Abs(static_cast<float>(T.Character->GetActorLocation().X) + 400.0f) < 40.0f);
	}
	return true;
}

/**
 * 结束通知的完整性：无限时长的运动不会自己结束，所以角色销毁与 World 销毁时必须按 Interrupted 通知，
 * 否则异步节点会永远挂起。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSWorldEndNotifyTest, "RMS.World.EndNotify", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSWorldEndNotifyTest::RunTest(const FString& Parameters)
{
	// 角色销毁：订阅方收到 Interrupted
	{
		FRMSWallWorld T;
		if (!T.Init(-1.0f))
		{
			AddWarning(TEXT("无法创建测试世界，结束通知场景被跳过"));
			return true;
		}
		URMSMotionSubsystem* Subsystem = URMSMotionSubsystem::Get(T.Wrapper.GetTestWorld());
		if (!Subsystem)
		{
			AddError(TEXT("测试世界里没有 URMSMotionSubsystem"));
			return true;
		}

		int32 EndedCount = 0;
		ERMSMotionEndReason LastReason = ERMSMotionEndReason::Completed;
		const FDelegateHandle Binding = Subsystem->OnMotionEnded().AddLambda([&](FRMSMotionHandle, ERMSMotionEndReason Reason)
		{
			++EndedCount;
			LastReason = Reason;
		});

		FRMSMotionRequest Request;
		Request.Params.Duration = -1.0f;
		Request.Force.Kind = ERMSForceKind::Constant;
		Request.Force.Strength = 200.0f;
		Request.InstanceName = TEXT("DestroyMe");
		const FRMSMotionHandle Handle = URMSMotionLibrary::PlayMotion(T.Character, Request);
		T.Tick(10);
		TestEqual(TEXT("销毁前没有结束通知"), EndedCount, 0);

		T.Character->Destroy();
		T.Tick(5);
		TestEqual(TEXT("角色销毁后恰好收到一次结束通知"), EndedCount, 1);
		TestEqual(TEXT("角色销毁的结束原因是 Interrupted"), static_cast<uint8>(LastReason), static_cast<uint8>(ERMSMotionEndReason::Interrupted));
		TestFalse(TEXT("角色销毁后运动不再活动"), Subsystem->IsActive(Handle));
		Subsystem->OnMotionEnded().Remove(Binding);
	}

	// World 销毁：未结束的运动在子系统反初始化时按 Interrupted 通知，不泄漏
	{
		FRMSWallWorld T;
		if (!T.Init(-1.0f))
		{
			AddWarning(TEXT("无法创建测试世界，World 销毁场景被跳过"));
			return true;
		}
		URMSMotionSubsystem* Subsystem = URMSMotionSubsystem::Get(T.Wrapper.GetTestWorld());
		if (!Subsystem)
		{
			AddError(TEXT("测试世界里没有 URMSMotionSubsystem"));
			return true;
		}

		int32 EndedCount = 0;
		ERMSMotionEndReason LastReason = ERMSMotionEndReason::Completed;
		Subsystem->OnMotionEnded().AddLambda([&](FRMSMotionHandle, ERMSMotionEndReason Reason)
		{
			++EndedCount;
			LastReason = Reason;
		});

		FRMSMotionRequest Request;
		Request.Params.Duration = -1.0f;
		Request.Force.Kind = ERMSForceKind::Constant;
		Request.Force.Strength = 200.0f;
		Request.InstanceName = TEXT("WorldGone");
		URMSMotionLibrary::PlayMotion(T.Character, Request);
		T.Tick(5);
		TestEqual(TEXT("World 销毁前没有结束通知"), EndedCount, 0);

		// 真正销毁测试世界，让引擎自己对子系统执行反初始化。
		// 不能手动调用 Deinitialize：测试世界销毁时引擎会再调一次，UTickableWorldSubsystem 对重复反初始化有断言
		// （初版测试这样写导致整个测试进程崩溃，那是测试的错，不是插件的错）
		T.Wrapper.DestroyTestWorld(true);
		TestEqual(TEXT("World 销毁时收到一次结束通知"), EndedCount, 1);
		TestEqual(TEXT("World 销毁的结束原因是 Interrupted"), static_cast<uint8>(LastReason), static_cast<uint8>(ERMSMotionEndReason::Interrupted));
		// 子系统随世界一起被销毁，之后不能再访问它，也不能再 Remove 绑定
	}
	return true;
}

#endif
