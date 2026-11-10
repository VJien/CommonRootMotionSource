# 跟随目标

终点不是固定的世界点，而是“一个 Actor 的位置 + 偏移”。

## 用法

```cpp
FRMSMotionRequest Request;
Request.Follow.TargetActor  = SomeActor;
Request.Follow.WorldOffset  = FVector(-100, 0, 0);   // 停在目标前方 100 厘米
Request.Follow.LostPolicy   = ERMSFollowLostPolicy::GoToLastKnown;
Request.TargetLocation      = SomeActor->GetActorLocation() + Request.Follow.WorldOffset;  // 初值
Request.Params.Duration     = 0.6f;
URMSMotionLibrary::PlayMotion(Character, Request);
```

每帧目标位置取 `TargetActor->GetActorLocation() + WorldOffset`。目标移动时，角色追的就是它当前的位置。

## 目标丢失

`LostPolicy`：

| 值 | 行为 |
| --- | --- |
| `GoToLastKnown`（默认） | 继续飞向最后一次已知位置，时间到即结束 |
| `End` | 立即结束（原因 `Interrupted`） |

## 目标尚未复制到本端时

客户端上的目标 Actor 可能还没复制过来（指针为空）。这时插件**不当作丢失**：沿用从服务器同步过来的目标位置继续运动，等目标出现后自动恢复跟随。只有目标明确被销毁才按 `LostPolicy` 处理。

## 联网

- 目标 Actor 用网络对象引用传输，不是裸指针。目标 Actor 需要能被复制。
- 目标位置是唯一随时间变化的状态，由服务器通过 `UpdateStateFrom` 校正到客户端。
- **`Matches` 不比较目标位置**，只比较：是否跟随、目标 Actor 身份、世界偏移、丢失策略。
  因为两端的目标位置天然有细微差异，若参与匹配，服务器校正时会把本地 Source 误判成另一个而丢弃。
- 目标更新不重置起点，也不改写剩余时长。

## 与多段路径同用

跟随目标时，`PathPoints` 与 `PathKind` 仍然生效——先按路径走到 `TargetLocation` 的相对形状，但每条路径的终点会随目标刷新。需要精确的路径形状时，建议不要与跟随同时使用。

## 怎么测试

在关卡里放一个可移动的 Actor，在蓝图里把它填进 `Request.Follow.TargetActor` 再调用 `PlayMotion`。开启 `RMS.Debug 2` 可以看到目标点与当前跟随点（蓝色，丢失时变红），详见 [08](08-debugging.md)。
