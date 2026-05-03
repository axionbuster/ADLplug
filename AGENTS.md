# AGENTS.md — ADLplug / OPNplug

## Repository structure

Two CMake-configured plugins from one tree, selected by `-DADLplug_CHIP=OPL3|OPN2`:

| Variant | Plugin name | CMake flag | Chip dir | AU validation |
|---------|-------------|------------|----------|---------------|
| OPL3    | ADLplug     | `OPL3`     | `sources/opl3/` | `auval -v aumu ADLM JPCm` |
| OPN2    | OPNplug     | `OPN2`     | `sources/opn2/` | `auval -v aumu OPNM JPCm` |

Shared core lives in `sources/` (processor, editor, worker, bank manager). Shared UI is in `sources/ui/`.

## Local development (macOS/arm64, OPN2 AU only)

```bash
./postpull                # cmake --build build-opn-au-arm64 + ditto AU to ~/Library/Audio/Plug-Ins/Components/
auval -v aumu OPNM JPCm  # validate the installed AU
```

The preconfigured build dir is `build-opn-au-arm64/`. It builds the OPN2 AU only (`-DADLplug_CHIP=OPN2 -DADLplug_AU=ON`).

## Submodules

All at `thirdparty/`. Notable forks:
- `thirdparty/JUCE` → `axionbuster/JUCE`
- `thirdparty/libOPNMIDI` → `axionbuster/libOPNMIDI` (branch `codex/opn-true-mono-handoff`)
- `thirdparty/libADLMIDI` → upstream Wohlstand/libADLMIDI

## Architecture

Three actors communicating through ring-buffer FIFOs:

- **Processor** — real-time audio thread, must never block. Acquires player lock for non-RT ops (zeroes output during lock).
- **Worker** — non-RT thread, handles chip count/emulator changes and envelope measurements.
- **Editor** — UI thread, initialized on creation and on processor ready-message.

State kept on both processor and editor sides; kept in sync via FIFO messages.

## Chip settings

Default emulator for new instances is determined in:
- `sources/opl3/adl/chip_settings.cc` (OPL3)
- `sources/opn2/adl/chip_settings.cc` (OPN2)

The `from_properties` fallback uses `get_emulator_defaults().default_index` when the `"emulator"` key is absent from saved state (not hardcoded 0).

## Code style

- C++11, `-stdlib=libc++` on macOS
- `.clang-format`: column limit 80, Stroustrup braces, indent 4, no tabs
- JUCE warnings disabled in CMakeLists.txt: `multichar`, `class-memaccess`

## Mono click probe

A standalone tool at `tools/mono_click_probe/` that replays MIDI events through `libOPNMIDI` and measures sample-to-sample discontinuities (clicks).

```bash
# Standalone CMake build (portable)
cmake -S tools/mono_click_probe -B build-mono-click-probe
cmake --build build-mono-click-probe -j
ctest --test-dir build-mono-click-probe --output-on-failure

# Quick macOS build linking against prebuilt libOPNMIDI.a
clang++ -std=c++17 \
  -I thirdparty/libOPNMIDI/include \
  tools/mono_click_probe/mono_click_probe.cpp \
  build-opn-au-arm64/libOPNMIDI.a \
  -o build-opn-au-arm64/mono_click_probe

# Run
build-mono-click-probe/mono_click_probe \
  thirdparty/libOPNMIDI/fm_banks/gs-by-papiezak-and-sneakernets.wopn \
  tools/mono_click_probe/temp_midi_bass_excerpt.tsv \
  <output.wav> <seconds> <mono|poly> [emulator-substring]
```

## CI

AppVeyor (`.appveyor.yml`). Builds source tarball, Windows (mingw32/64), macOS (osxcross), and Debian packages. Patches JUCE for MinGW before building.

## Developer notes

`DEVELOPER-NOTES.md` has detailed architecture documentation on processor/editor/worker communication and state management.
