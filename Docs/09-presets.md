# 预设

预设是**生成 `FRMSMotionRequest` 的蓝图函数**，把常见用法的合理默认值打包好。它们不是数据资产，也不新增任何运动类型：返回的就是一个普通 `Request`，你可以继续改任何字段，再交给 `PlayMotion`。

```
Make Dash Request / Make Knockback Request / Make Jump Request  →  (可选：修改字段)  →  Play Motion
```

## 三个预设

| 函数 | 路径 | 缓动 | 到达模式 | 结束速度 | 朝向参数 |
| --- | --- | --- | --- | --- | --- |
| `MakeDashRequest` | 直线 | EaseOut | Incremental | 清零 | **可调**，默认 `FaceMotion` |
| `MakeKnockbackRequest` | 直线 + 抬升 | EaseOut | Incremental | 清零 | 固定不改变 |
| `MakeJumpRequest` | 抛物线 | Linear | Guaranteed | 清零 | **可调**，默认 `FaceMotion` |

### 冲刺 `MakeDashRequest(StartLocation, Direction, Distance = 600, Duration = 0.3, RotationMode = FaceMotion, TargetRotation, InstanceName)`

- 起点就是传入的 `StartLocation`（胶囊中心，通常取 `Get Center Location`），终点 = 起点 + 单位化的 `Direction` × `Distance`。
- 用 `Incremental`：冲刺撞墙就停，不会往墙里反复找补（见 [02](02-reach-mode.md)）。
- `Direction` 为零或 `Distance` 为 0 时终点等于起点，不产生 NaN。
- **`RotationMode` 可直接改**：默认 `FaceMotion`（朝冲刺方向）。横向闪避但不想转身，传 `FaceTarget` 并把 `TargetRotation` 设为敌人方向；传 `None` 完全不动朝向。

### 击退 `MakeKnockbackRequest(StartLocation, Direction, Distance = 400, Duration = 0.4, Lift = 60, InstanceName)`

- 同冲刺，但沿正弦弧线抬升 `Lift` 厘米（起终点为 0），并且**不改变角色朝向**——被击退的角色不应该转身面向被推的方向。

### 跳跃 `MakeJumpRequest(StartLocation, Target, ApexHeight = 150, Duration = 0.7, RotationMode = FaceMotion, TargetRotation, InstanceName)`

- 抛物线精确落到 `Target`，`ApexHeight` 是顶点距起终点连线的高度。
- 用 `Guaranteed`，保证落到目标上。
- `RotationMode` 同冲刺，可直接改。
- **结束速度清零。** 这一点是实测发现的：最初用的是"保持最后一帧速度"，真实角色会带着速度冲过落点并继续下坠（落点偏出约 180 厘米）。改为清零后落点精确。

## 默认值只是建议

这些默认值是插件作者的判断，不是唯一正确答案。常见的调整：

```
Request = Make Dash Request(...)
Request.Params.ReachMode = Guaranteed     // 想让冲刺一定到达
Request.Params.Ease = Linear              // 匀速
Request.Priority = 5
Play Motion(Character, Request)
```

## 为什么没有数据资产形式的预设

曾经考虑过 `Preset` 数据资产，被否决了：大多数场景不需要自定义曲线，纯数据参数已经足够，资产只会增加联网时的对象引用风险和管理负担。预设只做函数。
