# Changelog

## v0.1.3 — Adaptive straightness and controller ownership
- Replaced the manual/automatic OR input model. While armed, the controller now owns the final Ship button state and can release thrust during a physical hold.
- Manual clicks arm/refresh assistance and supply cadence data; they no longer force an unconditional upward pulse.
- Added a direct Straightness (0–100) mapping to the velocity deadzone.
- Faster manual cadence tightens the deadzone by up to 42% according to Manual Click Alignment.
- Controller continues to use physics-step velocity feedback, filtered acceleration prediction, and hysteresis.
- Maximum CPS defaults to 120; removed the misleading Manual Click Strength setting.
- Improved release/restore handling when disabling assist, leaving Ship mode, dying, resetting, or quitting.
- Added richer telemetry for deadzone, predicted velocity, cadence, straightness, and acceleration.

## v0.1.2 — Manual activity gating and click alignment
- Initial attempt at manual activity gating and combined manual/automatic input ownership.

## v0.1.1 — Build fix
- Fixed Geode 5.x setting listeners by explicitly specifying each setting's value type.

## v0.1.0 — Initial release
- Initial adaptive Ship assistance controller with straightness profiles and manual input priority.
