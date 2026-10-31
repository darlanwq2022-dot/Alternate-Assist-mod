# Changelog

## v0.1.5 — Nova/input-chain compatibility
- Manual press/release events are always forwarded to the remaining Geode hook chain after being recorded.
- Prevented physical Ship input from being swallowed by the stabilizer.
- Corrections now run in released gaps between manual holds; synthetic edges are paused while the physical key is held.
- Removed synthetic manual-input restoration because physical input now reaches the game directly.
- Added physics-hook and forwarded-manual-event counters to debug telemetry.
- Preserved single-player Up Arrow to Player 1 routing and dual-mode behavior.

## v0.1.4 — Up Arrow / P2 input compatibility
- Added a setting to route Player 2 primary jump input to Player 1 in non-dual levels.
- Keeps Player 2 input independent in dual mode.

## v0.1.3 — Adaptive straightness
- Added direct Straightness control and physics-step feedback stabilization.
- Manual click cadence tightens velocity tolerance.

## v0.1.2 — Manual activity alignment
- Added manual activity gating and cadence measurement.

## v0.1.1 — Build fix
- Fixed Geode 5 setting listener type declarations.

## v0.1.0 — Initial release
- Initial adaptive Ship assistance controller.
