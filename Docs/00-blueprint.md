# 蓝图连法

这一页解决在蓝图里用这个插件时**最常见的两个卡点**：嵌套结构体拼不出来，以及 Request 的输入引脚怎么填。

## 一、`FRMSMotionRequest` 的输入引脚怎么填

`FRMSMotionRequest` 是一个结构体，但它在蓝图里**不是"一个节点收所有参数"**。你要用 `Make FRMSMotionRequest` 节点，它会摊开成若干引脚。其中 `Params`、`PathPoints`、`Force`、`Follow`、`Anim`、`Warp` 这些引脚**本身就是结构体或数组**，需要再接各自的 `Make` 节点或数组节点。

这并不意味着每个都要填。**一个请求只会走一种运动**，判定顺序是：

```
力（Force.Kind ≠ None）> 动画（Anim.Animation 非空）> 位移到终点（Params.PathKind）
```

所以：

| 你想做的事 | 必须填 | 不用管 |
| --- | --- | --- |
| 直线 / 折线 / 抛物线 | `Params`、`StartLocation`、`TargetLocation` | `Force`、`Anim`、`Warp`、`Follow` |
| 动画位移 | `Anim`（+ `Warp`） | `Params` 的时长/路径、`PathPoints`、`Force` |
| 跟随 Actor | `Follow`、`Params`（时长） | `Anim`、`Warp`、`Force` |
| 恒力 / 牵引 | `Force`、`Params.Duration` | `Anim`、`Warp`、`PathPoints` |

**关键：没填的引脚留默认值就对了，不会互相抢。** 例如做一个直线位移，`Force`、`Anim` 这两个引脚什么都不接——它们的默认值是"非力、非动画"，不会生效。

## 二、为什么 Warp 段很难手拼

`FRMSWarpParams.Segments` 是 `TArray<FRMSWarpSegment>`。蓝图里拼它的链路是：

```
Make FRMSWarpSegment → Make Array → Make FRMSWarpParams → 接到 Request.Warp
```

能做，但三层嵌套、而且 `Make Array` 要先把元素类型设成 `FRMSWarpSegment`，很多人卡在这里。**所以直接用便捷节点，不要手拼。**

## 三、便捷节点（推荐）

这些节点都返回一个填好的 `FRMSMotionRequest`，直接接到 `Play Motion` 即可。分类在 `RMS|Presets`，输入都是平铺的普通字段，没有嵌套结构体。

| 节点 | 用来做 | 主要输入 |
| --- | --- | --- |
| `Make Warped Animation Request` | **动画根位移对齐到终点**，可选段末朝向 + 转向时机曲线 | `StartLocation`、`Animation`、`Target`、`Rate`、`RotationMode`、`TargetRotation`、`RotationCurve` |
| `Make Animation Request` | 只回放动画根位移，不做 Warping（用来验证动画能不能读到根位移） | `StartLocation`、`Animation`、`Rate`、`AnimStart`、`AnimEnd`、`bIgnoreZ` |
| `Make Straight Request` | 直线到目标 | `StartLocation`、`Target`、`Duration`、`Ease`、`ReachMode`、`RotationMode`、`TargetRotation` |
| `Make Parabola Request` | 抛物线到目标 | `StartLocation`、`Target`、`ApexHeight`、`Duration`、`ReachMode`、`RotationMode`、`TargetRotation` |
| `Make Path Request` | 折线或样条 | `StartLocation`、`PathPoints`、`Kind`、`Duration` |
| `Make Follow Request` | 跟随 Actor | `StartLocation`、`TargetActor`、`WorldOffset`、`Duration`、`LostPolicy`、`RotationMode`、`TargetRotation` |
| `Make Constant Force Request` | 恒力 | `StartLocation`、`Direction`、`Strength`、`Duration` |
| `Make Pull Request` | 牵引 | `StartLocation`、`TargetActor`、`Strength`、`StopDistance`、`bIgnoreZ`、`Duration` |
| `Make Dash Request` | 冲刺（预设） | `StartLocation`、`Direction`、`Distance`、`Duration`、`RotationMode`、`TargetRotation` |
| `Make Knockback Request` | 击退（预设） | `StartLocation`、`Direction`、`Distance`、`Duration`、`Lift` |
| `Make Jump Request` | 跳跃（预设） | `StartLocation`、`Target`、`ApexHeight`、`Duration`、`RotationMode`、`TargetRotation` |

### 旋转节点（`RMS|Rotation`）

| 节点 | 用来做 |
| --- | --- |
| `With Rotation` | 给已构造好的 Request 设置朝向。位移运动改 `Params`；已有 Warp 段时同时改段，避免「参数设了但动画不转」 |
| `With Warp Rotation` | 只改动画的 Warp 段朝向，不碰 `Params`，支持多段 |

### 朝向怎么填

`RotationMode` 三选一：

| 值 | 效果 | 用在 |
| --- | --- | --- |
| `None` | 完全不动朝向 | 朝向交给别处控制 |
| `FaceMotion` | 朝运动方向 | 冲刺、跳跃（预设默认） |
| `FaceTarget` | 转到 `TargetRotation` 指定的绝对角度 | **横向闪避仍面朝敌人**、锁朝向位移 |

想控制「什么时候转完」，给 `RotationCurve` 一条曲线（横轴时间 0～1，纵轴旋转进度 0～1）。留空 = 整段匀速转。无论曲线怎么设，结束时朝向都精确等于目标。

**一个坑：** `AccumulateMode = Additive` 时引擎不应用旋转，会静默不转。开了转向没反应先查这个（默认是 `Override`，正常不会碰到）。

## 三b、`StartLocation` 填什么

**填「胶囊中心」，不是脚底。** 最省事的做法是接 `Get Center Location(你的角色)`。

所有便捷节点都不收角色指针，只要一个 `FVector StartLocation`。原因不是省事，而是**联网安全**：如果节点内部去读角色位置，服务器和客户端会各自读到本机值，两端起点不同 → `Matches` 判定不一致 → 服务器校正失效。所以起点必须由一端算好后显式传进去。

目标点如果来自射线（打地面），必须先用 `Foot To Center` 转一次：

```text
LineTrace 命中点 → Foot To Center(Character) → Target
```

忘了填（零向量）时插件会写 Warning。

## 位置换算节点

`RMS|Location` 分类下四个纯函数，用来在「胶囊中心」和「脚底」之间换算：

| 节点 | 作用 |
| --- | --- |
| `Get Center Location(Character)` | 角色当前胶囊中心（= `GetActorLocation()`） |
| `Get Foot Location(Character)` | 角色当前脚底点 |
| `Foot To Center(Character, FootLocation)` | 脚底点 → 胶囊中心 |
| `Center To Foot(Character, CenterLocation)` | 胶囊中心 → 脚底点 |

## 四、你最初的用法：动画去掉 RM 后对齐终点

用 **`Make Warped Animation Request`** 一个节点搞定：

```
StartLocation = Get Center Location(你的角色)
Animation     = 你的动画资产
Target        = 终点（世界坐标，胶囊中心；来自射线时先 Foot To Center）
Rate          = 1.0（要和你播动画的速率一致）
RotationMode  = 看需求（见下）
     │
     ▼
Play Motion (Character)
```

它内部会自动：

- 把 `Anim.Animation` 填好、`AnimEnd` 设为动画末尾；
- 生成一个覆盖**整段动画**的 Warp 段，`Target` 就是你要的终点；
- 设 `ReachMode = Guaranteed`（保证到达）和 `FinishVelocity = Zero`（到点停下，不会冲过头）；
- `RotationMode = FaceMotion` 时，把段朝向设为 `起点 → 终点` 的水平方向，段末精确转到该朝向；`FaceTarget` 时用 `TargetRotation` 指定的角度。

**动画本身仍由你自己播放**（`Play Montage`）。插件只接管位移，不播动画。

### `RotationMode` 怎么选

攻击动画的典型困境：动画本体不转头（或者你在蒙太奇里锁了旋转），但你要求角色朝目标位移、最后也面朝目标。这时必须开朝向 Warping——插件接管朝向，和动画无关。

- 平移 + 最后面朝目标 → **开**
- 只是位移，朝向交给动画或别的逻辑 → **关**
- 要控制「前段不转、末段集中转完」→ 直接给 `RotationCurve` 一条曲线（详见 [05](05-warping.md)）

### 先确认这个动画的根位移读得到

同一个动画，先用 **`Make Animation Request`**（不填 Warp）跑一次：

- 角色有位移 → 根位移能读到，换成 `Make Warped Animation Request` 即可；
- 角色完全不动 → 这个动画读不到根位移数据，需要先确认「去掉 RM」是用哪种方式做的（关开关 vs 烘焙掉）。

## 五、必须注意的四点

1. **`AnimEnd` 填 `-1`**（表示到末尾），不要填 0。
2. **`Rate` 要和实际播放速率一致**，否则位移和动作对不上。
3. **动画位移与目标的俯仰角不同时**，只能保证水平方向和长度对得上，Z 方向对不上终点。缩放默认限幅 0.1～4.0 倍，超出就到不了目标。
4. **起点和目标都在胶囊中心语义下**。一个来自 `GetActorLocation`、一个来自射线命中点，是最常见的「差半个身高」来源。

## 六、反复使用时用变量

链路长的时候，在蓝图里加三个结构体变量（`FRMSAnimationParams`、`FRMSWarpParams`、`FRMSMotionRequest`），用 **Set Members in ...** 逐字段赋值，最后把 Request 变量喂给 `Play Motion`。比每次拉一长串 `Make` 干净。

## 七、我没验证过的部分

- **这些便捷节点的引脚我没在蓝图编辑器里目视确认过**（编译通过、结构体与函数都标了蓝图可访问，按规则应该都出现）。
- `Make Warped Animation Request` 的窗口计算依赖 `UAnimSequenceBase::GetPlayLength()`，有单元测试覆盖无资产与参数钳制，但**没有用真实动画验证过 Warping 的落点精度**。
- 朝向 Warping 的数学（进度比例、段末收敛、多段串联、中途偏转后收回）有单元测试覆盖，但**没有用真实动画 + 真实角色跑过**，实机的转动观感需要你验收。

发现某个引脚不存在或类型不对，告诉我具体是哪个节点哪个引脚，我改。
