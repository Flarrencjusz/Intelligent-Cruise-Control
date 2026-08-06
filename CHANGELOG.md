# Changelog

All notable changes to Intelligent Cruise Control are documented here.

## 1.1.0 - 2026-08-06

- Wait for cruise-setpoint telemetry to acknowledge every semantic button click.
- Require a released-input frame between clicks to prevent duplicate events.
- Learn the user's configured cruise increment from the acknowledged change.
- Stop at the closest non-speeding setpoint when a limit is unreachable instead
  of alternating indefinitely, such as 84/86 km/h for an 85 km/h limit with a
  2 km/h increment.
- Add regression tests for 80 to 50 km/h and unreachable odd limits.
- Add automatic PowerShell and manual DLL installation instructions.
- Keep release-facing documentation encoding-safe and ASCII-only.
- Prepare the project for GitHub releases under GPLv3.

## 1.0.0 - 2026-07-28

- Initial ETS2 1.60 Windows x64 plugin.
- Automatic cruise adjustment on current speed-limit changes.
- Page Up mini-HUD hotkey and persistent on/off switch.
