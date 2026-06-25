# Splitlet

[![Build](https://github.com/xavierzhu/Splitlet/actions/workflows/build.yml/badge.svg)](https://github.com/xavierzhu/Splitlet/actions/workflows/build.yml)

Splitlet is a tiny Windows tray app that moves the active window into custom left and right screen regions.

It is built for ultrawide monitors, multi-monitor desks, and anyone who wants a lighter alternative to a full window manager.

[Download the latest release](https://github.com/xavierzhu/Splitlet/releases/latest)

## Tiny by Design

Splitlet is intentionally small:

| Metric | Current value |
| --- | ---: |
| Release zip | 413 KiB |
| Self-contained `Splitlet.exe` | 1.10 MiB |
| Files in the release zip | 3 |
| Private memory after launch | ~1.6 MiB |
| Working set after launch | ~7.6 MiB |

Measured from the `v0.1.0` Windows x64 release build on Windows. Runtime memory can vary slightly by system.

## Features

- Move the active window to a custom left or right region with global hotkeys.
- Configure split ratio, gap, taskbar respect, zone locking, and auto start.
- Set split behavior per monitor from the tray settings window.
- Restore a snapped window to its previous placement.
- Pause or exit from the tray menu.
- Native Win32 app with local configuration only.

## Download

1. Open the [latest release](https://github.com/xavierzhu/Splitlet/releases/latest).
2. Download `Splitlet-v*-windows-x64.zip`.
3. Extract the zip.
4. Run `Splitlet.exe`.

If `config.ini` is missing, Splitlet creates a default one next to the executable.

## Default Hotkeys

```text
WIN+ALT+LEFT   Move active window to the left region
WIN+ALT+RIGHT  Move active window to the right region
WIN+ALT+UP     Restore the window to its last saved position and unlock it
WIN+ALT+F5     Reload config.ini
WIN+ALT+Q      Exit
```

Some elevated windows can only be moved when `Splitlet.exe` is also run as administrator.

## Tray Menu

Right-click the Splitlet tray icon to:

- Open settings.
- Pause or resume window management.
- Exit Splitlet.

The settings window lets you choose a display and edit that display's split ratio, gap, and enabled state. It also includes global toggles for taskbar respect, zone locking, and auto start, plus a button to open `config.ini` in the default text editor.

## Configuration

Splitlet stores settings in `config.ini` next to `Splitlet.exe`.

```ini
left_ratio=0.60
gap=8
respect_taskbar=true
lock_zones=true
auto_start=false

hotkey_left=WIN+ALT+LEFT
hotkey_right=WIN+ALT+RIGHT
hotkey_restore=WIN+ALT+UP
hotkey_reload=WIN+ALT+F5
hotkey_exit=WIN+ALT+Q

[monitor:\\.\DISPLAY1]
enabled=true
left_ratio=0.60
gap=8
```

Notes:

- `left_ratio` is clamped to `0.10` through `0.90`.
- `gap` is clamped to `0` through `200` pixels.
- `respect_taskbar=true` uses the monitor work area and avoids the taskbar.
- `lock_zones=true` keeps snapped windows inside their assigned region, including when the maximize button is clicked.
- `auto_start=true` starts Splitlet automatically when the current Windows user signs in.
- If a display has `enabled=false`, split hotkeys do nothing for windows on that display.

## Privacy

Splitlet is designed to stay local:

- No telemetry.
- No account.
- No cloud sync.
- No window title or app content collection.
- Configuration is stored locally in `config.ini`.

## Build

Default build creates a self-contained executable with the MinGW runtime statically linked:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build --config Release
```

The executable is generated at:

```text
build\Splitlet.exe
```

For a smaller executable, build without static MinGW runtime linking:

```powershell
cmake -S . -B build-small -G "MinGW Makefiles" -DSPLITLET_STATIC_RUNTIME=OFF
cmake --build build-small --config Release
```

This makes `Splitlet.exe` much smaller, but it requires the MinGW runtime DLLs such as `libgcc_s_seh-1.dll` and `libstdc++-6.dll` to be available on the target machine.

## Roadmap

- Layout presets.
- Hotkey editing in the settings window.
- Import and export settings.
- Installer package.
- Automatic update checks.

## License

Splitlet is licensed under the [MIT License](LICENSE).

## Contributing

Issues and pull requests are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) before opening larger changes.
