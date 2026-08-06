# Intelligent Cruise Control for ETS2

Intelligent Cruise Control is a Windows x64 plugin for Euro Truck Simulator 2
1.60. When the Route Advisor reports a new speed limit, the plugin adjusts an
already-active cruise-control setpoint to that limit using ETS2's supported
cruise increase/decrease semantic inputs.

The plugin is built on the official SCS Telemetry & Input SDK. It does not scan
memory, hook undocumented game functions, alter maps, or modify save files.

## Features

- Reacts when the current Route Advisor speed limit changes.
- Waits for telemetry confirmation after every cruise-control click, preventing
  duplicate clicks caused by delayed game updates.
- Learns the configured cruise increment from the observed setpoint change.
- Stops at the closest non-speeding setpoint when a target cannot be represented
  by the selected increment; it will not alternate endlessly around the limit.
- Leaves cruise control off when the player has not enabled it.
- Ignores unavailable and unlimited-road speed-limit values.
- Does not override later manual cruise changes until the next limit change.
- Provides a compact on/off mini HUD, shown or hidden with **Page Up**.

## Recommended ETS2 setting

Set **Cruise control grid/increment to 1 km/h or 1 mph** in ETS2's gameplay
settings. This is strongly recommended because it allows the plugin to reach
both odd and even limits exactly.

With a 2 km/h increment, for example, a setpoint on the even-numbered grid
cannot reach an 85 km/h limit. Version 1.1.0 safely settles at 84 km/h instead
of repeatedly switching between 84 and 86, but a 1 km/h increment reaches 85
exactly.

## How adjustment works

The official SDK exposes the current cruise setpoint but no direct "set cruise
to this speed" function. The plugin therefore emits `cruiectrlinc` or
`cruiectrldec` semantic button clicks.

Only one click may be outstanding at a time. The next click is not emitted
until ETS2 telemetry confirms that the previous click changed the setpoint, and
there is always a released-input frame between clicks. Consequently, the number
of clicks automatically follows the player's configured increment. Going from
80 to 50 km/h requires 30 clicks at 1 km/h, 15 clicks at 2 km/h, or 6 clicks at
5 km/h.

"Instant" means as quickly as ETS2 accepts and acknowledges its normal cruise
button events.

## Installation

### Automatic installation with PowerShell

1. Download and extract the ZIP from the project's GitHub Releases page.
2. Close ETS2.
3. Open PowerShell in the extracted folder and run:

   ```powershell
   .\scripts\install.ps1
   ```

4. For a non-default Steam library, provide the installation path:

   ```powershell
   .\scripts\install.ps1 -GamePath "D:\SteamLibrary\steamapps\common\Euro Truck Simulator 2"
   ```

5. Start ETS2, load a profile, and activate cruise control normally.
6. Press **Page Up** to open the mini HUD and use its switch.

### Manual installation

1. Download and extract the release ZIP.
2. Close ETS2.
3. Locate `intelligent_cruise_control.dll` in the extracted folder.
4. Open the ETS2 installation directory. In Steam, this can be found through
   **Euro Truck Simulator 2 > Properties > Installed Files > Browse**.
5. Open `bin\win_x64` inside the installation directory.
6. Create a folder named `plugins` there if it does not already exist.
7. Copy `intelligent_cruise_control.dll` into the `plugins` folder.
8. Start ETS2, load a profile, and press **Page Up** to open the mini HUD.

For either installation method, the final DLL path must be:

```text
<ETS2>\bin\win_x64\plugins\intelligent_cruise_control.dll
```

Preferences are stored outside the game directory at:

```text
%LOCALAPPDATA%\IntelligentCruiseControl\settings.ini
```

## Configuration

The settings file is created on first launch. Available values are documented
in [`config/settings.ini.example`](config/settings.ini.example).

- `enabled`: persistent automatic-adjustment state (`1` or `0`).
- `hud_hotkey_vk`: Windows virtual-key code; `33` is Page Up.
- `hud_x` and `hud_y`: initial desktop position of the mini HUD.

Page Up is also a common ETS2 binding for cruise increase. The plugin registers
it as a global HUD hotkey while loaded. If a controller setup still receives
both actions, remove the Page Up cruise-increase binding or configure another
virtual-key code.

## Uninstallation

Close ETS2 and run:

```powershell
.\scripts\uninstall.ps1
```

The uninstaller removes only this plugin DLL. Saved preferences are retained.

## Compatibility and limitations

- Target: Euro Truck Simulator 2 1.60.x on Windows x64.
- The target follows the Route Advisor's selected truck/general speed-limit
  mode.
- No map definitions are replaced, so ordinary map and economy mods should not
  conflict with the adjustment logic.
- The mini HUD is a separate topmost Win32 window. Borderless or windowed mode
  is recommended because exclusive fullscreen may cover it.
- The mini HUD is not integrated into ETS2's built-in assistance-icon row.
- Not intended for TruckersMP or environments that prohibit native plugins.
- If the global hotkey is already owned by another application, ETS2's log will
  contain an ICC warning and the HUD hotkey will be unavailable.

## Building from source

Requirements:

- Windows 10 or 11 x64.
- PowerShell 5.1 or newer.
- Visual Studio 2022 with **Desktop development with C++**.

Download the checksum-pinned official SCS SDK and build a release package:

```powershell
.\scripts\bootstrap-sdk.ps1
.\build.ps1 -ReleasePackage -Version 1.1.0
```

The build treats warnings as errors, runs the controller regression tests, and
creates the ZIP under `build\`. GitHub Actions performs the same steps for
pushes and pull requests.

Downloaded SDK files, extracted base-game references, and build products are
excluded from source control and release archives.

## License

Copyright (c) 2026 Intelligent Cruise Control contributors.

This project is licensed under the GNU General Public License version 3 only
(`GPL-3.0-only`). See [`LICENSE`](LICENSE). The SCS SDK is downloaded separately
and remains subject to SCS Software's own SDK license.
