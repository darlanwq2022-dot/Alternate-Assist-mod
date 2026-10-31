# Alternate Assist Mod v0.1.6 — True Straight-Fly Control

Target: Windows · Geometry Dash 2.2081 · Geode 5.11.2.

## What changed in v0.1.6

The previous versions forwarded physical click press/release edges to the game and paused the controller while the physical key was held. That meant the Ship still followed the player's click pattern, and the stabilizer could only add pulses between clicks. This is why irregular clicking could keep producing an oscillating flight path.

v0.1.6 adds **Exclusive Ship Control (Straight Fly)**, enabled by default. While the player is in Ship mode and assist is armed:

- Physical press/release events are consumed by Alternate Assist and used only to arm/refresh the assist activity window.
- The raw human click rhythm is not used as Ship thrust.
- The feedback controller continues running during physical holds and chooses the final press/release state from gravity-relative vertical velocity and an acceleration estimate.
- The controller can press OR release even while the user is physically holding/clicking, so the Ship can remain stable independently of the user's click pattern.
- When the user stops clicking and the activity window expires, controller output is released. If assist is disabled or the player leaves Ship during a physical hold, the held input is restored.

The Straightness slider now maps much more aggressively: 100% corresponds to an approximately 0.008 velocity deadzone before cadence tightening. Hysteresis remains to reduce rapid chatter. Maximum CPS controls the controller's own pulse rate and is not tied to the user's CPS.

## Important compatibility note

Exclusive control intentionally consumes the raw physical Ship press/release event inside Alternate Assist, because passing that raw event to the game would make the Ship follow the user's oscillating click pattern again. This means other input-buffer mods that require raw `handleButton` events may not be able to buffer those same physical events while Exclusive Ship Control is active. Test Alternate Assist by itself first. If Nova/C​​BF is required, use the legacy mode (Exclusive Ship Control OFF) to pass raw inputs through, but that mode cannot guarantee straight fly against arbitrary manual click timing.

The Up Arrow routing option remains: if the key map sends Up Arrow as Player 2 jump in a single-player level, Alternate Assist routes it to Player 1. Dual levels preserve the normal Player 2 route.

## Suggested initial settings

- Enable Ship Assist: ON
- Exclusive Ship Control: ON
- Straightness: 100
- Maximum CPS: 120
- Manual Activity Window: 0.45 s
- Target Vertical Velocity: 0
- Manual Click Alignment: 0 initially (fully independent of click cadence)
- Correction Resume Delay: 0

Click or hold the jump key to arm and keep the stabilizer active. The controller, not the key pattern, determines thrust while armed. Stop clicking to let the activity window expire and hand control back.

## Build

Build with Geode SDK 5.11.2 / GD 2.2081 using CMake or the included GitHub Actions workflow. This archive contains source, not a compiled `.geode` binary. A full SDK build and in-game testing are still required.
