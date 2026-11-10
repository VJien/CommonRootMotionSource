// Copyright JTW Studio 2026. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RMSMotionLibrary.h"
#include "RMSMotionMath.h"
#include "RMSMotionSubsystem.h"
#include "RMSNetTestTypes.h"
#include "Components/BoxComponent.h"
#include "Animation/AnimSequence.h"
#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Tests/EnsureScope.h"
#include "Tests/NetTestHelpers.h"

namespace
{
	/**
	 * 进程内一个服务器加一个客户端，玩家出生为 ARMSNetTestCharacter。
	 * 这是真实的 NetDriver、真实的 ServerMove/ClientAdjust 流程，不是模拟。
	 */
	struct FRMSNetFixture
	{
		UE::Net::FNetTestEnsureScope EnsureScope;
		TUniquePtr<UE::Net::FTestWorlds> Worlds;
		ARMSNetTestCharacter* ServerCharacter = nullptr;
		ARMSNetTestCharacter* ClientCharacter = nullptr;

		bool Init(FAutomationTestBase& Test)
		{
			const FString MapName = TEXT("/Engine/Maps/Entry");
			const FString GameMode = TEXT("/Script/RootMotionSourcePlus.RMSNetTestGameMode");
			Worlds = MakeUnique<UE::Net::FTestWorlds>(MapName, GameMode);
			if (!Worlds->CreateAndConnectClient())
			{
				Test.AddWarning(TEXT("无法创建并连接测试客户端：联网测试被跳过（环境不支持，不是通过）"));
				return false;
			}

			APlayerController* ServerPC = Worlds->GetServerPlayerControllerOfClient(0);
			ServerCharacter = ServerPC ? Cast<ARMSNetTestCharacter>(ServerPC->GetPawn()) : nullptr;
			if (!ServerCharacter)
			{
				Test.AddWarning(TEXT("服务器上客户端的 Pawn 不是 ARMSNetTestCharacter（GameMode 未生效）：联网测试被跳过"));
				return false;
			}

			// 等客户端把这个 Pawn 复制过来
			Worlds->TickAllUntil([&]() { ClientCharacter = Cast<ARMSNetTestCharacter>(Worlds->FindReplicatedObjectOnClient(ServerCharacter)); return ClientCharacter != nullptr; });
			if (!ClientCharacter)
			{
				Test.AddWarning(TEXT("客户端没有复制出角色：联网测试被跳过"));
				return false;
			}
			return true;
		}

		void Tick(int32 Frames) { Worlds->TickAll(Frames); }
	};

	/** 构造一条两端完全相同的请求。联网要求两端参数逐位一致。 */
	FRMSMotionRequest MakeStraightRequest(const FVector& Start, const FVector& End, float Duration, ERMSReachMode Mode)
	{
		FRMSMotionRequest Request;
		Request.Params.Duration = Duration;
		Request.Params.Ease = ERMSEase::Linear;
		Request.Params.ReachMode = Mode;
		Request.Params.FinishVelocity = ERMSFinishVelocity::Zero;
		Request.StartLocation = Start;
		Request.TargetLocation = End;
		Request.InstanceName = TEXT("NetStraight");
		return Request;
	}
}

/**
 * 联网核心验证：服务器与客户端各自以相同参数 PlayMotion。
 * 这是 spec D3 的硬要求，此前只有序列化往返的单机测试，从未用真实 NetDriver 验证过。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSNetMotionTest, "RMS.Net.Motion", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSNetMotionTest::RunTest(const FString& Parameters)
{
	FRMSNetFixture F;
	if (!F.Init(*this))
	{
		return true;
	}

	// 先稳定几帧，取两端的起点
	F.Tick(10);
	const FVector Start = F.ServerCharacter->GetActorLocation();
	AddInfo(FString::Printf(TEXT("联网起点：服务器=%s 客户端=%s"), *Start.ToCompactString(), *F.ClientCharacter->GetActorLocation().ToCompactString()));

	// 两端各自调用：参数逐位一致
	const FRMSMotionRequest Request = MakeStraightRequest(Start, Start + FVector(600, 0, 0), 1.0f, ERMSReachMode::Guaranteed);
	const FRMSMotionHandle ServerHandle = URMSMotionLibrary::PlayMotion(F.ServerCharacter, Request);
	const FRMSMotionHandle ClientHandle = URMSMotionLibrary::PlayMotion(F.ClientCharacter, Request);
	TestTrue(TEXT("服务器 PlayMotion 返回有效 Handle"), ServerHandle.IsValid());
	TestTrue(TEXT("客户端 PlayMotion 返回有效 Handle"), ClientHandle.IsValid());

	// 跑约 2 秒
	F.Tick(120);

	const FVector ServerEnd = F.ServerCharacter->GetActorLocation();
	const FVector ClientEnd = F.ClientCharacter->GetActorLocation();
	AddInfo(FString::Printf(TEXT("联网终点：服务器=%s 客户端=%s 差=%.2f"), *ServerEnd.ToCompactString(), *ClientEnd.ToCompactString(), FVector::Dist(ServerEnd, ClientEnd)));

	TestTrue(TEXT("服务器到达终点附近"), FVector::Dist(ServerEnd, Start + FVector(600, 0, 0)) < 15.0);
	TestTrue(TEXT("客户端到达终点附近"), FVector::Dist(ClientEnd, Start + FVector(600, 0, 0)) < 15.0);
	TestTrue(TEXT("两端终点一致"), FVector::Dist(ServerEnd, ClientEnd) < 10.0);

	// 两端运动都已正常结束，而不是被校正丢弃后挂着
	URMSMotionSubsystem* ServerSub = URMSMotionSubsystem::Get(F.Worlds->Server.GetWorld());
	URMSMotionSubsystem* ClientSub = URMSMotionSubsystem::Get(F.ClientCharacter->GetWorld());
	TestFalse(TEXT("服务器运动已结束"), ServerSub && ServerSub->IsActive(ServerHandle));
	TestFalse(TEXT("客户端运动已结束"), ClientSub && ClientSub->IsActive(ClientHandle));
	return true;
}

/**
 * 到达模式联网（P1-18，用户最担心的风险）：Incremental 下两端被挡的帧数可能不同导致位置分叉。
 * 这里只在服务器一侧放一堵墙：服务器被挡、客户端预测不被挡，是最容易分叉的情形，
 * 然后观察服务器校正能否把客户端拉回，以及是否出现持续拉扯。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSNetReachModeTest, "RMS.Net.ReachMode", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSNetReachModeTest::RunTest(const FString& Parameters)
{
	for (const ERMSReachMode Mode : { ERMSReachMode::Guaranteed, ERMSReachMode::Incremental })
	{
		FRMSNetFixture F;
		if (!F.Init(*this))
		{
			return true;
		}
		F.Tick(10);
		const FVector Start = F.ServerCharacter->GetActorLocation();

		// 只在服务器世界放墙：客户端不知道墙的存在，会预测走到底
		UWorld* ServerWorld = F.Worlds->Server.GetWorld();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Wall = ServerWorld->SpawnActor<AActor>(AActor::StaticClass(), Start + FVector(300, 0, 0), FRotator::ZeroRotator, Params);
		if (Wall)
		{
			UBoxComponent* Box = NewObject<UBoxComponent>(Wall, TEXT("WallBox"));
			Box->SetBoxExtent(FVector(20, 500, 500));
			Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Wall->SetRootComponent(Box);
			Box->RegisterComponent();
			Box->SetWorldLocation(Start + FVector(300, 0, 0));
		}

		const FRMSMotionRequest Request = MakeStraightRequest(Start, Start + FVector(800, 0, 0), 1.0f, Mode);
		URMSMotionLibrary::PlayMotion(F.ServerCharacter, Request);
		URMSMotionLibrary::PlayMotion(F.ClientCharacter, Request);

		// 运动期间逐帧记录两端的差，找最大分叉与是否持续拉扯
		float MaxGap = 0.0f;
		for (int32 Frame = 0; Frame < 70; ++Frame)
		{
			F.Tick(1);
			MaxGap = FMath::Max(MaxGap, static_cast<float>(FVector::Dist(F.ServerCharacter->GetActorLocation(), F.ClientCharacter->GetActorLocation())));
		}
		const float MidServerX = static_cast<float>(F.ServerCharacter->GetActorLocation().X - Start.X);

		// 运动期间的诊断（逐帧采样证实）：服务器被墙挡住，客户端预测不知道有墙。
		// 运动刚结束时的两端差反映插件自身的联网表现；之后引擎 CMC 的位置校正才会把客户端拉回
		float GapAtMotionEnd = 0.0f;
		for (int32 Extra = 0; Extra < 30; ++Extra)
		{
			URMSMotionSubsystem* SS = URMSMotionSubsystem::Get(F.Worlds->Server.GetWorld());
			URMSMotionSubsystem* CS = URMSMotionSubsystem::Get(F.ClientCharacter->GetWorld());
			if ((!SS || SS->GetActiveMotions().Num() == 0) && (!CS || CS->GetActiveMotions().Num() == 0))
			{
				break;
			}
			F.Tick(1);
		}
		GapAtMotionEnd = static_cast<float>(FVector::Dist(F.ServerCharacter->GetActorLocation(), F.ClientCharacter->GetActorLocation()));

		// 把墙移走，再跑一段时间，看最终是否收敛
		if (Wall)
		{
			Wall->SetActorLocation(FVector(500000, 0, 0));
		}
		F.Tick(120);
		const float FinalGap = static_cast<float>(FVector::Dist(F.ServerCharacter->GetActorLocation(), F.ClientCharacter->GetActorLocation()));
		const float ServerFinalX = static_cast<float>(F.ServerCharacter->GetActorLocation().X - Start.X);
		const float ClientFinalX = static_cast<float>(F.ClientCharacter->GetActorLocation().X - Start.X);

		const TCHAR* Name = Mode == ERMSReachMode::Guaranteed ? TEXT("Guaranteed") : TEXT("Incremental");
		AddInfo(FString::Printf(TEXT("联网撞墙 %s：运动中最大两端差=%.1f 运动结束时两端差=%.1f 服务器被挡时前进=%.1f 之后由引擎位置校正收敛：服务器=%.1f 客户端=%.1f 最终差=%.1f"), Name, MaxGap, GapAtMotionEnd, MidServerX, ServerFinalX, ClientFinalX, FinalGap));

		TestTrue(*FString::Printf(TEXT("%s：服务器被墙挡住没有穿墙"), Name), MidServerX < 300.0f);
		// 最终收敛是引擎 CMC 位置校正的能力，不是插件保证的；这里只断言“最终不会永久分叉”
		TestTrue(*FString::Printf(TEXT("%s：最终两端收敛（由引擎位置校正完成，差小于 30）"), Name), FinalGap < 30.0f);

		// 记录插件自身的表现：服务器被挡、客户端不被挡时，运动期间两端必然分叉。
		// 这是已知并接受的事实（客户端预测无法知道服务器端才有的墙），不是缺陷；断言它存在，防止日后被悄悄改变
		TestTrue(*FString::Printf(TEXT("%s：服务器端有墙而客户端没有时，运动期间两端确实分叉（已知行为）"), Name), MaxGap > 20.0f);
	}
	return true;
}

/**
 * PackageMap 真实解析（P1-15、P2-09、P2-18 的核心，此前只在测试里把引用设为空）。
 * 跟随目标是一个会复制的 Actor：验证客户端上能解析到同一个目标 Actor，并真的跟着它走。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSNetFollowTest, "RMS.Net.Follow", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSNetFollowTest::RunTest(const FString& Parameters)
{
	FRMSNetFixture F;
	if (!F.Init(*this))
	{
		return true;
	}
	F.Tick(10);
	const FVector Start = F.ServerCharacter->GetActorLocation();
	UWorld* ServerWorld = F.Worlds->Server.GetWorld();

	// 服务器上生成一个会复制的目标 Actor（用测试角色类，自带复制），放在前方 500
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARMSNetTestCharacter* ServerTarget = ServerWorld->SpawnActor<ARMSNetTestCharacter>(Start + FVector(500, 300, 0), FRotator::ZeroRotator, Params);
	if (!ServerTarget)
	{
		AddError(TEXT("无法在服务器生成跟随目标"));
		return true;
	}
	ServerTarget->SetActorEnableCollision(false);

	// 等目标复制到客户端
	ARMSNetTestCharacter* ClientTarget = nullptr;
	F.Worlds->TickAllUntil([&]() { ClientTarget = Cast<ARMSNetTestCharacter>(F.Worlds->FindReplicatedObjectOnClient(ServerTarget)); return ClientTarget != nullptr; });
	TestNotNull(TEXT("跟随目标已复制到客户端（真实 PackageMap 能解析到同一个 Actor）"), ClientTarget);
	if (!ClientTarget)
	{
		return true;
	}

	// 两端各自以各自本端的目标 Actor 引用调用 PlayMotion，其余参数逐位一致
	FRMSMotionRequest ServerRequest = MakeStraightRequest(Start, ServerTarget->GetActorLocation(), 1.0f, ERMSReachMode::Guaranteed);
	ServerRequest.InstanceName = TEXT("NetFollow");
	ServerRequest.Follow.TargetActor = ServerTarget;
	FRMSMotionRequest ClientRequest = ServerRequest;
	ClientRequest.Follow.TargetActor = ClientTarget;

	URMSMotionLibrary::PlayMotion(F.ServerCharacter, ServerRequest);
	URMSMotionLibrary::PlayMotion(F.ClientCharacter, ClientRequest);

	// 运动中途把目标在服务器上移到别处，客户端应当随复制看到新位置并跟着走
	F.Tick(20);
	ServerTarget->SetActorLocation(Start + FVector(900, -200, 0));
	F.Tick(120);

	const FVector TargetNow = ServerTarget->GetActorLocation();
	const FVector ServerEnd = F.ServerCharacter->GetActorLocation();
	const FVector ClientEnd = F.ClientCharacter->GetActorLocation();
	AddInfo(FString::Printf(TEXT("联网跟随：目标=%s 服务器终点=%s 客户端终点=%s 两端差=%.1f 与目标距离 服务器=%.1f 客户端=%.1f"), *TargetNow.ToCompactString(), *ServerEnd.ToCompactString(), *ClientEnd.ToCompactString(), FVector::Dist(ServerEnd, ClientEnd), FVector::Dist(ServerEnd, TargetNow), FVector::Dist(ClientEnd, TargetNow)));

	TestTrue(TEXT("服务器角色最终到达目标移动后的位置"), FVector::Dist(ServerEnd, TargetNow) < 25.0);
	TestTrue(TEXT("客户端角色最终到达目标移动后的位置（客户端确实解析并跟随了复制过来的目标）"), FVector::Dist(ClientEnd, TargetNow) < 25.0);
	TestTrue(TEXT("两端终点一致"), FVector::Dist(ServerEnd, ClientEnd) < 15.0);
	return true;
}

/**
 * 曲线资产的真实 PackageMap 解析：用引擎里确实存在、两端都能按路径加载的曲线资产。
 * 对象引用走 SerializeObject，需要资产有稳定路径；内存里临时 NewObject 的曲线无法跨网络，这是预期行为。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSNetCurveTest, "RMS.Net.Curve", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSNetCurveTest::RunTest(const FString& Parameters)
{
	FRMSNetFixture F;
	if (!F.Init(*this))
	{
		return true;
	}

	// 找一条真实存在、有稳定路径的 UCurveFloat 资产
	UCurveFloat* Curve = nullptr;
	// /Engine/Tutorial/ContentIntroCurve 是引擎自带、有稳定路径的 UCurveFloat 资产（已用文件头核实存在）
	Curve = LoadObject<UCurveFloat>(nullptr, TEXT("/Engine/Tutorial/ContentIntroCurve.ContentIntroCurve"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	// 找不到就判失败而不是跳过：“通过”必须代表真实资产确实经过了 PackageMap
	if (!TestNotNull(TEXT("引擎自带曲线资产 /Engine/Tutorial/ContentIntroCurve 可加载"), Curve))
	{
		return true;
	}

	F.Tick(10);
	const FVector Start = F.ServerCharacter->GetActorLocation();
	FRMSMotionRequest Request = MakeStraightRequest(Start, Start + FVector(400, 0, 0), 1.0f, ERMSReachMode::Guaranteed);
	Request.InstanceName = TEXT("NetCurve");
	Request.Params.CustomProgressCurve = Curve;

	URMSMotionLibrary::PlayMotion(F.ServerCharacter, Request);
	URMSMotionLibrary::PlayMotion(F.ClientCharacter, Request);

	// 先证明曲线确实改变了轨迹：中途（约 40% 时间）的位置应当明显偏离线性插值的 40%。
	// 否则“两端终点一致”可能只是因为曲线没有生效，不能证明对象引用被两端正确解析
	F.Tick(24);
	const float ServerMidFrac = static_cast<float>((F.ServerCharacter->GetActorLocation().X - Start.X) / 400.0);
	const float ClientMidFrac = static_cast<float>((F.ClientCharacter->GetActorLocation().X - Start.X) / 400.0);
	const float LinearFrac = 24.0f / 60.0f;
	AddInfo(FString::Printf(TEXT("联网曲线中途进度：服务器=%.3f 客户端=%.3f 线性应为=%.3f"), ServerMidFrac, ClientMidFrac, LinearFrac));
	TestTrue(TEXT("曲线确实改变了服务器端的轨迹（中途进度偏离线性）"), FMath::Abs(ServerMidFrac - LinearFrac) > 0.05f);
	TestTrue(TEXT("曲线确实改变了客户端的轨迹（客户端解析到了同一条曲线，没有退化成线性）"), FMath::Abs(ClientMidFrac - LinearFrac) > 0.05f);
	TestTrue(TEXT("中途两端进度一致"), FMath::Abs(ServerMidFrac - ClientMidFrac) < 0.05f);
	F.Tick(96);

	const float Gap = static_cast<float>(FVector::Dist(F.ServerCharacter->GetActorLocation(), F.ClientCharacter->GetActorLocation()));
	AddInfo(FString::Printf(TEXT("联网曲线：曲线=%s 服务器=%s 客户端=%s 两端差=%.2f"), *Curve->GetPathName(), *F.ServerCharacter->GetActorLocation().ToCompactString(), *F.ClientCharacter->GetActorLocation().ToCompactString(), Gap));
	TestTrue(TEXT("使用真实曲线资产时两端终点一致"), Gap < 15.0f);
	return true;
}

/**
 * 动画资产的真实 PackageMap 解析与根位移回放（P2-15、P2-11 的核心，此前只验证过数学与序列化）。
 * 用引擎自带的教程动画。先探测它是否带根位移：没有根位移的动画无法验证位移本身，只能验证资产引用被两端解析。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSNetAnimTest, "RMS.Net.Anim", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSNetAnimTest::RunTest(const FString& Parameters)
{
	FRMSNetFixture F;
	if (!F.Init(*this))
	{
		return true;
	}

	UAnimSequence* Anim = LoadObject<UAnimSequence>(nullptr, TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/Tutorial_Walk_Fwd.Tutorial_Walk_Fwd"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!TestNotNull(TEXT("引擎自带动画 Tutorial_Walk_Fwd 可加载"), Anim))
	{
		return true;
	}

	const bool bHasRootMotion = Anim->HasRootMotion();
	AddInfo(FString::Printf(TEXT("联网动画：%s 长度=%.2fs 带根位移=%d"), *Anim->GetPathName(), Anim->GetPlayLength(), bHasRootMotion ? 1 : 0));

	F.Tick(10);
	const FVector Start = F.ServerCharacter->GetActorLocation();
	FRMSMotionRequest Request;
	Request.Anim.Animation = Anim;
	Request.Anim.Rate = 1.0f;
	Request.StartLocation = Start;
	Request.TargetLocation = Start;
	Request.InstanceName = TEXT("NetAnim");

	const FRMSMotionHandle ServerHandle = URMSMotionLibrary::PlayMotion(F.ServerCharacter, Request);
	const FRMSMotionHandle ClientHandle = URMSMotionLibrary::PlayMotion(F.ClientCharacter, Request);
	TestTrue(TEXT("服务器动画运动有效"), ServerHandle.IsValid());
	TestTrue(TEXT("客户端动画运动有效"), ClientHandle.IsValid());

	F.Tick(FMath::CeilToInt(Anim->GetPlayLength() * 60.0f) + 30);

	const FVector ServerEnd = F.ServerCharacter->GetActorLocation();
	const FVector ClientEnd = F.ClientCharacter->GetActorLocation();
	const float Travelled = static_cast<float>(FVector::Dist(ServerEnd, Start));
	AddInfo(FString::Printf(TEXT("联网动画：服务器位移=%.1f 客户端终点=%s 两端差=%.2f"), Travelled, *ClientEnd.ToCompactString(), FVector::Dist(ServerEnd, ClientEnd)));

	TestTrue(TEXT("动画回放两端终点一致"), FVector::Dist(ServerEnd, ClientEnd) < 15.0);
	if (bHasRootMotion)
	{
		TestTrue(TEXT("带根位移的动画确实产生了位移"), Travelled > 20.0f);
	}
	else
	{
		AddWarning(TEXT("该动画不带根位移：只验证了“动画资产引用被两端解析且不崩溃”，没有验证根位移本身的联网表现"));
	}
	return true;
}

/**
 * 力类联网（P3-12）：无限时长的力，两端都要能被结束；牵引目标会复制。
 * 这是 P3 新写的、联网风险最高的部分。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSNetForceTest, "RMS.Net.Force", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSNetForceTest::RunTest(const FString& Parameters)
{
	// 恒力：两端各自施加，推进一致，两端各自 StopAllMotions 后停下
	{
		FRMSNetFixture F;
		if (!F.Init(*this))
		{
			return true;
		}
		F.Tick(10);
		const FVector Start = F.ServerCharacter->GetActorLocation();

		FRMSMotionRequest Request;
		Request.Params.Duration = -1.0f;
		Request.Params.FinishVelocity = ERMSFinishVelocity::Zero;
		Request.Force.Kind = ERMSForceKind::Constant;
		Request.Force.Direction = FVector(1, 0, 0);
		Request.Force.Strength = 400.0f;
		Request.StartLocation = Start;
		Request.InstanceName = TEXT("NetConstantForce");
		URMSMotionLibrary::PlayMotion(F.ServerCharacter, Request);
		URMSMotionLibrary::PlayMotion(F.ClientCharacter, Request);

		F.Tick(60);
		const float ServerOneSec = static_cast<float>(F.ServerCharacter->GetActorLocation().X - Start.X);
		const float ClientOneSec = static_cast<float>(F.ClientCharacter->GetActorLocation().X - Start.X);
		F.Tick(60);
		const float ServerTwoSec = static_cast<float>(F.ServerCharacter->GetActorLocation().X - Start.X);
		const float ClientTwoSec = static_cast<float>(F.ClientCharacter->GetActorLocation().X - Start.X);
		AddInfo(FString::Printf(TEXT("联网恒力：1 秒 服务器=%.1f 客户端=%.1f；2 秒 服务器=%.1f 客户端=%.1f"), ServerOneSec, ClientOneSec, ServerTwoSec, ClientTwoSec));

		TestTrue(TEXT("恒力：服务器 1 秒推进约 400"), ServerOneSec > 300.0f && ServerOneSec < 500.0f);
		TestTrue(TEXT("恒力：客户端 1 秒推进约 400"), ClientOneSec > 300.0f && ClientOneSec < 500.0f);
		TestTrue(TEXT("恒力：2 秒时两端一致（差小于 30）"), FMath::Abs(ServerTwoSec - ClientTwoSec) < 30.0f);

		URMSMotionSubsystem* ServerSub = URMSMotionSubsystem::Get(F.Worlds->Server.GetWorld());
		URMSMotionSubsystem* ClientSub = URMSMotionSubsystem::Get(F.ClientCharacter->GetWorld());
		TestTrue(TEXT("恒力：无限时长两端 2 秒后都仍活动，没有被校正悄悄丢弃"), ServerSub && ClientSub && ServerSub->GetActiveMotions().Num() == 1 && ClientSub->GetActiveMotions().Num() == 1);

		// 两端各自停止
		URMSMotionLibrary::StopAllMotions(F.ServerCharacter);
		URMSMotionLibrary::StopAllMotions(F.ClientCharacter);
		F.Tick(5);
		const float ServerAtStop = static_cast<float>(F.ServerCharacter->GetActorLocation().X);
		const float ClientAtStop = static_cast<float>(F.ClientCharacter->GetActorLocation().X);
		F.Tick(60);
		TestTrue(TEXT("恒力：两端停止后服务器不再前进"), FMath::Abs(static_cast<float>(F.ServerCharacter->GetActorLocation().X) - ServerAtStop) < 25.0f);
		TestTrue(TEXT("恒力：两端停止后客户端不再前进"), FMath::Abs(static_cast<float>(F.ClientCharacter->GetActorLocation().X) - ClientAtStop) < 25.0f);
		TestEqual(TEXT("恒力：服务器停止后登记表为空"), ServerSub ? ServerSub->GetActiveMotions().Num() : -1, 0);
		TestEqual(TEXT("恒力：客户端停止后登记表为空"), ClientSub ? ClientSub->GetActiveMotions().Num() : -1, 0);
	}

	// 牵引：目标是会复制的 Actor，两端各自以本端的目标引用调用
	{
		FRMSNetFixture F;
		if (!F.Init(*this))
		{
			return true;
		}
		F.Tick(10);
		const FVector Start = F.ServerCharacter->GetActorLocation();
		UWorld* ServerWorld = F.Worlds->Server.GetWorld();

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ARMSNetTestCharacter* ServerTarget = ServerWorld->SpawnActor<ARMSNetTestCharacter>(Start + FVector(800, 0, 0), FRotator::ZeroRotator, Params);
		if (!ServerTarget)
		{
			AddError(TEXT("无法在服务器生成牵引目标"));
			return true;
		}
		ServerTarget->SetActorEnableCollision(false);

		ARMSNetTestCharacter* ClientTarget = nullptr;
		F.Worlds->TickAllUntil([&]() { ClientTarget = Cast<ARMSNetTestCharacter>(F.Worlds->FindReplicatedObjectOnClient(ServerTarget)); return ClientTarget != nullptr; });
		TestNotNull(TEXT("牵引目标已复制到客户端"), ClientTarget);
		if (!ClientTarget)
		{
			return true;
		}

		FRMSMotionRequest ServerRequest;
		ServerRequest.Params.Duration = -1.0f;
		ServerRequest.Params.FinishVelocity = ERMSFinishVelocity::Zero;
		ServerRequest.Force.Kind = ERMSForceKind::Pull;
		ServerRequest.Force.Strength = 500.0f;
		ServerRequest.Force.StopDistance = 150.0f;
		ServerRequest.Force.bIgnoreZ = true;
		ServerRequest.StartLocation = Start;
		ServerRequest.InstanceName = TEXT("NetPull");
		ServerRequest.Force.PullTarget = ServerTarget;
		FRMSMotionRequest ClientRequest = ServerRequest;
		ClientRequest.Force.PullTarget = ClientTarget;

		URMSMotionLibrary::PlayMotion(F.ServerCharacter, ServerRequest);
		URMSMotionLibrary::PlayMotion(F.ClientCharacter, ClientRequest);

		F.Tick(240);
		const float ServerDist = static_cast<float>(FVector::Dist(F.ServerCharacter->GetActorLocation(), ServerTarget->GetActorLocation()));
		const float ClientDist = static_cast<float>(FVector::Dist(F.ClientCharacter->GetActorLocation(), ClientTarget->GetActorLocation()));
		const float Gap = static_cast<float>(FVector::Dist(F.ServerCharacter->GetActorLocation(), F.ClientCharacter->GetActorLocation()));
		AddInfo(FString::Printf(TEXT("联网牵引：到目标距离 服务器=%.1f 客户端=%.1f（停止距离 150）两端差=%.1f"), ServerDist, ClientDist, Gap));

		TestTrue(TEXT("牵引：服务器停在停止距离附近"), ServerDist > 110.0f && ServerDist < 200.0f);
		TestTrue(TEXT("牵引：客户端停在停止距离附近"), ClientDist > 110.0f && ClientDist < 200.0f);
		TestTrue(TEXT("牵引：两端一致"), Gap < 30.0f);

		// 服务器移动目标，两端都应被拉向新位置
		ServerTarget->SetActorLocation(Start + FVector(1600, 300, 0));
		F.Tick(240);
		const float ServerDist2 = static_cast<float>(FVector::Dist(F.ServerCharacter->GetActorLocation(), ServerTarget->GetActorLocation()));
		const float ClientDist2 = static_cast<float>(FVector::Dist(F.ClientCharacter->GetActorLocation(), ClientTarget->GetActorLocation()));
		AddInfo(FString::Printf(TEXT("联网牵引（目标移动后）：服务器=%.1f 客户端=%.1f 两端差=%.1f"), ServerDist2, ClientDist2, static_cast<float>(FVector::Dist(F.ServerCharacter->GetActorLocation(), F.ClientCharacter->GetActorLocation()))));
		TestTrue(TEXT("牵引：目标移动后服务器被拉到新停止距离"), ServerDist2 > 110.0f && ServerDist2 < 200.0f);
		TestTrue(TEXT("牵引：目标移动后客户端被拉到新停止距离"), ClientDist2 > 110.0f && ClientDist2 < 200.0f);

		URMSMotionLibrary::StopAllMotions(F.ServerCharacter);
		URMSMotionLibrary::StopAllMotions(F.ClientCharacter);
	}
	return true;
}

/** 折线与样条的联网：路径点逐点序列化，两端走出同一条路径，终点落在最后一个路径点。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMSNetPathTest, "RMS.Net.Path", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRMSNetPathTest::RunTest(const FString& Parameters)
{
	for (const ERMSPathKind Kind : { ERMSPathKind::Polyline, ERMSPathKind::Spline, ERMSPathKind::Parabola })
	{
		FRMSNetFixture F;
		if (!F.Init(*this))
		{
			return true;
		}
		F.Tick(10);
		const FVector Start = F.ServerCharacter->GetActorLocation();

		FRMSMotionRequest Request;
		Request.Params.Duration = 1.0f;
		Request.Params.Ease = ERMSEase::Linear;
		Request.Params.FinishVelocity = ERMSFinishVelocity::Zero;
		Request.Params.PathKind = Kind;
		Request.StartLocation = Start;
		Request.InstanceName = TEXT("NetPath");
		FVector ExpectedEnd;
		if (Kind == ERMSPathKind::Parabola)
		{
			Request.TargetLocation = Start + FVector(600, 0, 100);
			Request.Params.ArcHeight = 200.0f;
			ExpectedEnd = Request.TargetLocation;
		}
		else
		{
			Request.PathPoints = { Start + FVector(200, 150, 0), Start + FVector(400, -150, 80), Start + FVector(700, 100, 0) };
			Request.TargetLocation = Request.PathPoints.Last();
			ExpectedEnd = Request.PathPoints.Last();
		}

		URMSMotionLibrary::PlayMotion(F.ServerCharacter, Request);
		URMSMotionLibrary::PlayMotion(F.ClientCharacter, Request);

		// 运动中途两端的位置也应一致，而不只是终点：这证明两端走的是同一条路径而不是殊途同归
		F.Tick(30);
		const float MidGap = static_cast<float>(FVector::Dist(F.ServerCharacter->GetActorLocation(), F.ClientCharacter->GetActorLocation()));
		F.Tick(120);
		const FVector ServerEnd = F.ServerCharacter->GetActorLocation();
		const FVector ClientEnd = F.ClientCharacter->GetActorLocation();
		const TCHAR* Name = Kind == ERMSPathKind::Polyline ? TEXT("折线") : Kind == ERMSPathKind::Spline ? TEXT("样条") : TEXT("抛物线");
		AddInfo(FString::Printf(TEXT("联网%s：中途两端差=%.1f 终点：服务器距目标=%.1f 客户端距目标=%.1f 两端差=%.1f"), Name, MidGap, FVector::Dist(ServerEnd, ExpectedEnd), FVector::Dist(ClientEnd, ExpectedEnd), FVector::Dist(ServerEnd, ClientEnd)));

		TestTrue(*FString::Printf(TEXT("%s：中途两端走的是同一条路径（差小于 20）"), Name), MidGap < 20.0f);
		TestTrue(*FString::Printf(TEXT("%s：服务器到达终点"), Name), FVector::Dist(ServerEnd, ExpectedEnd) < 15.0);
		TestTrue(*FString::Printf(TEXT("%s：客户端到达终点"), Name), FVector::Dist(ClientEnd, ExpectedEnd) < 15.0);
		TestTrue(*FString::Printf(TEXT("%s：两端终点一致"), Name), FVector::Dist(ServerEnd, ClientEnd) < 10.0);
	}
	return true;
}

#endif
