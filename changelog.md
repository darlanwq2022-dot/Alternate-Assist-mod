# Changelog

## v0.1.2 - Manual activity gating and click alignment
- Assistance is armed only by recent real manual input and stops after a configurable idle timeout.
- Added Manual Activity Window setting.
- Added Manual Click Alignment setting; recent manual cadence tightens the velocity tolerance progressively.
- Centralized button ownership as `manualHeld || autoPressed` so auto releases cannot cancel real holds and manual clicks can take over auto holds without duplicate input edges.
- Synthetic corrections run only between physical holds, filling gaps in an ongoing click stream.
- Lowered the default Manual Resume Delay from 0.12s to 0.02s for responsive gap filling.
- Preserved rate limit, gravity-relative velocity control, Ship-only detection, and optional Player 2 support.

## v0.1.1 - Build fix
- Fixed Geode 5.x setting listeners by explicitly specifying each setting's value type.

## v0.1.0 - Initial release
- Initial adaptive Ship assistance controller with straightness profiles and manual input priority.
