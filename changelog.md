# Changelog

## v0.1.8 — Threshold Legacy Controller
- Reverted the flight controller to the original v0.1.0 velocity-hysteresis press/release logic.
- Added a manual CPS threshold gate: native inputs pass through until two consecutive press intervals meet the configured threshold.
- Decoupled active controller output from manual click timing; manual clicks only arm/refresh the controller.
- Restored the original PlayerObject::update callback instead of hooking PlayerObject::updateJump.
- Kept adjustable Straightness, target vertical velocity, Maximum Assist CPS, activity timeout, Player 2 support, and single-player Up Arrow routing.
- Added telemetry for measured manual CPS, deadzone, vertical velocity, active state, and update counts.
