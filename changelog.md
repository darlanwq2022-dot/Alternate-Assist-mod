# Changelog

## v0.1.7 — CPS threshold and fixed cadence
- Restored native Ship input pass-through below the manual CPS threshold.
- Added configurable Manual Trigger CPS (default 5 CPS).
- Requires two consecutive press intervals near/above threshold to activate, with a 10% sampling tolerance for frame quantization.
- Added configurable Assist CPS (default 16 complete press/release cycles per second).
- Manual click rhythm now only arms/refreshes cadence mode; it does not synchronize the generated pulses.
- Added adaptive pulse-duty feedback driven by gravity-relative vertical velocity.
- Added Base Pulse Duty setting for tuning steady upward/downward drift.
- Automatically releases generated input and returns to native input after the activity window expires.
- Removed the previous always-exclusive mode that could make Ship input appear nonfunctional.

## Previous
See the Git history / prior packaged changelogs for v0.1.0–v0.1.6.
