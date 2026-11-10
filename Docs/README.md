# JTW Root Motion Source Plus

网络安全的、数据驱动的 Root Motion 运动调度插件，基于 `UCharacterMovementComponent`。
对任意 `ACharacter` 可用，角色上不需要挂任何组件。

- 引擎版本：UE 5.8
- 版权：JTW Studio

## 入门

1. 启用插件 `RootMotionSourcePlus`。
2. 从蓝图调用 `PlayMotion(Character, Request)`，或使用异步节点 `Play Motion And Wait`。
3. 运动结束时收到 `OnCompleted` / `OnInterrupted` / `OnFailed`。

不需要创建任何资产。`FRMSMotionParams` 是纯数据，直接填在 `FRMSMotionRequest` 里即可。

## 文档索引

| 文档 | 内容 |
| --- | --- |
| [**完整手册 MANUAL**](MANUAL.md) | **所有 API、所有运动方式的图示、12 个完整案例** |
| [00 蓝图连法](00-blueprint.md) | 在蓝图里怎么拼 Request、哪些字段容易卡住、便捷节点 |
| [01 请求与参数](01-request-and-params.md) | `FRMSMotionRequest`、`FRMSMotionParams` 各字段的实际作用 |
| [02 到达模式](02-reach-mode.md) | `Guaranteed` 与 `Incremental` 的区别，以及联网影响 |
| [03 移动方式](03-movement-kinds.md) | 直线、曲线偏移、折线、样条、抛物线 |
| [04 动画根位移](04-animation.md) | 回放动画根位移、时间与速率 |
| [05 动画 Warping](05-warping.md) | 把动画根位移对齐到目标、窗口标记、已知限制 |
| [06 跟随目标](06-follow.md) | 跟随 Actor、目标丢失策略 |
| [07 联网](07-networking.md) | 两端如何各自触发、`Matches` 与序列化约束 |
| [08 调试](08-debugging.md) | 轨迹绘制（起点、终点、中途点、时长）、屏幕面板、蓝图测试方法 |
| [09 预设](09-presets.md) | 冲刺、击退、跳跃：生成 Request 的蓝图函数 |
| [10 力](10-forces.md) | 恒力、牵引、无限时长与 `StopAllMotions` |
| [11 联网验收指南](11-network-acceptance.md) | 已有的自动化联网测试及真实结果、没覆盖的部分与自验方法 |

## 已知限制（先看这里）

- **联网需要两端各自调用 `PlayMotion`。** 插件不做“一端发起、自动同步到另一端”。
- **动画 Warping 用“水平旋转 + 等比缩放”。** 动画位移与目标的俯仰角不同时，Z 方向对不上目标，详见 [05](05-warping.md)。
- **Warping 的缩放默认限幅 0.1～4.0。** 目标超过 4 倍远时到不了目标，这是有意的。
- **动画根位移的转换旋转在播放开始时固定。** 动画期间角色转向时，位移方向不跟着转。
- **跟随目标的 Actor 在本端尚未复制出来时**，沿用同步过来的目标位置继续运动，不会中断。
- 本版本的 `Incremental` 到达模式在联网下可能两端位置分叉，需要位置校正收敛，详见 [02](02-reach-mode.md)。
- **力没有终点，`ReachMode` 对力无效。** 无限时长（`Duration < 0`）的力必须用 `StopMotion` 或 `StopAllMotions` 结束，详见 [10](10-forces.md)。
- **测试角色没有 Controller 时**，引擎的 `CharacterMovementComponent` 默认不会为它跑移动（引擎对无控制器角色只认动画根运动，不认 RootMotionSource）。给 AI 或测试用角色使用本插件前，请开启 `bRunPhysicsWithNoController`。
