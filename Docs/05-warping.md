# 动画 Warping

把动画的根位移**对齐到指定的世界目标**：动画原本走 120 厘米，目标在 300 厘米外，就把整段根位移拉伸过去。同一个段还可以顺带**把朝向转到目标方向**，并且能控制旋转在哪一段时间里完成。

Warping 依附在动画回放之上，必须同时设置 `Anim.Animation`。

> 所有 `Target` 与 `PathPoints` 的世界坐标，语义都是**角色胶囊中心**（等于 `GetActorLocation()`），不是脚底。射线打地面得到的命中点要先经 `Foot To Center` 转换，见 [01](01-request-and-params.md)。

## 用法

```cpp
FRMSMotionRequest Request;
Request.Anim.Animation = MyMontage;

FRMSWarpSegment Segment;
Segment.WindowStart = 0.3f;          // 动画时间（秒）
Segment.WindowEnd   = 0.8f;
Segment.Target      = TargetLocation;
Segment.bWarp       = true;
Request.Warp.Segments.Add(Segment);

Request.Warp.MinScale = 0.1f;        // 缩放限幅
Request.Warp.MaxScale = 4.0f;

URMSMotionLibrary::PlayMotion(Character, Request);
```

## 窗口的行为

`Segments` 会被自动整理：

1. 按 `WindowStart` 排序，裁剪到动画区间内。
2. 长度为 0 或完全越界的段被丢弃。
3. 重叠时前一段优先，后一段的起点推迟到前一段终点。
4. 段与段之间、首段之前、末段之后的**间隙**自动补成 `bWarp = false` 的段——这些区间按动画原样播放，不会瞬移。
5. 整个动画区间保证被无缝覆盖，不会有两段同时生效。

`bWarp = false` 的段忽略 `Target`，也不做朝向 Warping。

## 数学：水平旋转 + 等比缩放

设：

- `R` = 当前动画时间到本段终点的动画根位移（世界空间）
- `D` = 当前位置到本段目标的向量

插件求一个变换 `T`，使 `T(R) = D`。`T` 由两部分组成：

- **绕世界 Z 轴的旋转**：把 `R` 的水平方向转到 `D` 的水平方向。
- **等比缩放**：`|D| / |R|`，被钳制在 `[MinScale, MaxScale]`。

逐帧的动画位移也经同一个 `T` 变换，所以整段走完后角色落在段目标上。

### ⚠ 已知限制

1. **俯仰角不同时 Z 方向对不上。** 上面的相似变换保证长度和水平方向，但 `R` 与 `D` 的俯仰角不同时，`T(R)` 的 Z 分量不等于 `D` 的 Z 分量。
   如果动画需要**精确落到不同高度的目标**上，实测确认表现是否可接受。
2. **缩放限幅。** 目标超过动画位移的 `MaxScale` 倍远时，到不了目标（宁可到不了也不过度扭曲动画）。低于 `MinScale` 同理。
3. **剩余动画位移接近 0 时不做变换**（恒等）。例如动画根位移本来就很小，Warping 无从下手。

## 朝向 Warping

位移和朝向是两个独立开关：可以只位移、只转朝向、或两者都做。

```cpp
Segment.bWarpRotation = true;
Segment.TargetRotation = FRotator(0.0, 90.0, 0.0);   // 只取 Yaw
```

**典型用例：锁朝向的攻击动画。** 攻击动画本体不转头（或者你在蒙太奇里锁了旋转），但你需要角色朝目标点位移、最后也面朝目标点：

```cpp
// 一个节点搞定：位移对齐 + 段末朝向目标
FRMSMotionRequest Request = URMSMotionLibrary::MakeWarpedAnimationRequest(
    URMSMotionLibrary::GetCenterLocation(Character),
    AttackMontage,
    TargetLocation,
    1.0f,        // Rate
    0.0f, -1.0f, // AnimStart / AnimEnd
    ERMSRotationMode::FaceMotion);   // 段末朝目标方向

URMSMotionLibrary::PlayMotion(Character, Request);
```

`RotationMode = FaceMotion` 会把段朝向设为 `起点 → 终点` 的水平方向；要指定别的朝向就用 `FaceTarget` 并填 `TargetRotation`；`None` 则完全不动朝向。

### 旋转的完成时机

`RotationCurve` 控制**这一段的旋转进度曲线**：横轴段内时间进度 0～1，纵轴旋转进度 0～1，留空为线性。

- 线性（默认）：匀角速度，从头转到尾。
- `0～0.6 为 0，0.6～1 升到 1`：前段完全不转，末段集中转完——挥砍的发力感。
- `0～0.15 升到 1，之后为 1`：起手瞬间就转到位，之后保持。

无论曲线怎么设，**段结束时旋转都精确等于 `TargetRotation`**（进度到 1 时强制补足），不会停在差一点的位置。

### 实现要点（为什么不用给起始朝向）

引擎把 Source 输出的旋转当作**增量**：新朝向 = 输出 × 当前朝向。所以插件每帧不输出绝对朝向，而是输出「剩余误差 × 本帧应消耗的比例」：

- 比例 = `(s(本帧末) - s(本帧初)) / (1 - s(本帧初))`，`s` 是曲线给出的旋转进度。
- 前段 `s` 恒为 0 → 比例为 0 → 完全不转。
- 最后一帧 `s` 到 1 → 比例为 1 → 一次转到目标。
- 中途被别的逻辑扭偏了朝向，下一帧依然以**实际当前朝向**为基准，会自动收回来。

这样插件不需要知道起始朝向，也不用把它序列化——少一个联网时要对齐的字段。

只绕世界 Z 轴转，走最短弧（跨 ±180° 按 2° 算，不会绕一圈），俯仰和翻滚不受影响。

### 多段串联

一帧内可能跨过多个段。每段以自己的入口朝向为基准算出增量，再推进到下一段，最后整体作为本帧增量输出。所以多段写「转到 45°，再转到 -135°」，最终就落在 -135°。

段之间如果有间隙段，朝向停在前一个旋转段的结果上，不会因为间隙而回弹。

## 被阻挡时

- `Guaranteed`：每帧以角色**实际位置**重算 `T`，被挡后下一帧自动找补。
- `Incremental`：以“段目标减去整段变换后位移”作为期望起点，即假设此前一直按计划走，被挡的位移不找补。

见 [02](02-reach-mode.md)。

## 窗口标记（Notify）

在动画上放 **RMS Warp Window** 节点，填 `TargetName`。运行时用名字到位置的映射生成窗口：

```cpp
TArray<FName> Names = { TEXT(“WallTop”) };
TArray<FVector> Locations = { WallTopLocation };
Request.Warp = URMSWarpLibrary::BuildWarpFromAnimation(Montage, Names, Locations);
```

- 窗口在 `PlayMotion` 时**一次性读取动画上的标记**，不依赖通知在播放时被触发，因此不受跳帧、打断、网络延迟影响。
- 名字找不到对应位置时，该窗口被忽略并写 Warning（该时间段按原样播放）。
- 多窗口即多个标记，区间由标记自身的起止时间决定。
- 这条路径只生成位移窗口，不设置朝向——需要朝向 Warping 时，在返回的 `Segments` 上逐个补 `bWarpRotation` / `TargetRotation`。

## 序列化

目标段、缩放限幅、动画、速率、动画区间、转换旋转全部进入 `NetSerialize` 与 `Matches`。

段级旋转也同步：`bWarpRotation`、`TargetRotation.Yaw`（**只发 Yaw**，俯仰与翻滚不上网且接收端清零）、`RotationCurve` 走 PackageMap 对象引用。

`Matches` 对旋转字段的敏感度：`bWarpRotation` 变化、`TargetRotation.Yaw` 变化、`RotationCurve` 变化都判定为不同。段未启用旋转时这些字段不参与匹配（它们本就不生效）。

`RotationCurve` 在对端解析不到时，整个 Source 被放弃——曲线决定旋转轨迹，两端不一致会导致朝向分叉，不能悄悄退化成线性。

### 位移运动的朝向字段

`Params` 侧同样入网：`RotationMode` 字节、`Params.TargetRotation.Yaw`（只发 Yaw）、`Params.RotationCurve` 有无标志 + 条件对象引用。

`Matches` 按模式决定比较范围：

| `RotationMode` | 比较什么 |
| --- | --- |
| `None` | 只比模式，角度与曲线不参与（不生效） |
| `FaceMotion` | 模式 + 曲线（方向由速度决定，角度无意义） |
| `FaceTarget` | 模式 + `TargetRotation.Yaw` + 曲线 |

段数量上限 64，接收端越界视为数据错误并拒绝该包。

## 怎么测试

在蓝图里设置 `Request.Anim` 与 `Request.Warp`（或用 `BuildWarpFromAnimation` 从动画标记生成），再调用 `PlayMotion`。开启 `RMS.Debug 2` 可以看到各个目标段（当前段红色高亮）与窗口区间，详见 [08](08-debugging.md)。

朝向 Warping 想看清转了多少，用带朝向的目标物体做参照最直观；控制台只画轨迹（起点 / 终点 / 中途点 / 时长），不画朝向曲线。

