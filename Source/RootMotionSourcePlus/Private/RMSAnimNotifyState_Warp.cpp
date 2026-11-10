// Copyright JTW Studio 2026. All Rights Reserved.

#include "RMSAnimNotifyState_Warp.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"

FString URMSAnimNotifyState_Warp::GetNotifyName_Implementation() const
{
	return FString::Printf(TEXT("RMS Warp: %s"), *TargetName.ToString());
}

FRMSWarpParams URMSWarpLibrary::BuildWarpFromAnimation(const UAnimSequenceBase* Animation, const TArray<FName>& TargetNames, const TArray<FVector>& TargetLocations)
{
	FRMSWarpParams Result;
	if (!Animation)
	{
		UE_LOG(LogRMS, Warning, TEXT("BuildWarpFromAnimation：动画为空"));
		return Result;
	}
	if (TargetNames.Num() != TargetLocations.Num())
	{
		UE_LOG(LogRMS, Warning, TEXT("BuildWarpFromAnimation：目标名数量 %d 与位置数量 %d 不一致，已忽略多出的部分"), TargetNames.Num(), TargetLocations.Num());
	}
	const int32 MappingCount = FMath::Min(TargetNames.Num(), TargetLocations.Num());

	for (const FAnimNotifyEvent& Event : Animation->Notifies)
	{
		const URMSAnimNotifyState_Warp* Marker = Cast<URMSAnimNotifyState_Warp>(Event.NotifyStateClass);
		if (!Marker)
		{
			continue;
		}

		int32 Found = INDEX_NONE;
		for (int32 Index = 0; Index < MappingCount; ++Index)
		{
			if (TargetNames[Index] == Marker->TargetName)
			{
				Found = Index;
				break;
			}
		}
		if (Found == INDEX_NONE)
		{
			UE_LOG(LogRMS, Warning, TEXT("BuildWarpFromAnimation：窗口目标名 %s 没有对应位置，该时间段按原样播放"), *Marker->TargetName.ToString());
			continue;
		}

		FRMSWarpSegment Segment;
		Segment.WindowStart = Event.GetTriggerTime();
		Segment.WindowEnd = Event.GetEndTriggerTime();
		Segment.Target = TargetLocations[Found];
		Segment.bWarp = true;
		Result.Segments.Add(Segment);
	}
	return Result;
}
