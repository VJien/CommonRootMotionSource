// Copyright JTW Studio 2026. All Rights Reserved.

#include "RMSMotionMath.h"
#include "RMSPathMath.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"

float RMSMath::ClampDuration(float Duration)
{
	return FMath::Max(Duration, MinDuration);
}

float RMSMath::ClampRate(float Rate)
{
	return FMath::Max(Rate, MinRate);
}

void RMSMath::ResolveAnimRange(float PlayLength, float AnimStart, float AnimEnd, float& OutStart, float& OutEnd)
{
	const float Length = FMath::Max(PlayLength, 0.0f);
	OutStart = FMath::Clamp(AnimStart, 0.0f, Length);
	const float RequestedEnd = (AnimEnd <= 0.0f || AnimEnd > Length) ? Length : AnimEnd;
	OutEnd = FMath::Max(RequestedEnd, OutStart);
}

float RMSMath::AnimRealDuration(float RangeStart, float RangeEnd, float Rate)
{
	return ClampDuration((RangeEnd - RangeStart) / ClampRate(Rate));
}

float RMSMath::RealTimeToAnimTime(float RangeStart, float RangeEnd, float Rate, float RealTime)
{
	return FMath::Clamp(RangeStart + FMath::Max(RealTime, 0.0f) * ClampRate(Rate), RangeStart, RangeEnd);
}

FVector RMSMath::FilterAnimTranslation(const FVector& Translation, bool bIgnoreZ)
{
	return bIgnoreZ ? FVector(Translation.X, Translation.Y, 0.0) : Translation;
}

FVector RMSMath::AnimDeltaToWorld(const FVector& ComponentSpaceDelta, const FQuat& ComponentRotation)
{
	return ComponentRotation.RotateVector(ComponentSpaceDelta);
}

float RMSMath::ApplyEase(ERMSEase Ease, float Alpha)
{
	const float T = FMath::Clamp(Alpha, 0.0f, 1.0f);
	switch (Ease)
	{
	case ERMSEase::EaseIn:    return T * T;
	case ERMSEase::EaseOut:   return 1.0f - (1.0f - T) * (1.0f - T);
	case ERMSEase::EaseInOut: return T * T * (3.0f - 2.0f * T);
	case ERMSEase::Linear:
	default:                  return T;
	}
}

float RMSMath::EvaluateProgress(const FRMSMotionParams& Params, float TimeAlpha)
{
	const float T = FMath::Clamp(TimeAlpha, 0.0f, 1.0f);
	return Params.CustomProgressCurve ? Params.CustomProgressCurve->GetFloatValue(T) : ApplyEase(Params.Ease, T);
}

float RMSMath::EvaluateArc(float ArcHeight, float TimeAlpha)
{
	return ArcHeight * FMath::Sin(UE_PI * FMath::Clamp(TimeAlpha, 0.0f, 1.0f));
}

float RMSMath::EvaluateParabola(float Peak, float ProgressAlpha)
{
	const float U = FMath::Clamp(ProgressAlpha, 0.0f, 1.0f);
	return 4.0f * Peak * U * (1.0f - U);
}

void RMSMath::BuildShape(ERMSPathKind Kind, const FVector& Start, const FVector& Target, const TArray<FVector>& PathPoints, float ArcHeight, FRMSMotionShape& OutShape)
{
	OutShape.Kind = Kind;
	OutShape.Start = Start;
	OutShape.Target = Target;
	OutShape.ArcHeight = ArcHeight;
	OutShape.Polyline.Reset();
	OutShape.Cumulative.Reset();

	if (Kind == ERMSPathKind::Polyline || Kind == ERMSPathKind::Spline)
	{
		if (PathPoints.Num() == 0)
		{
			// 没有路径点：退化为起点到终点的直线，避免出现“空路径永不结束”
			OutShape.Kind = ERMSPathKind::Straight;
			return;
		}
		TArray<FVector> WithStart;
		WithStart.Reserve(PathPoints.Num() + 1);
		WithStart.Add(Start);
		WithStart.Append(PathPoints);
		RMSPath::BuildPolyline(WithStart, Kind == ERMSPathKind::Spline, OutShape.Polyline);
		RMSPath::BuildCumulativeLength(OutShape.Polyline, OutShape.Cumulative);
	}
}

FVector RMSMath::ShapeEndpoint(const FRMSMotionShape& Shape)
{
	if ((Shape.Kind == ERMSPathKind::Polyline || Shape.Kind == ERMSPathKind::Spline) && Shape.Polyline.Num() > 0)
	{
		return Shape.Polyline.Last();
	}
	return Shape.Target;
}

FVector RMSMath::EvaluateShape(const FRMSMotionShape& Shape, float ProgressAlpha)
{
	const float U = FMath::Clamp(ProgressAlpha, 0.0f, 1.0f);
	switch (Shape.Kind)
	{
	case ERMSPathKind::Polyline:
	case ERMSPathKind::Spline:
		return RMSPath::SampleAtDistance(Shape.Polyline, Shape.Cumulative, U * RMSPath::TotalLength(Shape.Cumulative));
	case ERMSPathKind::Parabola:
	{
		FVector Position = FMath::Lerp(Shape.Start, Shape.Target, static_cast<double>(U));
		Position.Z += EvaluateParabola(Shape.ArcHeight, U);
		return Position;
	}
	case ERMSPathKind::Straight:
	default:
		return FMath::Lerp(Shape.Start, Shape.Target, static_cast<double>(U));
	}
}

FVector RMSMath::EvaluatePosition(const FRMSMotionParams& Params, const FRMSMotionShape& Shape, float TimeAlpha)
{
	const float Progress = EvaluateProgress(Params, TimeAlpha);
	FVector Position = EvaluateShape(Shape, Progress);
	// 偏移相对于整条运动的起点与终点（多点路径取最后一点），与路径形状无关
	Position += EvaluateOffset(Params.OffsetCurve, Progress, Shape.Start, ShapeEndpoint(Shape));
	// 直线与多点路径使用 Params.ArcHeight 的正弦弧线；抛物线自带高度，不再叠加
	if (Shape.Kind != ERMSPathKind::Parabola)
	{
		Position.Z += EvaluateArc(Params.ArcHeight, TimeAlpha);
	}
	return Position;
}

FVector RMSMath::OffsetToWorld(const FVector& LocalOffset, const FVector& Start, const FVector& End)
{
	FVector Forward(End.X - Start.X, End.Y - Start.Y, 0.0);
	if (!Forward.Normalize())
	{
		Forward = FVector::XAxisVector;
	}
	// 右手系、Z 向上：前进方向 X 转到右侧 Y
	const FVector Right = FVector::CrossProduct(FVector::ZAxisVector, Forward);
	return Forward * LocalOffset.X + Right * LocalOffset.Y + FVector::ZAxisVector * LocalOffset.Z;
}

FVector RMSMath::EvaluateOffset(const UCurveVector* Curve, float ProgressAlpha, const FVector& Start, const FVector& End)
{
	if (!Curve)
	{
		return FVector::ZeroVector;
	}
	return OffsetToWorld(Curve->GetVectorValue(FMath::Clamp(ProgressAlpha, 0.0f, 1.0f)), Start, End);
}

FQuat RMSMath::ComputeFacingDelta(const FQuat& CurrentRotation, const FVector& Velocity)
{
	const FVector Horizontal(Velocity.X, Velocity.Y, 0.0);
	if (Horizontal.IsNearlyZero(1.0))
	{
		return FQuat::Identity;
	}
	const FQuat Desired = Horizontal.ToOrientationQuat();
	// 只取偏航差，避免把俯仰/翻滚也转进角色
	const float DeltaYaw = FMath::FindDeltaAngleDegrees(CurrentRotation.Rotator().Yaw, Desired.Rotator().Yaw);
	return FQuat(FVector::UpVector, FMath::DegreesToRadians(DeltaYaw));
}

FVector RMSMath::ApplyAxisLock(const FVector& Vector, const FVector& LockedAxes)
{
	return FVector(
		LockedAxes.X != 0.0 ? 0.0 : Vector.X,
		LockedAxes.Y != 0.0 ? 0.0 : Vector.Y,
		LockedAxes.Z != 0.0 ? 0.0 : Vector.Z);
}

FVector RMSMath::ComputeVelocity(const FRMSMotionParams& Params, const FRMSMotionShape& Shape, float Duration,
	float CurrentTime, float SimTime, float StepTime, const FVector& ActualLocation)
{
	if (StepTime <= UE_SMALL_NUMBER)
	{
		return FVector::ZeroVector;
	}

	const float SafeDuration = ClampDuration(Duration);
	const FVector Next = EvaluatePosition(Params, Shape, (CurrentTime + SimTime) / SafeDuration);

	if (Params.ReachMode == ERMSReachMode::Incremental)
	{
		const FVector Now = EvaluatePosition(Params, Shape, CurrentTime / SafeDuration);
		return (Next - Now) / StepTime;
	}
	return (Next - ActualLocation) / StepTime;
}

namespace
{
	/** 方向归一；零向量返回零向量，调用方据此退化为原地不动 */
	FVector SafeDirection(const FVector& Direction)
	{
		return Direction.GetSafeNormal();
	}
}

void RMSMath::PresetDash(const FVector& Start, const FVector& Direction, float Distance, float Duration, FRMSMotionParams& OutParams, FVector& OutTarget)
{
	OutTarget = Start + SafeDirection(Direction) * FMath::Max(Distance, 0.0f);
	OutParams = FRMSMotionParams();
	OutParams.Duration = ClampDuration(Duration);
	OutParams.PathKind = ERMSPathKind::Straight;
	OutParams.Ease = ERMSEase::EaseOut;
	// 冲刺被挡就停，不往墙里找补
	OutParams.ReachMode = ERMSReachMode::Incremental;
	OutParams.FinishVelocity = ERMSFinishVelocity::Zero;
	OutParams.RotationMode = ERMSRotationMode::FaceMotion;
}

void RMSMath::PresetKnockback(const FVector& Start, const FVector& Direction, float Distance, float Duration, float Lift, FRMSMotionParams& OutParams, FVector& OutTarget)
{
	OutTarget = Start + SafeDirection(Direction) * FMath::Max(Distance, 0.0f);
	OutParams = FRMSMotionParams();
	OutParams.Duration = ClampDuration(Duration);
	OutParams.PathKind = ERMSPathKind::Straight;
	OutParams.Ease = ERMSEase::EaseOut;
	OutParams.ArcHeight = FMath::Max(Lift, 0.0f);
	OutParams.ReachMode = ERMSReachMode::Incremental;
	OutParams.FinishVelocity = ERMSFinishVelocity::Zero;
	// 被击退时角色不应转向运动方向
	OutParams.RotationMode = ERMSRotationMode::None;
}

void RMSMath::PresetJump(const FVector& Start, const FVector& Target, float ApexHeight, float Duration, FRMSMotionParams& OutParams)
{
	OutParams = FRMSMotionParams();
	OutParams.Duration = ClampDuration(Duration);
	OutParams.PathKind = ERMSPathKind::Parabola;
	// 跳跃要精确落到目标；抛物线自带高度，顶点高度由 ArcHeight 给出
	OutParams.ArcHeight = FMath::Max(ApexHeight, 0.0f);
	OutParams.Ease = ERMSEase::Linear;
	OutParams.ReachMode = ERMSReachMode::Guaranteed;
	// 跳跃落点要精确：到点后速度清零。初版用 Keep（保持最后一帧速度），真实角色测试发现角色会带着速度冲过落点并继续下坠
	OutParams.FinishVelocity = ERMSFinishVelocity::Zero;
	OutParams.RotationMode = ERMSRotationMode::FaceMotion;
}

FVector RMSMath::ComputeConstantForceVelocity(const FVector& Direction, float Strength)
{
	const float Safe = FMath::Clamp(Strength, 0.0f, MaxForceStrength);
	return Direction.GetSafeNormal() * Safe;
}

FVector RMSMath::ComputePullVelocity(const FVector& From, const FVector& Target, float Strength, float StopDistance, bool bIgnoreZ, const UCurveFloat* StrengthOverDistance)
{
	FVector Delta = Target - From;
	if (bIgnoreZ)
	{
		Delta.Z = 0.0;
	}
	const double Distance = Delta.Size();
	const float Stop = FMath::Clamp(StopDistance, 0.0f, MaxStopDistance);
	if (Distance <= static_cast<double>(Stop) || Distance <= UE_SMALL_NUMBER)
	{
		return FVector::ZeroVector;
	}

	float Multiplier = 1.0f;
	if (StrengthOverDistance)
	{
		Multiplier = FMath::Max(StrengthOverDistance->GetFloatValue(static_cast<float>(Distance)), 0.0f);
	}
	return (Delta / Distance) * (FMath::Clamp(Strength, 0.0f, MaxForceStrength) * Multiplier);
}

float RMSMath::EvaluateRotationProgress(const UCurveFloat* RotationCurve, float TimeAlpha)
{
	const float T = FMath::Clamp(TimeAlpha, 0.0f, 1.0f);
	const float S = RotationCurve ? RotationCurve->GetFloatValue(T) : T;
	// 钳到 0~1：曲线末端必须为 1，否则段结束时会停在半路
	return FMath::Clamp(S, 0.0f, 1.0f);
}

float RMSMath::ComputeRotationFraction(const UCurveFloat* RotationCurve, float SegmentStart, float SegmentEnd, float FromTime, float ToTime)
{
	const float Span = SegmentEnd - SegmentStart;
	if (Span <= UE_KINDA_SMALL_NUMBER)
	{
		// 段长为 0：没有可分配的进度。调用方应把退化段当作“立即转到位”处理
		return ToTime >= SegmentEnd ? 1.0f : 0.0f;
	}

	// 段内时间进度；FromTime 与 ToTime 都被钳进本段，因此跨帧进入或离开本段时不会算出越界比例
	const float FromAlpha = FMath::Clamp((FromTime - SegmentStart) / Span, 0.0f, 1.0f);
	const float ToAlpha = FMath::Clamp((ToTime - SegmentStart) / Span, 0.0f, 1.0f);

	const float FromS = EvaluateRotationProgress(RotationCurve, FromAlpha);
	const float ToS = EvaluateRotationProgress(RotationCurve, ToAlpha);

	// 段末强制补足：s 到 1 时剩余误差必须一次清掉，否则会因浮点残差停在差一点的位置
	if (ToS >= 1.0f)
	{
		return 1.0f;
	}

	const float Remaining = 1.0f - FromS;
	if (Remaining <= UE_KINDA_SMALL_NUMBER)
	{
		// 进度已经不剩（曲线末段平坦且本帧没有推进）：不转，避免除零
		return 0.0f;
	}
	return FMath::Clamp((ToS - FromS) / Remaining, 0.0f, 1.0f);
}

FQuat RMSMath::ComputeYawRotationDelta(const FQuat& CurrentRotation, float TargetYaw, float Fraction)
{
	const float Amount = FMath::Clamp(Fraction, 0.0f, 1.0f);
	if (Amount <= UE_KINDA_SMALL_NUMBER)
	{
		return FQuat::Identity;
	}
	// 只绕世界 Z 轴转，沿最短弧；Yaw 之外的俯仰/翻滚不参与，角色不会歪
	const float DeltaYaw = FMath::FindDeltaAngleDegrees(CurrentRotation.Rotator().Yaw, TargetYaw);
	const float StepYaw = DeltaYaw * Amount;
	if (FMath::IsNearlyZero(StepYaw, 0.001f))
	{
		return FQuat::Identity;
	}
	return FQuat(FVector::UpVector, FMath::DegreesToRadians(StepYaw));
}

