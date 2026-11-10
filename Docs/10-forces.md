# 力

力是一种**没有终点**的运动：每帧直接给角色一个速度，直到被停止（或到时）。

设置 `Request.Force.Kind` 即可（默认 `None`，表示按位移处理）。力类请求会忽略所有位移字段（路径点、缓动、弧线、目标等）。

## 两种力

| 种类 | 作用 | 用到的字段 |
| --- | --- | --- |
| `Constant` 恒力 | 沿固定方向持续施加速度 | `Direction`、`Strength` |
| `Pull` 牵引 | 朝目标 Actor 持续施加吸引速度 | `PullTarget`、`Strength`、`StopDistance`、`bIgnoreZ`、`StrengthOverDistance` |

## 速度单位与帧率

`Strength` 的单位是**厘米/秒**，与帧率无关。同样跑 1 秒，60 帧、20 帧或 120 帧下的总位移相同（有自动化测试固定）。

## 无限时长

`Params.Duration` **小于 0** 表示无限时长：引擎不会超时，力会一直作用，直到你主动结束：

- `StopMotion(WorldContext, Handle)` 结束单个。
- `StopAllMotions(Character)` 结束该角色身上的全部运动，返回停止的数量。
- 角色被销毁、或 World 被销毁时，会按 `Interrupted` 通知，不会泄漏。

位移类仍然把时长钳到 0.01 秒下限；只有力类允许负数。

## 牵引

```cpp
FRMSMotionRequest Request;
Request.Params.Duration = -1.0f;                    // 无限
Request.Force.Kind        = ERMSForceKind::Pull;
Request.Force.PullTarget  = SomeActor;
Request.Force.Strength    = 600.0f;                 // 厘米/秒
Request.Force.StopDistance = 150.0f;                // 距目标小于此值时停住
Request.Force.bIgnoreZ    = true;                   // 只取水平方向
URMSMotionLibrary::PlayMotion(Character, Request);
```

- 目标移动时，角色被拉向目标的**当前**位置。
- 距目标 ≤ `StopDistance` 时速度为 0，角色停在该圈外；目标走远后会再被拉动。
- `bIgnoreZ` 默认为真：只取水平方向与水平距离，适合地面角色。空中牵引（例如飞行单位）需要关掉。
- `StrengthOverDistance`（可选）：距离（厘米）到强度倍率的曲线，留空表示恒定 1。曲线值为负时按 0 处理，不会反向。
- 目标与角色重合时速度为 0，不产生 NaN。

### 目标丢失

- 目标被销毁：沿用最后已知位置，运动仍然活动，不崩溃。
- 目标在客户端尚未复制出来：沿用服务器同步过来的位置，等目标出现自动恢复，**不**放弃整个运动（与 [06 跟随](06-follow.md) 的取舍一致）。

## 与到达模式的关系

力没有终点，所以 **`ReachMode` 对力无意义，会被忽略**。结束速度策略（`FinishVelocity`）仍然生效：无限时长的力被 `StopMotion` 结束时，通常希望设为 `Zero`，让角色停下来而不是带着速度滑出去。

## 联网

- 力种类、方向、强度、停止距离、忽略 Z、距离曲线、牵引目标身份全部参与 `Matches` 与序列化。
- **牵引目标的位置不参与 `Matches`**（它随时间变化，两端有细微差异）；位置由服务器通过 `UpdateStateFrom` 校正。
- 距离曲线经 `PackageMap` 传输，解析不到时**放弃该运动**（曲线决定速度大小，退化会让两端走出不同速度）；牵引目标 Actor 解析不到时**不放弃**。
- 接收端对强度、停止距离做范围校验（强度上限 100000 厘米/秒、停止距离上限 100000 厘米），超限或非有限值直接拒绝整个包，防止恶意包造成瞬移。
- 无限时长与有限时长是不同的 Source，不会被误判为同一个。

## 与旧插件的区别

旧插件的牵引（`LinearConstraint`）有这些缺陷，本实现全部修复：

| 旧缺陷 | 本实现 |
| --- | --- |
| `Duration = -1` 会触发 `checkf` 崩溃 | 无限时长是合法输入 |
| 把力当“每帧位移”再除 dt，速度与帧率成反比 | 力直接是速度，与帧率无关 |
| 距离曲线未判空 | 空曲线表示恒定倍率 1 |
| 没有忽略 Z 的选项 | `bIgnoreZ`，默认真 |
| 平滑分支恒为死代码 | 不移植 |

## 调试

`RMS.Debug 1` 起：

| 内容 | 含义 |
| --- | --- |
| 橙色箭头 | 恒力方向，标注强度 |
| 蓝色连线 + 蓝色球 | 牵引：角色到目标（目标未解析时变红） |
| 青色圈 | 牵引的停止距离，以目标为中心 |

屏幕面板（`RMS.Debug 2`）显示 `Force:Constant` / `Force:Pull`、是否无限时长、网络角色。
