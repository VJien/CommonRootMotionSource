// Copyright JTW Studio 2026. All Rights Reserved.

#include "RMSMotionSource.h"
#include "RMSMotionMath.h"
#include "RMSPathMath.h"
#include "RMSWarpMath.h"
#include "RMSSettings.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	/** 位置比较容差，只用于吸收浮点误差 */
	constexpr float LocationTolerance = 0.1f;

	bool ParamsMatch(const FRMSMotionParams& A, const FRMSMotionParams& B)
	{
		const bool bRotationMatch = A.RotationMode != B.RotationMode
			? false
			: (A.RotationMode == ERMSRotationMode::None
				? true
				// FaceMotion 不看 TargetRotation（方向由速度决定），FaceTarget 才比较
				: (A.RotationMode == ERMSRotationMode::FaceTarget
					? FMath::IsNearlyEqual(A.TargetRotation.Yaw, B.TargetRotation.Yaw, 0.01f)
					: true));

		return FMath::IsNearlyEqual(A.Duration, B.Duration, UE_SMALL_NUMBER)
			&& A.ReachMode == B.ReachMode
			&& A.Ease == B.Ease
			&& FMath::IsNearlyEqual(A.ArcHeight, B.ArcHeight, 0.01f)
			&& bRotationMatch
			&& A.RotationCurve == B.RotationCurve
			&& A.FinishVelocity == B.FinishVelocity
			&& FMath::IsNearlyEqual(A.FinishClampSpeed, B.FinishClampSpeed, 0.01f)
			&& A.AccumulateMode == B.AccumulateMode
			&& A.PathKind == B.PathKind
			&& A.CustomProgressCurve == B.CustomProgressCurve
			&& A.OffsetCurve == B.OffsetCurve;
	}

	/** 力：比较种类、方向、强度、停止距离、忽略 Z、曲线与目标身份。牵引目标的位置随时间变化，不参与 */
	bool ForceMatches(const FRMSForceParams& A, const FRMSForceParams& B)
	{
		return A.Kind == B.Kind
			&& A.Direction.Equals(B.Direction, 0.001)
			&& FMath::IsNearlyEqual(A.Strength, B.Strength, 0.01f)
			&& FMath::IsNearlyEqual(A.StopDistance, B.StopDistance, 0.01f)
			&& A.bIgnoreZ == B.bIgnoreZ
			&& A.StrengthOverDistance == B.StrengthOverDistance
			&& A.PullTarget == B.PullTarget;
	}

	bool WarpMatches(const FRMSWarpParams& A, const FRMSWarpParams& B)
	{
		if (A.Segments.Num() != B.Segments.Num()
			|| !FMath::IsNearlyEqual(A.MinScale, B.MinScale, 0.001f)
			|| !FMath::IsNearlyEqual(A.MaxScale, B.MaxScale, 0.001f))
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Segments.Num(); ++Index)
		{
			const FRMSWarpSegment& X = A.Segments[Index];
			const FRMSWarpSegment& Y = B.Segments[Index];
			if (!FMath::IsNearlyEqual(X.WindowStart, Y.WindowStart, 0.001f)
				|| !FMath::IsNearlyEqual(X.WindowEnd, Y.WindowEnd, 0.001f)
				|| X.bWarp != Y.bWarp
				|| (X.bWarp && !FVector::PointsAreNear(X.Target, Y.Target, LocationTolerance))
				|| X.bWarpRotation != Y.bWarpRotation
				|| (X.bWarpRotation
					&& (!FMath::IsNearlyEqual(X.TargetRotation.Yaw, Y.TargetRotation.Yaw, 0.01f)
						|| X.RotationCurve != Y.RotationCurve)))
			{
				return false;
			}
		}
		return true;
	}

	bool PointsMatch(const TArray<FVector>& A, const TArray<FVector>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (!FVector::PointsAreNear(A[Index], B[Index], LocationTolerance))
			{
				return false;
			}
		}
		return true;
	}
}

FRMSMotionSource::FRMSMotionSource()
{
}

FRootMotionSource* FRMSMotionSource::Clone() const
{
	return new FRMSMotionSource(*this);
}

bool FRMSMotionSource::Matches(const FRootMotionSource* Other) const
{
	if (!FRootMotionSource::Matches(Other))
	{
		return false;
	}

	// 基类已保证 ScriptStruct 相同，可安全转换
	const FRMSMotionSource* OtherCast = static_cast<const FRMSMotionSource*>(Other);

	// 跟随运动的 TargetLocation 随目标移动而变，两端会有微小差异，不能参与匹配，
	// 否则服务器校正时会把本地 Source 误判为另一个而丢弃；跟随运动用目标 Actor 身份、偏移与策略识别
	const bool bTargetMatch = bIsFollowing || OtherCast->bIsFollowing
		? (bIsFollowing == OtherCast->bIsFollowing
			&& Follow.TargetActor == OtherCast->Follow.TargetActor
			&& Follow.WorldOffset.Equals(OtherCast->Follow.WorldOffset, LocationTolerance)
			&& Follow.LostPolicy == OtherCast->Follow.LostPolicy)
		: FVector::PointsAreNear(TargetLocation, OtherCast->TargetLocation, LocationTolerance);

	return ParamsMatch(Params, OtherCast->Params)
		&& FVector::PointsAreNear(StartLocation, OtherCast->StartLocation, LocationTolerance)
		&& bTargetMatch
		&& PointsMatch(PathPoints, OtherCast->PathPoints)
		&& ForceMatches(Force, OtherCast->Force)
		&& WarpMatches(Warp, OtherCast->Warp)
		&& Anim.Animation == OtherCast->Anim.Animation
		&& FMath::IsNearlyEqual(Anim.Rate, OtherCast->Anim.Rate, 0.001f)
		&& Anim.bIgnoreZ == OtherCast->Anim.bIgnoreZ
		&& FMath::IsNearlyEqual(AnimRangeStart, OtherCast->AnimRangeStart, 0.001f)
		&& FMath::IsNearlyEqual(AnimRangeEnd, OtherCast->AnimRangeEnd, 0.001f)
		&& AnimComponentRotation.Equals(OtherCast->AnimComponentRotation, 0.001);
}

bool FRMSMotionSource::MatchesAndHasSameState(const FRootMotionSource* Other) const
{
	return FRootMotionSource::MatchesAndHasSameState(Other);
}

bool FRMSMotionSource::UpdateStateFrom(const FRootMotionSource* SourceToTakeStateFrom, bool bMarkForSimulatedCatchup)
{
	if (!FRootMotionSource::UpdateStateFrom(SourceToTakeStateFrom, bMarkForSimulatedCatchup))
	{
		return false;
	}

	// 基类已保证 ScriptStruct 相同。跟随运动的目标位置是唯一随时间变化的状态，由权威端同步过来
	const FRMSMotionSource* Authoritative = static_cast<const FRMSMotionSource*>(SourceToTakeStateFrom);
	if (bIsFollowing)
	{
		TargetLocation = Authoritative->TargetLocation;
		bFollowTargetLost = Authoritative->bFollowTargetLost;
	}
	if (Force.Kind == ERMSForceKind::Pull)
	{
		PullLastKnownTarget = Authoritative->PullLastKnownTarget;
		bPullHasTarget = Authoritative->bPullHasTarget;
	}
	return true;
}

bool FRMSMotionSource::RefreshFollowTarget()
{
	if (!bIsFollowing || bFollowTargetLost)
	{
		return !bFollowTargetLost;
	}

	// 目标 Actor 在本端尚未解析出来（还没复制到）时，不当作“丢失”：保留权威端同步过来的 TargetLocation 继续运动，
	// 直到目标出现；只有目标明确被销毁才按丢失策略处理
	if (Follow.TargetActor == nullptr)
	{
		return true;
	}

	if (IsValid(Follow.TargetActor) && !Follow.TargetActor->IsActorBeingDestroyed())
	{
		TargetLocation = Follow.TargetActor->GetActorLocation() + Follow.WorldOffset;
		return true;
	}

	// 目标丢失：TargetLocation 保持最后一次已知位置
	bFollowTargetLost = true;
	if (Follow.LostPolicy == ERMSFollowLostPolicy::End)
	{
		Status.SetFlag(ERootMotionSourceStatusFlags::MarkedForRemoval);
	}
	return false;
}

const FRMSMotionShape& FRMSMotionSource::GetShape() const
{
	// 哈希覆盖所有决定形状的字段；字段被 UpdateStateFrom 或反序列化改写后会自动重建
	uint32 Hash = GetTypeHash(static_cast<uint8>(Params.PathKind));
	Hash = HashCombine(Hash, GetTypeHash(StartLocation));
	Hash = HashCombine(Hash, GetTypeHash(TargetLocation));
	Hash = HashCombine(Hash, GetTypeHash(Params.ArcHeight));
	for (const FVector& Point : PathPoints)
	{
		Hash = HashCombine(Hash, GetTypeHash(Point));
	}

	if (!CachedShape.IsValid() || CachedShapeHash != Hash)
	{
		CachedShape = MakeShared<FRMSMotionShape>();
		RMSMath::BuildShape(Params.PathKind, StartLocation, TargetLocation, PathPoints, Params.ArcHeight, *CachedShape);
		CachedShapeHash = Hash;
	}
	return *CachedShape;
}

FVector FRMSMotionSource::GetEndpoint() const
{
	return RMSMath::ShapeEndpoint(GetShape());
}

FVector FRMSMotionSource::EvaluateExpectedLocation(float Time) const
{
	return RMSMath::EvaluatePosition(Params, GetShape(), Time / RMSMath::ClampDuration(Duration));
}

FVector FRMSMotionSource::ExtractWorldDelta(float From, float To) const
{
	if (!Anim.Animation || To <= From)
	{
		return FVector::ZeroVector;
	}
	FAnimExtractContext Context(static_cast<double>(To), true);
	FTransform Delta = FTransform::Identity;
	if (const UAnimMontage* Montage = Cast<UAnimMontage>(Anim.Animation.Get()))
	{
		Delta = Montage->ExtractRootMotionFromTrackRange(From, To, Context);
	}
	else
	{
		Delta = Anim.Animation->ExtractRootMotionFromRange(From, To, Context);
	}
	return RMSMath::FilterAnimTranslation(RMSMath::AnimDeltaToWorld(Delta.GetTranslation(), AnimComponentRotation), Anim.bIgnoreZ);
}

void FRMSMotionSource::PrepareAnimation(float SimulationTime, float MovementTickTime, const ACharacter& Character)
{
	if (MovementTickTime <= UE_SMALL_NUMBER || !Anim.Animation)
	{
		return;
	}

	// 动画区间 [AnimRangeStart, AnimRangeEnd]；真实时间到动画时间只用一套公式
	const float Prev = RMSMath::RealTimeToAnimTime(AnimRangeStart, AnimRangeEnd, Anim.Rate, GetTime());
	const float Curr = RMSMath::RealTimeToAnimTime(AnimRangeStart, AnimRangeEnd, Anim.Rate, GetTime() + SimulationTime);
	if (Curr <= Prev)
	{
		return;
	}

	FVector Translation = ExtractWorldDelta(Prev, Curr);
	FQuat RotationDelta = FQuat::Identity;

	// Warping：本帧可能跨越多个段，逐段处理，每段的位移经该段的变换后累加
	if (Warp.IsValid())
	{
		const RMSWarp::FLimits Limits{ Warp.MinScale, Warp.MaxScale };
		const FVector Actual = Character.GetActorLocation();
		// 本帧旋转用一个“已转过的”工作朝向推进：跨多段时后一段以前一段的结果为基准，
		// 否则两段增量会各自从同一朝向算出，合成后转过量
		FQuat WorkingRotation = Character.GetActorQuat();
		FVector Warped = FVector::ZeroVector;
		float From = Prev;
		while (From < Curr - UE_KINDA_SMALL_NUMBER)
		{
			const int32 Index = RMSWarp::FindSegmentIndex(Warp.Segments, From);
			if (Index == INDEX_NONE)
			{
				break;
			}
			const FRMSWarpSegment& Segment = Warp.Segments[Index];
			const float To = FMath::Min(Curr, Segment.WindowEnd);
			if (To <= From)
			{
				// 浮点边界上推进不了：直接把剩余部分原样计入，避免死循环
				Warped += ExtractWorldDelta(From, Curr);
				break;
			}

			const FVector PieceDelta = ExtractWorldDelta(From, To);
			if (!Segment.bWarp)
			{
				Warped += PieceDelta;
			}
			else
			{
				// 剩余动画位移 R：当前动画时间到本段终点
				const FVector Remaining = ExtractWorldDelta(From, Segment.WindowEnd);
				// Guaranteed：以角色实际位置为准，被挡后下一帧自动找补；
				// Incremental：以本段起点处的期望位置为准，被挡的位移不找补。
				// 期望位置 = 本段目标减去本段起点到终点的整段变换后位移，等价于假设此前一直按计划走
				FVector Origin = Actual;
				if (Params.ReachMode == ERMSReachMode::Incremental)
				{
					Origin = Segment.Target - ExtractWorldDelta(From, Segment.WindowEnd);
				}
				const FVector ToTarget = Segment.Target - Origin;
				const RMSWarp::FSimilarity Transform = RMSWarp::Solve(Remaining, ToTarget, Limits);
				Warped += RMSWarp::WarpDelta(PieceDelta, Transform);
			}

			// 朝向 Warping 与位移 Warping 各自独立开关：本段开了就转，全程以角色当前朝向为基准
			if (Segment.bWarpRotation)
			{
				const float Fraction = RMSMath::ComputeRotationFraction(
					Segment.RotationCurve, Segment.WindowStart, Segment.WindowEnd, From, To);
				// 一帧最多跨多个段：每段以自己的入口朝向为基准算出增量，再把工作朝向推进过去，
				// 最后整体作为本帧增量输出。间隙段（bWarpRotation=false）不产生旋转，朝向停在前一个旋转段的结果
				const FQuat Step = RMSMath::ComputeYawRotationDelta(WorkingRotation, Segment.TargetRotation.Yaw, Fraction);
				RotationDelta = Step * RotationDelta;
				WorkingRotation = Step * WorkingRotation;
			}
			From = To;
		}
		Translation = Warped;
	}
	Translation = RMSMath::ApplyAxisLock(Translation, URMSSettings::Get()->GetLockedAxes());

	// 动画根位移本身就是本帧位移，按 StepTime 归一成速度即可，不需要“找补”
	RootMotionParams.Set(FTransform(RotationDelta, Translation / MovementTickTime, FVector::OneVector));
}

void FRMSMotionSource::PrepareForce(float SimulationTime, float MovementTickTime, const ACharacter& Character)
{
	if (MovementTickTime <= UE_SMALL_NUMBER)
	{
		return;
	}

	// 力直接是速度（厘米/秒）。引擎自带的恒力 Source 同样把速度直接放进输出的平移分量，
	// 速度不随步长缩放，因此与帧率无关
	FVector Velocity = FVector::ZeroVector;
	if (Force.Kind == ERMSForceKind::Constant)
	{
		Velocity = RMSMath::ComputeConstantForceVelocity(Force.Direction, Force.Strength);
	}
	else if (Force.Kind == ERMSForceKind::Pull)
	{
		// 目标在本端尚未解析（还没复制过来）时沿用最后已知位置，不中断；没有任何已知位置时不动
		if (IsValid(Force.PullTarget) && !Force.PullTarget->IsActorBeingDestroyed())
		{
			PullLastKnownTarget = Force.PullTarget->GetActorLocation();
			bPullHasTarget = true;
		}
		if (bPullHasTarget)
		{
			Velocity = RMSMath::ComputePullVelocity(Character.GetActorLocation(), PullLastKnownTarget, Force.Strength, Force.StopDistance, Force.bIgnoreZ, Force.StrengthOverDistance.Get());
		}
	}

	Velocity = RMSMath::ApplyAxisLock(Velocity, URMSSettings::Get()->GetLockedAxes());
	RootMotionParams.Set(FTransform(Velocity));
}

void FRMSMotionSource::PrepareRootMotion(float SimulationTime, float MovementTickTime, const ACharacter& Character, const UCharacterMovementComponent& MoveComponent)
{
	RootMotionParams.Clear();

	if (bIsFollowing)
	{
		RefreshFollowTarget();
		if (Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval))
		{
			return;
		}
	}

	if (Force.IsForce())
	{
		PrepareForce(SimulationTime, MovementTickTime, Character);
		SetTime(GetTime() + SimulationTime);
		return;
	}

	if (Anim.IsValid())
	{
		PrepareAnimation(SimulationTime, MovementTickTime, Character);
		SetTime(GetTime() + SimulationTime);
		return;
	}

	if (Duration > RMSMath::MinDuration && MovementTickTime > UE_SMALL_NUMBER)
	{
		const FVector Velocity = RMSMath::ApplyAxisLock(
			RMSMath::ComputeVelocity(Params, GetShape(), Duration, GetTime(), SimulationTime, MovementTickTime, Character.GetActorLocation()),
			URMSSettings::Get()->GetLockedAxes());

		FTransform Transform(Velocity);
		// 引擎把输出旋转当作增量，不能直接给绝对朝向
		if (Params.RotationMode == ERMSRotationMode::FaceMotion)
		{
			// 即时对齐速度方向：适合冲刺这类“朝哪走朝哪”的运动
			Transform.SetRotation(RMSMath::ComputeFacingDelta(Character.GetActorQuat(), Velocity));
		}
		else if (Params.RotationMode == ERMSRotationMode::FaceTarget)
		{
			// 整段匀速（或按曲线）转到固定朝向：与位移 Warping 用同一套比例趋近
			const float Fraction = RMSMath::ComputeRotationFraction(
				Params.RotationCurve, 0.0f, Duration, GetTime(), GetTime() + SimulationTime);
			Transform.SetRotation(RMSMath::ComputeYawRotationDelta(Character.GetActorQuat(), Params.TargetRotation.Yaw, Fraction));
		}
		RootMotionParams.Set(Transform);
	}

	SetTime(GetTime() + SimulationTime);
}

bool FRMSMotionSource::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	if (!FRootMotionSource::NetSerialize(Ar, Map, bOutSuccess))
	{
		return false;
	}

	Ar << StartLocation;
	Ar << TargetLocation;

	// 路径点：先发数量再逐点发送；接收端数量越界视为数据错误，防止恶意包撑爆内存
	int32 PointCount = PathPoints.Num();
	Ar << PointCount;
	if (Ar.IsError())
	{
		bOutSuccess = false;
		return false;
	}
	if (Ar.IsLoading())
	{
		if (PointCount < 0 || PointCount > RMSPath::MaxPoints)
		{
			UE_LOG(LogRMS, Error, TEXT("FRMSMotionSource::NetSerialize 路径点数量非法：%d"), PointCount);
			bOutSuccess = false;
			return false;
		}
		PathPoints.SetNum(PointCount);
	}
	for (int32 Index = 0; Index < PointCount; ++Index)
	{
		Ar << PathPoints[Index];
	}

	// 力：种类与数值是纯数据，曲线与牵引目标走对象引用
	uint8 ForceKindByte = static_cast<uint8>(Force.Kind);
	uint8 bForceIgnoreZ = Force.bIgnoreZ ? 1 : 0;
	uint8 bPullHasTargetByte = bPullHasTarget ? 1 : 0;
	Ar << ForceKindByte << bForceIgnoreZ << bPullHasTargetByte;
	Ar << Force.Direction << Force.Strength << Force.StopDistance << PullLastKnownTarget;
	uint8 bHasForceCurve = Force.StrengthOverDistance ? 1 : 0;
	uint8 bHasPullTarget = Force.PullTarget ? 1 : 0;
	Ar << bHasForceCurve << bHasPullTarget;
	if (Ar.IsLoading())
	{
		if (ForceKindByte > static_cast<uint8>(ERMSForceKind::Pull))
		{
			UE_LOG(LogRMS, Error, TEXT("FRMSMotionSource::NetSerialize 力种类非法：%d"), ForceKindByte);
			bOutSuccess = false;
			return false;
		}
		Force.Kind = static_cast<ERMSForceKind>(ForceKindByte);
		Force.bIgnoreZ = bForceIgnoreZ != 0;
		bPullHasTarget = bPullHasTargetByte != 0;
		// 数值越界视为数据错误，防止恶意包造成瞬移
		if (!FMath::IsFinite(Force.Strength) || Force.Strength < 0.0f || Force.Strength > RMSMath::MaxForceStrength
			|| !FMath::IsFinite(Force.StopDistance) || Force.StopDistance < 0.0f || Force.StopDistance > RMSMath::MaxStopDistance
			|| Force.Direction.ContainsNaN() || PullLastKnownTarget.ContainsNaN())
		{
			UE_LOG(LogRMS, Error, TEXT("FRMSMotionSource::NetSerialize 力参数越界或非有限值"));
			bOutSuccess = false;
			return false;
		}
	}

	// 跟随：偏移、策略与丢失标志是纯数据；目标 Actor 走网络对象引用
	uint8 bHasFollow = bIsFollowing ? 1 : 0;
	uint8 LostPolicyByte = static_cast<uint8>(Follow.LostPolicy);
	uint8 bLostByte = bFollowTargetLost ? 1 : 0;
	Ar << bHasFollow << LostPolicyByte << bLostByte;
	Ar << Follow.WorldOffset;
	if (Ar.IsLoading())
	{
		bIsFollowing = bHasFollow != 0;
		Follow.LostPolicy = static_cast<ERMSFollowLostPolicy>(LostPolicyByte);
		bFollowTargetLost = bLostByte != 0;
	}

	// Warping 目标段：数量设上限，接收端越界视为数据错误
	int32 SegmentCount = Warp.Segments.Num();
	Ar << SegmentCount;
	if (Ar.IsError())
	{
		bOutSuccess = false;
		return false;
	}
	if (Ar.IsLoading())
	{
		if (SegmentCount < 0 || SegmentCount > RMSPath::MaxPoints)
		{
			UE_LOG(LogRMS, Error, TEXT("FRMSMotionSource::NetSerialize Warping 段数量非法：%d"), SegmentCount);
			bOutSuccess = false;
			return false;
		}
		Warp.Segments.SetNum(SegmentCount);
	}
	for (int32 Index = 0; Index < SegmentCount; ++Index)
	{
		FRMSWarpSegment& Segment = Warp.Segments[Index];
		uint8 bWarpByte = Segment.bWarp ? 1 : 0;
		uint8 bWarpRotationByte = Segment.bWarpRotation ? 1 : 0;
		float TargetYaw = Segment.TargetRotation.Yaw;
		Ar << Segment.WindowStart << Segment.WindowEnd << Segment.Target << bWarpByte << bWarpRotationByte << TargetYaw;
		if (Ar.IsLoading())
		{
			Segment.bWarp = bWarpByte != 0;
			Segment.bWarpRotation = bWarpRotationByte != 0;
			// 只同步 Yaw：俯仰与翻滚不参与朝向 Warping，传全量旋转没有意义还会引入两端差异
			Segment.TargetRotation = FRotator(0.0, TargetYaw, 0.0);
			// 曲线引用在下面的统一对象段处理，这里先清掉旧值，避免接收端残留本地数据
			Segment.RotationCurve = nullptr;
		}
	}
	Ar << Warp.MinScale << Warp.MaxScale;

	// 段级旋转曲线：只有真正用到时才走对象引用。逐段写“有/无”，再按顺序发对象，
	// 避免为没用曲线的段付 PackageMap 代价。数量上限与段数一致，已在上面校验
	for (int32 Index = 0; Index < SegmentCount; ++Index)
	{
		uint8 bHasRotationCurve = Warp.Segments[Index].RotationCurve ? 1 : 0;
		Ar << bHasRotationCurve;
		if (bHasRotationCurve == 0)
		{
			continue;
		}
		if (!Map)
		{
			UE_LOG(LogRMS, Error, TEXT("FRMSMotionSource::NetSerialize 缺少 PackageMap，无法序列化段旋转曲线"));
			bOutSuccess = false;
			return false;
		}
		UObject* RotationCurveObject = Warp.Segments[Index].RotationCurve.Get();
		if (!Map->SerializeObject(Ar, UCurveFloat::StaticClass(), RotationCurveObject) || Ar.IsError())
		{
			bOutSuccess = false;
			return false;
		}
		// 曲线决定旋转完成时机，解析不到会让两端朝向轨迹不同，必须放弃整个 Source
		if (Ar.IsLoading() && RotationCurveObject == nullptr)
		{
			UE_LOG(LogRMS, Error, TEXT("FRMSMotionSource::NetSerialize 段旋转曲线在本端无法解析，放弃该 Source"));
			bOutSuccess = false;
			return false;
		}
		Warp.Segments[Index].RotationCurve = Cast<UCurveFloat>(RotationCurveObject);
	}

	// 动画回放：区间、旋转与速率固定下来，两端一致
	uint8 bHasAnim = Anim.IsValid() ? 1 : 0;
	Ar << bHasAnim;
	Ar << Anim.AnimStart << Anim.AnimEnd << Anim.Rate << Anim.bIgnoreZ;
	Ar << AnimRangeStart << AnimRangeEnd;
	Ar << AnimComponentRotation;

	Ar << Params.Duration;
	Ar << Params.ArcHeight;
	Ar << Params.FinishClampSpeed;
	// 只发 Yaw：朝向转向只绕世界 Z，俯仰与翻滚不参与，传全量只会引入两端差异
	float ParamsTargetYaw = Params.TargetRotation.Yaw;
	Ar << ParamsTargetYaw;

	uint8 PathKindByte = static_cast<uint8>(Params.PathKind);
	uint8 ReachModeByte = static_cast<uint8>(Params.ReachMode);
	uint8 EaseByte = static_cast<uint8>(Params.Ease);
	uint8 RotationByte = static_cast<uint8>(Params.RotationMode);
	uint8 FinishByte = static_cast<uint8>(Params.FinishVelocity);
	uint8 AccumulateByte = static_cast<uint8>(Params.AccumulateMode);
	uint8 bHasRotationCurve = Params.RotationCurve ? 1 : 0;
	Ar << PathKindByte << ReachModeByte << EaseByte << RotationByte << FinishByte << AccumulateByte << bHasRotationCurve;

	// 曲线是对象引用：每条用一位标志表示有无，只有存在时才需要 PackageMap
	uint8 bHasCurve = Params.HasCustomCurve() ? 1 : 0;
	uint8 bHasOffset = Params.HasOffsetCurve() ? 1 : 0;
	Ar << bHasCurve << bHasOffset;

	if (Ar.IsLoading())
	{
		Params.PathKind = static_cast<ERMSPathKind>(PathKindByte);
		Params.ReachMode = static_cast<ERMSReachMode>(ReachModeByte);
		Params.Ease = static_cast<ERMSEase>(EaseByte);
		Params.RotationMode = static_cast<ERMSRotationMode>(RotationByte);
		Params.FinishVelocity = static_cast<ERMSFinishVelocity>(FinishByte);
		Params.AccumulateMode = static_cast<ERootMotionAccumulateMode>(AccumulateByte);
		Params.TargetRotation = FRotator(0.0, ParamsTargetYaw, 0.0);
		if (bHasRotationCurve == 0)
		{
			// 对端声明没有曲线：清掉本端残留，避免沿用旧值造成两端轨迹不同
			Params.RotationCurve = nullptr;
		}
	}

	if (bHasRotationCurve)
	{
		if (!Map)
		{
			UE_LOG(LogRMS, Error, TEXT("FRMSMotionSource::NetSerialize 缺少 PackageMap，无法序列化转向曲线"));
			bOutSuccess = false;
			return false;
		}
		UObject* RotationCurveObject = Params.RotationCurve.Get();
		if (!Map->SerializeObject(Ar, UCurveFloat::StaticClass(), RotationCurveObject) || Ar.IsError())
		{
			bOutSuccess = false;
			return false;
		}
		// 曲线决定转向完成时机，解析不到会让两端朝向轨迹不同，必须放弃整个 Source
		if (Ar.IsLoading() && RotationCurveObject == nullptr)
		{
			UE_LOG(LogRMS, Error, TEXT("FRMSMotionSource::NetSerialize 转向曲线在本端无法解析，放弃该 Source"));
			bOutSuccess = false;
			return false;
		}
		Params.RotationCurve = Cast<UCurveFloat>(RotationCurveObject);
	}

	if (bHasCurve)
	{
		if (!Map)
		{
			UE_LOG(LogRMS, Error, TEXT("FRMSMotionSource::NetSerialize 缺少 PackageMap，无法序列化自定义曲线"));
			bOutSuccess = false;
			return false;
		}

		UObject* CurveObject = Params.CustomProgressCurve.Get();
		if (!Map->SerializeObject(Ar, UCurveFloat::StaticClass(), CurveObject) || Ar.IsError())
		{
			bOutSuccess = false;
			return false;
		}

		// 对端声明有曲线却解析不到：两端会得到不同的路径，必须显式失败而不是悄悄退化成缓动
		if (Ar.IsLoading() && CurveObject == nullptr)
		{
			UE_LOG(LogRMS, Error, TEXT("FRMSMotionSource::NetSerialize 自定义曲线在本端无法解析，放弃该 Source"));
			bOutSuccess = false;
			return false;
		}
		Params.CustomProgressCurve = Cast<UCurveFloat>(CurveObject);
	}
	else if (Ar.IsLoading())
	{
		Params.CustomProgressCurve = nullptr;
	}

	if (bHasOffset)
	{
		if (!Map)
		{
			UE_LOG(LogRMS, Error, TEXT("FRMSMotionSource::NetSerialize 缺少 PackageMap，无法序列化偏移曲线"));
			bOutSuccess = false;
			return false;
		}
		UObject* OffsetObject = Params.OffsetCurve.Get();
		if (!Map->SerializeObject(Ar, UCurveVector::StaticClass(), OffsetObject) || Ar.IsError())
		{
			bOutSuccess = false;
			return false;
		}
		if (Ar.IsLoading() && OffsetObject == nullptr)
		{
			UE_LOG(LogRMS, Error, TEXT("FRMSMotionSource::NetSerialize 偏移曲线在本端无法解析，放弃该 Source"));
			bOutSuccess = false;
			return false;
		}
		Params.OffsetCurve = Cast<UCurveVector>(OffsetObject);
	}
	else if (Ar.IsLoading())
	{
		Params.OffsetCurve = nullptr;
	}

	if (bHasForceCurve)
	{
		if (!Map)
		{
			UE_LOG(LogRMS, Error, TEXT("FRMSMotionSource::NetSerialize 缺少 PackageMap，无法序列化牵引距离曲线"));
			bOutSuccess = false;
			return false;
		}
		UObject* ForceCurveObject = Force.StrengthOverDistance.Get();
		if (!Map->SerializeObject(Ar, UCurveFloat::StaticClass(), ForceCurveObject) || Ar.IsError())
		{
			bOutSuccess = false;
			return false;
		}
		// 曲线决定速度大小，解析不到时两端强度不同，必须放弃整个 Source
		if (Ar.IsLoading() && ForceCurveObject == nullptr)
		{
			UE_LOG(LogRMS, Error, TEXT("FRMSMotionSource::NetSerialize 牵引距离曲线在本端无法解析，放弃该 Source"));
			bOutSuccess = false;
			return false;
		}
		Force.StrengthOverDistance = Cast<UCurveFloat>(ForceCurveObject);
	}
	else if (Ar.IsLoading())
	{
		Force.StrengthOverDistance = nullptr;
	}

	if (bHasPullTarget)
	{
		if (!Map)
		{
			UE_LOG(LogRMS, Error, TEXT("FRMSMotionSource::NetSerialize 缺少 PackageMap，无法序列化牵引目标"));
			bOutSuccess = false;
			return false;
		}
		UObject* PullTargetObject = Force.PullTarget.Get();
		if (!Map->SerializeObject(Ar, AActor::StaticClass(), PullTargetObject) || Ar.IsError())
		{
			bOutSuccess = false;
			return false;
		}
		// 牵引目标解析不到不是致命错误（目标可能尚未复制过来），沿用最后已知位置，与跟随目标一致
		Force.PullTarget = Cast<AActor>(PullTargetObject);
	}
	else if (Ar.IsLoading())
	{
		Force.PullTarget = nullptr;
	}

	if (bHasFollow)
	{
		if (!Map)
		{
			UE_LOG(LogRMS, Error, TEXT("FRMSMotionSource::NetSerialize 缺少 PackageMap，无法序列化跟随目标"));
			bOutSuccess = false;
			return false;
		}
		UObject* TargetObject = Follow.TargetActor.Get();
		if (!Map->SerializeObject(Ar, AActor::StaticClass(), TargetObject) || Ar.IsError())
		{
			bOutSuccess = false;
			return false;
		}
		// 跟随目标解析不到不是致命错误（目标 Actor 可能尚未在本端复制过来），
		// 此时保留序列化过来的 TargetLocation，直到目标出现；与动画/曲线不同，不放弃整个 Source
		Follow.TargetActor = Cast<AActor>(TargetObject);
	}
	else if (Ar.IsLoading())
	{
		Follow.TargetActor = nullptr;
	}

	if (bHasAnim)
	{
		if (!Map)
		{
			UE_LOG(LogRMS, Error, TEXT("FRMSMotionSource::NetSerialize 缺少 PackageMap，无法序列化动画"));
			bOutSuccess = false;
			return false;
		}
		UObject* AnimObject = Anim.Animation.Get();
		if (!Map->SerializeObject(Ar, UAnimSequenceBase::StaticClass(), AnimObject) || Ar.IsError())
		{
			bOutSuccess = false;
			return false;
		}
		if (Ar.IsLoading() && AnimObject == nullptr)
		{
			UE_LOG(LogRMS, Error, TEXT("FRMSMotionSource::NetSerialize 动画在本端无法解析，放弃该 Source"));
			bOutSuccess = false;
			return false;
		}
		Anim.Animation = Cast<UAnimSequenceBase>(AnimObject);
	}
	else if (Ar.IsLoading())
	{
		Anim.Animation = nullptr;
	}

	bOutSuccess = true;
	return true;
}

UScriptStruct* FRMSMotionSource::GetScriptStruct() const
{
	return StaticStruct();
}

FString FRMSMotionSource::ToSimpleString() const
{
	return FString::Printf(TEXT("[ID:%u] FRMSMotionSource %s Reach=%s"),
		LocalID, *InstanceName.GetPlainNameString(),
		Params.ReachMode == ERMSReachMode::Guaranteed ? TEXT("Guaranteed") : TEXT("Incremental"));
}

void FRMSMotionSource::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Params.CustomProgressCurve);
	Collector.AddReferencedObject(Params.OffsetCurve);
	Collector.AddReferencedObject(Params.RotationCurve);
	Collector.AddReferencedObject(Anim.Animation);
	Collector.AddReferencedObject(Follow.TargetActor);
	Collector.AddReferencedObject(Force.StrengthOverDistance);
	Collector.AddReferencedObject(Force.PullTarget);
	for (FRMSWarpSegment& Segment : Warp.Segments)
	{
		Collector.AddReferencedObject(Segment.RotationCurve);
	}
}
