# Alternate Assist Mod v0.1.1

An adjustable Geometry Dash Ship flight-assistance mod for Geode 5.11.2 / GD 2.2081 (Windows).

## What it does

The mod observes the active player's Ship mode and gravity-relative vertical velocity. It applies rate-limited press/release transitions through `GJBaseGameLayer::handleButton` to keep vertical velocity near a target value. It does **not** directly change player position, velocity, rotation, collision state, or physics constants.

The controller uses a hysteresis band:

- When the Ship drifts too far in the direction of gravity, it presses.
- When the Ship is moving too far against gravity, it releases.
- Inside the tolerance band, it keeps the previous input state to avoid rapid oscillation.

Normal and inverted gravity are handled by converting Y velocity into a gravity-relative axis.

## Settings

Open the mod's Geode settings page:

- Enable Ship Assist (off by default)
- Extreme Straight / Semi-Straight / Light Assist / Custom profile
- Custom Deadzone
- Maximum CPS (complete press/release cycles per second)
- Target Vertical Velocity
- Manual input always takes priority while held
- Manual Resume Delay
- Optional Player 2 support

The settings UI is generated from `mod.json` by Geode. Auto input uses the same game-layer input route as normal clicks; an internal guard prevents it from being mistaken for manual input.

## Build

Set `GEODE_SDK` to your Geode SDK directory, then configure/build with CMake using Geode's normal workflow. The included GitHub Actions workflow also builds a Windows `.geode` artifact using the Geode SDK release channel. Push this project to a GitHub repository and run the **Build Alternate Assist Mod** workflow; download the artifact from the completed Actions run.

Target: Windows, Geometry Dash 2.2081, Geode 5.11.2.

## Important behavior / limitations

- This first version supports Ship mode only. It intentionally does not assist Wave, UFO, Swing, or other modes.
- Player 1 is supported by default. Player 2 is opt-in.
- Manual input always takes priority while held, then the controller resumes after the configured delay.
- This is a feedback controller, not a perfect trajectory solver. The result depends on game speed, gravity, ship size, physics tick timing, and the level's conditions. Tune the deadzone and CPS in-game.
- Rotated gameplay / nonstandard gravity transforms need separate in-game verification.
- No compiled `.geode` binary is included; the source package must be built with the user's Geode SDK.


## v0.1.1 build fix

Geode 5.x requires an explicit template type for `listenForSettingChanges<T>`. All seven setting listeners now specify the exact value type declared by `mod.json`.
