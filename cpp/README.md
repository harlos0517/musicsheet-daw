# cpp/ — native app (JUCE 8)

## Build (Windows)

Needs CMake >= 3.25 and Visual Studio 2022 Build Tools (C++ workload, which
includes MSVC and Ninja).

### Quick: `build.ps1`

From any PowerShell, including the VS Code terminal:

```powershell
cpp\build.ps1              # debug
cpp\build.ps1 -Run         # debug, then start the app
cpp\build.ps1 -Test        # debug, then run the unit tests
cpp\build.ps1 release
cpp\build.ps1 -Fresh       # discard the CMake cache first
```

If `cl` and `ninja` aren't on PATH yet, the script finds the newest stable VS
install with vswhere and enters its x64 dev shell. That shell stays active in
the current terminal session.

### VS Code

Open the repository folder and install the recommended extensions when asked.
CMake Tools reads `CMakePresets.json` and sets up the MSVC environment itself,
so configure, build and debug work from its status bar without a developer
shell; IntelliSense takes its flags from the same build.

### Formatting

`cpp/.clang-format` describes the code's style (written for clang-format 21), so
format-on-save keeps it. To check every file without changing anything:

```powershell
clang-format --dry-run --Werror (git ls-files 'cpp/src/*.cpp' 'cpp/src/*.h' 'cpp/tests/*.cpp')
```

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

## Layout

```
cpp/
├── build.ps1, CMakeLists.txt, CMakePresets.json, cmake/Dependencies.cmake
├── resources/default.layout.json   LayoutSetup: profiles "default" and "large-score", one layout
├── src/
│   ├── core/                       library msd_core: no JUCE, testable without a window
│   │   ├── types/                  MusicData.h, LayoutProfile.h, LayoutSetup.h, and for playback
│   │   │                           PlaybackSetup.h, InterpretationProfile.h, InstrumentProfile.h,
│   │   │                           MusicEvent.h, EventOverride.h
│   │   └── json/                   Load.h/.cpp (JSON → types), EnumNames.h
│   ├── layout/                     library msd_layout: MusicData → boxes, no JUCE either
│   │   ├── PageLayout.h            reflow() → Layout.flow, layoutPages()
│   │   ├── SystemLayout.h          one system: spacing, notes, beams, barlines
│   │   ├── Accidentals.h, Transpose.h, StaffPosition.h
│   │   └── SmuflFont.h, SmuflGlyphNames.h, Geometry.h
│   ├── playback/                   library msd_playback: MusicData + PlaybackSetup → MIDI, no JUCE
│   │                               (being ported from prototype-web/src/core/playback)
│   │   ├── Timeline.h              measures and voice events in written time
│   │   ├── Repeats.h               the order measures are played in
│   │   ├── ScoreIndex.h            every event by ID; anchors → positions
│   │   ├── Tempo.h                 the tempo map: played ticks ↔ ms
│   │   ├── BuildEvents.h           buildMusicEvents(): score + setup → MusicEvents, per track
│   │   │                           BuildCurves.h, BuildNotes.h and Swells.h in that order
│   │   ├── Curves.h, Interpretation.h   curve values and shapes; profiles' `extends`
│   │   └── RenderMidi.h            events → MIDI messages in ms, per output (renderTracks)
│   └── app/                        the JUCE window: MainComponent, ScoreView, MusicFont
└── tests/                          Catch2 unit tests (msd_tests)
```

## Types from TS

`src/core/types/*.h` mirror `prototype-web/src/core/types/*.ts`: same names, same
fields in the same order. The comment at the top of `MusicData.h` explains how each
TS shape maps to C++.

Changing a type means changing it in TS and in three places here:

1. the struct in `src/core/types/`
2. its `read()` in `src/core/json/Load.cpp` (one `field(...)` line per member;
   a new struct also needs its `read()` declared at the top of that file)
3. for a new enum, its names in `src/core/json/EnumNames.h` and in the list in
   `tests/EnumNamesTests.cpp`

The loader reads the files `pnpm midi` writes into `prototype-web/midi-out/`: the
scores (`*.music.json`), the playback setups they were played with (`*.setup.json`)
and the events playback made from them (`*.events.json`). To check they all load:

```powershell
cpp\build\debug\tests\msd_tests.exe "[exported]"
```

The same tag also lays out the first system of every staff, reflows every score
onto pages, and checks that playback in C++ gives the timing, events and MIDI
messages the TS wrote (event velocities may differ by 1e-12: V8's `exp` and `pow`
round a few results differently). Debug takes about 6 minutes, most of it the big
imported scores.

## Layout (the engraving kind)

Two steps, so that pages don't move while the music is edited
(`src/layout/PageLayout.h`):

- `reflow()` breaks the music into systems and pages and places every system and
  staff. Its result is `Layout.flow`. It runs when a score is opened and on
  **Reflow**, never on its own. Systems take measures while their natural widths
  fit the page; the user's break, no-break and staff-gap elements are kept.
- `layoutPages()` lays out the pages a flow describes. A measure added since the
  reflow joins the system before it (squeezed, or overfull), a system whose start
  measure is gone joins the one before, and a staff the flow doesn't know goes
  below the others.

`layoutSystem()` (`src/layout/SystemLayout.h`) lays out one system: every staff
spaced together, so notes sounding at the same time line up. The result is a list
of `LayoutBox`es in staff spaces, each with the `nodeId` it came from, a `Part`,
its bounds, and a SMuFL glyph, a line or a beam to draw. It handles clefs, keys
and time signatures, noteheads, ledger lines, accidentals (key, measure and tie
rules), dots, stems, flags, beams (grouped by beat), rests, seconds in chords,
barlines joined per instrument, braces, and transposing instruments at written
pitch. Spacing never squeezes symbols into each other: a system that can't fit
comes out wider than asked.

Glyph sizes, stem anchors and line thicknesses come from the font's SMuFL
metadata (`SmuflFont`), so any SMuFL font with metadata can replace Leland.
Leland v0.80 is fetched at configure time into `build/_deps/leland-src`.

Profiles in `resources/default.layout.json`: `default` (A4, 1.75 mm staves) and
`large-score` (A3, 1 mm staves, 4 sp between staves). The app uses `large-score`
when a system with every staff wouldn't fit the default's page.

Not done yet: ties, slurs, tuplet brackets, dynamics, articulations, text and
instrument names, grace notes, clef changes inside a system, naturals at key
changes, beam positions relative to staff lines, and vertical spacing by content
(staves are a fixed distance apart, so far-out notes can reach the next staff).

## Painting

The app (`src/app`) only draws what layout decided. `MusicFont` turns SMuFL glyph
names into outlines: the codepoint comes from SMuFL's `glyphnames.json`, and the
outline is scaled to staff spaces by comparing `noteheadBlack`'s outline with its
width in the font's metadata. `ScoreView` fills each glyph and line, scaled by the
zoom. Leland, its metadata, `glyphnames.json` and the default layout are compiled
into the app.

The app shows the pages one below the other. Hovering a symbol shows its `nodeId`,
part and page; **Boxes** draws every box's bounds; **Reflow** reflows the score.
The flow lives only in memory for now.

```powershell
# score and first page to show are both optional
& "cpp\build\debug\src\app\MusicsheetDawApp_artefacts\Debug\Musicsheet DAW.exe" prototype-web\midi-out\piano.music.json 1
```

Scores load on the UI thread: the big ones (`smb_3m`, 77 MB) freeze the window for
several seconds, longer in Debug.

## Licensing

The project is licensed under the AGPLv3 or later (`/LICENSE`). JUCE is used
under its AGPLv3 option, so the whole app is AGPLv3.

`/THIRD_PARTY_NOTICES.md` lists everything the app contains and its license.
At configure time `cmake/Notices.cmake` appends the full license texts, read from
the fetched sources, and the build copies the result and `LICENSE.txt` next to the
exe. When a dependency is added or updated, update that file's table, and add its
license file to the list in `cmake/Notices.cmake` (a missing file stops the
configure). Linking another JUCE module can bring its own bundled libraries: see
JUCE's `LICENSE.md` for which.
