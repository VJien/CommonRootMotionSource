// Copyright JTW Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/RootMotionSource.h"
#include "RMSTypes.h"
#include "RMSMotionSource.generated.h"

struct FRMSMotionShape;

/**
 * 唯一的 Root Motion Source。位移是 (Params, 起点, 终点, 时长, 当前时间) 的纯函数，
 * 因此服务器校正回放与本地预测在同一输入下结果一致。
 */
USTRUCT()
struct ROOTMOTIONSOURCEPLUS_API FRMSMotionSource : public FRootMotionSource
{
	GENERATED_USTRUCT_BODY()

	FRMSMotionSource();

	/** 运动参数；序列化时发送全部数值，只有自定义曲线才走对象引用 */
	FRMSMotionParams Params;

	FVector StartLocation = FVector::ZeroVector;
	FVector TargetLocation = FVector::ZeroVector;

	/** 多点路径的点（不含起点）。网络上逐点发送；弧长表在两端各自由它推出，不上网 */
	TArray<FVector> PathPoints;

	/** 力参数；Kind 非 None 时本 Source 是力，位移相关字段被忽略 */
	FRMSForceParams Force;

	/** 牵引目标最近一次已知位置。目标在本端尚未解析或已销毁时沿用，不单独同步（由服务器 UpdateStateFrom 校正） */
	FVector PullLastKnownTarget = FVector::ZeroVector;

	/** 牵引目标是否曾经解析成功过；没有成功过时不能用 PullLastKnownTarget（它还是初值） */
	bool bPullHasTarget = false;

	/** 是否为无限时长（Duration 小于 0）。引擎不会超时清理，只能 StopMotion 结束 */
	bool IsInfinite() const { return Duration < 0.0f; }

	/** 跟随参数 */
	FRMSFollowParams Follow;

	/**
	 * 是否为跟随运动，由 Apply 时设定并序列化。必须独立于 Follow.TargetActor：
	 * 目标 Actor 在客户端可能尚未复制过来，反序列化得到空指针，此时仍然是跟随运动。
	 */
	bool bIsFollowing = false;

	/** 目标最近一次已知位置，丢失时用它收尾。随 TargetLocation 一并同步，不单独存放 */
	bool bFollowTargetLost = false;

	/** 动画根位移回放参数；Animation 非空时本 Source 按动画回放 */
	FRMSAnimationParams Anim;

	/**
	 * 动画 Warping 目标段（已由 Apply 整理为连续、有序、覆盖整个动画区间的列表）。
	 * 为空表示不做 Warping，只原样回放动画。目标段全部是序列化字段，运行时不读取任何姿态。
	 */
	FRMSWarpParams Warp;

	/**
	 * 动画根位移从组件空间转世界空间所用的旋转，Apply 时取 **Mesh** 的组件旋转并序列化。
	 * 引擎规定动画根位移是组件空间的量（同 USkeletalMeshComponent::ConvertLocalRootMotionToWorld），
	 * 所以这里是数学换算必须用 Mesh，与“朝向基准”无关——FaceMotion 与朝向 Warping 一律以 Actor 朝向为准。
	 * 运行时固定为 Apply 时的值，换来两端一致，代价是动画期间角色转向时位移方向不跟随。
	 */
	FQuat AnimComponentRotation = FQuat::Identity;

	/** 动画有效区间（Apply 时由动画长度解析，序列化以保证两端一致） */
	float AnimRangeStart = 0.0f;
	float AnimRangeEnd = 0.0f;

	/** 调试用，不参与网络 */
	int32 DebugHandleId = 0;

	virtual FRootMotionSource* Clone() const override;
	virtual bool Matches(const FRootMotionSource* Other) const override;
	virtual bool MatchesAndHasSameState(const FRootMotionSource* Other) const override;
	virtual bool UpdateStateFrom(const FRootMotionSource* SourceToTakeStateFrom, bool bMarkForSimulatedCatchup = false) override;
	virtual void PrepareRootMotion(float SimulationTime, float MovementTickTime, const ACharacter& Character, const UCharacterMovementComponent& MoveComponent) override;
	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;
	virtual UScriptStruct* GetScriptStruct() const override;
	virtual FString ToSimpleString() const override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	/**
	 * 刷新跟随目标位置。服务器与客户端各自在 PrepareRootMotion 前调用；
	 * 目标仍然有效时返回 true 并更新 TargetLocation；目标丢失时按策略处理。
	 */
	bool RefreshFollowTarget();

	/**
	 * 动画根位移区间 [From, To] 的世界空间位移（经 AnimComponentRotation 转换，未做 Warping）。
	 * 纯读取动画资产，无副作用。
	 */
	FVector ExtractWorldDelta(float From, float To) const;

	/** 力的一帧：恒力或牵引，直接给速度。 */
	void PrepareForce(float SimulationTime, float MovementTickTime, const ACharacter& Character);

	/** 动画根位移回放的一帧。 */
	void PrepareAnimation(float SimulationTime, float MovementTickTime, const ACharacter& Character);

	/** 某一时刻的期望位置（Actor 位置）。纯计算，不访问角色。 */
	FVector EvaluateExpectedLocation(float Time) const;

	/** 由序列化字段重建几何形状。首次使用或字段变化后调用；是纯函数的缓存，不是状态。 */
	const FRMSMotionShape& GetShape() const;

	/** 形状的终点（折线与样条为最后一个路径点） */
	FVector GetEndpoint() const;

private:
	mutable TSharedPtr<FRMSMotionShape> CachedShape;
	mutable uint32 CachedShapeHash = 0;
};

template<>
struct TStructOpsTypeTraits<FRMSMotionSource> : public TStructOpsTypeTraitsBase2<FRMSMotionSource>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};
