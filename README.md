# Splitlet

A tiny Windows background tool that moves the active window into a custom left or right screen region.

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

## Usage

Place `config.ini` next to `Splitlet.exe`, then run the exe. The program has no UI and stays in the background.

Default hotkeys:

```text
WIN+ALT+LEFT   Move active window to the left region
WIN+ALT+RIGHT  Move active window to the right region
WIN+ALT+UP     Restore the window to its last saved position and unlock it
WIN+ALT+F5     Reload config.ini
WIN+ALT+Q      Exit
```

If `config.ini` is missing, the program creates a default one in the exe directory.

Right-click the Splitlet tray icon to open settings, pause/resume window management, or exit the program. The tray menu is in English, and the settings window opens in the center of the screen. In settings, choose a display to edit that display's split ratio, gap, and split enable state.

## Configuration

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
- `gap` is the pixel space between the two regions.
- `respect_taskbar=true` uses the monitor work area, avoiding the taskbar.
- `lock_zones=true` keeps snapped windows inside their assigned region, including when the maximize button is clicked.
- `auto_start=true` starts Splitlet automatically when the current Windows user signs in.
- The tray Settings window can choose a display, edit that display's `enabled`, `left_ratio`, and `gap`, and toggle global `respect_taskbar`, `lock_zones`, and `auto_start`.
- If a display has `enabled=false`, split hotkeys do nothing for windows on that display.
- Pause temporarily disables Splitlet window actions without exiting the tray program.
- Some elevated windows can only be moved when `Splitlet.exe` is also run as administrator.
