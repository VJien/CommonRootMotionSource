// Copyright JTW Studio 2026. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RMSMotionLibrary.h"
#include "RMSMotionMath.h"
#include "RMSMotionSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Tests/AutomationCommon.h"

namespace
{
	/** 在测试世界里生成一个真实的 ACharacter，并手动推进若干帧。 */
	struct FRMSTestWorld
	{
		FTestWorldWrapper Wrapper;
		ACharacter* Character = nullptr;
		bool bReady = false;

		bool Init()
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
			// 悬空测试：关闭重力并设为飞行模式，避免地面与重力干扰位移断言
			// 测试角色没有 Controller：引擎的 CMC 只有开启此开关才会为无控制器的角色跑移动（引擎的 IsPlayingRootMotion 只认动画根运动，不认 RootMotionSource）
			Character->GetCharacterMovement()->bRunPhysicsWithNoController = true;
			Character->GetCharacterMovement()->GravityScale = 0.0f;
			Character->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
			bReady = true;
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
}

/**
 * 真实角色上的 PrepareRootMotion 完整流程。
 * 之前所有位移测试都只覆盖纯函数与序列化，这里首次用真实 ACharacter + CMC 手动 Tick 验证逐帧行为。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSWorldMotionTest, "RMS.World.Motion", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSWorldMotionTest::RunTest(const FString& Parameters)
{
	FRMSTestWorld T;
	if (!T.Init())
	{
		AddWarning(TEXT("无法创建测试世界或角色：真实角色测试被跳过（环境不支持，不是通过）"));
		return true;
	}

	// 直线位移：0.5 秒内到达终点
	{
		T.Character->SetActorLocation(FVector(0, 0, 1000));
		FRMSMotionRequest Request;
		Request.Params.Duration = 0.5f;
		Request.Params.Ease = ERMSEase::Linear;
		Request.StartLocation = T.Character->GetActorLocation();
		Request.TargetLocation = Request.StartLocation + FVector(600, 0, 0);
		Request.InstanceName = TEXT("WorldStraight");

		const FRMSMotionHandle Handle = URMSMotionLibrary::PlayMotion(T.Character, Request);
		TestTrue(TEXT("真实角色：PlayMotion 返回有效 Handle"), Handle.IsValid());
		TestTrue(TEXT("真实角色：运动处于活动状态"), URMSMotionLibrary::IsMotionActive(T.Character, Handle));

		T.Tick(45);
		const FVector After = T.Character->GetActorLocation();
		TestTrue(TEXT("真实角色：0.75 秒后已到达终点附近"), FVector::Dist(After, FVector(600, 0, 1000)) < 5.0);
		TestFalse(TEXT("真实角色：到时后运动已结束"), URMSMotionLibrary::IsMotionActive(T.Character, Handle));
	}

	// 力类：无限时长恒力持续推动，StopMotion 后停止
	{
		T.Character->SetActorLocation(FVector(0, 0, 1000));
		T.Character->GetCharacterMovement()->Velocity = FVector::ZeroVector;

		FRMSMotionRequest Request;
		Request.Params.Duration = -1.0f;
		Request.Params.FinishVelocity = ERMSFinishVelocity::Zero;
		Request.Force.Kind = ERMSForceKind::Constant;
		Request.Force.Direction = FVector(1, 0, 0);
		Request.Force.Strength = 400.0f;
		Request.StartLocation = T.Character->GetActorLocation();
		Request.InstanceName = TEXT("WorldForce");

		const FRMSMotionHandle Handle = URMSMotionLibrary::PlayMotion(T.Character, Request);
		TestTrue(TEXT("真实角色：恒力 Handle 有效"), Handle.IsValid());

		T.Tick(60);
		const float AfterOneSecond = static_cast<float>(T.Character->GetActorLocation().X);
		TestTrue(TEXT("真实角色：恒力 1 秒推进约 400 厘米（容差较大：引擎首帧启动延迟）"), AfterOneSecond > 300.0f && AfterOneSecond < 500.0f);
		TestTrue(TEXT("真实角色：无限时长 1 秒后仍在活动"), URMSMotionLibrary::IsMotionActive(T.Character, Handle));

		T.Tick(60);
		TestTrue(TEXT("真实角色：无限时长 2 秒后仍在活动，没有自己结束"), URMSMotionLibrary::IsMotionActive(T.Character, Handle));
		const float AfterTwoSeconds = static_cast<float>(T.Character->GetActorLocation().X);
		TestTrue(TEXT("真实角色：第二秒继续推进"), AfterTwoSeconds > AfterOneSecond + 300.0f);

		TestTrue(TEXT("真实角色：StopMotion 成功"), URMSMotionLibrary::StopMotion(T.Character, Handle));
		TestFalse(TEXT("真实角色：停止后不再活动"), URMSMotionLibrary::IsMotionActive(T.Character, Handle));
		const float AtStop = static_cast<float>(T.Character->GetActorLocation().X);
		T.Tick(30);
		TestTrue(TEXT("真实角色：停止后位置不再显著变化"), FMath::Abs(static_cast<float>(T.Character->GetActorLocation().X) - AtStop) < 20.0f);
	}

	// StopAllMotions：同时存在两个无限时长的力，一次清理
	{
		FRMSMotionRequest A;
		A.Params.Duration = -1.0f;
		A.Force.Kind = ERMSForceKind::Constant;
		A.Force.Direction = FVector(0, 1, 0);
		A.Force.Strength = 100.0f;
		A.InstanceName = TEXT("ForceA");
		FRMSMotionRequest B = A;
		B.Force.Direction = FVector(0, -1, 0);
		B.InstanceName = TEXT("ForceB");
		URMSMotionLibrary::PlayMotion(T.Character, A);
		URMSMotionLibrary::PlayMotion(T.Character, B);
		TestEqual(TEXT("真实角色：两个力同时活动"), URMSMotionLibrary::GetActiveMotionCount(T.Character), 2);
		TestEqual(TEXT("真实角色：StopAllMotions 返回停止数量"), URMSMotionLibrary::StopAllMotions(T.Character), 2);
		TestEqual(TEXT("真实角色：全部停止后数量为 0"), URMSMotionLibrary::GetActiveMotionCount(T.Character), 0);
	}

	// 同名冲突策略：Replace 与 Block
	{
		FRMSMotionRequest First;
		First.Params.Duration = -1.0f;
		First.Force.Kind = ERMSForceKind::Constant;
		First.Force.Strength = 50.0f;
		First.InstanceName = TEXT("Dup");
		First.ConflictPolicy = ERMSConflictPolicy::Replace;
		const FRMSMotionHandle H1 = URMSMotionLibrary::PlayMotion(T.Character, First);
		const FRMSMotionHandle H2 = URMSMotionLibrary::PlayMotion(T.Character, First);
		TestFalse(TEXT("真实角色：Replace 后旧运动不再活动"), URMSMotionLibrary::IsMotionActive(T.Character, H1));
		TestTrue(TEXT("真实角色：Replace 后新运动活动"), URMSMotionLibrary::IsMotionActive(T.Character, H2));

		FRMSMotionRequest Blocked = First;
		Blocked.ConflictPolicy = ERMSConflictPolicy::Block;
		AddExpectedError(TEXT("同名运动已存在"), EAutomationExpectedErrorFlags::Contains, 1);
		const FRMSMotionHandle H3 = URMSMotionLibrary::PlayMotion(T.Character, Blocked);
		TestFalse(TEXT("真实角色：Block 返回无效 Handle"), H3.IsValid());
		URMSMotionLibrary::StopAllMotions(T.Character);
	}
	return true;
}

#endif
