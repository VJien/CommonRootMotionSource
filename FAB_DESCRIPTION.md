# Fab 上架商店文案与宣传物料指南 (JTW Root Motion Source Plus)

本文档提供可直接复制粘贴到 Fab 后台的**英文版（主推）**与**中文版**商店介绍文案，以及截图/视频录制清单。

---

## 一、Fab 后台基础字段（Basic Info）

- **Title (商品标题):**
  `JTW Root Motion Source Plus - Network-Safe Procedural & Data-Driven Root Motion`
- **Category (分类):**
  `Code Plugins` -> `Gameplay` (或 `Movement` / `Animation`)
- **Price (价格):**
  `Free`（免费）或入门低价
- **Supported Engine Versions:**
  `5.8`
- **Supported Target Platforms:**
  `Win64`
- **Short Description (简短描述，限 140~250 字符):**
  > **EN:** A lightweight, network-safe Root Motion Source scheduler for CharacterMovementComponent. Drive dashes, jumps, splines, actor-following, animation warping, and pull forces on any Character via a single Blueprint node with built-in 3D trajectory debugging.
  >
  > **CN:** 轻量、网络安全的 CharacterMovementComponent 根运动调度插件。无需派生组件或配置资产，单节点即可在任意 Character 上驱动冲刺、抛物线跳跃、样条路径、动态目标跟随、动画 Warping 与持续牵引力，内置 3D 轨迹调试。

---

## 二、Fab 商店详情正文（Full Description - 英文版，推荐直接粘贴）

# JTW Root Motion Source Plus

🚀 **Procedural, curve-driven, and animation-warped Root Motion for any `ACharacter`—without component bloat.**

**JTW Root Motion Source Plus** is a lightweight, network-safe motion scheduler built on Unreal Engine's native `FRootMotionSource` and `UCharacterMovementComponent`. Instead of maintaining dozens of custom `FRootMotionSource` subclasses, `UGameplayTask` objects, or custom `CharacterMovementComponent` base classes, you drive every kind of character displacement through a single unified source and a pure-data request struct.

Works immediately on any standard `ACharacter`—no actor components to attach, no DataAssets to maintain, and no GAS dependency required.

---

### ✨ Key Features

- **Zero Intrusion, Instant Setup**
  Call `PlayMotion(Character, Request)` from C++ or Blueprints, or use the cancellable async node **Play Motion And Wait** (`OnCompleted`, `OnInterrupted`, `OnFailed`).
- **Guaranteed vs. Incremental Reach Modes (`ERMSReachMode`)**
  - **Guaranteed:** Automatically compensates for blocked frames so your character reaches the exact target location when the duration ends.
  - **Incremental:** Advances strictly by each frame's expected step—if blocked by a wall, blocked displacement is discarded naturally instead of dragging the character along obstacles.
- **4 Path Shapes + Local Offset Curves**
  - **Straight:** Point-to-point linear movement with optional sine `ArcHeight`.
  - **Polyline & Catmull-Rom Spline:** Multi-point trajectories (up to 64 points) parameterized by **arc length** for uniform speed across uneven waypoints.
  - **Analytic Parabola:** Exact parabolic jump trajectories with configurable `ArcHeight`, landing accurately even when start and end elevations differ.
  - **Custom Curves:** Built-in easing (`Linear`, `EaseIn`, `EaseOut`, `EaseInOut`), optional `CustomProgressCurve` (`UCurveFloat`), and heading-space 3D `OffsetCurve` (`UCurveVector`).
- **Animation Root Motion & Multi-Window Warping**
  Replay root motion from any `UAnimSequence` or `UAnimMontage`, and align specific time windows to world targets using `FRMSWarpSegment` or the non-intrusive **RMS Warp Window** (`URMSAnimNotifyState_Warp`) notify state.
- **Rotational Warping with Timing Curves (`ERMSRotationMode`)**
  Turn the character independently of how it moves, and decide *when* the turn happens.
  - **`FaceMotion`:** face the direction of travel.
  - **`FaceTarget`:** rotate to a specific absolute yaw — glide sideways while still facing the enemy, for example.
  - **Per-segment rotation for animations:** locked-rotation attack animations can still end up facing the target.
  - **`RotationCurve`:** shape the turn across the motion (e.g. hold still for the first 60%, then snap around on the impact frame). The final orientation always lands exactly on target regardless of the curve.
  - Rotation is emitted as a delta and converged by proportional approach, so it self-corrects if something else nudges the character mid-motion — no start rotation needs to be supplied or replicated.
- **Explicit Capsule-Center Coordinates + Foot/Center Helpers**
  Every world coordinate is the **capsule center** (`GetActorLocation`), documented as such. Four `RMS|Location` helpers (`Get Center Location`, `Get Foot Location`, `Foot To Center`, `Center To Foot`) convert to and from foot-level points, so raycast ground hits never land half a capsule off.
- **Dynamic Actor Following**
  Track moving `TargetActor` targets with world-space offsets and configurable lost-target policies (`GoToLastKnown` or `End`).
- **Frame-Rate Independent Forces (Finite or Infinite)**
  Apply **Constant** directional velocity or **Pull** (attraction toward a target Actor with `StopDistance`, horizontal `bIgnoreZ`, and distance-over-strength curves). Supports infinite duration (`Duration < 0`) with one-call cleanup via `StopMotion` / `StopAllMotions`.
- **One-Node Blueprint Presets**
  Includes `MakeDashRequest`, `MakeKnockbackRequest`, `MakeJumpRequest`, and `MakeDirectionalRequest`—ready to play as-is or tweak any field before execution.
- **Multiplayer Prediction & Server Correction Ready**
  Complete `NetSerialize`, `Matches`, and `UpdateStateFrom` implementation across all parameters, paths, warp segments, follow targets, and forces.
- **Built-in 3D Trajectory & HUD Debugger (Zero Shipping Overhead)**
  Toggle `RMS.Debug 1-3` in the console to visualize expected trajectories, start/end spheres, numbered waypoints, 25%/50%/75% time ticks, warp windows, pull stop-distance rings, and an on-screen active motion HUD. Completely compiled out in Shipping builds (`#if !UE_BUILD_SHIPPING`).

---

### 🛠️ Quick Start

1. Enable **JTW Root Motion Source Plus** in **Edit → Plugins**.
2. In any Blueprint, add **Get Center Location** (pass your character) → **Make Dash Request** (or **Make Jump Request** / **Make Knockback Request**) → **Play Motion And Wait**.
   > All `Make*` nodes take an explicit `StartLocation` instead of a character pointer. This is deliberate: if the node read the character's position internally, the server and the predicting client would each sample their own local value, `Matches` would reject the pair, and server correction would silently stop working. Pass the center once and both sides stay identical.
3. Open the console during PIE and run `RMS.Debug 2` to inspect the live 3D trajectory and active motion state.

---

### 📋 Technical Details

- **Modules:** `RootMotionSourcePlus` (Runtime)
- **Number of C++ Classes / Structs:** 18 public types (`URMSMotionLibrary`, `URMSWarpLibrary`, `URMSAsyncAction_PlayMotion`, `URMSMotionSubsystem`, `URMSAnimNotifyState_Warp`, `URMSSettings`, `FRMSMotionSource`, `FRMSMotionRequest`, `FRMSMotionParams`, etc.)
- **Network Replicated:** Yes (via `UCharacterMovementComponent` `FRootMotionSourceGroup` prediction & correction; both server and predicting client call `PlayMotion` with matching parameters)
- **Engine Compatibility:** Unreal Engine 5.8
- **Supported Development & Target Platforms:** Windows (Win64)
- **Documentation:** Included in the plugin (`README.md` and `Docs/` directory)

*Developed by JTW Studio.*

---

## 三、Fab 商店详情正文（中文对照版）

# JTW Root Motion Source Plus

🚀 **面向任意 `ACharacter` 的程序化、曲线驱动与动画对齐根运动（Root Motion）调度器——零组件侵入，即插即用。**

**JTW Root Motion Source Plus** 是基于 Unreal Engine 原生 `FRootMotionSource` 与 `UCharacterMovementComponent` 构建的轻量级、网络安全运动调度插件。无需派生自定义 `CharacterMovementComponent`，无需在角色身上挂载任何 Actor Component，也无需创建繁琐的 DataAsset，只需一个纯数据结构体 `FRMSMotionRequest` 与一个蓝图节点即可驱动冲刺、击退、抛物线跳跃、多点折线/样条巡航、动态目标跟随、动画 Warping 以及持续牵引力场。

### ✨ 核心特性

- **零侵入、极简入口**：支持 C++ / 蓝图静态库 `URMSMotionLibrary::PlayMotion`，以及自带 `OnCompleted` / `OnInterrupted` / `OnFailed` 引脚的可取消异步节点 **Play Motion And Wait**。
- **双到达模式（`Guaranteed` 强制到达 vs `Incremental` 逐帧递进）**：
  - `Guaranteed`：每帧自动补偿被阻挡的位移，确保时长结束时精准到达目标点；
  - `Incremental`：严格按每帧期望增量推进，撞墙即停、不沿墙面强行找补滑行。
- **4 种几何路径 + 朝向空间曲线偏移**：支持直线（`Straight`）、弧长匀速折线（`Polyline`）、Catmull-Rom 平滑样条（`Spline`）与解析抛物线（`Parabola`，支持不等高精准落点），并可叠加 `CustomProgressCurve` 与三维 `OffsetCurve`。
- **动画根位移提取与多窗口 Warping**：支持直接提取 `UAnimSequence` / `UAnimMontage` 根位移，并通过 `RMS Warp Window` 动画通知状态或代码段将位移对齐到世界目标。
- **可控制时机的旋转 Warping（`ERMSRotationMode`）**：转向与位移解耦，并且能决定「什么时候转」。
  - `FaceMotion`：朝运动方向；
  - `FaceTarget`：转到指定的绝对朝向——横向闪避的同时仍面朝敌人；
  - **动画段级朝向**：锁了旋转的攻击动画也能在段末精确面朝目标；
  - **`RotationCurve`**：控制转身落在运动的哪个阶段（例如前 60% 完全不转、命中帧集中转完），无论曲线怎么设，结束时朝向都精确等于目标；
  - 旋转以增量形式输出并按比例趋近，中途被外力扭偏会自动收回，**不需要提供也不需要同步起始朝向**。
- **统一的胶囊中心坐标语义 + 脚底/中心换算**：所有世界坐标都是**胶囊中心**（`GetActorLocation`），代码与文档均明确标注；`RMS|Location` 分类下四个换算节点（`Get Center Location`、`Get Foot Location`、`Foot To Center`、`Center To Foot`），射线打地面的命中点不会差半个胶囊高。
- **动态目标跟随（Follow）与无终点力场（Constant / Pull）**：支持实时追踪移动 Actor（可配丢失策略），以及与帧率无关的恒力推进和带停止距离圈的牵引吸引（支持 `Duration < 0` 无限时长与 `StopAllMotions` 一键清理）。
- **开箱即用预设函数**：内置 `MakeDashRequest`（冲刺）、`MakeKnockbackRequest`（击退）、`MakeJumpRequest`（抛物线跳跃）与 `MakeDirectionalRequest`。
- **内置 3D 轨迹与屏幕面板调试器**：控制台输入 `RMS.Debug 2` 即可实时查看轨迹线、起终点、编号中途点、时间刻度、Warping 目标与牵引停止圈；Shipping 包自动剔除，零运行时开销。

---

## 四、给你的截图与视频准备建议（4~5 张图 + 15~30 秒短片即可）

1. **截图 1（主展示图 / 3D 轨迹调试图）**：
   - 在编辑器 PIE 里开启 `RMS.Debug 2`，同时跑一个 `Parabola`（抛物线跳跃）或 `Spline`（多点样条曲线），截下带有青色轨迹线、`Start` 绿球、`End` 红球、`Point 1/2` 橙色球、25%/50%/75% 时间刻度以及左上角 HUD 状态文字的画面（视觉效果最直观、最吸睛）。
2. **截图 2（蓝图极简用法展示）**：
   - 在蓝图里截一张：`Make Dash Request` / `Make Jump Request` $\rightarrow$ `Play Motion And Wait`（展示 `OnCompleted` / `OnInterrupted` / `OnFailed` 三个回调引脚），并展开旁边的 `FRMSMotionRequest` 结构体引脚，体现“一个节点搞定位移”。
3. **截图 3（牵引力场 `Pull` 调试效果）**：
   - 开启 `RMS.Debug 2` 触发一个 `Pull` 牵引运动，截下蓝色的目标连线和青色的 `StopDistance` 停止距离圆圈。
4. **截图 4（Project Settings 与 `RMS Warp Window` 动画通知）**：
   - 拼合或单独截一张 **Project Settings → Plugins → JTW Root Motion Source Plus** 设置面板（展示 Debug Level、Trajectory Samples、Locked Axes `bLockX/Y/Z`）。
5. **短视频（可选，15~30 秒）**：
   - 开着 `RMS.Debug 2` 依次演示：冲刺撞墙即停（`Incremental`） vs 绕开障碍找补到达（`Guaranteed`） $\rightarrow$ 抛物线精准跳台 $\rightarrow$ 样条曲线移动 $\rightarrow$ 牵引拉怪到停止圈外停住。
