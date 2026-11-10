# JTW Root Motion Source Plus 完整手册

> 面向使用者：所有 API 的用法、所有运动方式的图示、以及完整的案例。
> 引擎版本 UE 5.8 · 版权 JTW Studio

---

## 目录

1. [它是什么、解决什么问题](#1-它是什么解决什么问题)
2. [架构与数据流](#2-架构与数据流)
3. [五分钟上手](#3-五分钟上手)
4. [核心 API（5 个）](#4-核心-api5-个)
5. [构造器 API（11 个）](#5-构造器-api11-个)
6. [位置换算 API（4 个）](#6-位置换算-api4-个)
7. [旋转 API](#7-旋转-api)
8. [异步节点](#8-异步节点)
9. [8 种运动方式的图示与说明](#9-8-种运动方式的图示与说明)
10. [到达模式：被挡住之后怎么办](#10-到达模式被挡住之后怎么办)
11. [动画 Warping 详解](#11-动画-warping-详解)
12. [枚举全表](#12-枚举全表)
13. [项目设置](#13-项目设置)
14. [调试](#14-调试)
15. [联网](#15-联网)
16. [完整案例集](#16-完整案例集)
17. [常见问题与限制](#17-常见问题与限制)

---

## 1. 它是什么、解决什么问题

**一句话：** 用数据描述一次位移，交给角色移动组件执行，联网一致，并且可以选"被挡住后要不要找补"。

### 解决的六类问题

| 问题 | 引擎自带方案 | 本插件 |
| --- | --- | --- |
| 冲刺、击退、跳跃需要精确控制路径与时长 | 只能靠 Montage 根运动，或手写 `LaunchCharacter` | 数据描述，路径可直、可弯、可多点 |
| 被墙挡住后，角色被持续往墙里推 | — | 可选 `Incremental`：被挡的位移直接丢弃 |
| 动画根运动被关掉了，但仍想要位移 | 需要重新开根运动 | 直接读动画根位移，可拉伸到目标 |
| 联网时两端位移不一致 | 自己处理校正 | 两端用相同参数各自调用，字段全进序列化 |
| **运动过程中要转向，且方向与运动方向不同** | 自己插值 `SetActorRotation`，容易与根运动打架 | `FaceTarget`：整段匀速转到指定绝对朝向 |
| **动画锁了旋转，但仍要求最后面朝目标** | 需要重新开根运动或改动画 | 段级朝向 Warping，还能控制什么时候转完 |

### 不需要什么

- 不需要在角色上挂组件
- 不需要创建任何数据资产（参数是纯数据）
- 不需要改 `CharacterMovementComponent`

---

## 2. 架构与数据流

```
  你的代码（蓝图 / C++）
          │
          │  FRMSMotionRequest（纯数据：怎么动）
          ▼
  ┌──────────────────────────┐
  │  URMSMotionLibrary       │  ← 唯一公共入口，全是静态函数
  │  PlayMotion / Stop...    │     不需要 World Context，不需要组件
  └──────────┬───────────────┘
             │ 校验 + 冲突策略（同名 Replace / Block）
             ▼
  ┌──────────────────────────┐
  │  URMSMotionSubsystem     │  ← 世界级登记表（你不直接调用）
  │  · Handle → 活动运动映射  │     · 只在有运动时 Tick，空闲零开销
  │  · 结束判定与事件分发      │     · 调试历史
  └──────────┬───────────────┘
             │ 登记
             ▼
  ┌──────────────────────────┐
  │  FRMSMotionSource        │  ← 唯一的 Root Motion Source
  │  PrepareRootMotion()     │     每帧算速度，交给 CMC
  └──────────┬───────────────┘
             │  ApplyRootMotionSource
             ▼
  ┌──────────────────────────┐
  │  UCharacterMovementComponent │  ← 引擎原生：应用位移、网络校正
  └──────────────────────────┘
```

### 一个请求只走一种运动：判定优先级

```
        ┌─────────────────────────┐
        │ Request.Force.Kind      │
        │   ≠ None ?              │
        └───────┬─────────────────┘
           是   │   否
          ┌─────▼──────┐   ┌─────────────────────────┐
          │  力类       │   │ Request.Anim.Animation  │
          │ 恒力/牵引   │   │   非空 ?                │
          └────────────┘   └───────┬─────────────────┘
                              是   │   否
                             ┌─────▼──────┐   ┌────────────────────┐
                             │ 动画位移    │   │ Params.PathKind    │
                             │ （可 +Warp）│   │ 直线/折线/样条/抛物线│
                             └────────────┘   └────────────────────┘
```

**这一条很重要**：你在蓝图里 `Force`、`Anim`、`PathPoints` 三个引脚都空着就对了——空 = 不生效，不会互相抢。

`Follow` 不在这个优先级里，它只在"位移到终点"时每帧刷新终点位置。

---

## 3. 五分钟上手

### 先记住一条：坐标一律是「胶囊中心」

所有世界坐标（起点、终点、路径点、Warp 段目标）的语义都是**角色胶囊中心**，等于 `GetActorLocation()`。**不是脚底**，两者差一个胶囊半高（默认 88 厘米）。

最常见的坑是起点和终点来自不同来源：

```text
起点 = Character->GetActorLocation()   → 中心，地面上方 88
终点 = 射线打地面的命中点              → 脚底，贴地面
                                        差一个半高，角色会往下钻
```

插件不替你猜。四个换算节点在 `RMS|Location` 分类：

| 节点 | 作用 |
| --- | --- |
| `Get Center Location(Character)` | 角色胶囊中心（= `GetActorLocation()`），**起点用这个** |
| `Get Foot Location(Character)` | 角色脚底点 |
| `Foot To Center(Character, FootLocation)` | 脚底 → 中心，**射线命中点用这个** |
| `Center To Foot(Character, CenterLocation)` | 中心 → 脚底 |

### 最小可用：往前冲 600 厘米

```
Get Center Location (Self)
    │
    ▼
Make Dash Request
    StartLocation = 上面的返回值
    Direction     = Get Actor Forward Vector
    Distance      = 600
    Duration      = 0.3
          │
          ▼
Play Motion
    Character = Self
    Request   = 上面的返回值
    → 返回 Handle（一般不用管）
```

结束时会怎样：撞墙就停（`Incremental`），到点速度清零。

> **为什么起点要单独接一个节点？** 构造器不收 `Character` 指针。原因是联网安全：节点内部若去读角色位置，服务器和客户端会各自读到本机值，两端起点不同 → `Matches` 判定不一致 → 服务器校正失效。起点必须由一端算好后显式传入。

### 想看它怎么走的

控制台输入：

```
RMS.Debug 2
```

会画出起点、终点、时间刻度、当前进度，屏幕左上出现一行状态。

---

## 4. 核心 API（5 个）

全部是 `URMSMotionLibrary` 的静态函数，蓝图里直接搜名字。

### 4.1 `PlayMotion`

```
FRMSMotionHandle PlayMotion(ACharacter* Character, const FRMSMotionRequest& Request)
```

| 项 | 说明 |
| --- | --- |
| 返回 | `FRMSMotionHandle`。`Id != 0` 为有效；请求被拒绝时返回无效 Handle |
| 不传 World Context | Character 本身就是上下文 |
| 被拒绝的情况 | 角色/CMC 无效、动画资产为空时无路径、同名 `Block` 策略命中 → 写 Warning + 广播 `Failed` |
| 联网 | **两端各自调用一次**，参数必须一致 |

**蓝图：** 分类 `RMS`，节点名 `Play Motion`。

**C++：**
```cpp
const FVector Start = URMSMotionLibrary::GetCenterLocation(this);
FRMSMotionRequest Request = URMSMotionLibrary::MakeDashRequest(Start, GetActorForwardVector(), 600.f, 0.3f);
const FRMSMotionHandle Handle = URMSMotionLibrary::PlayMotion(this, Request);
```

> **起点为什么必须显式传？** 构造器不收 `Character*`。它们内部若去读角色位置，服务器和客户端会各自读到本机值，两端起点不同 → `Matches` 判定不一致 → 服务器校正被丢弃。见 [§15.1](#151-基本约定)。

### 4.2 `StopMotion`

```
bool StopMotion(const UObject* WorldContextObject, FRMSMotionHandle Handle)
```

中途停止，结束原因记为 `Interrupted`。返回是否真的停了一个活动运动。

**蓝图：** 需要任何带 World 的对象（Self 就行）。

**用途：** 无限时长的力、玩家打断技能、切场景前清理。

```cpp
URMSMotionLibrary::StopMotion(this, Handle);
```

### 4.3 `StopAllMotions`

```
int32 StopAllMotions(ACharacter* Character)
```

停掉该角色身上**所有**活动运动，返回停止的数量。**无限时长的力必须靠它或 `StopMotion` 结束**（它们不会自己结束）。

```cpp
const int32 Count = URMSMotionLibrary::StopAllMotions(this);   // 例如返回 2
```

### 4.4 `IsMotionActive`

```
bool IsMotionActive(const UObject* WorldContextObject, FRMSMotionHandle Handle)
```

还在跑吗。常用于"这个技能已经结束了就不再处理"。

### 4.5 `GetActiveMotionCount`

```
int32 GetActiveMotionCount(const ACharacter* Character)
```

这个角色身上有几个活动运动。调试或门控用（例如"已经有位移在跑，别再叠加"）。

---

## 5. 构造器 API（11 个）

这些函数**只是帮你填好 `FRMSMotionRequest`**，返回普通结构体。你可以接着改任何字段再交给 `PlayMotion`。

分类都在 `RMS|Presets`，输入全是平铺字段，**没有任何嵌套结构体需要手拼**。

### 5.1 一览

| 函数 | 运动方式 | 默认到达模式 | 默认结束速度 | 能设朝向 |
| --- | --- | --- | --- | --- |
| `MakeDashRequest` | 直线 | `Incremental` | `Zero` | 是（默认 `FaceMotion`） |
| `MakeKnockbackRequest` | 直线 + 弧线抬升 | `Incremental` | `Zero` | 否（固定不改朝向） |
| `MakeJumpRequest` | 抛物线 | `Guaranteed` | `Zero` | 是（默认 `FaceMotion`） |
| `MakeStraightRequest` | 直线 | 可指定（默认 Guaranteed） | `Zero` | 是（默认 `None`） |
| `MakeParabolaRequest` | 抛物线 | 可指定（默认 Guaranteed） | `Zero` | 是（默认 `None`） |
| `MakePathRequest` | 折线 / 样条 | `Incremental` | `Zero` | 否（用 `With Rotation` 补） |
| `MakeFollowRequest` | 跟随 Actor | 可指定 | `Zero` | 是（默认 `None`） |
| `MakeAnimationRequest` | 动画根位移 | 无（不涉及） | 无 | 否（用 `With Warp Rotation` 补） |
| `MakeWarpedAnimationRequest` | 动画 + Warping | `Guaranteed` | `Zero` | 是（默认 `FaceMotion`） |
| `MakeConstantForceRequest` | 恒力 | 无（力不适用） | `Zero` | 否（力不适用） |
| `MakePullRequest` | 牵引 | 无（力不适用） | `Zero` | 否（力不适用） |

所有 `Make*` 都以显式 `StartLocation` 为起点，不收角色指针（原因见 [§15.1](#151-基本约定)）。

### 5.2 `MakeDashRequest` — 冲刺

```
Make Dash Request(StartLocation, Direction, Distance = 600, Duration = 0.3,
                  RotationMode = FaceMotion, TargetRotation, InstanceName)
```

- 终点 = 起点 + 归一化的 `Direction` × `Distance`
- 缓动 `EaseOut`（先快后慢）
- **`Incremental`**：撞墙就停，不会往墙里找补
- **`RotationMode` 可直接改**：默认 `FaceMotion`（朝冲刺方向）。横向闪避但不想转身，传 `FaceTarget` 并把 `TargetRotation` 设为敌人方向；传 `None` 完全不动朝向

```
时间    0        0.15       0.3
速度    █████████▓▓▓▓▓░░░░░     ← EaseOut：先快后慢
位置    0 ─────────────────→ 600
```

`Direction` 为零或 `Distance` 为 0 时终点 = 起点，不产生 NaN。

**锁朝向冲刺（横向闪避）：**

```
俯视：
       敌人
        ▲
        │  TargetRotation = 朝敌人
        │
     角色 ──→ 移动方向（+X，与朝向垂直）
```

设 `RotationMode = FaceTarget`、`TargetRotation = (指向敌人的方向)`，角色就从侧面滑出去但一直面朝敌人。

### 5.3 `MakeKnockbackRequest` — 击退

```
Make Knockback Request(StartLocation, Direction, Distance = 400, Duration = 0.4, Lift = 60, InstanceName)
```

与冲刺相同，另外：

- `ArcHeight = Lift`：沿正弦弧线抬升（起终点为 0，中间隆起）
- **不改变朝向**：被击退的角色不该转身面向被推的方向

```
侧视：
        ╭───╮  ← 抬升 60
   ●───╯     ╰───●
  起点            终点 400
```

### 5.4 `MakeJumpRequest` — 跳跃到点

```
Make Jump Request(StartLocation, Target, ApexHeight = 150, Duration = 0.7,
                  RotationMode = FaceMotion, TargetRotation, InstanceName)
```

- 真抛物线，**精确落到 `Target`**
- `ApexHeight` 是顶点距起终点连线的**高度**（不是绝对 Z）
- `Guaranteed`：保证到点
- 结束速度 `Zero`：到点停下（这一点是实测修正的，见下）
- `RotationMode` 默认 `FaceMotion`，可直接改

```
侧视：
            ●  ← 顶点 = 起终点连线 + ApexHeight
          ╱   ╲
        ╱       ╲
  ●───────────────●   ← Target
```

> **为什么结束速度是 Zero：** 最初用的是"保持最后一帧速度"，真实角色会带着速度冲过落点并继续下坠，实测落点偏出约 180 厘米。改成清零后落点精确。

### 5.5 `MakeStraightRequest` — 直线（可完全控制）

```
Make Straight Request(StartLocation, Target, Duration = 0.3, Ease = EaseOut,
                      ReachMode = Guaranteed, RotationMode = None, TargetRotation, InstanceName)
```

需要自己决定"撞墙要不要找补"时用这个，而不是 `MakeDashRequest`。

`RotationMode` 默认 `None`（不动朝向，避免意外改变既有行为）。需要转向就显式传 `FaceMotion` 或 `FaceTarget`。

### 5.6 `MakeParabolaRequest` — 抛物线（可完全控制）

```
Make Parabola Request(StartLocation, Target, ApexHeight = 150, Duration = 0.6,
                      ReachMode = Guaranteed, RotationMode = None, TargetRotation, InstanceName)
```

### 5.7 `MakePathRequest` — 折线 / 样条

```
Make Path Request(StartLocation, PathPoints, Kind = Spline, Duration = 0.6, InstanceName)
```

- `PathPoints` 是**路径点数组（不含起点）**，终点自动取最后一个点
- 按**弧长匀速**参数化，不是按点序号——点距悬殊时速度不会突变
- 最多 64 个点，超出截断并写 Warning
- `Incremental`：被挡就停
- **没有朝向参数**（路径走向本身就多变，逐段朝向不一定是你要的）。需要转向就在构造后接 `With Rotation`

```
折线 Polyline：        样条 Spline：
   ●                     ●
    ╲                     ╲
     ●   ●                 ╰──╮
      ╲ ╱                     ╰─●
       ●                        ╲
                                 ●
（直角转折）              （平滑过渡，经过所有点）
```

### 5.8 `MakeFollowRequest` — 跟随 Actor

```
Make Follow Request(StartLocation, TargetActor, WorldOffset, Duration = 0.6,
                    LostPolicy = GoToLastKnown, ReachMode = Guaranteed,
                    RotationMode = None, TargetRotation, InstanceName)
```

- 终点每帧 = `TargetActor` 当前位置 + `WorldOffset`
- `Duration < 0` 表示无限（需 `StopMotion` 结束）
- `LostPolicy`：目标销毁后是继续飞向最后位置，还是立刻结束
- `RotationMode` 默认 `None`。追赶类运动常用 `FaceTarget` 锁住面朝目标，避免背身追人

```
  目标 Actor  ──→ 移动
      │
      │ WorldOffset（例如 -100，停在目标前方）
      ▼
   角色 ──→ 追过去
```

### 5.9 `MakeAnimationRequest` — 只回放动画根位移

```
Make Animation Request(StartLocation, Animation, Rate = 1.0, AnimStart = 0,
                       AnimEnd = -1, bIgnoreZ = false, InstanceName)
```

**这是诊断节点**：先确认"这个动画的根位移能不能读到"，再决定要不要加 Warping。

- 时长自动 = `(AnimEnd - AnimStart) / Rate`
- `AnimEnd = -1` 表示到动画末尾
- `bIgnoreZ`：忽略根位移的 Z 分量

### 5.10 `MakeWarpedAnimationRequest` — 动画对齐到终点 ⭐

```
Make Warped Animation Request(StartLocation, Animation, Target, Rate = 1.0,
                              AnimStart = 0, AnimEnd = -1, RotationMode = FaceMotion, TargetRotation, RotationCurve, InstanceName)
```

**这是"动画去掉 RM 后仍要精确落到终点"的标准做法。** 内部自动：

1. 填 `Anim.Animation`
2. 生成一个覆盖**整段动画**的 Warp 段，目标是 `Target`
3. 设 `Guaranteed`（保证到达）+ `FinishVelocity = Zero`（到点停下）
4. `RotationMode` 为 `FaceMotion` 时把段朝向设为 `起点 → 终点` 的水平方向；`FaceTarget` 用 `TargetRotation`；`None` 不动朝向

**`RotationMode` 怎么选：**

| 值 | 段末朝向 | 用在 |
| --- | --- | --- |
| `FaceMotion`（默认） | `起点 → 终点` 的水平方向 | 攻击向前推进，最后面朝目标 |
| `FaceTarget` | 你给的 `TargetRotation` | 朝向与位移方向不同（例如侧向绕到目标侧面但面朝它） |
| `None` | 不改朝向 | 朝向交给动画或别的逻辑 |

`RotationCurve` 控转向完成的时机，横轴段内时间 0～1、纵轴旋转进度 0～1，留空为整段匀速转。想「前段不转、末段集中转完」就画一条 `0～0.6 平、0.6～1 升到 1`。

开了朝向 Warping 后朝向**由插件接管**，与动画本身转不转无关。这正是"攻击动画本体不转头但仍要面朝目标"的解法，详见 [§11.3b](#113b-朝向-warping攻击动画专用)。

```
动画原始位移（例如 120 厘米）：
  ●──────●
  0     120

目标在 300 厘米处：
  ●──────────────●
  0             300

Warping 把整段位移等比拉长 2.5 倍：
  ●────────────────────────●
  0                       300
```

详见 [§11](#11-动画-warping-详解)。

### 5.11 `MakeConstantForceRequest` — 恒力

```
Make Constant Force Request(StartLocation, Direction, Strength = 500, Duration = -1, InstanceName)
```

- `Strength` 单位**厘米/秒**，与帧率无关
- `Duration < 0`（默认）= 无限，**必须 `StopMotion` / `StopAllMotions` 结束**
- 力没有终点，`ReachMode` 会被忽略

```
恒力（风、传送带、持续推力）：
  ├──────────────────→
  每帧都给 500 cm/s 的速度
```

### 5.12 `MakePullRequest` — 牵引

```
Make Pull Request(StartLocation, TargetActor, Strength = 500, StopDistance = 150,
                  bIgnoreZ = true, Duration = -1, InstanceName)
```

- 朝目标 Actor 持续吸引，距离 ≤ `StopDistance` 时速度为 0（停在圈外）
- `bIgnoreZ` 默认**真**：只取水平方向与水平距离（适合地面角色；空中牵引要关掉）
- 目标销毁后沿用最后已知位置，不崩溃

```
俯视：
        ╭────────╮  ← 停止距离圈 150
        │  ● 目标 │
        ╰────────╯
     ↑
     │ 角色被拉向目标，进圈就停
  角色
```

### 5.13 `MakeDirectionalRequest` — 底层通用入口

```
Make Directional Request(StartLocation, Direction, Distance, const FRMSMotionParams& Params, InstanceName)
```

`Params` 是你自己拼好的完整参数。要精细控制所有参数、又只需要"沿方向走一段"时用。

---

## 6. 位置换算 API（4 个）

分类 `RMS|Location`，全是纯函数，不改变任何运动语义，只做坐标换算。

| 节点 | 签名 | 作用 |
| --- | --- | --- |
| `Get Center Location` | `(Character) → FVector` | 角色胶囊中心，等于 `GetActorLocation()` |
| `Get Foot Location` | `(Character) → FVector` | 角色脚底点 |
| `Foot To Center` | `(Character, FootLocation) → FVector` | 脚底点 → 胶囊中心 |
| `Center To Foot` | `(Character, CenterLocation) → FVector` | 胶囊中心 → 脚底点 |

### 为什么需要它们

插件所有世界坐标（起点、终点、路径点、Warp 段目标）的语义统一是**胶囊中心**。但实际游戏里这两个来源经常混用：

```
起点 = Character->GetActorLocation()   → 中心，地面上方 88 厘米
终点 = 射线打地面的命中点              → 脚底，贴在地面
                                        差了一个胶囊半高
```

角色会因此"往下钻"或者悬在半空。插件不替你猜，只按给的坐标算，所以要显式换算。

### 典型写法

```
LineTrace（打地面）
      │ 命中点（脚底语义）
      ▼
Foot To Center (Character)
      │ 胶囊中心语义
      ▼
TargetLocation

Get Center Location (Character)
      │
      ▼
StartLocation
```

两个 `Get * Location` 只是让语义在蓝图里自解释——`Get Center Location` 等价于直接拖 `GetActorLocation`，但下次看连线时不用猜。

### 换算依赖什么

用 `UCapsuleComponent::GetScaledCapsuleHalfHeight()` 沿**世界 Z** 换算（胶囊轴恒为竖直，所以不需要胶囊自身朝向）。

角色为空或没有胶囊组件时**原样返回**，不产生 NaN。蹲伏时半高会变，所以换算总是在调用时刻取当前值。

---

## 7. 旋转 API

三处能设朝向，按你用哪种运动选：

| 运动类型 | 用什么 | 在哪 |
| --- | --- | --- |
| 位移（直线 / 抛物线 / 折线 / 跟随） | `Params.RotationMode` + `Params.TargetRotation` + `Params.RotationCurve` | 构造器参数，或 `With Rotation` |
| 动画根位移 + Warping | `Warp.Segments[].bWarpRotation` + `TargetRotation` + `RotationCurve` | `Make Warped Animation Request` 参数，或 `With Warp Rotation` |
| 力（恒力 / 牵引） | 不支持 | — |

### 7.1 三种模式

`ERMSRotationMode`：

| 值 | 行为 | 典型用法 |
| --- | --- | --- |
| `None` | 完全不改朝向 | 朝向由动画或别处控制 |
| `FaceMotion` | 朝**运动方向**，每帧即时对齐 | 冲刺、跳跃（预设默认） |
| `FaceTarget` | **整段运动**匀速（或按曲线）转到 `TargetRotation` | 横向闪避仍面朝敌人 |

`FaceMotion` 和 `FaceTarget` 的关键差别：

```
FaceMotion：方向由速度决定
   角色 ──→          角色
          ╲             ↘
           ╲ 走曲线      （每帧朝向当下的速度方向）
            ●

FaceTarget：方向由你指定，与怎么走无关
   敌人 ▲             敌人 ▲
        │                  │
        │                  │
     角色 ──→            角色 ──→
   （朝 +X 走，朝 +Y 转） （走完仍朝 +Y）
```

### 7.2 控转向完成时机

`RotationCurve`：横轴时间进度 0～1，纵轴旋转进度 0～1。留空 = 线性（整段匀角速度）。

```
前段不转、末段集中转完（挥砍发力）
  旋转进度
   1 │                    ╭────
     │                 ╭──╯
     │              ╭──╯
   0 │──────────────╯
     └────────────────────── 时间进度
     0        0.6        1.0

线性（默认）
   1 │              ╱
     │           ╱
     │        ╱
   0 │─────╱
     └──────────────────────
```

**无论曲线怎么设，运动结束时朝向都精确等于目标。** 曲线只决定中途怎么分配，末帧会强制补足。

### 7.3 两个后置修改节点

构造之后想追加或改朝向，不用重新拼 Request：

| 节点 | 作用 | 何时用 |
| --- | --- | --- |
| `With Rotation(Request, Mode, TargetRotation, Curve)` | 改 `Params`，**同时**改已有 Warp 段 | 通用；动画 Request 也能用 |
| `With Warp Rotation(Request, Mode, TargetRotation, Curve)` | 只改 Warp 段，不碰 `Params` | 多段 Warping 要逐段控制 |

`With Rotation` 对动画 Request 会同步改段，避免「`Params` 设了但动画不转」——这是刻意的，因为动画分支不读 `Params.RotationMode`。

间隙段（`bWarp = false`）**不会被施加旋转**，保持原样播放。

传 `None` 会清空目标朝向与曲线，不会残留上次的值。

### 7.4 实现方式（为什么不给起始朝向）

引擎把 Source 输出的旋转当作**增量**（新朝向 = 输出 × 当前朝向，`CharacterMovementComponent.cpp:3055`）。所以插件每帧输出「剩余误差 × 本帧应消耗的比例」，而不是绝对朝向：

- 曲线把比例压到 0 → 前段完全不转
- 最后一帧比例到 1 → 精确转到目标
- 中途被别处扭偏了朝向 → 下一帧以实际朝向为基准，自动收回

好处是插件不需要存起始朝向，少一个联网要对齐的字段。只绕世界 Z 轴、走最短弧（179° → -179° 只转 2°，不绕一圈），俯仰与翻滚不受影响。

### 7.5 两个坑

**坑一：`AccumulateMode = Additive` 时旋转被静默丢弃。**

引擎只对 `Override` 模式的 Source 应用旋转（`RootMotionSource.cpp:1746`）。默认是 `Override`，正常碰不到，但如果你手动改过 `AccumulateMode`，转向会完全无效且没有报错。

**坑二：动画运动不看 `Params.RotationMode`。**

`Anim.Animation` 非空时走动画分支，那个分支用段级 `bWarpRotation`。想给动画加朝向就用 `Make Warped Animation Request` 的参数或 `With Rotation`（它会自动同步到段）。

---

## 8. 异步节点

### `Play Motion And Wait`

```
URMSAsyncAction_PlayMotion* PlayMotionAndWait(ACharacter* Character, FRMSMotionRequest Request)
```

蓝图里的用法：**拖出 `Play Motion And Wait` 节点，它自带三个输出执行引脚**。

```
Play Motion And Wait
    Character = Self
    Request   = 构造好的请求
      │
      ├── On Completed   → 正常播完
      ├── On Interrupted → 被 Stop / 被替换 / 角色销毁
      └── On Failed      → 请求非法或 Block 策略命中
```

每个引脚都带两个输出参数：`Handle`、`Reason`。

**特点：**

- 可取消（`Cancel` 等价于 `StopMotion`）
- 不需要 GameplayTasks 组件，没有 GAS 的项目也能用
- 只订阅子系统事件，不承担联网同步

**用法场景：** 连招里"这一击位移结束再进入下一段"。

```
Play Motion And Wait ──On Completed──→ 播下一段动画
                     ──On Interrupted─→ 回到 Idle
```

---

## 9. 8 种运动方式的图示与说明

按 `Params.PathKind` 或填了哪个字段决定。

### 9.1 直线 `Straight`

```
俯视/侧视：
  ●──────────────────────→●
  起点                   终点
```

| 字段 | 说明 |
| --- | --- |
| `StartLocation` / `TargetLocation` | 起终点（世界空间，**胶囊中心**） |
| `Params.Duration` | 时长，最低钳到 0.01 |
| `Params.Ease` | `Linear` 匀速 / `EaseIn` 先慢 / `EaseOut` 先快 / `EaseInOut` 两头慢 |
| `Params.RotationMode` | `None` / `FaceMotion` / `FaceTarget`，见 [§7](#7-旋转-api) |
| `Params.TargetRotation` | `FaceTarget` 时的绝对朝向（只取 Yaw） |
| `Params.RotationCurve` | 转向完成时机，留空 = 整段匀速 |

### 9.2 折线 `Polyline`

```
  ●
   ╲
    ●    ●
     ╲  ╱
      ●
```

按**弧长匀速**。点距 10 和点距 1000 混在一起走起来也匀速。

### 9.3 样条 `Spline`

```
  ●
   ╰──╮
       ╰─●
         ╲
          ●
```

Catmull-Rom，经过所有路径点，转折平滑。同样按弧长匀速。

### 9.4 抛物线 `Parabola`

```
        ●  ← 顶点 = 起终点连线 + ArcHeight
      ╱   ╲
    ╱       ╲
  ●───────────●
```

- `ArcHeight` 是**高度**，不是绝对 Z
- 起终点不等高时，终点 Z 精确
- 自带高度，**不再叠加** `Params.ArcHeight` 的正弦弧线

### 9.5 跟随 `Follow`

见 §5.8 `MakeFollowRequest`。终点由目标实时决定。

### 9.6 动画根位移 `Animation`

```
动画资产里的根骨骼轨迹：
  帧0        帧N
   ●─────────→●     ← 这段位移被读出来，驱动角色

动画本身可以照常播（你也可以选择不播，只要它的根位移数据还在）
```

**只取位移，不取旋转。** 动画自带的根旋转被丢弃——想转向要用段级朝向 Warping（[§11.3b](#113b-朝向-warping攻击动画专用)）。这条设计是有意的：攻击动画的转身常常要配合特效帧，交给插件按曲线控制比跟动画走更可控。

### 9.7 动画 Warping `Animation + Warp`

```
原始：  ●───→●              （动画自身的根位移，例如 120）
目标：  ●──────────→●       （你要它落到 300）

结果：  ●────────────────→●  （整段等比拉长，节奏保持）
        动画本来"先慢后快"，拉长后仍然"先慢后快"
```

详细见 [§11](#11-动画-warping-详解)。

> 这一段还能顺带做**朝向**：段级 `bWarpRotation` + `TargetRotation` + `RotationCurve`。见 [§11.3b](#113b-朝向-warping攻击动画专用)。

### 9.8 力 `Force`（恒力 / 牵引）

```
恒力：  ├──────────────────→   固定方向，固定速度
牵引：  目标 ● ←────────────── 角色     （越远越快或恒定，进停止距离圈就停）
```

两种力都**没有终点**，`ReachMode` 无效，`Duration < 0` 表示无限。力类**不支持朝向**，`RotationMode` 会被忽略。

### 9.9 路径叠加偏移曲线（所有位移方式通用）

任何位移方式都可以再叠一条 `Params.OffsetCurve`（`UCurveVector`）：

- 横轴 = **位移进度** 0～1（已过缓动）
- 纵轴 = 偏移向量，单位**厘米**
- 在**朝向空间**里给：**X = 前进方向，Y = 右侧，Z = 世界上方**

```
俯视（直线 + 向右的偏移）：
  ●──────╮
          ╰────╮
                ╰──●
```

⚠ 插件不会把曲线端点归零。想让起终点不被偏移，让曲线在 0 和 1 处为 0。

---

## 10. 到达模式：被挡住之后怎么办

这是本插件区别于其他方案的核心点之一，值得单独理解。

### 10.1 两种模式

| 模式 | 每帧速度怎么算 | 被挡几帧后 |
| --- | --- | --- |
| `Guaranteed` | 目标时刻位置 − **角色实际位置** | 后面的帧把差距补回来，**一定到点** |
| `Incremental` | 目标时刻位置 − **上一时刻的期望位置** | 被挡的位移**永久丢弃**，可能到不了 |

### 10.2 图示：中间有一堵墙

目标 1000，时长 1 秒，每秒 1000 厘米。第 3～5 帧被墙挡住（贴合真实测试结果）。

```
Guaranteed
位置
1000 ┤                              ●──●   到点
 800 ┤                        ●─────╯
 600 ┤                  ●─────╯
 400 ┤            ●─────╯
 245 ┤      ●─────╯   ← 撞墙（245），之后被"拽"回来
   0 └──────┬──────┬───────────────→ 时间
          撞墙   离开墙

Incremental
位置
 700 ┤                          ●───●
 400 ┤            ●─────╯
 245 ┤      ●─────╯   ← 撞墙，丢掉了 3 帧位移
   0 └──────┬──────┬───────────────→ 时间
                  离开墙后按原节奏继续，到不了 1000
```

**实际测试数据（真实角色 + 真实碰撞墙）：**

| | 撞墙时位置 | 墙移走后终点 |
| --- | --- | --- |
| `Guaranteed` | 245.2 | **800.0**（到点） |
| `Incremental` | 245.2 | **565.2**（差 235） |

### 10.3 怎么选

| 场景 | 建议 |
| --- | --- |
| 冲刺、位移技能、扑击 | `Incremental` — 撞墙就停，手感像正常移动 |
| 技能要求"一定把我送到那儿" | `Guaranteed` |
| 翻越、指定落点 | `Guaranteed` |
| 力类（恒力/牵引） | 不适用，会被忽略 |

### 10.4 联网上的差别（重要）

两种模式在**运动期间**都会两端分叉，因为客户端预测不知道只有服务器才有的墙：

| | 运动中最大两端差 | 运动中观感 | 运动结束后 |
| --- | --- | --- | --- |
| `Guaranteed` | 554.7 | 客户端一路冲到底，服务器停在墙前 | 由引擎位置校正收敛 |
| `Incremental` | 53.1 | 客户端在墙前**反复回弹**，肉眼可见拉扯 | 由引擎位置校正收敛 |

**收敛是引擎 `CharacterMovementComponent` 的服务器位置校正做的，不是插件保证的。** 如果你的玩法里"客户端会撞到只有服务器才有的东西"很常见，`Incremental` 的拉扯是需要权衡的体验代价。

---

## 11. 动画 Warping 详解

### 11.1 适用场景

- 动画的根运动被**关掉/禁掉**了，但你仍想要动画自带的位移节奏
- 动画位移距离固定，但目标距离每次不同（矮墙 90cm / 高墙 120cm）
- 攻击需要贴到目标身前，但目标远近不一

### 11.2 数学本质

设：

- **R** = 当前动画时间到窗口终点的**剩余动画位移**（世界空间）
- **D** = 角色当前位置到窗口目标的向量

插件求一个变换 **T** 使 **T(R) = D**：

```
        R                     D
  ●─────→            ●───────────────→
                        │
                   T = 水平旋转 + 等比缩放
                        │
                        ▼
  T(R) 与 D 同方向、同长度
```

逐帧的动画位移也经同一个 T 变换，所以整段走完后角色落在目标上。

**不是** 旧插件的"缩放 + 两个方向剪切"——那会扭曲动画。这里只有旋转和等比缩放。

### 11.3 完整用法

**方案 A：一个节点（推荐）**

```
Make Warped Animation Request
    StartLocation = Get Center Location (Self)
    Animation     = 你的动画
    Target        = 终点世界坐标（胶囊中心）
    Rate          = 1.0
    RotationMode  = 段末朝哪（FaceMotion=朝目标方向 / FaceTarget=指定角度 / None=不动）
          │
          ▼
Play Motion (Character)
```

**方案 B：多窗口（动画中段做一次对齐，前后原样播放）**

```
动画时间轴：
  0 ──────── 0.3 ══════════ 0.8 ──────── 1.5
             │     Warping 窗口      │
             原样播放               原样播放
```

手填 `Warp.Segments`（C++）：

```cpp
FRMSMotionRequest Request = URMSMotionLibrary::MakeAnimationRequest(GetActorLocation(), MyMontage);
FRMSWarpSegment Segment;
Segment.WindowStart = 0.3f;
Segment.WindowEnd   = 0.8f;
Segment.Target      = TargetLocation;
Segment.bWarp       = true;
Request.Warp.Segments.Add(Segment);
URMSMotionLibrary::PlayMotion(this, Request);
```

**方案 C：用动画上的标记（最省事的多窗口做法）**

1. 在动画上放 **RMS Warp Window** 节点，填 `TargetName`（如 `WallTop`）
2. 蓝图里：

```
Build Warp From Animation
    Animation       = 你的动画
    TargetNames     = ["WallTop"]
    TargetLocations = [墙面顶部坐标]
          │
          ▼  FRMSWarpParams
    接到 Request.Warp
```

标记只是**静态数据**：在 `PlayMotion` 时一次性读取，不依赖通知在播放时被触发，所以不受跳帧、打断、网络延迟影响。

这条路径只生成位移窗口；要朝向 Warping 就在返回的 `Segments` 上补 `bWarpRotation` / `TargetRotation`。

### 11.3b 朝向 Warping（攻击动画专用）

**场景：** 攻击动画本体不转头，或者你在蒙太奇里锁了旋转。你要角色朝目标点位移，**最后也面朝目标点**。

这是 §7 旋转 API 在动画这条路上的用法。核心是把朝向挂在 **Warp 段**上，而不是 `Params`——因为动画分支不读 `Params.RotationMode`。

```cpp
Segment.bWarpRotation = true;
Segment.TargetRotation = FRotator(0.0, 90.0, 0.0);   // 只取 Yaw
Segment.RotationCurve = MyCurve;                     // 可选：控完成时机
```

或者用构造器一步到位：

```
Make Warped Animation Request(..., RotationMode = FaceMotion, TargetRotation, RotationCurve)
```

三种模式、时机的画法、为什么不需要起始朝向，全部见 [§7](#7-旋转-api)。这里只讲 Warping 特有的两点：

**1. 段末精确到位，不看曲线**

不管 `RotationCurve` 怎么画，段结束时朝向都**精确等于** `TargetRotation`。曲线只决定中途怎么分配。

**2. 多段串联**

一帧内跨多段时，每段以前一段的结果为基准推进（用「工作朝向」依次前进，不是各自从同一朝向算）。

```
段 A：0.0 ~ 0.5   转到 45°
段 B：0.5 ~ 1.0   转到 -135°

  ┌─ 段 A ─┐┌─ 段 B ──────┐
  │   45°  ││      -135°  │
0°┴────────┴──────────────┴─
                       结果 -135°
```

写「先转到 45°，再转到 -135°」，最终落在 -135°。段间若有间隙段，朝向停在前一个旋转段的结果上，不回弹。

> **未实机验证：** 旋转数学（进度比例、段末收敛、多段串联、中途偏转后收回）有单元测试覆盖，但没用真实动画 + 真实角色跑过，转动观感需要验收。


### 11.4 窗口的自动整理

你给的 `Segments` 会被自动整理：

```
输入（乱序、重叠、越界）：
   [0.6-0.9] [0.2-0.6] [1.4-1.6]

整理后（连续、有序、覆盖整个动画区间）：
  [0.0-0.2 原样] [0.2-0.6 Warp] [0.6-0.9 Warp] [0.9-1.5 原样]
```

规则：

1. 按起点排序，裁剪到动画区间
2. 长度为 0 或完全越界的丢弃
3. **重叠时前一段优先**，后一段起点推迟
4. 空隙自动补成 `bWarp = false` 的段 → **不会瞬移**
5. 整个区间无缝覆盖，不会两段同时生效

### 11.5 缩放限幅

```
0.1 ────────── 1.0 ────────── 4.0
 │              │              │
压缩太多        原始比例        放大太多
到不了目标      刚好            到不了目标
```

默认 `MinScale = 0.1`、`MaxScale = 4.0`。超出就到不了目标——**这是有意的**，宁可到不了也不过度扭曲动画。可以调。

### 11.6 已知限制（必须知道）

| 限制 | 说明 | 缓解 |
| --- | --- | --- |
| **俯仰角不同时 Z 对不上** | 相似变换保证长度和水平方向，但 R 与 D 的俯仰角不同时 Z 分量对不上目标 | 目标高度差大时，改用 `MakeParabolaRequest` |
| **缩放限幅** | 目标超过动画位移 4 倍远时到不了 | 调 `MaxScale` |
| **转换旋转在开始时固定** | 动画期间角色转向，位移方向不跟着转 | 动画内不要有明显转身 |
| **剩余位移接近 0 时不变换** | 动画根位移本身很小时无从下手 | 用带明显根位移的动画 |
| **朝向 Warping 未实机验证** | 数学有单元测试（进度比例、段末收敛、多段串联、偏转后收回），但没有用真实动画 + 真实角色跑过 | 实机验收时重点看转动观感与是否与动画节奏打架 |

---

## 12. 枚举全表

### `ERMSMotionEndReason` — 结束原因

| 值 | 含义 | 触发场景 |
| --- | --- | --- |
| `Completed` | 正常播完 | 时长走完 |
| `Interrupted` | 被打断 | `StopMotion`、角色销毁、World 销毁 |
| `Replaced` | 被替换 | 同名 + `Replace` 策略 |
| `Failed` | 未真正开始 | 参数非法、`Block` 策略命中 |

### `ERMSConflictPolicy` — 同名冲突

| 值 | 行为 |
| --- | --- |
| `Allow` | 不管同名，直接叠加 |
| `Replace` | 先停掉同名旧运动（旧运动记为 `Replaced`），再开始新的 |
| `Block` | 同名存在时拒绝新请求（`Failed`，返回无效 Handle） |

`InstanceName` 是判重的依据。不填（`None`）时不做同名检查。

### `ERMSReachMode` — 到达模式

见 [§10](#10-到达模式被挡住之后怎么办)。`Guaranteed` / `Incremental`。

### `ERMSPathKind` — 路径形状

`Straight` / `Polyline` / `Spline` / `Parabola`。

### `ERMSEase` — 内置缓动

| 值 | 速度曲线 |
| --- | --- |
| `Linear` | 匀速 |
| `EaseIn` | 先慢后快（`t²`） |
| `EaseOut` | 先快后慢 |
| `EaseInOut` | 两头慢中间快（smoothstep） |

想要别的形状用 `Params.CustomProgressCurve`（`UCurveFloat`，横轴时间 0～1，纵轴位移 0～1）。

### `ERMSFinishVelocity` — 结束速度

| 值 | 行为 |
| --- | --- |
| `Keep` | 保持最后一帧的速度（角色会继续滑） |
| `Zero` | 速度清零（停在原地） |
| `Clamp` | 水平速度限制到 `FinishClampSpeed` |

⚠ `Keep` 容易让角色冲过头。**要精确落点就用 `Zero`。**

### `ERMSRotationMode` — 朝向

| 值 | 行为 |
| --- | --- |
| `None` | 不改变朝向 |
| `FaceMotion` | 朝运动方向（每帧即时对齐，输出旋转增量） |
| `FaceTarget` | 整段运动匀速（或按 `RotationCurve`）转到 `Params.TargetRotation` 指定的绝对朝向 |

只影响 Yaw。**只对位移运动有效**——动画运动不看这个枚举，用 `Warp.Segments[].bWarpRotation`。详见 [§7](#7-旋转-api)。

> **引擎限制：** `AccumulateMode = Additive` 时旋转会被引擎静默丢弃（只有 `Override` 的 Source 应用旋转，`RootMotionSource.cpp:1746`）。默认是 `Override`，正常不会遇到。

### `ERMSFollowLostPolicy` — 跟随目标丢失

`GoToLastKnown`（继续飞向最后位置）/ `End`（立刻结束）。

### `ERMSForceKind` — 力种类

`None`（不是力）/ `Constant`（恒力）/ `Pull`（牵引）。

---

## 13. 项目设置

**Project Settings → Plugins → JTW Root Motion Source Plus**

| 项 | 默认 | 作用 |
| --- | --- | --- |
| `DefaultDebugLevel` | 0 | `RMS.Debug` 未设置时的默认级别 |
| `TrajectorySamples` | 16 | 轨迹绘制采样段数 |
| `EventHistoryCapacity` | 128 | 保留多少条事件历史 |
| `bLockX` / `bLockY` / `bLockZ` | 全 false | 锁定轴：位移在这些轴上不产生速度。**横版项目锁 Y** |

⚠ 锁定轴**不进网络序列化**，两端必须用同一份项目设置（同一 build 天然满足）。

---

## 14. 调试

### 控制台命令

| 命令 | 作用 |
| --- | --- |
| `RMS.Debug 0` | 关 |
| `RMS.Debug 1` | 轨迹 |
| `RMS.Debug 2` | 轨迹 + 屏幕面板 |
| `RMS.Debug 3` | 全量 |
| `RMS.Dump` | 打印活动运动 + 最近事件历史 |
| `RMS.Filter <名字>` | 只显示实例名匹配的运动；无参数清除 |

**没有触发运动的测试命令。** 控制台只负责"看"，测试用例请在蓝图里调 `PlayMotion`（带资产的用例在控制台里敲路径不实用）。

### 绘制图例（`RMS.Debug ≥ 1`）

```
  ● "起点"        绿色球
  ● "终点 0.50s"  红色球 + 总时长
  ○ "中途点 1"    橙色小球，按顺序编号
  · "0.13s"       白色点 + 时间刻度（每 25% 时长一个）
  ●               洋红球 = 当前已走到的位置
  ●               黄色球 = 角色当前位置
  ─────           青色线 = 预期轨迹

  力：
  ───→ "恒力 500 cm/s"    橙色箭头
  ●←───────●              蓝色连线 = 牵引，蓝球 = 目标
  ◯ 青色圈                停止距离
```

### 屏幕面板（`RMS.Debug ≥ 2`）

```
RMS #3 AttackWarp [Anim+Warp] 62%  剩余 0.19s  P0  Guar  Auto
```

`[种类]` 可能是：`Straight` / `Polyline` / `Spline` / `Parabola` / `Anim` / `Anim+Warp` / `Force:Constant` / `Force:Pull`，可再带 `+Follow` / `+FollowLost`。

### 事件历史

运动结束后仍可用 `RMS.Dump` 回看：开始、被替换、结束原因。

---

## 15. 联网

### 15.1 基本约定

**两端各自调用，参数必须一致。**

```cpp
// 服务器与预测客户端都会执行到这里
URMSMotionLibrary::PlayMotion(Character, Request);
```

参数来源如果各端各算（比如各自读角色位置），可能有细微差异，导致 `Matches` 判为不同 Source。**能由一端确定的值，请由一端确定后再同步。**

**这就是构造器不收角色指针的原因。** 所有 `Make*Request` 只要一个显式 `FVector StartLocation`；如果节点内部去读角色位置，两端会各自取到本机值：

```
危险做法（插件刻意不提供）：节点内部读 Character->GetActorLocation()
  服务器：起点 = (1200.03, 800.01, 88.0)
  客户端：起点 = (1200.07, 799.98, 88.0)   ← 预测位置有细微差异
  → Matches 判定不同 → 服务器校正被丢弃
```

正确做法：

```cpp
const FVector Start = URMSMotionLibrary::GetCenterLocation(Character);
const FRMSMotionRequest Request = URMSMotionLibrary::MakeDashRequest(Start, Direction, 600.0f, 0.3f);
URMSMotionLibrary::PlayMotion(Character, Request);   // 两端用同一个 Request
```

同样地，`Request.StartLocation` 为零向量时插件会写 Warning——通常意味着忘了填。

### 15.2 `Matches` 比较什么

引擎用 `Matches` 把服务器校正数据配到本地 Source。以下任一项变了就是不同 Source：

- 基类：优先级、叠加模式、实例名、时长
- 参数：路径种类、缓动、到达模式、弧线高度、结束速度、缩放限幅
- **朝向（位移）**：`RotationMode`、`TargetRotation.Yaw`、`RotationCurve` 引用。按模式分级比较，见下表
- 几何：起点、终点、**每一个路径点**（容差 0.1 厘米）
- 动画：资产、速率、忽略 Z、动画区间、转换旋转
- **Warping**：每一个目标段的窗口、目标、`bWarp`、`bWarpRotation`、`TargetRotation.Yaw`、`RotationCurve` 引用
- 跟随：是否跟随、目标 Actor 身份、世界偏移、丢失策略（**不比较目标位置**）
- 力：种类、方向、强度、停止距离、忽略 Z、曲线、目标身份（**不比较目标位置**）

朝向字段按模式分级，因为部分字段在特定模式下不生效：

| `RotationMode` | 比较什么 | 为什么 |
| --- | --- | --- |
| `None` | 只比模式 | 角度与曲线都不生效 |
| `FaceMotion` | 模式 + 曲线 | 方向由速度决定，`TargetRotation` 无意义 |
| `FaceTarget` | 模式 + `TargetRotation.Yaw` + 曲线 | 三者都影响结果 |

旋转只发 **Yaw**：俯仰与翻滚不上网，接收端一律清零。

### 15.3 对象引用怎么走网络

| 引用 | 对端解析不到时 |
| --- | --- |
| 自定义曲线、偏移曲线、动画 | **放弃该 Source** + 错误日志（曲线/动画决定整条轨迹，退化会让两端走出不同路径） |
| 跟随目标、牵引目标 | **不放弃**，沿用同步过来的目标位置，等目标出现 |

### 15.4 已验证 / 未验证

**已用真实 NetDriver 验证（`RMS.Net.*`，7 项）：**

| 项 | 结果 |
| --- | --- |
| 直线位移两端一致 | 差 0.00 |
| 折线 / 样条 / 抛物线 | 中途差 10～17，终点差 0.0 |
| 恒力 | 2 秒服务器 796.8 / 客户端 803.4 |
| 牵引 | 两端都停在距目标 144.3，差 0.0 |
| 跟随复制目标 | 两端都落在目标上，差 0.0 |
| 曲线资产经 PackageMap | 中途进度 0.765 / 0.776（线性应 0.4），两端一致 |
| 动画资产经 PackageMap | 资产被解析，但该动画不带根位移 |

**未验证：** 偏移曲线（引擎自带内容里没有 `UCurveVector`）、带根位移动画与 Warping 的联网表现、模拟代理、网络延迟/丢包、多客户端。

详见 `Docs/11-network-acceptance.md`。

---

## 16. 完整案例集

> 所有案例的 `StartLocation` 都接 `Get Center Location(Self)`——**不是** `Get Actor Location` 直连，虽然两者等价，但名字能防止下次误填脚底点。目标点如果来自射线，先过 `Foot To Center`。

### 案例 1：玩家冲刺（撞墙就停）

```
[输入 Dash]
    │
    ▼
Get Center Location(Self) ──┐
                            ▼
Make Dash Request(StartLocation, Get Actor Forward Vector, 600, 0.3, "Dash")
    │
    ▼
Play Motion(Self)
```

要点：`Incremental`，撞墙停下，不会贴墙抖。默认 `RotationMode = FaceMotion`，朝冲刺方向。

### 案例 2：位移技能（必须送到位）

```
Make Straight Request(StartLocation, TargetLocation, 0.4, EaseOut, Guaranteed,
                      None, ZeroRotator, "Blink")
    │
    ▼
Play Motion(Self)
```

要点：`Guaranteed`。如果目标点可能被墙挡住，角色会卡在墙前，用 `RMS.Debug 2` 能看到。`RotationMode` 传 `None`——瞬移类技能通常不希望转身，朝向交给逻辑。

### 案例 3：受击击退 + 短暂浮空

```
Make Knockback Request(StartLocation, -HitDirection, 400, 0.4, 60, "HitReact")
    │
    ▼
Play Motion(Self)
```

要点：不改变朝向（受击不该转身），带 60 厘米抬升。这是唯一固定不改朝向的预设。

### 案例 4：跳跃到平台

```
Make Jump Request(StartLocation, PlatformTopLocation, 150, 0.7,
                  FaceMotion, ZeroRotator, "Jump")
    │
    ▼
Play Motion(Self)
```

要点：`ApexHeight` 是**距离起终点连线的高度**。起终点等高时，顶点在连线之上 150。

`PlatformTopLocation` 如果是射线打在平台顶面得到的点（脚底语义），**必须先 `Foot To Center`**，否则角色会穿进平台半个身位。

### 案例 5：攻击位移——动画去掉 RM 后精确贴到目标 ⭐

这是你最初的场景，现在还能顺带解决朝向。

```
第 1 步：播放动画（位移不由动画提供）
Play Montage(AttackMontage)          ← 动画本身关掉了根运动

第 2 步：用插件接管位移 + 朝向
Make Warped Animation Request
    StartLocation  = Get Center Location(Self)
    Animation      = AttackMontage
    Target         = 目标身前坐标（胶囊中心语义）
    Rate           = 1.0             ← 必须与 Play Montage 的速率一致
    RotationMode   = FaceMotion      ← 段末面朝目标方向
    TargetRotation = （FaceMotion 时不用填）
    RotationCurve  = （可选，控转向时机）
    │
    ▼
Play Motion(Self, "AttackMove")
```

**先做这一步诊断**（同一个动画，不带 Warp）：

```
Make Animation Request(StartLocation, Animation = AttackMontage, Rate = 1.0)
    │
    ▼
Play Motion(Self)
```

- 角色有位移 → 根位移能读到，换成 `Make Warped Animation Request`
- 角色完全不动 → 这个动画读不到根位移，需要先确认"去掉 RM"是用哪种方式做的

### 案例 5b：锁朝向的攻击——前段不转，末段集中转完 ⭐

攻击动画本体不转头（或蒙太奇里锁了旋转），要求：位移朝目标走，**但转身发生在挥砍那一刻**。

```
Make Warped Animation Request
    StartLocation = Get Center Location(Self)
    Animation     = AttackMontage
    Target        = 目标身前坐标
    Rate          = 1.0
    RotationMode  = FaceMotion
    RotationCurve = Curve_TurnAtImpact      ← 关键
    │
    ▼
Play Motion(Self, "AttackMove")
```

`Curve_TurnAtImpact` 画成：

```
旋转进度
 1 │                    ╭────────
   │                 ╭──╯
   │              ╭──╯
 0 │──────────────╯
   └──────────────────────────── 动画时间进度
   0        0.6              1.0
```

0 → 0.6 恒为 0（完全不转），0.6 → 1 升到 1（末段集中转完）。**段结束时朝向精确等于目标**，曲线怎么画都不影响这个结果。

要换成「朝向与位移方向不同」（比如绕到目标侧面但面朝它），把 `RotationMode` 改成 `FaceTarget`，`TargetRotation` 填目标方向。

### 案例 5c：横向闪避但仍面朝敌人

位移方向与朝向不同的典型场景。

```
俯视：
          敌人
           ▲
           │ 面朝这里
           │
        角色 ──→ 往右闪
```

```
Make Dash Request(StartLocation, GetRightVector, 400, 0.25,
                  FaceTarget, LookAtEnemyRotation, "Dodge")
    │
    ▼
Play Motion(Self)
```

`RotationMode = FaceTarget` 让朝向独立于运动方向；`LookAtEnemyRotation` 是「角色指向敌人」的朝向（用 `Find Look at Rotation(角色位置, 敌人位置)`）。

如果想让躲闪时**瞬间**就转好再位移，把 `Params.RotationCurve` 设为 0→0.1 升到 1 的陡曲线。

### 案例 6：连招——位移结束再进下一段

```
Play Motion And Wait(Request = 第一段请求)
    │
    ├── On Completed   → 播第二段攻击
    └── On Interrupted → 回到 Idle
```

### 案例 7：多段曲线位移（比如弧形绕到背后）

```
Make Path Request
    StartLocation = Get Center Location(Self)
    PathPoints = [P1, P2, P3]     ← 不含起点，终点自动取 P3
    Kind       = Spline
    Duration   = 0.8
    │
    ▼
With Rotation(RotationMode = FaceTarget, TargetRotation = 朝原目标)   ← 可选
    │
    ▼
Play Motion(Self)
```

要点：按弧长匀速；点距不均也不影响速度。`Make Path Request` 没有朝向参数，需要转向就接 `With Rotation`。

### 案例 8：跳到会移动的平台（跟随）

```
Make Follow Request
    StartLocation = Get Center Location(Self)
    TargetActor = MovingPlatform
    WorldOffset = (0, 0, 100)     ← 停在平台上方 100
    Duration    = -1              ← 无限
    LostPolicy  = End             ← 平台没了就结束
    RotationMode = FaceTarget     ← 可选：一直面朝平台
    TargetRotation = （跟随中由你决定；留 None 则不转）
    │
    ▼
Play Motion(Self)
```

结束时记得 `StopAllMotions(Self)` 或 `StopMotion`。

### 案例 9：把敌人拉过来

```
Make Pull Request
    StartLocation = Get Center Location(Enemy)
    TargetActor  = Self           ← 拉向玩家
    Strength     = 600
    StopDistance = 150
    bIgnoreZ     = true           ← 地面角色
    Duration     = 0.5            ← 拉 0.5 秒后停
    │
    ▼
Play Motion(Enemy)
```

要点：`StopDistance` 保证不会把敌人拉到穿模。力类不支持朝向（`RotationMode` 被忽略）。

### 案例 10：风区持续推力

```
[进入风区]
Make Constant Force Request(StartLocation, WindDirection, 300, -1, "Wind")
    │
    ▼
Play Motion(Self)

[离开风区]
Stop All Motions(Self)
```

要点：无限时长必须手动结束。

### 案例 11：横版项目锁 Y 轴

Project Settings 里勾 `bLockY`，之后所有运动都不会产生 Y 方向位移。

```
俯视：
  X ←→ 玩法平面
  Z ↑↓ 跳跃
  Y ⊗  被锁死
```

### 案例 12：两个运动互斥（同名 + Block）

```
[请求 1]
Request.InstanceName = "AttackMove"
Request.ConflictPolicy = Replace
→ Play Motion          ← 得到 Handle A

[请求 2]（同名，Replace）
→ Play Motion          ← A 被停（原因 Replaced），B 开始

[请求 3]（同名，Block）
→ Play Motion          ← 返回无效 Handle，原因 Failed，写 Warning
```

---

## 17. 常见问题与限制

### 17.1 蓝图连不上某个引脚

用构造器（§5）而不是手拼 `FRMSMotionRequest`。`Warp.Segments` 是 `TArray<FRMSWarpSegment>`，手工拼需要三层嵌套，几乎无法维护。

### 17.2 角色完全不动

按顺序排查：

| 检查 | 说明 |
| --- | --- |
| 角色有没有 Controller？ | 引擎的 CMC 对**无控制器**角色默认不跑移动（`IsPlayingRootMotion` 只认动画根运动，不认 RootMotionSource）。AI/测试角色需要开 `bRunPhysicsWithNoController` |
| 移动模式是什么？ | `MOVE_None` 下不会移动 |
| `RMS.Dump` 里有这个运动吗？ | 有 → 运动在跑但没推动角色；没有 → 请求被拒绝了 |
| 控制台有没有 Warning？ | 参数非法会写 Warning |
| 动画根位移能读到吗？ | 用 `Make Animation Request`（不带 Warp）单独验证 |

### 17.3 角色冲过落点

结束速度是 `Keep`。改用 `Zero`（`MakeJumpRequest` / `MakeWarpedAnimationRequest` 已经是 `Zero`）。

### 17.4 Warping 落点偏了

| 原因 | 检查 |
| --- | --- |
| 目标太远/太近，超出缩放限幅 | 看 `RMS.Debug 2`，或调 `MaxScale` / `MinScale` |
| 目标高度差大 | 俯仰角限制，改用抛物线 |
| 动画实际没有根位移 | 用 `Make Animation Request` 先验证 |

### 17.5 位移和动画对不上

`Rate` 与 `Play Montage` 的速率不一致。插件按 `Rate` 自己算位移节奏，不读 Montage 的实际播放位置。

### 17.5b 角色不转向

按顺序排查：

| 检查 | 说明 |
| --- | --- |
| 是动画运动吗？ | 动画分支**不读** `Params.RotationMode`，要用 `Warp.Segments[].bWarpRotation` 或 `Make Warped Animation Request` 的 `RotationMode` 参数 |
| `AccumulateMode` 是 `Additive` 吗？ | 引擎只对 `Override` 应用旋转（`RootMotionSource.cpp:1746`）。`Additive` 时旋转被静默丢弃，不报错 |
| 是力类运动吗？ | 恒力 / 牵引不支持朝向 |
| 目标朝向等于当前朝向吗？ | 差 0 度时每帧增量为单位旋转，看不到转动是正常的 |
| 是纯垂直位移吗？ | `FaceMotion` 在水平投影重合时没有朝向可言，插件会主动关掉旋转（不造任意方向）|
| 曲线末端不到 1？ | 实现里会强制补足，但如果你画错了曲线（比如末段回落），中途行为会很怪 |

### 17.5c 转向看起来「卡一下才转」

曲线把旋转压到了后段。这是 `RotationCurve` 的设计效果（前段不转、末段集中转完），想整段匀速转就把曲线留空。

### 17.6 联网两端位置不一致

| 原因 | 处理 |
| --- | --- |
| 只有一端调了 `PlayMotion` | 两端都要调 |
| 两端参数不同（例如各自算的起点） | 由一端确定参数再同步。**这是构造器不收角色指针的原因** |
| `Incremental` + 客户端撞到服务器才有的东西 | 已知行为，运动期间必然分叉，靠引擎位置校正收敛 |
| 目标是会移动的 Actor | 确认目标 Actor 本身能复制 |
| 转向曲线两端不同 | 曲线解析不到时插件会放弃整个 Source（不会悄悄退化成线性） |

### 17.7 力停不下来

`Duration < 0` 是无限时长，必须 `StopMotion` 或 `StopAllMotions`。

### 17.8 本版本已知限制汇总

| 限制 | 影响 |
| --- | --- |
| Warping 俯仰角不同时 Z 对不上 | 目标高度差大时改用抛物线 |
| Warping 缩放限幅 0.1~4.0 | 超远目标到不了 |
| 动画转换旋转在开始时固定 | 动画内转身时位移方向不跟随 |
| 动画分支不读 `Params.RotationMode` | 动画要转向必须用段级开关或 `With Rotation` |
| `Additive` 模式下旋转被引擎丢弃 | 想转向就用 `Override`（默认） |
| 力类不支持朝向 | 恒力 / 牵引不会转角色 |
| 旋转只作用于 Yaw | 俯仰与翻滚不参与，也不会被序列化 |
| 朝向 Warping 未实机验证 | 数学有测试覆盖，转动观感待验收 |
| `Follow` / `Pull` 不比较目标位置 | 两端目标位置有差异时不会误判；但也意味着目标位置的差异靠引擎校正 |
| 偏移曲线不自动归零端点 | 曲线端点非 0 时起终点会被偏移 |
| 锁定轴不进序列化 | 两端必须同一份项目设置 |
| 测试用反射类在 Shipping 里存在空壳 | `ARMSNetTestCharacter` / `ARMSNetTestGameMode`，无逻辑 |
