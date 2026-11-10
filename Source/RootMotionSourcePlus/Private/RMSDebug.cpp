// Copyright JTW Studio 2026. All Rights Reserved.

#include "RMSDebug.h"

#if !UE_BUILD_SHIPPING

#include "RMSMotionMath.h"
#include "RMSMotionSource.h"
#include "RMSMotionSubsystem.h"
#include "RMSWarpMath.h"
#include "RMSSettings.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"

namespace
{
	/** -1 表示使用 Project Settings 的默认级别 */
	TAutoConsoleVariable<int32> CVarDebugLevel(
		TEXT("RMS.Debug"), -1,
		TEXT("RMS debug level: 0=Off, 1=Trajectory, 2=Trajectory+HUD, 3=Full; -1=Use Project Settings default."),
		ECVF_Default);

	FString GProfileFilter;

	struct FDebugEvent
	{
		double Time = 0.0;
		int32 HandleId = 0;
		FName Name;
		FString Text;
	};

	TArray<FDebugEvent> GHistory;

	bool PassesFilter(FName DisplayName)
	{
		return GProfileFilter.IsEmpty() || DisplayName.ToString().Contains(GProfileFilter);
	}

	void PushEvent(int32 HandleId, FName DisplayName, const FString& Text, const UWorld* World)
	{
		FDebugEvent& Event = GHistory.AddDefaulted_GetRef();
		Event.Time = World ? World->GetTimeSeconds() : 0.0;
		Event.HandleId = HandleId;
		Event.Name = DisplayName;
		Event.Text = Text;

		const int32 Capacity = URMSSettings::Get()->EventHistoryCapacity;
		if (GHistory.Num() > Capacity)
		{
			GHistory.RemoveAt(0, GHistory.Num() - Capacity, EAllowShrinking::No);
		}
		UE_LOG(LogRMS, Verbose, TEXT("[%d] %s %s"), HandleId, *DisplayName.ToString(), *Text);
	}

	const TCHAR* ReasonToString(ERMSMotionEndReason Reason)
	{
		switch (Reason)
		{
		case ERMSMotionEndReason::Completed:   return TEXT("Completed");
		case ERMSMotionEndReason::Interrupted: return TEXT("Interrupted");
		case ERMSMotionEndReason::Replaced:    return TEXT("Replaced");
		case ERMSMotionEndReason::Failed:      return TEXT("Failed");
		}
		return TEXT("?");
	}

	const TCHAR* RoleToString(ENetRole Role)
	{
		switch (Role)
		{
		case ROLE_Authority:           return TEXT("Auth");
		case ROLE_AutonomousProxy:     return TEXT("Auto");
		case ROLE_SimulatedProxy:      return TEXT("Sim");
		default:                       return TEXT("None");
		}
	}

	void DumpAll(UWorld* World)
	{
		const URMSMotionSubsystem* Subsystem = URMSMotionSubsystem::Get(World);
		UE_LOG(LogRMS, Display, TEXT("---- RMS Active Motions: %d ----"), Subsystem ? Subsystem->GetActiveMotions().Num() : 0);
		if (Subsystem)
		{
			for (const TPair<int32, FRMSActiveMotion>& Pair : Subsystem->GetActiveMotions())
			{
				const FRMSActiveMotion& Entry = Pair.Value;
				UE_LOG(LogRMS, Display, TEXT("  #%d %s Name=%s SourceId=%u %s"),
					Pair.Key, *Entry.DisplayName.ToString(), *Entry.InstanceName.ToString(), Entry.RootMotionSourceId,
					Entry.Source.IsValid() ? *Entry.Source->ToSimpleString() : TEXT("(no source)"));
			}
		}
		UE_LOG(LogRMS, Display, TEXT("---- RMS Recent Events: %d ----"), GHistory.Num());
		for (const FDebugEvent& Event : GHistory)
		{
			UE_LOG(LogRMS, Display, TEXT("  t=%.3f #%d %s %s"), Event.Time, Event.HandleId, *Event.Name.ToString(), *Event.Text);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs CmdDump(
		TEXT("RMS.Dump"), TEXT("Dump active motions and recent event history."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) { DumpAll(World); }));

	FAutoConsoleCommandWithWorldAndArgs CmdFilter(
		TEXT("RMS.Filter"), TEXT("Filter displayed motions by InstanceName substring; pass no args to clear."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			GProfileFilter = Args.Num() > 0 ? Args[0] : FString();
			UE_LOG(LogRMS, Display, TEXT("RMS.Filter = \"%s\""), *GProfileFilter);
		}));

}

void RMSDebug::RecordEvent(int32 HandleId, FName DisplayName, const TCHAR* EventName, const UWorld* World)
{
	PushEvent(HandleId, DisplayName, EventName, World);
}

void RMSDebug::RecordEnd(int32 HandleId, FName DisplayName, ERMSMotionEndReason Reason, const UWorld* World)
{
	PushEvent(HandleId, DisplayName, FString::Printf(TEXT("End:%s"), ReasonToString(Reason)), World);
}

int32 RMSDebug::GetLevel()
{
	const int32 Value = CVarDebugLevel.GetValueOnGameThread();
	return Value >= 0 ? Value : URMSSettings::Get()->DefaultDebugLevel;
}

void RMSDebug::Draw(const URMSMotionSubsystem* Subsystem)
{
	const int32 Level = GetLevel();
	UWorld* World = Subsystem ? Subsystem->GetWorld() : nullptr;
	if (Level <= 0 || !World)
	{
		return;
	}

	const int32 Samples = URMSSettings::Get()->TrajectorySamples;
	for (const TPair<int32, FRMSActiveMotion>& Pair : Subsystem->GetActiveMotions())
	{
		const FRMSActiveMotion& Entry = Pair.Value;
		if (!PassesFilter(Entry.DisplayName) || !Entry.Source.IsValid())
		{
			continue;
		}

		const FRMSMotionSource& Source = *Entry.Source;
		const float Duration = FMath::Max(Source.GetDuration(), UE_SMALL_NUMBER);
		const float Progress = FMath::Clamp(Source.GetTime() / Duration, 0.0f, 1.0f);

		const UCharacterMovementComponent* ForceMovement = Entry.Movement.Get();
		const AActor* ForceOwner = ForceMovement ? ForceMovement->GetOwner() : nullptr;

		// 力没有终点与时间轨迹：只画力方向、牵引连线与停止距离圈，然后进入下一个运动
		if (Source.Force.IsForce())
		{
			if (ForceOwner)
			{
				const FVector Origin = ForceOwner->GetActorLocation();
				if (Source.Force.Kind == ERMSForceKind::Constant)
				{
					const FVector Direction = Source.Force.Direction.GetSafeNormal();
					DrawDebugDirectionalArrow(World, Origin, Origin + Direction * 150.0f, 30.0f, FColor::Orange, false, -1.0f, 0, 3.0f);
					DrawDebugString(World, Origin + Direction * 150.0f + FVector(0, 0, 20), FString::Printf(TEXT("Constant %.0f cm/s"), Source.Force.Strength), nullptr, FColor::Orange, 0.0f, true, 1.0f);
				}
				else
				{
					if (Source.bPullHasTarget)
					{
						const FVector Target = Source.PullLastKnownTarget;
						DrawDebugLine(World, Origin, Target, FColor::Blue, false, -1.0f, 0, 2.0f);
						DrawDebugSphere(World, Target, 14.0f, 8, Source.Force.PullTarget ? FColor::Blue : FColor::Red, false, -1.0f);
						// 停止距离圈：以目标为中心，角色进入此圈后速度为 0
						DrawDebugCircle(World, Target, Source.Force.StopDistance, 32, FColor::Cyan, false, -1.0f, 0, 2.0f, FVector(1, 0, 0), FVector(0, 1, 0), false);
						DrawDebugString(World, Target + FVector(0, 0, 30), FString::Printf(TEXT("Pull %.0f cm/s StopDist %.0f%s"), Source.Force.Strength, Source.Force.StopDistance, Source.Force.PullTarget ? TEXT("") : TEXT(" (Target Unresolved)")), nullptr, FColor::Blue, 0.0f, true, 1.0f);
					}
				}
				DrawDebugSphere(World, Origin, 12.0f, 8, FColor::Yellow, false, -1.0f);
			}
			if (Level >= 2 && GEngine)
			{
				const FString ForceText = FString::Printf(TEXT("RMS #%d %s [%s] %s  %s"),
					Pair.Key, *Entry.DisplayName.ToString(), Source.Force.Kind == ERMSForceKind::Constant ? TEXT("Force:Constant") : TEXT("Force:Pull"),
					Source.IsInfinite() ? TEXT("Infinite") : *FString::Printf(TEXT("Rem %.2fs"), FMath::Max(Duration - Source.GetTime(), 0.0f)),
					ForceOwner ? RoleToString(ForceOwner->GetLocalRole()) : TEXT("?"));
				GEngine->AddOnScreenDebugMessage(static_cast<uint64>(0x524D5300) + Pair.Key, 0.0f, FColor::White, ForceText);
			}
			continue;
		}

		// 预期轨迹
		FVector Previous = Source.EvaluateExpectedLocation(0.0f);
		for (int32 Index = 1; Index <= Samples; ++Index)
		{
			const FVector Next = Source.EvaluateExpectedLocation(Duration * Index / Samples);
			DrawDebugLine(World, Previous, Next, FColor::Cyan, false, -1.0f, 0, 1.5f);
			Previous = Next;
		}
		DrawDebugSphere(World, Source.StartLocation, 10.0f, 8, FColor::Green, false, -1.0f);
		DrawDebugSphere(World, Source.GetEndpoint(), 10.0f, 8, FColor::Red, false, -1.0f);
		const FVector LabelUp(0, 0, 25);
		DrawDebugString(World, Source.StartLocation + LabelUp, TEXT("Start"), nullptr, FColor::Green, 0.0f, true, 1.0f);
		DrawDebugString(World, Source.GetEndpoint() + LabelUp, FString::Printf(TEXT("End  %.2fs"), Duration), nullptr, FColor::Red, 0.0f, true, 1.0f);
		// 多点路径：标出并编号每个中途点，便于核对匀速参数化是否经过所有点
		// 最后一个路径点就是终点，不重复编号
		for (int32 Index = 0; Index < Source.PathPoints.Num(); ++Index)
		{
			const bool bIsEndpoint = (Source.Params.PathKind == ERMSPathKind::Polyline || Source.Params.PathKind == ERMSPathKind::Spline) && Index == Source.PathPoints.Num() - 1;
			if (bIsEndpoint)
			{
				continue;
			}
			DrawDebugSphere(World, Source.PathPoints[Index], 6.0f, 6, FColor::Orange, false, -1.0f);
			DrawDebugString(World, Source.PathPoints[Index] + LabelUp, FString::Printf(TEXT("Point %d"), Index + 1), nullptr, FColor::Orange, 0.0f, true, 1.0f);
		}
		// 时间刻度：沿轨迹每 25% 时长标一个，直观看出各段耗时
		for (int32 Tick = 1; Tick <= 3; ++Tick)
		{
			const float TickTime = Duration * Tick * 0.25f;
			DrawDebugPoint(World, Source.EvaluateExpectedLocation(TickTime), 8.0f, FColor::White, false, -1.0f);
			DrawDebugString(World, Source.EvaluateExpectedLocation(TickTime) + FVector(0, 0, 12), FString::Printf(TEXT("%.2fs"), TickTime), nullptr, FColor::White, 0.0f, true, 0.8f);
		}
		// 弧长进度：标出当前已走过的位置，用于核对匀速（等时间间隔应等距离）
		DrawDebugSphere(World, Source.EvaluateExpectedLocation(Source.GetTime()), 8.0f, 6, FColor::Magenta, false, -1.0f);

		// 动画 Warping：画出每个目标段的目标点与区间，当前段高亮
		if (Source.Warp.IsValid())
		{
			const int32 CurrentIndex = Source.Anim.IsValid()
				? RMSWarp::FindSegmentIndex(Source.Warp.Segments, RMSMath::RealTimeToAnimTime(Source.AnimRangeStart, Source.AnimRangeEnd, Source.Anim.Rate, Source.GetTime()))
				: INDEX_NONE;
			for (int32 Index = 0; Index < Source.Warp.Segments.Num(); ++Index)
			{
				const FRMSWarpSegment& Segment = Source.Warp.Segments[Index];
				if (!Segment.bWarp)
				{
					// 间隙段不 Warping，只画一条竖线表示区间边界
					continue;
				}
				const bool bCurrent = Index == CurrentIndex;
				DrawDebugSphere(World, Segment.Target, bCurrent ? 18.0f : 10.0f, 10, bCurrent ? FColor::Red : FColor::Silver, false, -1.0f);
				DrawDebugString(World, Segment.Target + FVector(0, 0, 30), FString::Printf(TEXT("Warp[%d] %.2f-%.2f"), Index, Segment.WindowStart, Segment.WindowEnd), nullptr, bCurrent ? FColor::Red : FColor::Silver, 0.0f, true, 1.0f);
			}
		}

		// 跟随目标：画出目标 Actor 与当前跟随点
		if (Source.bIsFollowing)
		{
			if (const AActor* FollowTarget = Source.Follow.TargetActor.Get())
			{
				DrawDebugSphere(World, FollowTarget->GetActorLocation(), 14.0f, 8, FColor::Blue, false, -1.0f);
				DrawDebugLine(World, FollowTarget->GetActorLocation(), Source.TargetLocation, FColor::Blue, false, -1.0f, 0, 2.0f);
			}
			DrawDebugSphere(World, Source.TargetLocation, 10.0f, 8, Source.bFollowTargetLost ? FColor::Red : FColor::Blue, false, -1.0f);
		}

		const UCharacterMovementComponent* Movement = Entry.Movement.Get();
		const AActor* Owner = Movement ? Movement->GetOwner() : nullptr;
		if (Owner)
		{
			DrawDebugSphere(World, Owner->GetActorLocation(), 12.0f, 8, FColor::Yellow, false, -1.0f);
		}

		if (Level >= 2 && GEngine)
		{
			FString Kind;
			if (Source.Anim.IsValid())
			{
				Kind = Source.Warp.IsValid() ? TEXT("Anim+Warp") : TEXT("Anim");
			}
			else
			{
				switch (Source.Params.PathKind)
				{
				case ERMSPathKind::Polyline: Kind = TEXT("Polyline"); break;
				case ERMSPathKind::Spline:   Kind = TEXT("Spline"); break;
				case ERMSPathKind::Parabola: Kind = TEXT("Parabola"); break;
				default:                     Kind = TEXT("Straight"); break;
				}
			}
			if (Source.bIsFollowing)
			{
				Kind += Source.bFollowTargetLost ? TEXT("+FollowLost") : TEXT("+Follow");
			}

			const FString Text = FString::Printf(TEXT("RMS #%d %s [%s] %.0f%%  Rem %.2fs  P%d  %s  %s"),
				Pair.Key, *Entry.DisplayName.ToString(), *Kind, Progress * 100.0f, FMath::Max(Duration - Source.GetTime(), 0.0f),
				static_cast<int32>(Source.Priority), Source.Params.ReachMode == ERMSReachMode::Guaranteed ? TEXT("Guar") : TEXT("Inc"),
				Owner ? RoleToString(Owner->GetLocalRole()) : TEXT("?"));
			GEngine->AddOnScreenDebugMessage(static_cast<uint64>(0x524D5300) + Pair.Key, 0.0f, FColor::White, Text);
		}
	}
}

#endif
