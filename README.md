# JTW Root Motion Source Plus

A lightweight, network-safe, data-driven Root Motion Source scheduler for Unreal Engine's `UCharacterMovementComponent`.

Works out-of-the-box on **any standard `ACharacter`**—no custom movement component subclass, no actor component attachment, and no data assets required.

- **Engine Version:** Unreal Engine 5.8
- **Target Platforms:** Windows (Win64)
- **Author:** JTW Studio

---

## Key Features

- **Zero Component Intrusion:** Call `URMSMotionLibrary::PlayMotion(Character, Request)` from C++ or Blueprints, or use the latent node **Play Motion And Wait** (`OnCompleted`, `OnInterrupted`, `OnFailed`).
- **Single Unified Source & Pure Data Params:** All movements are driven by a single `FRMSMotionSource` struct and pure value struct `FRMSMotionParams`—no variant class explosion and no mandatory DataAsset setup.
- **Two Reach Modes (`ERMSReachMode`):**
  - `Guaranteed`: Compensates for blocked frames so the character reaches the exact target at the end of the duration.
  - `Incremental`: Advances strictly by each frame's expected delta; blocked displacement is discarded naturally without dragging along walls.
- **Rich Trajectory & Motion Modes:**
  - **Path Kinds:** `Straight` (Point-to-Point), `Polyline` (Arc-length parameterized multi-point path), `Spline` (Catmull-Rom smooth spline), and `Parabola` (analytic jump arc with exact landing height).
  - **Curves:** Built-in easing (`Linear`, `EaseIn`, `EaseOut`, `EaseInOut`), optional `CustomProgressCurve` (`UCurveFloat`), and local heading-space `OffsetCurve` (`UCurveVector`).
  - **Animation Root Motion & Warping:** Extract and replay root motion from any `UAnimSequenceBase` (`UAnimSequence` or `UAnimMontage`), optionally aligning segments to world targets via `FRMSWarpSegment` or `URMSAnimNotifyState_Warp` (`RMS Warp Window`).
  - **Dynamic Actor Follow:** Track a moving `TargetActor` + `WorldOffset` with configurable lost-target behavior (`GoToLastKnown` or `End`).
  - **Frame-Rate Independent Forces:** `Constant` directional velocity and `Pull` (radial attraction toward a target Actor with `StopDistance`, horizontal `bIgnoreZ`, and optional `StrengthOverDistance` curve). Supports finite or infinite (`Duration < 0`) duration.
- **Ready-to-Use Blueprint Presets:** `MakeDashRequest`, `MakeKnockbackRequest`, `MakeJumpRequest`, and `MakeDirectionalRequest`.
- **Network Prediction & Correction Ready:** Complete `NetSerialize`, `Matches`, and `UpdateStateFrom` implementation for `UCharacterMovementComponent` client prediction and server correction.
- **Built-in 3D Trajectory & HUD Debugger:** Zero-cost in Shipping builds (`#if !UE_BUILD_SHIPPING`). Toggle trajectories, waypoints, time ticks, warp targets, pull radii, and active motion HUD via `RMS.Debug 1-3`, `RMS.Dump`, and `RMS.Filter`.

---

## Quick Start

### 1. Enable the Plugin
Enable **JTW Root Motion Source Plus** (`RootMotionSourcePlus`) in **Edit → Plugins**, then restart the editor.

### 2. Blueprint Usage
1. Add **Get Center Location** and pass your character — it returns the capsule center, which is what every coordinate in this plugin means.
2. Construct an `FRMSMotionRequest` (or call **Make Dash Request** / **Make Knockback Request** / **Make Jump Request** / **Make Directional Request**), feeding that center into `StartLocation`.
3. Call **Play Motion And Wait** (async latent node) or **Play Motion** (immediate handle return).
4. To cancel early, call **Stop Motion** (with the returned `FRMSMotionHandle`) or **Stop All Motions** on the character.

> **Why `StartLocation` and not a character pointer?** If a constructor read the character's position internally, the server and the predicting client would each sample their own local value. `Matches` would then treat the two as different sources and server correction would silently stop working. Pass the center once and both sides stay identical.

### 3. C++ Usage
Add `"RootMotionSourcePlus"` to your module's `PublicDependencyModuleNames` in `<YourProject>.Build.cs`, then:

```cpp
#include "RMSMotionLibrary.h"

// StartLocation is always the capsule center; GetCenterLocation == Character->GetActorLocation()
const FVector Start = URMSMotionLibrary::GetCenterLocation(Character);

FRMSMotionRequest Request = URMSMotionLibrary::MakeDashRequest(
    Start,
    Character->GetActorForwardVector(),
    /*Distance=*/ 600.0f,
    /*Duration=*/ 0.3f,
    /*RotationMode=*/ ERMSRotationMode::FaceMotion,
    /*TargetRotation=*/ FRotator::ZeroRotator,
    /*InstanceName=*/ TEXT("PlayerDash")
);
FRMSMotionHandle Handle = URMSMotionLibrary::PlayMotion(Character, Request);
```

---

## Core Concepts & API Summary

### 1. Reach Mode (`ERMSReachMode`)
- **`Guaranteed` (Default):** Each frame computes velocity from `(ExpectedPositionAtNextTime - CurrentActualActorLocation)`. If the character is briefly blocked by an obstacle and then freed, it catches up to reach `TargetLocation` on time.
- **`Incremental`:** Each frame computes velocity strictly from `(ExpectedPositionAtNextTime - ExpectedPositionAtCurrentTime)`. Blocked frames are not compensated—ideal for dashes and knockbacks that should stop against walls instead of sliding around them.

### 2. Path Kinds (`ERMSPathKind`)
- **`Straight`:** Linear interpolation from `StartLocation` to `TargetLocation` (with optional sine `ArcHeight`).
- **`Polyline` / `Spline`:** Multi-point path through `StartLocation` and `PathPoints` (up to 64 points). Parameterized by **arc length** so movement speed remains uniform even when waypoint spacing is uneven. `TargetLocation` is ignored; the final point in `PathPoints` is the endpoint.
- **`Parabola`:** Analytic parabolic arc $h(u) = 4 \cdot \text{Peak} \cdot u(1 - u)$ from `StartLocation` to `TargetLocation`, landing accurately even when start and target heights differ.

### 3. Animation Root Motion & Warping
- Set `Request.Anim.Animation` (`UAnimSequence` or `UAnimMontage`), `AnimStart`, `AnimEnd` (`<= 0` for full length), `Rate`, and `bIgnoreZ`.
- To warp animation root motion to world targets:
  - Add **RMS Warp Window** (`URMSAnimNotifyState_Warp`) notifies to your animation and set `TargetName`, then call `URMSWarpLibrary::BuildWarpFromAnimation(Animation, TargetNames, TargetLocations)` to populate `Request.Warp`; or
  - Fill `Request.Warp.Segments` (`WindowStart`, `WindowEnd`, `Target`, `bWarp`) directly.
- Warping applies horizontal yaw rotation + uniform 3D scaling clamped to `[MinScale, MaxScale]` (default `0.1` to `4.0`).

### 4. Dynamic Actor Follow (`FRMSFollowParams`)
- Set `Request.Follow.TargetActor` and `WorldOffset`. Each frame updates the target endpoint to `TargetActor->GetActorLocation() + WorldOffset`.
- `LostPolicy`: `GoToLastKnown` (continues to the last valid location) or `End` (stops immediately with `Interrupted`).

### 4b. Rotation (`ERMSRotationMode`, `Params.TargetRotation`, `Params.RotationCurve`)
Three modes, all yaw-only:

| `RotationMode` | Behaviour |
| --- | --- |
| `None` | Leave orientation untouched |
| `FaceMotion` | Face the direction of travel, aligned every frame |
| `FaceTarget` | Rotate to the absolute `TargetRotation` over the whole motion |

- `FaceTarget` is how you move in one direction while facing another (e.g. strafing sideways while still facing the enemy).
- `Params.RotationCurve` shapes *when* the turn happens: X is time progress 0-1, Y is rotation progress 0-1. Leave it empty for a constant-rate turn. The final orientation always lands exactly on `TargetRotation` regardless of the curve.
- For **animation** motions the mode lives on the warp segment instead (`Warp.Segments[].bWarpRotation` + `TargetRotation` + `RotationCurve`), because the animation branch deliberately ignores `Params.RotationMode`. `Make Warped Animation Request` exposes all three as pins.
- Two helper nodes for adjusting after construction: **With Rotation** (updates `Params` *and* any existing warp segments) and **With Warp Rotation** (warp segments only).
- Rotation is emitted as a delta and converged by proportional approach, so it self-corrects if something else nudges the character mid-motion. No start rotation needs to be supplied or replicated.
- **Engine constraint:** rotation is only applied when `AccumulateMode` is `Override` (`FRootMotionSourceGroup::GetOverrideRootMotionRotation`). With `Additive` the turn is silently dropped.

### 4c. Location Helpers (`RMS|Location`)
Every world coordinate in this plugin means the **capsule center** (`GetActorLocation()`), never a foot-level point. Two differ by the capsule half-height (88 cm by default), so mixing sources is the most common cause of a character sinking into the floor.

| Node | Purpose |
| --- | --- |
| **Get Center Location** | Capsule center; use for `StartLocation` |
| **Get Foot Location** | Foot-level point |
| **Foot To Center** | Convert a raycast ground hit into a capsule-center point |
| **Center To Foot** | Convert a capsule center into a foot-level point |

### 5. Forces (`FRMSForceParams`)
- Set `Request.Force.Kind` to `Constant` (directional speed in cm/s) or `Pull` (attraction toward `PullTarget` in cm/s, stopping within `StopDistance`, optional `StrengthOverDistance` curve, and `bIgnoreZ`).
- Set `Request.Params.Duration = -1.0f` for **infinite duration** forces; stop them anytime with `StopMotion` or `StopAllMotions`.

### 6. Debugging Console Commands (Non-Shipping Only)
| Command | Description |
| --- | --- |
| `RMS.Debug 0-3` | `0` = Off, `1` = 3D Trajectories & Force Gizmos, `2` = Trajectories + On-Screen Active Motion HUD, `3` = Full |
| `RMS.Dump` | Prints all active motions and the ring buffer of recent motion events to the Output Log |
| `RMS.Filter <Name>` | Filters debug visualization by `InstanceName` substring (pass no arguments to clear) |

### 7. Project Settings
Configure under **Project Settings → Plugins → JTW Root Motion Source Plus**:
- `DefaultDebugLevel` (0–3)
- `TrajectorySamples` (4–64)
- `EventHistoryCapacity` (16–1024)
- `bLockX` / `bLockY` / `bLockZ`: Axis plane constraints (e.g., lock `Y` for 2.5D side-scrollers).

---

## Networking Notes & Known Limitations

1. **Both Ends Must Trigger `PlayMotion`:** Following standard UE `FRootMotionSource` prediction architecture (such as GameplayAbilities), both the server and the predicting client must call `PlayMotion` with identical parameters. The plugin does not automatically replicate a one-sided call via RPC.
2. **Animation Component Rotation Snapshot:** For network determinism, `AnimComponentRotation` is captured once from the character's skeletal mesh when `PlayMotion` starts and serialized. Rotating the character mid-animation does not rotate the extracted root motion trajectory.
3. **Animation Warping Pitch Difference:** Similarity warping rotates horizontally (around world Z) and scales uniformly in 3D (`[MinScale, MaxScale]`). If the animation's natural displacement pitch angle differs from the target's elevation angle, the Z axis will not align to a different elevation—use `Parabola` or path modes for exact vertical landing across variable heights.
4. **AI / Unpossessed Characters:** Unreal's `UCharacterMovementComponent` ignores `FRootMotionSource` on characters without a Controller unless `bRunPhysicsWithNoController = true` is enabled on the movement component.

For additional Chinese documentation and per-feature details, see the `Docs/` folder included with the plugin.

---
*Developed by JTW Studio.*
