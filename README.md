# Alternate Assist Mod v0.1.3 — Adaptive Straightness

A configurable Geometry Dash Ship stabilizer for **Geode 5.11.2 / GD 2.2081 (Windows)**.

## What changed in v0.1.3

The v0.1.2 input model combined manual and synthetic button states with an OR operation. That meant a physical hold could keep thrust active even when the stabilizer wanted to release it. v0.1.3 gives the controller ownership of the final Ship jump-button state while assistance is armed.

- A physical click or hold arms the stabilizer.
- Repeated clicks refresh the configurable Manual Activity Window.
- Manual click cadence is measured; faster clicking progressively tightens the correction deadzone.
- The controller continues to choose press/release even while the physical button is held, so it can counter upward or downward drift.
- When the user stops clicking, releases, and the activity window expires, automatic input is released and the controller disengages.
- Outside Ship mode, or with the mod disabled, normal input is passed through.

**Important:** while assistance is enabled and the player is in Ship mode, physical click edges are used as activation/cadence signals, not forwarded as unmodified long holds. The mod controls the actual jump-button state. Disable Ship Assist to return to normal 1:1 manual input.

## Settings

- **Straightness (0–100):** main control. 0 gives loose correction; 100 uses the tightest velocity deadzone.
- **Maximum CPS (1–120):** maximum complete automatic press/release cycles per second. Actual cadence is also limited by physics updates.
- **Manual Activity Window:** how long assist remains armed after the latest click; a held button keeps it armed.
- **Manual Click Alignment (0–100%):** faster measured click cadence tightens the velocity deadzone by up to 42% at full setting.
- **Target Vertical Velocity:** 0 targets horizontal flight; small offsets permit controlled drift.
- **Correction Resume Delay:** optional delay after manual release before feedback corrections resume.
- **Player 2 Assist:** optional dual-player support.
- **Debug Controller Telemetry:** reports velocity, prediction, deadzone, output state, manual CPS, and estimated acceleration to the Geode log.

## Controller design

The controller runs around `PlayerObject::updateJump(float dt)`, the game's physics-step update. It reads gravity-relative vertical velocity (`m_yVelocity` adjusted for gravity orientation), filters an acceleration estimate, predicts velocity a short horizon ahead, then applies a hysteresis-based press/release controller. It does not directly write the player's position, velocity, rotation, collision state, or physics constants.

A faster manual click cadence increases precision by reducing the deadzone; it does not force additional upward thrust pulses. This is deliberate: unconditional thrust on every click would make the ship less straight when click frequency increases.

## Recommended first test

- Enable Ship Assist: ON
- Straightness: **100**
- Maximum CPS: **120**
- Manual Activity Window: **0.32 s**
- Manual Click Alignment: **100%**
- Target Vertical Velocity: **0**
- Correction Resume Delay: **0**
- Debug Controller Telemetry: ON for diagnosis

Test in an empty, straight Ship section first. If the Ship oscillates rapidly, lower Straightness (e.g. 85–90) or Maximum CPS. If it drifts too much, increase Straightness and make sure click alignment is enabled.

## Build

Set `GEODE_SDK` to your Geode SDK directory and build with CMake, or use the included GitHub Actions workflow. Target: Windows, Geometry Dash 2.2081, Geode 5.11.2.

## Limitations

This is a feedback controller, not a mathematical guarantee of perfectly horizontal travel in every situation. Portals, pads, orbs, collisions, speed changes, gravity changes, ship-size changes, and other mods that alter player input/physics can affect stability. The source archive does not include a compiled `.geode` binary.
