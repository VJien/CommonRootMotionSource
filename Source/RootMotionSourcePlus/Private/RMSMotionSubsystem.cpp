// Copyright JTW Studio 2026. All Rights Reserved.

#include "RMSMotionSubsystem.h"
#include "RMSMotionSource.h"
#include "RMSDebug.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

URMSMotionSubsystem* URMSMotionSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<URMSMotionSubsystem>() : nullptr;
}

TStatId URMSMotionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URMSMotionSubsystem, STATGROUP_Tickables);
}

FRMSMotionHandle URMSMotionSubsystem::AllocateHandle()
{
	FRMSMotionHandle Handle;
	Handle.Id = NextHandleId++;
	if (NextHandleId <= 0)
	{
		NextHandleId = 1;
	}
	return Handle;
}

FRMSMotionHandle URMSMotionSubsystem::Register(UCharacterMovementComponent* Movement, const TSharedPtr<FRMSMotionSource>& Source, uint16 RootMotionSourceId, FName DisplayName)
{
	FRMSMotionHandle Handle = AllocateHandle();
	FRMSActiveMotion& Entry = Active.Add(Handle.Id);
	Entry.Handle = Handle;
	Entry.Movement = Movement;
	Entry.Source = Source;
	Entry.RootMotionSourceId = RootMotionSourceId;
	Entry.InstanceName = Source->InstanceName;
	Entry.DisplayName = DisplayName;
	Entry.StartWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Source->DebugHandleId = Handle.Id;
#if !UE_BUILD_SHIPPING
	RMSDebug::RecordEvent(Handle.Id, DisplayName, TEXT("Begin"), GetWorld());
#endif
	return Handle;
}

bool URMSMotionSubsystem::Stop(FRMSMotionHandle Handle, ERMSMotionEndReason Reason)
{
	FRMSActiveMotion* Entry = Active.Find(Handle.Id);
	if (!Entry)
	{
		return false;
	}
	if (UCharacterMovementComponent* Movement = Entry->Movement.Get())
	{
		Movement->RemoveRootMotionSourceByID(Entry->RootMotionSourceId);
	}
	Finish(Handle.Id, Reason);
	return true;
}

bool URMSMotionSubsystem::IsActive(FRMSMotionHandle Handle) const
{
	return Active.Contains(Handle.Id);
}

int32 URMSMotionSubsystem::CountActiveFor(const UCharacterMovementComponent* Movement) const
{
	int32 Count = 0;
	for (const TPair<int32, FRMSActiveMotion>& Pair : Active)
	{
		if (Pair.Value.Movement.Get() == Movement)
		{
			++Count;
		}
	}
	return Count;
}

int32 URMSMotionSubsystem::StopAllFor(const UCharacterMovementComponent* Movement, ERMSMotionEndReason Reason)
{
	// 先收集句柄：Stop 会修改 Active
	TArray<FRMSMotionHandle, TInlineAllocator<8>> Handles;
	for (const TPair<int32, FRMSActiveMotion>& Pair : Active)
	{
		if (Pair.Value.Movement.Get() == Movement)
		{
			Handles.Add(Pair.Value.Handle);
		}
	}
	int32 Count = 0;
	for (const FRMSMotionHandle& Handle : Handles)
	{
		if (Stop(Handle, Reason))
		{
			++Count;
		}
	}
	return Count;
}

FRMSMotionHandle URMSMotionSubsystem::FindActiveByName(const UCharacterMovementComponent* Movement, FName InstanceName) const
{
	if (!InstanceName.IsNone())
	{
		for (const TPair<int32, FRMSActiveMotion>& Pair : Active)
		{
			if (Pair.Value.Movement.Get() == Movement && Pair.Value.InstanceName == InstanceName)
			{
				return Pair.Value.Handle;
			}
		}
	}
	return FRMSMotionHandle();
}

void URMSMotionSubsystem::NotifyFailed(FRMSMotionHandle Handle)
{
#if !UE_BUILD_SHIPPING
	RMSDebug::RecordEvent(Handle.Id, NAME_None, TEXT("Failed"), GetWorld());
#endif
	MotionEnded.Broadcast(Handle, ERMSMotionEndReason::Failed);
}

void URMSMotionSubsystem::Tick(float DeltaTime)
{
	// 先收集再结束：Finish 会修改 Active，且回调可能再次播放运动
	TArray<TPair<int32, ERMSMotionEndReason>, TInlineAllocator<4>> Ended;

	for (const TPair<int32, FRMSActiveMotion>& Pair : Active)
	{
		const FRMSActiveMotion& Entry = Pair.Value;
		UCharacterMovementComponent* Movement = Entry.Movement.Get();
		if (!Movement)
		{
			Ended.Emplace(Pair.Key, ERMSMotionEndReason::Interrupted);
			continue;
		}

		// 不能依赖本地保存的指针：服务器校正可能让 CMC 用新对象替换本地 Source，
		// 因此始终按 ID 向 CMC 查询当前实例
		const TSharedPtr<FRootMotionSource> Current = Movement->GetRootMotionSourceByID(Entry.RootMotionSourceId);
		if (!Current.IsValid())
		{
			// 被 CMC 清理：已走完时长视为完成，否则视为被打断
			const bool bFinished = Entry.Source.IsValid() && Entry.Source->Status.HasFlag(ERootMotionSourceStatusFlags::Finished);
			Ended.Emplace(Pair.Key, bFinished ? ERMSMotionEndReason::Completed : ERMSMotionEndReason::Interrupted);
		}
		else if (Current->Status.HasFlag(ERootMotionSourceStatusFlags::Finished))
		{
			Ended.Emplace(Pair.Key, ERMSMotionEndReason::Completed);
		}
		else if (Current->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval))
		{
			Ended.Emplace(Pair.Key, ERMSMotionEndReason::Interrupted);
		}
	}

	for (const TPair<int32, ERMSMotionEndReason>& End : Ended)
	{
		Finish(End.Key, End.Value);
	}

#if !UE_BUILD_SHIPPING
	RMSDebug::Draw(this);
#endif
}

void URMSMotionSubsystem::Finish(int32 HandleId, ERMSMotionEndReason Reason)
{
	FRMSActiveMotion Entry;
	if (!Active.RemoveAndCopyValue(HandleId, Entry))
	{
		return;
	}
#if !UE_BUILD_SHIPPING
	RMSDebug::RecordEnd(HandleId, Entry.DisplayName, Reason, GetWorld());
#endif
	MotionEnded.Broadcast(Entry.Handle, Reason);
}

void URMSMotionSubsystem::Deinitialize()
{
	// World 销毁：未结束的运动一律按打断通知，避免异步节点永远挂起
	TArray<int32> Ids;
	Active.GetKeys(Ids);
	for (const int32 Id : Ids)
	{
		Finish(Id, ERMSMotionEndReason::Interrupted);
	}
	Super::Deinitialize();
}
