# 调试

所有调试设施在 **Shipping 构建中不编译**，不影响包体与性能。

## 设计取向

控制台只负责**看**：把运动的轨迹、起点、终点、中途点与时长画出来，并列出活动运动与历史。
**不提供触发运动的测试命令。** 真实用例（动画、Warping、跟随）需要资产与上下文，在控制台里敲参数既复杂又没有代表性，请直接在蓝图里调用 `PlayMotion` 来测试。

## 控制台命令

| 命令 | 作用 |
| --- | --- |
| `RMS.Debug <0-3>` | 0 关 / 1 轨迹 / 2 轨迹加屏幕面板 / 3 全量。不设置时使用 Project Settings 的默认值 |
| `RMS.Dump` | 打印活动运动与最近事件历史 |
| `RMS.Filter <名字>` | 只显示实例名包含该字符串的运动；无参数则清除 |

## 场景绘制（`RMS.Debug ≥ 1`）

| 内容 | 含义 |
| --- | --- |
| 青色线 | 预期轨迹 |
| 绿色球 + “起点” | 起点 |
| 红色球 + “终点 X.XXs” | 终点与总时长（折线/样条为最后一个路径点） |
| 橙色小球 + “中途点 N” | 每个中途点，按顺序编号 |
| 白色点 + 时间 | 沿轨迹每 25% 时长标一个刻度，看各段耗时是否合理 |
| 洋红球 | 当前已走过的位置（核对匀速：等时间间隔应等距离） |
| 黄色球 | 角色当前位置 |
| 红色大球 / 灰色小球 | Warping 当前段 / 其他段的段目标，带窗口区间 |
| 蓝色球 | 跟随目标 Actor 与当前跟随点（目标丢失时变红） |

## 屏幕面板（`RMS.Debug ≥ 2`）

每个活动运动一行：Handle、实例名、种类（`Straight`/`Polyline`/`Spline`/`Parabola`/`Anim`/`Anim+Warp`/`+Follow`）、进度、剩余时间、优先级、到达模式（`Guar`/`Inc`）、网络角色（`Auth`/`Auto`/`Sim`）。

## 事件历史

运动结束后仍可回看：执行 `RMS.Dump` 能看到开始、被替换、结束原因等记录。容量由 Project Settings 的 `EventHistoryCapacity` 控制。

## 在蓝图里测试

最小用例——一条带中途点的曲线，开着 `RMS.Debug 2` 就能看到轨迹：

1. 在能力或关卡蓝图里构造 `Request`：
   - `Params.PathKind = Spline`、`Params.Duration = 1.0`
   - `StartLocation` = 角色当前位置
   - `PathPoints` = 几个世界坐标点
2. 调用 `PlayMotion(Character, Request)`，或用异步节点 `Play Motion And Wait` 等待结束。
3. 控制台输入 `RMS.Debug 2`。

动画回放、Warping、跟随同理：在 `Request.Anim` / `Request.Warp` / `Request.Follow` 里填入真实资产与目标，再调用 `PlayMotion`。

联网时两端各自调用 `PlayMotion`，参数必须一致，详见 [07](07-networking.md)。

## Project Settings

**Project Settings → Plugins → JTW Root Motion Source Plus**：

| 项 | 作用 |
| --- | --- |
| `DefaultDebugLevel` | `RMS.Debug` 未设置时的默认级别 |
| `TrajectorySamples` | 轨迹绘制的采样段数 |
| `EventHistoryCapacity` | 保留多少条事件 |
| `bLockX/Y/Z` | 锁定轴：位移在这些轴上不产生速度。横版项目常锁 Y。**两端使用同一份项目设置，不进入网络序列化** |
