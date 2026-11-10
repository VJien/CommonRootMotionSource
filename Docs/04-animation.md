# 动画根位移

把动画自带的根位移当作运动来源，不依赖 Montage 的根运动管道。

## 用法

```cpp
FRMSMotionRequest Request;
Request.Anim.Animation = MySequence;   // UAnimSequenceBase（Sequence 或 Montage）
Request.Anim.AnimStart  = 0.0f;        // 动画起点（秒）
Request.Anim.AnimEnd    = -1.0f;       // -1 表示动画末尾
Request.Anim.Rate       = 1.0f;
Request.Anim.bIgnoreZ   = false;
URMSMotionLibrary::PlayMotion(Character, Request);
```

设置 `Anim.Animation` 后，`PathPoints`、`PathKind`、`TargetLocation` 都不参与位移。

## 时长与时间

- 时长由动画区间与速率决定：`(AnimEnd - AnimStart) / Rate`。
- 动画时间 = `AnimStart + 已播放的真实时间 × Rate`。
- `Rate` 低于 0.01 会被钳到 0.01，不会除零。
- `AnimEnd` 小于等于 0 或超出动画长度时取动画长度；早于 `AnimStart` 时钳到 `AnimStart`（区间长度为 0，运动立即完成）。

## ⚠ 转换旋转在播放开始时固定

引擎自己每帧读模型当前的变换把根位移转到世界空间；本插件在 `PlayMotion` 时取一次**模型（Mesh）的世界旋转**，并把它序列化。

- 必须用 Mesh 而不是 Actor：引擎规定动画根位移是**组件空间**的量（`USkeletalMeshComponent::ConvertLocalRootMotionToWorld` 就是这么算的）。这是个数学换算，与「朝向基准」无关。
- 好处：服务器和客户端用同一个旋转，两端结果一致。
- 代价：**动画播放期间角色转向时，位移方向不会跟着转**。动画内的根旋转也不参与（只取位移）。

如果动画本身包含明显转身，这个限制会看得出来。

## 朝向：谁来转角色

动画根位移分支本身**不读 `RotationMode`**。想让角色转向，用 Warping 的段级开关：

| 需求 | 用什么 |
| --- | --- |
| 段末朝**指定绝对朝向**，且能控制什么时候转完 | `Warp.Segments[].bWarpRotation` + `TargetRotation` + `RotationCurve`，见 [05](05-warping.md) |
| 朝向**速度方向**（朝哪走朝哪） | 只对位移运动有意义（`Params.RotationMode = FaceMotion`）；动画分支不读它 |

最常用的组合是 `MakeWarpedAnimationRequest(..., ERMSRotationMode::FaceMotion)`：位移对齐到终点，同时段末朝向目标。这是「攻击动画锁了旋转但仍要面朝目标」的标准做法。

为什么动画分支不读 `RotationMode`：动画的节奏由动画本身决定，再叠一个「朝速度方向」会让刻意锁朝向的攻击动画被强行转头。段级开关语义更清楚，也不影响不用 Warping 的纯位移运动。

## 资产引用与联网

动画资产经 `PackageMap` 传输，对端需要有同一个资产。

- 对端解析不到时，这个 Source 会被**放弃**并有错误日志，不会悄悄退化成“无位移”。
- 这与曲线资产的规则一致，详见 [07](07-networking.md)。

## 怎么测试

插件本身不含任何动画资产。在蓝图里把项目里已有的动画填进 `Request.Anim.Animation` 再调用 `PlayMotion`，用 `RMS.Debug 2` 观察进度与剩余时间，详见 [08](08-debugging.md)。
