# Alternate Assist Mod v0.1.7 — CPS Threshold + Fixed Cadence

Target: Windows · Geometry Dash 2.2081 · Geode 5.11.2.

## What changed

v0.1.7 replaces the previous always-exclusive input takeover with a threshold-gated state machine:

1. **Below the Manual Trigger CPS threshold:** physical Ship press/release events are forwarded normally to Geometry Dash and other hooks. The Ship therefore remains usable even when Alternate Assist is enabled.
2. **Activation threshold:** the mod measures physical press-edge frequency. Two consecutive press intervals at or above approximately 90% of the configured threshold (default 5 CPS) activate cadence assist. The 10% tolerance compensates for input timing quantization on frame-based sampling. The threshold-crossing press is consumed and replaced by the generated cadence.
3. **Fixed Assist CPS:** once active, the controller generates a fixed number of complete press/release cycles per second (default 16 CPS). Physical clicks only refresh the activity window; their rhythm does not set or phase-lock the generated cadence.
4. **Adaptive duty feedback:** the cycle frequency stays fixed, while the percentage of each cycle spent pressed is adjusted using gravity-relative vertical velocity. This is intended to correct consistent upward/downward drift.
5. **Automatic hand-back:** when the key is released and no new manual edges arrive for the configured activity window, the synthetic press is released and native input pass-through resumes.

## Recommended first test

- Enable Ship Assist: ON
- Manual Trigger CPS: 5
- Assist CPS: 16
- Straightness / Feedback Strength: 100
- Base Pulse Duty: 50%
- Target Vertical Velocity: 0
- Assist Hold After Last Click: 0.45 s
- Up Arrow routing: ON if your Up Arrow is bound to Jump P2

Start clicking at roughly 5 CPS. The first slower clicks remain native. After two consecutive press intervals meet the threshold, Alternate Assist switches to 16 CPS generated cycles. Change Assist CPS to tune the cadence; change Base Pulse Duty only if it repeatedly drifts in one direction.

## Important notes

- Assist CPS means complete press+release cycles per second, not individual button edges. A 16 CPS setting therefore produces up to 32 state transitions per second, subject to the game's physics update rate.
- The practical maximum is capped at 30 cycles/s to avoid requesting a cadence that common 60 Hz physics updates cannot represent reliably.
- Input-buffer mods may not buffer the physical events while cadence mode is active, because those events are intentionally consumed and replaced by generated press/release events. Below the threshold, native inputs are forwarded normally.
- The controller changes Ship input duty, not player position or velocity directly. A perfectly horizontal result is not guaranteed by one fixed CPS value across every gravity, speed, portal and physics configuration; tune Assist CPS and Base Pulse Duty in-game.

## Build

Build with Geode SDK 5.11.2 / GD 2.2081 using CMake or the included GitHub Actions workflow. This archive contains source, not a compiled `.geode` binary. A full SDK build and in-game testing are still required.
