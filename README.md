# Alternate Assist Mod v0.1.5 — Nova / Input-Chain Compatibility

Target: Windows · Geometry Dash 2.2081 · Geode 5.11.2.

## v0.1.5 compatibility fix

The previous build consumed manual Ship press/release events by returning early from `GJBaseGameLayer::handleButton`. That could prevent later Geode hooks, including input-buffer hooks, from observing the real input. v0.1.5 always forwards physical input edges through the remaining hook chain after recording them for the stabilizer.

The stabilizer now avoids generating synthetic edges while the physical key is held. It adds correction presses in the released gaps between manual taps. This prevents a synthetic release from cancelling a physical hold and keeps the user's raw press/release stream available to Nova and other input mods.

The controller still uses `PlayerObject::updateJump` for physics feedback and calls the original method on every path. Debug Controller Telemetry now includes `physicsCalls` and `manualEvents` counters. If `physicsCalls` rises while playing Ship, Alternate Assist's physics hook is running. If the Nova warning persists despite `physicsCalls` increasing, another mod may still be interfering with Nova's separate physics hook.

## Important limitation

The Nova warning explicitly says its physics hook is not running and suspects a TPS/physics-bypass/speedhack mod. This source fix removes a definite hook-chain problem in Alternate Assist, but it cannot identify or patch an unrelated mod without that mod's name/version or its own log. For isolation, test with Nova + Alternate Assist only (and CBF if desired), temporarily disabling any separate TPS/physics-bypass/speedhack mod. Re-enable other mods one at a time.

## Recommended test

1. Install v0.1.5 and keep **Route Player 2 Jump to Player 1 in Single-Player** enabled if Up Arrow is mapped to Jump P2.
2. Turn on **Debug Controller Telemetry**.
3. Test Ship with Assist OFF: Up Arrow must move the Ship and Nova must see the press/release events.
4. Turn Assist ON. Use short repeated taps. The assist should only add pulses between released gaps; a held key remains your input.
5. Check Geode logs for `AA P1` lines. `physicsCalls` should increase and `manualEvents` should increase when you tap. Share those lines plus Nova's warning if it persists.

## Build

Build with the Geode SDK through CMake or the included GitHub Actions workflow. The source archive does not include a precompiled `.geode` binary.
