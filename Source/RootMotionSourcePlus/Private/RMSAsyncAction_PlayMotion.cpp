// Copyright JTW Studio 2026. All Rights Reserved.

#include "RMSAsyncAction_PlayMotion.h"
#include "RMSMotionLibrary.h"
#include "RMSMotionSubsystem.h"
#include "GameFramework/Character.h"

URMSAsyncAction_PlayMotion* URMSAsyncAction_PlayMotion::PlayMotionAndWait(ACharacter* Character, FRMSMotionRequest Request)
{
	URMSAsyncAction_PlayMotion* Action = NewObject<URMSAsyncAction_PlayMotion>();
	Action->TargetCharacter = Character;
	Action->PendingRequest = Request;
	Action->RegisterWithGameInstance(Character);
	return Action;
}

void URMSAsyncAction_PlayMotion::Activate()
{
	ACharacter* Character = TargetCharacter.Get();
	URMSMotionSubsystem* Subsystem = URMSMotionSubsystem::Get(Character ? Character->GetWorld() : nullptr);
	if (!Subsystem)
	{
		OnFailed.Broadcast(FRMSMotionHandle(), ERMSMotionEndReason::Failed);
		SetReadyToDestroy();
		return;
	}

	// 先订阅再播放：非法请求会在 PlayMotion 内同步广播 Failed，晚订阅会漏掉
	EndedBinding = Subsystem->OnMotionEnded().AddUObject(this, &URMSAsyncAction_PlayMotion::HandleEnded);
	ActiveHandle = URMSMotionLibrary::PlayMotion(Character, PendingRequest);
}

void URMSAsyncAction_PlayMotion::HandleEnded(FRMSMotionHandle EndedHandle, ERMSMotionEndReason Reason)
{
	// 取得有效 Handle 后只响应自己的；Failed 发生在 PlayMotion 返回之前，此时 ActiveHandle 仍无效
	const bool bMine = ActiveHandle.IsValid() ? (EndedHandle == ActiveHandle) : (Reason == ERMSMotionEndReason::Failed);
	if (!bMine)
	{
		return;
	}

	Unbind();
	if (ShouldBroadcastDelegates())
	{
		switch (Reason)
		{
		case ERMSMotionEndReason::Completed:
			OnCompleted.Broadcast(EndedHandle, Reason);
			break;
		case ERMSMotionEndReason::Failed:
			OnFailed.Broadcast(EndedHandle, Reason);
			break;
		default:
			OnInterrupted.Broadcast(EndedHandle, Reason);
			break;
		}
	}
	SetReadyToDestroy();
}

void URMSAsyncAction_PlayMotion::Cancel()
{
	Unbind();
	if (const ACharacter* Character = TargetCharacter.Get())
	{
		URMSMotionLibrary::StopMotion(Character, ActiveHandle);
	}
	Super::Cancel();
}

void URMSAsyncAction_PlayMotion::Unbind()
{
	if (!EndedBinding.IsValid())
	{
		return;
	}
	if (const ACharacter* Character = TargetCharacter.Get())
	{
		if (URMSMotionSubsystem* Subsystem = URMSMotionSubsystem::Get(Character->GetWorld()))
		{
			Subsystem->OnMotionEnded().Remove(EndedBinding);
		}
	}
	EndedBinding.Reset();
}
