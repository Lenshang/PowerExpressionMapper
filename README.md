# PowerExpressionMapper

A CLAP / VST3 MIDI effect plugin for **configuring expression / instrument names per MIDI note** and pushing those names to the host. Built with [JUCE 8](https://github.com/juce-framework/JUCE) and [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) — sibling project of [PowerDrumMapper](../BitwigDrumMapper).

> Formerly named **Expression Mapper**. The internal plugin IDs (CLAP ID, VST3 class IDs) are unchanged, so existing host sessions keep resolving the same plugin — only the display name and file names changed. Remember to delete the old `Expression Mapper.vst3` / `.clap` from your plugin folders to avoid duplicates.

## Overview

**PowerExpressionMapper** does *naming only* — there is no note remapping:

- Each entry configures a **Name** and its **Note**. The names are reported to the host via the CLAP note-name extension, so Bitwig shows them on the piano roll / note lanes.
- **Notes that are in the list trigger directly** — they pass through untouched at their original sample position.
- **All notes that are NOT in the list are delayed** by a configurable number of samples (default 16). Delays that reach past the end of the current block are carried over sample-accurately to the next block.
- Non-note events (CC, pitch bend, …) are never delayed.

## Features

- **Name → note table UI** — Add / Remove / Clear entries, editable names, note cells with type-to-filter autocomplete: typing (`C1`, `38`, partial text) pops up a live-filtered suggestion list; unparseable input never sticks
- **Delay control** — a slider for the unlisted-note delay (0 – 8192 samples, persisted in the plugin state)
- **No remapping** — note numbers, channels and velocities always pass through unchanged
- **Sample-accurate block carry-over** — a delayed note is never quantised to the block edge
- **Per-format note-name reporting**:
  - **CLAP** — `clap.note-name` extension (Bitwig reads this)
  - **VST3** — `IUnitInfo` pitch names via JUCE's `AudioProcessor::getNameForMidiNoteNumber()` (queried by Cubase, FL Studio, Cakewalk; support in other hosts varies)
  - **VST2** — `effGetMidiKeyName` (same JUCE hook), should a VST2 build ever be added
- **Import / Export** — plain CSV (`.exprmap`): one `Name,Note` pair per line (`Note` accepts numbers like `38` or names like `D1`; Drum Mapper 5-field `.bwdrm` lines are also accepted on import)
- **Cross-platform** — Windows and macOS

## Plugin Formats

| Format | Target | Output Location |
|--------|--------|-----------------|
| CLAP | `PowerExpressionMapper_CLAP` | `build/PowerExpressionMapper_artefacts/Release/CLAP/` |
| VST3 | `PowerExpressionMapper_VST3` | `build/PowerExpressionMapper_artefacts/Release/VST3/` |
| Standalone | `PowerExpressionMapper_Standalone` | `build/PowerExpressionMapper_artefacts/Release/Standalone/` |

## Building from Source

### Prerequisites

- **CMake** >= 3.21
- **C++20** compiler (Windows: Visual Studio 2022)
- A **JUCE 8** checkout and a **clap-juce-extensions** checkout

By default the build reuses the sibling checkout at `../BitwigDrumMapper/libs/` (this is where this project was developed), so nothing needs to be downloaded:

```bash
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

To use your own copies:

```bash
cmake -B build -S . -DJUCE_DIR=<path to JUCE> -DCLAP_JUCE_EXTENSIONS_DIR=<path to clap-juce-extensions>
```

### Tests

The `PowerExpressionMapperTests` console target compiles the plugin sources headlessly and exercises the routing, state, CSV and note-name logic:

```bash
cmake --build build --config Release --target PowerExpressionMapperTests
./build/PowerExpressionMapperTests_artefacts/Release/PowerExpressionMapperTests.exe
```

Covered: listed-note pass-through, unlisted-note delay (within block, across blocks, multi-block), non-note pass-through, zero delay, ordering (listed fires before delayed), state round-trip, invalid-state rejection, CSV import/export, file round-trip, CLAP note-name reporting, model edge cases.

## Installation

### Windows
Copy the `.clap` or `.vst3` file to:
```
C:\Program Files\Common Files\CLAP\
C:\Program Files\Common Files\VST3\
```

## File Format

`.exprmap` CSV, one entry per line:

```
Kick,36
Snare,D1
```

- Field 1: entry name (what the host displays)
- Field 2: MIDI note number (0–127) or a note name using the DAW convention (`C3` = 60)

## Routing Rules (recap)

| Incoming event | Behaviour |
|----------------|-----------|
| Note on/off whose note is in the list | passes through at the original sample position |
| Note on/off whose note is NOT in the list | delayed by N samples (carried into later blocks sample-accurately) |
| Any other MIDI event | passes through untouched |

## Note Naming Convention

DAW/Bitwig convention where middle C = C3 (MIDI note 60): `C-2` = 0, `C0` = 24, `C3` = 60, `G9` = 127.

## Project Structure

```
PowerExpressionMapper/
├── CMakeLists.txt
├── Source/
│   ├── PluginProcessor.h/.cpp        # AudioProcessor: routing + delay queue + CLAP note-name
│   ├── PluginEditor.h/.cpp           # Editor (resizable, delay slider)
│   ├── ExpressionMapping.h/.cpp      # Data model (name/note entries + delay) + CSV/ValueTree I/O
│   ├── NoteNameUtils.h/.cpp          # Note name <-> MIDI number conversion
│   └── MappingTableComponent.h/.cpp  # Table UI
└── Tests/
    └── ProcessorTests.cpp            # Headless test runner (93 checks)
```

## CI/CD

GitHub Actions builds Windows and macOS (Universal Binary) artifacts and runs the unit tests on every push and pull request. Because this repo does not vendor JUCE, the workflow fetches JUCE and clap-juce-extensions itself at the same pinned revisions the local build uses. Pushing a `v*` tag (e.g. `v0.1.0`) additionally publishes a GitHub Release with `PowerExpressionMapper-Windows.zip` and `PowerExpressionMapper-macOS.zip` (each containing CLAP / VST3 / Standalone). See [`.github/workflows/build.yml`](.github/workflows/build.yml).

```bash
git tag v0.1.0
git push origin v0.1.0   # triggers the release build
```

## Tech Stack

- **JUCE 8.0.14** — Audio application framework
- **CLAP SDK 1.2.7** — via clap-juce-extensions
- **C++20** / **CMake 3.21+**

## License

This project is open source. JUCE and CLAP SDK are subject to their respective licenses.
