# Alternate Assist Mod v0.1.2

An adjustable Geometry Dash Ship flight-assistance mod for Geode 5.11.2 / GD 2.2081 (Windows).

## v0.1.2: manual-activity gated assistance

The controller no longer runs continuously after a level starts. It is armed only by real player input:

- A physical click starts a short assistance window.
- Each new physical click refreshes that window.
- Synthetic corrections are allowed only between physical holds.
- When the user stops clicking and the window expires, synthetic input is released and assistance goes idle.
- The activity window is configurable.

## Manual click alignment

Manual and automatic input now share one logical button state (`manualHeld OR autoPressed`). This prevents a synthetic release from cancelling a real hold and prevents an automatic pulse from double-pressing an already-held button. A physical click takes ownership of an automatic hold without generating a duplicate edge.

The mod estimates recent manual click cadence. Faster manual clicking progressively tightens the velocity deadzone, up to the configured Manual Click Alignment strength. This makes corrections more precise while keeping the configured CPS as the hard cap for synthetic pulses.

## Settings

- Enable Ship Assist (off by default)
- Extreme Straight / Semi-Straight / Light Assist / Custom profile
- Custom Deadzone
- Maximum CPS (complete automatic press/release cycles per second)
- Target Vertical Velocity
- Manual Resume Delay
- Manual Activity Window
- Manual Click Alignment
- Optional Player 2 support

## Controller behavior

The mod observes Ship mode and gravity-relative vertical velocity. It sends rate-limited press/release transitions through `GJBaseGameLayer::handleButton`. It does not directly modify player position, velocity, rotation, collision state, or physics constants. Normal and inverted gravity are handled by converting Y velocity into a gravity-relative axis.

## Build

Set `GEODE_SDK` to your Geode SDK directory, then configure/build with CMake using Geode's normal workflow. The included GitHub Actions workflow builds a Windows `.geode` artifact. Target: Windows, Geometry Dash 2.2081, Geode 5.11.2.

## Limitations

This is a feedback controller, not a perfect trajectory solver. The result depends on game speed, gravity, ship size, physics tick timing, and level conditions. The first version supports Ship mode only. Player 2 is optional. The source archive does not include a compiled `.geode` binary.
