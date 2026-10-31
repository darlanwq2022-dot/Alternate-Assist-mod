# Changelog

## v0.1.4 — Up Arrow / P2 input compatibility
- Added a setting to route Player 2 primary jump input to Player 1 in non-dual levels.
- Defaults to enabled to support keymaps that bind Up Arrow to Jump P2.
- Keeps Player 2 input independent in dual mode.
- Synthetic controller pulses bypass remapping and preserve their explicit player target.
- Input remapping occurs before Ship-assist capture, so Up Arrow can both control P1 and arm the stabilizer.

## v0.1.3 — Adaptive straightness
- Added direct Straightness control and physics-step feedback stabilization.
- Manual click cadence tightens velocity tolerance.

## v0.1.2 — Manual activity alignment
- Added manual activity gating and cadence measurement.

## v0.1.1 — Build fix
- Fixed Geode 5 setting listener type declarations.

## v0.1.0 — Initial release
- Initial adaptive Ship assistance controller.
