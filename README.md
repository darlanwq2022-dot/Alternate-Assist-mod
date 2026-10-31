# Alternate Assist Mod v0.1.8 — Threshold Legacy Controller

Windows / Geometry Dash 2.2081 / Geode 5.11.2.

## What changed

This release returns to the original v0.1.0 velocity-hysteresis controller, but places it behind a manual CPS gate:

- Below the configured Manual Trigger CPS, physical input is forwarded normally.
- After two consecutive press intervals meet the threshold, the current triggering press is consumed and the controller takes over.
- The controller presses while the Ship is drifting in the direction of gravity and releases while it is drifting against gravity.
- It is not phase-locked to the player's clicks and does not force a fixed 16 CPS pulse train.
- Maximum Assist CPS only rate-limits press/release transitions, preserving the original v0.1.0 control style.
- The controller uses `PlayerObject::update`, as the original prototype did, instead of adding a `PlayerObject::updateJump` physics hook. This is intended to reduce hook contention with physics/input-buffer mods; it does not guarantee compatibility with every third-party mod.
- Up Arrow / P2 jump routing remains available in single-player when enabled.

## Suggested starting settings

- Enable Ship Assist: ON
- Manual Trigger CPS: 5
- Maximum Assist CPS: 30
- Straightness: 100
- Target Vertical Velocity: 0
- Assist Hold After Last Click: 0.45 s

If the Nova Input Buffer still reports that its physics hook is not running, disable other TPS / physics-bypass / speedhack features one at a time and retest. That Nova warning is independent of this mod's CPS detector; this release no longer hooks `PlayerObject::updateJump`.

## Build

Compile with the Geode SDK for Windows and Geometry Dash 2.2081 using the included GitHub Actions workflow.
