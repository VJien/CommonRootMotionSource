# 请求与参数

## 位置语义：一律是「胶囊中心」

所有世界空间坐标（`StartLocation`、`TargetLocation`、`PathPoints`、`Warp.Segments[].Target`）的语义都是**角色胶囊中心**，等于 `ACharacter::GetActorLocation()`。

**不是脚底。** 脚底与中心相差一个胶囊半高（默认胶囊半高 88 厘米）。

这个规则必须统一，因为最常见的坑是**起点和终点来自不同来源**：

```text
StartLocation = Character->GetActorLocation()   // 中心，Z = 地面上方 88
TargetLocation = 射线打地面的命中点              // 脚底，Z = 地面
```

两者差了一个半高，角色会「往下钻」或者落在半空。插件不会替你猜——它只按你给的坐标算。

用这三个节点显式换算，语义就不会错：

| 节点 | 作用 |
| --- | --- |
| `Get Center Location(Character)` | 角色当前胶囊中心。等价 `GetActorLocation()`，但名字自解释 |
| `Get Foot Location(Character)` | 角色当前脚底点 |
| `Foot To Center(Character, FootLocation)` | 脚底点 → 胶囊中心。**射线的命中点用它** |
| `Center To Foot(Character, CenterLocation)` | 胶囊中心 → 脚底点 |

典型写法：

```text
LineTrace 命中点 → Foot To Center → TargetLocation
Character 的 Get Center Location → StartLocation
```

## `FRMSMotionRequest`：一次播放

| 字段 | 作用 |
| --- | --- |
| `Params` | 怎么动，见下表。纯数据，不需要资产 |
| `StartLocation` | 起点（世界空间，**胶囊中心**）。跟随目标时只作初值 |
| `TargetLocation` | 终点（世界空间，**胶囊中心**）。折线与样条时被忽略，终点取最后一个路径点 |
| `PathPoints` | 路径点（不含起点，**胶囊中心**），最多 64 个。仅折线与样条使用 |
| `Follow` | 跟随 Actor，见 [06](06-follow.md) |
| `Anim` | 动画根位移，见 [04](04-animation.md) |
| `Warp` | 动画 Warping（含朝向 Warping），见 [05](05-warping.md) |
| `InstanceName` | 实例名。同名冲突判断、联网校正配对都靠它 |
| `Priority` | 优先级，数值大者在 CMC 中后处理 |
| `ConflictPolicy` | 同名运动已存在时：`Allow` 叠加 / `Replace` 替换 / `Block` 拒绝 |

**`StartLocation` 没有默认值，也没有「自动取角色位置」。** 这是有意的：联网时两端各读本机位置会把起点固化成不同值，`Matches` 判定不一致后服务器校正会失效。起点必须由一端确定后显式传入（通常取 `Get Center Location`）。

忘了填（零向量）时会写 Warning，方便定位。

## `FRMSMotionParams`：怎么动

| 字段 | 作用 |
| --- | --- |
| `Duration` | 时长（秒）。低于 0.01 会被钳到 0.01，不会崩溃 |
| `ReachMode` | 到达模式，见 [02](02-reach-mode.md) |
| `Ease` | 内置缓动：`Linear` / `EaseIn`（先慢后快）/ `EaseOut` / `EaseInOut` |
| `ArcHeight` | 抬升高度（厘米），沿正弦弧线，起终点为 0。抛物线路径不使用此字段 |
| `RotationMode` | `None` 不转；`FaceMotion` 朝运动方向；`FaceTarget` 转到 `TargetRotation`（只对位移运动有效，动画用 Warp 段） |
| `TargetRotation` | `FaceTarget` 时的世界朝向（只取 Yaw）。例：横向闪避仍面朝敌人 |
| `RotationCurve` | 转向完成时机曲线（横轴时间 0～1，纵轴旋转进度 0～1），留空 = 整段匀速转 |
| `FinishVelocity` | 结束时的速度：`Keep` 保持 / `Zero` 清零 / `Clamp` 限速 |
| `FinishClampSpeed` | `Clamp` 时的水平速度上限 |
| `AccumulateMode` | 与角色已有速度的合成方式：`Override` 覆盖 / `Additive` 叠加 |
| `PathKind` | 路径种类，见 [03](03-movement-kinds.md) |
| `OffsetCurve` | 可选偏移曲线（`UCurveVector`），见 [03](03-movement-kinds.md) |
| `CustomProgressCurve` | 可选自定义位移曲线（`UCurveFloat`），留空用 `Ease` |

两种转向方式的区别：

- `RotationMode = FaceMotion`：朝向**速度方向**，每帧即时对齐。适合冲刺这类「朝哪走就朝哪」。
- `RotationMode = FaceTarget`：整段运动**匀速（或按曲线）转到指定绝对朝向**。适合「横向闪避但仍面朝敌人」。
- `Warp.Segments[].bWarpRotation`：用于**动画运动**（动画分支不读 `Params.RotationMode`），段末朝指定绝对朝向。

> **引擎限制：`AccumulateMode = Additive` 时旋转被静默丢弃。** 引擎只对 `Override` 模式的 Source 应用旋转（`RootMotionSource.cpp:1746`）。开了转向却看不到转，先检查这一项。

### 三种设置朝向的方式

| 场景 | 方式 |
| --- | --- |
| 构造时直接指定 | `MakeDashRequest` / `MakeStraightRequest` / `MakeParabolaRequest` / `MakeJumpRequest` / `MakeFollowRequest` 都有 `RotationMode` + `TargetRotation` 参数 |
| 动画位移 + 段末朝向 | `MakeWarpedAnimationRequest` 的 `RotationMode` + `TargetRotation` + `RotationCurve` |
| 构造后追加/修改 | `WithRotation(Request, Mode, TargetRotation, Curve)`，会同时改 `Params` 和已有 Warp 段 |

想控制转向完成的时机（例如前段不转、末段集中转完），给 `RotationCurve` 一条曲线：横轴是时间进度 0～1，纵轴是旋转进度 0～1。曲线末端必须能到 1（实现里会强制补足），所以运动结束时朝向总是精确等于目标。

## 常用蓝图函数

| 函数 | 作用 |
| --- | --- |
| `PlayMotion(Character, Request)` | 开始一次运动，返回 Handle |
| `StopMotion(WorldContext, Handle)` | 中途停止，结束原因 `Interrupted` |
| `IsMotionActive(WorldContext, Handle)` | 是否还在跑 |
| `GetActiveMotionCount(Character)` | 该角色身上有几个活动运动 |
| `MakeDirectionalRequest(StartLocation, Direction, Distance, Params, InstanceName)` | 便捷构造：沿方向走一段 |

所有 `Make*` 构造函数都接受**显式 `StartLocation`**，不接受角色指针（原因同上）。完整的表见 [00](00-blueprint.md)。

异步节点 `Play Motion And Wait` 有三个输出引脚：`OnCompleted`、`OnInterrupted`、`OnFailed`，可取消。

