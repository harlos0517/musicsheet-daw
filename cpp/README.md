# cpp/ — native app (JUCE 8)

## Build (Windows)

Needs CMake >= 3.25 and Visual Studio 2022 Build Tools (C++ workload, which
includes MSVC and Ninja).

### Quick: `build.ps1`

From any PowerShell, including the VS Code terminal:

```powershell
cpp\build.ps1              # debug
cpp\build.ps1 -Run         # debug, then start the app
cpp\build.ps1 release
cpp\build.ps1 -Fresh       # discard the CMake cache first
```

If `cl` and `ninja` aren't on PATH yet, the script finds the newest stable VS
install with vswhere and enters its x64 dev shell. That shell stays active in
the current terminal session.

### By hand

MSVC and Ninja are only on PATH inside a developer shell.

**PowerShell** (running `vcvars64.bat` from PowerShell does *not* work: it sets
the variables in a child cmd.exe that exits right away):

```powershell
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
cd cpp
cmake --preset debug
cmake --build --preset debug
```

Leave out `-Arch amd64 -HostArch amd64` and you get the 32-bit compiler.

**cmd.exe**: open *x64 Native Tools Command Prompt for VS 2022*, or run
`vcvars64.bat` in a cmd window, then run the same `cmake` lines.

The dev shell only applies to the current window, so enter it again in each new
terminal. After a configure that failed because the tools were missing, rerun it
with `cmake --preset debug --fresh`.

Output: `build/debug/src/app/MusicsheetDawApp_artefacts/Debug/Musicsheet DAW.exe`.
Fetched dependencies are shared between presets in `build/_deps`.

`release` works the same way. The melatonin inspector is linked only when
`MSD_ENABLE_INSPECTOR` is on (default: on for Debug, off otherwise); in the app,
**Ctrl+I** opens it.

## Licensing

| Component | License | Notes |
|---|---|---|
| Our code | GPLv3 | |
| JUCE 8 | AGPLv3 or commercial | **Decision needed**, see below |
| melatonin_inspector | MIT | Debug builds only |
| Leland (planned) | SIL OFL 1.1 | Ship its license file with the font |

GPLv3 §13 allows linking with AGPLv3 code, but the combined binary then carries
AGPLv3's terms for the JUCE part (including §13's network-use clause). Options:
keep GPLv3 for our code and accept that, relicense to AGPLv3 to keep it simple,
or buy a JUCE commercial license.
