# 联网

## 两端各自触发

插件**不做**“一端发起、自动同步到另一端”。典型用法是在 GAS 能力或输入处理里，两端各自调用 `PlayMotion`：

```cpp
// 服务器与预测客户端都会执行到这段
URMSMotionLibrary::PlayMotion(Character, Request);
```

两端必须给出**相同的参数**，否则 `Matches` 会把它们当成两个不同的 Source。

注意参数要**逐位一致**：起终点、路径点、时长这些值如果来自各端各自的计算（比如各自读角色位置），可能有细微差异。能由一端确定的值，请由一端确定后再同步给另一端。

### 为什么构造器不收角色指针

所有 `Make*Request` 都只接受显式的 `FVector StartLocation`，不接受 `ACharacter*`。这不是风格偏好，是联网正确性：

```cpp
// 危险做法（插件刻意不提供）：节点内部读 Character->GetActorLocation()
//   服务器：起点 = (1200.03, 800.01, 88.0)
//   客户端：起点 = (1200.07, 799.98, 88.0)   ← 预测位置有细微差异
//   → Matches 判定不同 → 服务器校正被丢弃
```

正确做法是由一端算好后显式传入：

```cpp
const FVector Start = URMSMotionLibrary::GetCenterLocation(Character);
const FRMSMotionRequest Request = URMSMotionLibrary::MakeDashRequest(Start, Direction, 600.0f, 0.3f);
URMSMotionLibrary::PlayMotion(Character, Request);   // 两端用同一个 Request
```

同理，`Request.StartLocation` 为零向量时插件会写 Warning——通常意味着忘了填。

## `Matches` 收什么

引擎用它把服务器校正的数据配到本地的 Source 上。本插件比较：

- 基类字段：优先级、叠加模式、实例名、时长
- 路径种类、缓动、到达模式、弧线高度、朝向、结束速度、缩放限幅、自定义曲线/偏移曲线引用
- 位移运动的朝向：`RotationMode`、`FaceTarget` 时的 `TargetRotation.Yaw`、`RotationCurve` 引用
- 起点、终点、每一个路径点（容差 0.1 厘米）
- 动画资产、速率、是否忽略 Z、动画区间、转换旋转
- Warping 的每一个目标段：窗口、目标、`bWarp`、`bWarpRotation`、`TargetRotation.Yaw`、`RotationCurve` 引用
- 跟随：是否跟随、目标 Actor 身份、世界偏移、丢失策略（**不比较目标位置**）

任何一项变了就是不同的 Source。这也是旧实现最容易出错的地方——漏一项就会让校正悄悄失效。

## 序列化什么

`NetSerialize` 发送的字段与上面 `Matches` 比较的字段基本一一对应，包括全部数值参数。

对象引用（自定义曲线、偏移曲线、动画资产、跟随目标）经 `PackageMap` 传输：

| 情形 | 行为 |
| --- | --- |
| 曲线或动画在本端解析不到 | **放弃该 Source**，写错误日志。不会悄悄退化成“无曲线 / 无位移” |
| 跟随目标在本端解析不到 | **不放弃**。沿用同步过来的目标位置继续运动，等目标出现 |

这个区别是有意的：曲线和动画决定整条轨迹，退化会让两端走出不同路径；跟随目标只是“终点在哪”，沿用已知位置是安全的。

## 数量上限

- 路径点 64 个
- Warping 目标段 64 个

接收端读到超限数量会拒绝整个包（`bOutSuccess = false`），避免恶意或损坏的包撑爆内存。

## `Incremental` 模式的特别说明

`Incremental` 下，两端被挡的帧数不同会导致位置**永久分叉**。插件没有额外的补偿机制，依赖角色移动组件自带的服务器位置校正收敛。

如果看到持续拉扯，先用 `RMS.Debug 2` 看两端的位置，再检查是不是这个原因。

## 模拟代理

`PrepareRootMotion` 只依赖序列化字段与动画资产，不读本地的模型当前姿态（动画转换旋转在播放开始时取值并序列化）。这是为了让模拟代理和服务器的轨迹一致。

## 尚未验证的部分

以下在开发阶段只有纯函数与序列化测试，**没有经过真实联网验证**：

- 经真实 `PackageMap` 的曲线、动画、目标 Actor 解析
- 模拟代理上的 Warping 轨迹一致性
- `Incremental` 的位置收敛

详见项目内验收记录。
