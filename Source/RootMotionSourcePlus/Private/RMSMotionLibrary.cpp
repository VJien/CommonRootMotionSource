// Copyright JTW Studio 2026. All Rights Reserved.

#include "RMSMotionLibrary.h"
#include "RMSMotionMath.h"
#include "RMSPathMath.h"
#include "RMSWarpMath.h"
#include "RMSMotionSource.h"
#include "RMSMotionSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Curves/CurveFloat.h"

namespace
{
	/** 请求非法时统一返回，并通过子系统通知订阅方 */
	FRMSMotionHandle Reject(URMSMotionSubsystem* Subsystem, const TCHAR* Reason)
	{
		UE_LOG(LogRMS, Warning, TEXT("PlayMotion 被拒绝：%s"), Reason);
		if (Subsystem)
		{
			Subsystem->NotifyFailed(Subsystem->AllocateHandle());
		}
		return FRMSMotionHandle();
	}

	URMSMotionSubsystem* SubsystemFromContext(const UObject* WorldContextObject)
	{
		const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
		return URMSMotionSubsystem::Get(World);
	}

	/**
	 * 按朝向模式给一个 Warp 段设置旋转。间隙段（bWarp=false）不动。
	 * WithRotation 与 WithWarpRotation 共用，避免两处逻辑各写一遍后走样。
	 */
	void ApplyRotationToSegment(FRMSWarpSegment& Segment, ERMSRotationMode RotationMode, const FRotator& TargetRotation,
		const FVector& StartLocation, UCurveFloat* RotationCurve)
	{
		if (!Segment.bWarp)
		{
			return;
		}

		if (RotationMode == ERMSRotationMode::None)
		{
			Segment.bWarpRotation = false;
			Segment.TargetRotation = FRotator::ZeroRotator;
			Segment.RotationCurve = nullptr;
			return;
		}

		Segment.RotationCurve = RotationCurve;
		if (RotationMode == ERMSRotationMode::FaceMotion)
		{
			// 段末朝段目标方向；起终点水平投影重合（纯垂直位移）时没有朝向可言，关掉旋转
			const FVector Flat(Segment.Target.X - StartLocation.X, Segment.Target.Y - StartLocation.Y, 0.0);
			if (Flat.SizeSquared() > 1.0)
			{
				Segment.bWarpRotation = true;
				Segment.TargetRotation = FRotator(0.0, Flat.Rotation().Yaw, 0.0);
			}
			else
			{
				Segment.bWarpRotation = false;
			}
			return;
		}

		Segment.bWarpRotation = true;
		Segment.TargetRotation = FRotator(0.0, TargetRotation.Yaw, 0.0);
	}
}

FRMSMotionHandle URMSMotionLibrary::PlayMotion(ACharacter* Character, const FRMSMotionRequest& Request)
{
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	URMSMotionSubsystem* Subsystem = URMSMotionSubsystem::Get(World);
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;

	if (!Character || !Movement || !Subsystem)
	{
		return Reject(Subsystem, TEXT("角色、CharacterMovement 或 World 子系统无效"));
	}

	if (Request.ConflictPolicy != ERMSConflictPolicy::Allow)
	{
		const FRMSMotionHandle Existing = Subsystem->FindActiveByName(Movement, Request.InstanceName);
		if (Existing.IsValid())
		{
			if (Request.ConflictPolicy == ERMSConflictPolicy::Block)
			{
				return Reject(Subsystem, TEXT("同名运动已存在（Block 策略）"));
			}
			Subsystem->Stop(Existing, ERMSMotionEndReason::Replaced);
		}
	}

	const FRMSMotionParams& Params = Request.Params;

	// 起点必须由调用方显式提供：插件不读角色位置，因为联网时两端各读本机位置会把起点固化成
	// 不同值，Matches 判定不一致后服务器校正会失效。零向量通常是忘了填（世界原点恰好在原点附近时除外）
	if (Request.StartLocation.IsNearlyZero(0.01))
	{
		UE_LOG(LogRMS, Warning, TEXT("PlayMotion：StartLocation 为零向量，请确认是否忘记填写起点（应为角色胶囊中心，取 GetCenterLocation）"));
	}

	TSharedPtr<FRMSMotionSource> Source = MakeShared<FRMSMotionSource>();
	Source->InstanceName = Request.InstanceName;
	Source->AccumulateMode = Params.AccumulateMode;
	Source->Priority = static_cast<uint16>(FMath::Clamp(Request.Priority, 0, 65535));
	Source->Params = Params;
	// 力类允许 Duration 小于 0 表示无限时长（引擎不超时，只能 StopMotion 结束）；位移类仍钳到下限，避免除零
	const bool bInfiniteForce = Request.Force.IsForce() && Params.Duration < 0.0f;
	Source->Params.Duration = bInfiniteForce ? -1.0f : RMSMath::ClampDuration(Params.Duration);
	Source->Duration = Source->Params.Duration;
	Source->StartLocation = Request.StartLocation;
	Source->TargetLocation = Request.TargetLocation;
	Source->PathPoints = Request.PathPoints;
	if (Source->PathPoints.Num() > RMSPath::MaxPoints)
	{
		UE_LOG(LogRMS, Warning, TEXT("PlayMotion 路径点数量 %d 超过上限 %d，已截断"), Source->PathPoints.Num(), RMSPath::MaxPoints);
		Source->PathPoints.SetNum(RMSPath::MaxPoints);
	}
	Source->Settings.SetFlag(ERootMotionSourceSettingsFlags::UseSensitiveLiftoffCheck);

	switch (Params.FinishVelocity)
	{
	case ERMSFinishVelocity::Keep:
		Source->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::MaintainLastRootMotionVelocity;
		break;
	case ERMSFinishVelocity::Zero:
		Source->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
		Source->FinishVelocityParams.SetVelocity = FVector::ZeroVector;
		break;
	case ERMSFinishVelocity::Clamp:
		Source->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::ClampVelocity;
		Source->FinishVelocityParams.ClampVelocity = Params.FinishClampSpeed;
		break;
	}

	// 力：数值在此钳制，Source 内也会再钳一次，两处使用同一组上限
	if (Request.Force.IsForce())
	{
		Source->Force = Request.Force;
		Source->Force.Strength = FMath::Clamp(Request.Force.Strength, 0.0f, RMSMath::MaxForceStrength);
		Source->Force.StopDistance = FMath::Clamp(Request.Force.StopDistance, 0.0f, RMSMath::MaxStopDistance);
		if (Request.Force.Kind == ERMSForceKind::Pull && Request.Force.PullTarget)
		{
			Source->PullLastKnownTarget = Request.Force.PullTarget->GetActorLocation();
			Source->bPullHasTarget = true;
		}
		// 力没有终点：位移相关字段清零，避免调试绘制或 Matches 被无关字段干扰
		Source->StartLocation = Request.StartLocation;
		Source->TargetLocation = Request.StartLocation;
		Source->PathPoints.Reset();
	}

	// 跟随 Actor：TargetLocation 作为初值，之后每帧被目标位置刷新
	if (Request.Follow.IsValid())
	{
		Source->Follow = Request.Follow;
		Source->bIsFollowing = true;
		Source->TargetLocation = Request.Follow.TargetActor->GetActorLocation() + Request.Follow.WorldOffset;
	}

	// 动画回放：Duration 由动画区间与速率决定，路径相关字段不参与
	if (Request.Anim.IsValid())
	{
		const UAnimSequenceBase* Animation = Request.Anim.Animation;
		Source->Anim = Request.Anim;
		Source->Anim.Rate = RMSMath::ClampRate(Request.Anim.Rate);
		RMSMath::ResolveAnimRange(Animation->GetPlayLength(), Request.Anim.AnimStart, Request.Anim.AnimEnd, Source->AnimRangeStart, Source->AnimRangeEnd);
		Source->Duration = RMSMath::AnimRealDuration(Source->AnimRangeStart, Source->AnimRangeEnd, Source->Anim.Rate);

		// Warping：整理成覆盖整个动画区间的连续段列表；缩放上下限在此校验
		if (Request.Warp.IsValid())
		{
			Source->Warp.MinScale = FMath::Clamp(Request.Warp.MinScale, 0.01f, 100.0f);
			Source->Warp.MaxScale = FMath::Clamp(Request.Warp.MaxScale, Source->Warp.MinScale, 100.0f);
			TArray<FRMSWarpSegment> Input = Request.Warp.Segments;
			if (Input.Num() > RMSPath::MaxPoints)
			{
				UE_LOG(LogRMS, Warning, TEXT("PlayMotion Warping 段数量 %d 超过上限 %d，已截断"), Input.Num(), RMSPath::MaxPoints);
				Input.SetNum(RMSPath::MaxPoints);
			}
			RMSWarp::NormalizeSegments(Input, Source->AnimRangeStart, Source->AnimRangeEnd, Source->Warp.Segments);
		}
		Source->Params.Duration = Source->Duration;
		// 引擎规定动画根位移是“组件空间”的量（ExtractRootMotionFromRange 返回 RootToComponent 后的相对变换），
		// 转到世界空间必须用 Mesh 的组件旋转，否则 Mesh 相对 Actor 有旋转偏移时位移方向会算歪。
		// 注意这只是一个数学换算，不是“朝向基准”：FaceMotion 与朝向 Warping 一律以 Actor 朝向为准。
		if (const USkeletalMeshComponent* Mesh = Character->GetMesh())
		{
			Source->AnimComponentRotation = Mesh->GetComponentQuat();
		}
	}

	const uint16 SourceId = Movement->ApplyRootMotionSource(Source);
	if (SourceId == static_cast<uint16>(ERootMotionSourceID::Invalid))
	{
		return Reject(Subsystem, TEXT("CMC 拒绝了 Root Motion Source"));
	}

	return Subsystem->Register(Movement, Source, SourceId, Request.InstanceName);
}

bool URMSMotionLibrary::StopMotion(const UObject* WorldContextObject, FRMSMotionHandle Handle)
{
	URMSMotionSubsystem* Subsystem = SubsystemFromContext(WorldContextObject);
	return Subsystem && Subsystem->Stop(Handle, ERMSMotionEndReason::Interrupted);
}

int32 URMSMotionLibrary::StopAllMotions(ACharacter* Character)
{
	if (!Character)
	{
		return 0;
	}
	URMSMotionSubsystem* Subsystem = URMSMotionSubsystem::Get(Character->GetWorld());
	return Subsystem ? Subsystem->StopAllFor(Character->GetCharacterMovement(), ERMSMotionEndReason::Interrupted) : 0;
}

bool URMSMotionLibrary::IsMotionActive(const UObject* WorldContextObject, FRMSMotionHandle Handle)
{
	const URMSMotionSubsystem* Subsystem = SubsystemFromContext(WorldContextObject);
	return Subsystem && Subsystem->IsActive(Handle);
}

int32 URMSMotionLibrary::GetActiveMotionCount(const ACharacter* Character)
{
	if (!Character)
	{
		return 0;
	}
	const URMSMotionSubsystem* Subsystem = URMSMotionSubsystem::Get(Character->GetWorld());
	return Subsystem ? Subsystem->CountActiveFor(Character->GetCharacterMovement()) : 0;
}

FVector URMSMotionLibrary::FootToCenter(const ACharacter* Character, FVector FootLocation)
{
	// 胶囊半高就是中心到脚底的距离；没有角色或胶囊时无法换算，原样返回，避免造出古怪位移
	if (!Character)
	{
		return FootLocation;
	}
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	if (!Capsule)
	{
		return FootLocation;
	}
	// 胶囊轴恒为世界 Z，所以沿世界 Z 加半高即可
	return FootLocation + FVector(0.0, 0.0, Capsule->GetScaledCapsuleHalfHeight());
}

FVector URMSMotionLibrary::CenterToFoot(const ACharacter* Character, FVector CenterLocation)
{
	if (!Character)
	{
		return CenterLocation;
	}
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	if (!Capsule)
	{
		return CenterLocation;
	}
	return CenterLocation - FVector(0.0, 0.0, Capsule->GetScaledCapsuleHalfHeight());
}

FVector URMSMotionLibrary::GetFootLocation(const ACharacter* Character)
{
	return Character ? CenterToFoot(Character, Character->GetActorLocation()) : FVector::ZeroVector;
}

FVector URMSMotionLibrary::GetCenterLocation(const ACharacter* Character)
{
	// 与 GetActorLocation() 等价；单独提供是为了让蓝图里“我传的是中心不是脚底”这件事自解释
	return Character ? Character->GetActorLocation() : FVector::ZeroVector;
}

FRMSMotionRequest URMSMotionLibrary::MakeDashRequest(FVector StartLocation, FVector Direction, float Distance, float Duration,
	ERMSRotationMode RotationMode, FRotator TargetRotation, FName InstanceName)
{
	FRMSMotionRequest Request;
	Request.InstanceName = InstanceName;
	Request.StartLocation = StartLocation;
	RMSMath::PresetDash(StartLocation, Direction, Distance, Duration, Request.Params, Request.TargetLocation);
	// 预设默认 FaceMotion；调用方可以改成 FaceTarget（锁朝向冲刺）或 None（完全不转）
	Request.Params.RotationMode = RotationMode;
	Request.Params.TargetRotation = TargetRotation;
	return Request;
}

FRMSMotionRequest URMSMotionLibrary::MakeKnockbackRequest(FVector StartLocation, FVector Direction, float Distance, float Duration, float Lift, FName InstanceName)
{
	FRMSMotionRequest Request;
	Request.InstanceName = InstanceName;
	Request.StartLocation = StartLocation;
	RMSMath::PresetKnockback(StartLocation, Direction, Distance, Duration, Lift, Request.Params, Request.TargetLocation);
	return Request;
}

FRMSMotionRequest URMSMotionLibrary::MakeJumpRequest(FVector StartLocation, FVector Target, float ApexHeight, float Duration,
	ERMSRotationMode RotationMode, FRotator TargetRotation, FName InstanceName)
{
	FRMSMotionRequest Request;
	Request.InstanceName = InstanceName;
	Request.StartLocation = StartLocation;
	Request.TargetLocation = Target;
	RMSMath::PresetJump(StartLocation, Target, ApexHeight, Duration, Request.Params);
	Request.Params.RotationMode = RotationMode;
	Request.Params.TargetRotation = TargetRotation;
	return Request;
}

FRMSMotionRequest URMSMotionLibrary::MakeWarpedAnimationRequest(FVector StartLocation, UAnimSequenceBase* Animation, FVector Target,
	float Rate, float AnimStart, float AnimEnd, ERMSRotationMode RotationMode, FRotator TargetRotation,
	UCurveFloat* RotationCurve, FName InstanceName)
{
	FRMSMotionRequest Request = MakeAnimationRequest(StartLocation, Animation, Rate, AnimStart, AnimEnd, false, InstanceName);
	Request.TargetLocation = Target;
	if (Animation)
	{
		// 单段 Warping：窗口覆盖整段动画，终点就是目标
		float RangeStart = 0.0f;
		float RangeEnd = 0.0f;
		RMSMath::ResolveAnimRange(Animation->GetPlayLength(), AnimStart, AnimEnd, RangeStart, RangeEnd);
		FRMSWarpSegment Segment;
		Segment.WindowStart = RangeStart;
		Segment.WindowEnd = RangeEnd;
		Segment.Target = Target;
		Segment.bWarp = true;

		switch (RotationMode)
		{
		case ERMSRotationMode::FaceMotion:
		{
			// 段末朝 StartLocation→Target 的水平方向。起终点水平投影重合（纯垂直位移）时没有朝向可言，
			// 此时不开旋转而不是造一个任意方向
			const FVector Flat(Target.X - StartLocation.X, Target.Y - StartLocation.Y, 0.0);
			if (Flat.SizeSquared() > 1.0)
			{
				Segment.bWarpRotation = true;
				Segment.TargetRotation = FRotator(0.0, Flat.Rotation().Yaw, 0.0);
			}
			break;
		}
		case ERMSRotationMode::FaceTarget:
			Segment.bWarpRotation = true;
			Segment.TargetRotation = FRotator(0.0, TargetRotation.Yaw, 0.0);
			break;
		default:
			// None：不做朝向 Warping，动画自带的旋转也不参与（插件只取根位移）
			break;
		}
		Segment.RotationCurve = RotationCurve;
		Request.Warp.Segments.Add(Segment);
	}
	// 动画位移已经决定了节奏，终点必须到达，且结束时停下避免冲过目标
	Request.Params.ReachMode = ERMSReachMode::Guaranteed;
	Request.Params.FinishVelocity = ERMSFinishVelocity::Zero;
	return Request;
}

FRMSMotionRequest URMSMotionLibrary::MakeAnimationRequest(FVector StartLocation, UAnimSequenceBase* Animation,
	float Rate, float AnimStart, float AnimEnd, bool bIgnoreZ, FName InstanceName)
{
	FRMSMotionRequest Request;
	Request.InstanceName = InstanceName;
	Request.Anim.Animation = Animation;
	Request.Anim.Rate = RMSMath::ClampRate(Rate);
	Request.Anim.AnimStart = AnimStart;
	Request.Anim.AnimEnd = AnimEnd;
	Request.Anim.bIgnoreZ = bIgnoreZ;
	Request.StartLocation = StartLocation;
	Request.TargetLocation = StartLocation;
	return Request;
}

FRMSMotionRequest URMSMotionLibrary::MakePathRequest(FVector StartLocation, const TArray<FVector>& PathPoints, ERMSPathKind Kind, float Duration, FName InstanceName)
{
	FRMSMotionRequest Request;
	Request.InstanceName = InstanceName;
	Request.StartLocation = StartLocation;
	Request.PathPoints = PathPoints;
	Request.TargetLocation = PathPoints.Num() > 0 ? PathPoints.Last() : StartLocation;
	Request.Params.Duration = RMSMath::ClampDuration(Duration);
	Request.Params.PathKind = Kind;
	Request.Params.Ease = ERMSEase::Linear;
	// 多点路径被挡时不该继续往墙里挤
	Request.Params.ReachMode = ERMSReachMode::Incremental;
	Request.Params.FinishVelocity = ERMSFinishVelocity::Zero;
	return Request;
}

FRMSMotionRequest URMSMotionLibrary::MakeStraightRequest(FVector StartLocation, FVector Target, float Duration,
	ERMSEase Ease, ERMSReachMode ReachMode, ERMSRotationMode RotationMode, FRotator TargetRotation, FName InstanceName)
{
	FRMSMotionRequest Request;
	Request.InstanceName = InstanceName;
	Request.StartLocation = StartLocation;
	Request.TargetLocation = Target;
	Request.Params.Duration = RMSMath::ClampDuration(Duration);
	Request.Params.PathKind = ERMSPathKind::Straight;
	Request.Params.Ease = Ease;
	Request.Params.ReachMode = ReachMode;
	Request.Params.RotationMode = RotationMode;
	Request.Params.TargetRotation = TargetRotation;
	Request.Params.FinishVelocity = ERMSFinishVelocity::Zero;
	return Request;
}

FRMSMotionRequest URMSMotionLibrary::MakeParabolaRequest(FVector StartLocation, FVector Target, float ApexHeight,
	float Duration, ERMSReachMode ReachMode, ERMSRotationMode RotationMode, FRotator TargetRotation, FName InstanceName)
{
	FRMSMotionRequest Request = MakeStraightRequest(StartLocation, Target, Duration, ERMSEase::Linear, ReachMode, RotationMode, TargetRotation, InstanceName);
	Request.Params.PathKind = ERMSPathKind::Parabola;
	Request.Params.ArcHeight = FMath::Max(ApexHeight, 0.0f);
	return Request;
}

FRMSMotionRequest URMSMotionLibrary::MakeFollowRequest(FVector StartLocation, AActor* TargetActor, FVector WorldOffset,
	float Duration, ERMSFollowLostPolicy LostPolicy, ERMSReachMode ReachMode,
	ERMSRotationMode RotationMode, FRotator TargetRotation, FName InstanceName)
{
	FRMSMotionRequest Request;
	Request.InstanceName = InstanceName;
	Request.StartLocation = StartLocation;
	Request.Follow.TargetActor = TargetActor;
	Request.Follow.WorldOffset = WorldOffset;
	Request.Follow.LostPolicy = LostPolicy;
	// 终点只是初值，之后每帧被目标位置刷新
	Request.TargetLocation = IsValid(TargetActor) ? TargetActor->GetActorLocation() + WorldOffset : StartLocation;
	Request.Params.Duration = Duration < 0.0f ? -1.0f : RMSMath::ClampDuration(Duration);
	Request.Params.ReachMode = ReachMode;
	Request.Params.RotationMode = RotationMode;
	Request.Params.TargetRotation = TargetRotation;
	Request.Params.FinishVelocity = ERMSFinishVelocity::Zero;
	return Request;
}

FRMSMotionRequest URMSMotionLibrary::MakeConstantForceRequest(FVector StartLocation, FVector Direction, float Strength, float Duration, FName InstanceName)
{
	FRMSMotionRequest Request;
	Request.InstanceName = InstanceName;
	Request.StartLocation = StartLocation;
	Request.TargetLocation = StartLocation;
	Request.Force.Kind = ERMSForceKind::Constant;
	Request.Force.Direction = Direction;
	Request.Force.Strength = FMath::Clamp(Strength, 0.0f, RMSMath::MaxForceStrength);
	Request.Params.Duration = Duration < 0.0f ? -1.0f : RMSMath::ClampDuration(Duration);
	Request.Params.FinishVelocity = ERMSFinishVelocity::Zero;
	return Request;
}

FRMSMotionRequest URMSMotionLibrary::MakePullRequest(FVector StartLocation, AActor* TargetActor, float Strength, float StopDistance, bool bIgnoreZ, float Duration, FName InstanceName)
{
	FRMSMotionRequest Request;
	Request.InstanceName = InstanceName;
	Request.StartLocation = StartLocation;
	Request.TargetLocation = StartLocation;
	Request.Force.Kind = ERMSForceKind::Pull;
	Request.Force.PullTarget = TargetActor;
	Request.Force.Strength = FMath::Clamp(Strength, 0.0f, RMSMath::MaxForceStrength);
	Request.Force.StopDistance = FMath::Clamp(StopDistance, 0.0f, RMSMath::MaxStopDistance);
	Request.Force.bIgnoreZ = bIgnoreZ;
	Request.Params.Duration = Duration < 0.0f ? -1.0f : RMSMath::ClampDuration(Duration);
	Request.Params.FinishVelocity = ERMSFinishVelocity::Zero;
	return Request;
}

FRMSMotionRequest URMSMotionLibrary::MakeDirectionalRequest(FVector StartLocation, FVector Direction, float Distance, const FRMSMotionParams& Params, FName InstanceName)
{
	FRMSMotionRequest Request;
	Request.Params = Params;
	Request.InstanceName = InstanceName;
	Request.StartLocation = StartLocation;
	Request.TargetLocation = StartLocation + Direction.GetSafeNormal() * Distance;
	return Request;
}

FRMSMotionRequest URMSMotionLibrary::WithRotation(const FRMSMotionRequest& Request, ERMSRotationMode RotationMode,
	FRotator TargetRotation, UCurveFloat* RotationCurve)
{
	FRMSMotionRequest Result = Request;
	Result.Params.RotationMode = RotationMode;
	Result.Params.TargetRotation = FRotator(0.0, TargetRotation.Yaw, 0.0);
	Result.Params.RotationCurve = RotationCurve;

	// 位移运动看 Params.RotationMode；一旦存在 Warp 段就同步改段，否则用户设了参数却没生效。
	// 闸门看段本身而不是 Anim.IsValid()：Warp 段可以独立于动画资产存在（例如先配好段再填动画）
	if (Result.Warp.Segments.Num() > 0)
	{
		for (FRMSWarpSegment& Segment : Result.Warp.Segments)
		{
			ApplyRotationToSegment(Segment, RotationMode, TargetRotation, Result.StartLocation, RotationCurve);
		}
	}
	return Result;
}

FRMSMotionRequest URMSMotionLibrary::WithWarpRotation(const FRMSMotionRequest& Request, ERMSRotationMode RotationMode,
	FRotator TargetRotation, UCurveFloat* RotationCurve)
{
	FRMSMotionRequest Result = Request;
	if (Result.Warp.Segments.Num() == 0)
	{
		UE_LOG(LogRMS, Warning, TEXT("WithWarpRotation：Request 没有 Warp 段，朝向不会被设置。请先用 MakeWarpedAnimationRequest 或填 Warp.Segments"));
		return Result;
	}

	for (FRMSWarpSegment& Segment : Result.Warp.Segments)
	{
		ApplyRotationToSegment(Segment, RotationMode, TargetRotation, Result.StartLocation, RotationCurve);
	}
	return Result;
}
