# Contributing

Thanks for helping improve Splitlet.

## Development Setup

Requirements:

- Windows
- CMake 3.20 or newer
- A C++17 compiler

Build with MinGW:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build --config Release
```

Build with Visual Studio:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

## Pull Requests

- Keep changes focused.
- Match the existing Win32 style unless a broader refactor is clearly needed.
- Update `README.md` when behavior, hotkeys, or configuration changes.
- Avoid adding runtime dependencies unless they solve a concrete user-facing problem.

## Bug Reports

Please include:

- Windows version
- Number of monitors and scaling settings
- Splitlet version
- Steps to reproduce
- Expected behavior
- Actual behavior
