# Changelog

## v0.1.6 — True straight-fly control
- Added Exclusive Ship Control mode, ON by default.
- Physical press/release edges arm/refresh the controller but no longer dictate Ship thrust while exclusive mode is active.
- Controller now keeps running during physical holds and independently chooses press/release based on vertical velocity feedback.
- Strengthened Straightness-to-velocity-deadzone mapping (100% ≈ 0.008 before optional cadence tightening).
- Maximum CPS now clearly controls the controller output, not the user's input rate.
- Increased default manual activity window to 0.45 seconds and set default Straightness to 100.
- Added a compatibility note: exclusive control may bypass raw input-buffer mods that require physical handleButton events; legacy pass-through mode remains available.

## v0.1.5 — Nova/input-chain compatibility
- Forwarded manual input through the Geode hook chain; corrections ran only in released gaps.

## v0.1.4 — Up Arrow / P2 input compatibility
- Added single-player Player 2 jump-to-Player 1 routing.

## v0.1.3 — Adaptive straightness
- Added direct Straightness control and physics-step feedback.

## v0.1.2 — Manual activity alignment
- Added manual activity gating and cadence measurement.

## v0.1.1 — Build fix
- Fixed Geode 5 setting listener type declarations.

## v0.1.0 — Initial release
- Initial adaptive Ship assistance controller.
