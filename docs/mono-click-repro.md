# Mono click repro and offline probe

## Current findings

- The AU installed by `./postpull` was verified to match the just-built binary, so the observed clicks are coming from the current code, not a stale install.
- The user repro uses:
  - bank: `thirdparty/libOPNMIDI/fm_banks/gs-by-papiezak-and-sneakernets.wopn`
  - patch: `M056 Trumpet` (program 56)
  - range: low notes
  - source material: the first few seconds of `temp-midi-bass.mid`
- A stripped offline probe now replays the first **10s** worth of channel events from that MIDI file (107 exported events through `9.994449442s`) and scores sample-to-sample discontinuity spikes around note edges.
- For that 10-second slice, the largest spikes are still identical in **mono** and **poly** playback on MAME YM2612, so this repro slice is **not currently mono-exclusive**.
- The same slice is also **emulator-sensitive**: all tested backends still click, but the worst absolute peak size changes substantially by emulator.

## Measured spike summary

The probe computes a median sample-to-sample delta baseline, then measures the largest local peak near each event boundary.

Observed worst spikes on the current build for the checked-in 10-second slice:

| Time (s) | Peak delta | Ratio vs median delta | Nearest event |
| --- | ---: | ---: | --- |
| 5.030570 | 0.00680563 | 55.75x | `pitchwheel ch=1 pitch=-253` |
| 5.041680 | 0.00534074 | 43.75x | `pitchwheel ch=1 pitch=-192` |
| 5.062490 | 0.00445570 | 36.50x | `pitchwheel ch=1 pitch=-117` |
| 5.020840 | 0.00396741 | 32.50x | `pitchwheel ch=1 pitch=-347` |
| 5.113900 | 0.00393689 | 32.25x | `pitchwheel ch=1 pitch=-31` |
| 5.125010 | 0.00375378 | 30.75x | `pitchwheel ch=1 pitch=-22` |

Observed backend difference on the same mono render:

| Emulator | Worst peak delta | Notes |
| --- | ---: | --- |
| `MAME YM2612` | 0.00680563 | worst measured absolute peak on this 10-second excerpt |
| `Nuked OPN2` | 0.00653096 | still very clicky here, with a lower absolute peak but a noisier baseline |
| `Neko Project II Kai OPNA` | 0.00460829 | lower than MAME on this excerpt, still clearly discontinuous |
| `GENS 2.10 OPN2` | 0.00430311 | lower than MAME here, still clearly discontinuous |

## What is checked in

To avoid checking in the full melody, only the extracted repro event slice is stored:

- harness source: `tools/mono_click_probe/mono_click_probe.cpp`
- harness CMake entrypoint: `tools/mono_click_probe/CMakeLists.txt`
- extracted event slice: `tools/mono_click_probe/temp_midi_bass_excerpt.tsv`

The full `temp-midi-bass.mid` file is intentionally **not** checked in.

## Probe behavior

The probe:

1. Loads the Papiezak/Sneakernets DMXOPN2 bank.
2. Selects program 56 on channel 1.
3. Replays the extracted event slice for 10 seconds.
4. Can run in `mono` or `poly` mode.
5. Can optionally select an emulator by substring (default: `mame`).
6. Writes a WAV file, a tab-separated spike report, and a PPM visualization beside the WAV path.
7. Prints the largest discontinuity spikes plus the generated artifact paths.

The `mono` mode in this probe mirrors the current plugin-side mono scheduling model closely enough to compare mono-vs-poly behavior on the same event sequence.

## Example usage

### Existing local shortcut

If you already built the full macOS plugin tree, you can still build the probe directly against the existing static archive:

```bash
clang++ -std=c++17 \
  -Ithirdparty/libOPNMIDI/include \
  tools/mono_click_probe/mono_click_probe.cpp \
  build-opn-au-arm64/libOPNMIDI.a \
  -o build-opn-au-arm64/mono_click_probe
```

### Portable cloud/Linux build

Build the probe as a standalone CMake project:

```bash
cmake -S tools/mono_click_probe -B build-mono-click-probe
cmake --build build-mono-click-probe -j
```

This path only builds `thirdparty/libOPNMIDI` plus the probe itself, so it works on Linux/cloud runners and does not require macOS or Audio Units.

Run the smoke test:

```bash
ctest --test-dir build-mono-click-probe --output-on-failure
```

The smoke test leaves these artifact types next to the requested WAV output path:

- `*.wav` — rendered audio
- `*.spikes.tsv` — machine-readable spike list
- `*.ppm` — waveform/discontinuity visualization with event and top-spike markers

Run in mono mode:

```bash
build-mono-click-probe/mono_click_probe \
  thirdparty/libOPNMIDI/fm_banks/gs-by-papiezak-and-sneakernets.wopn \
  tools/mono_click_probe/temp_midi_bass_excerpt.tsv \
  build-mono-click-probe/mono_click_probe_mono.wav \
  10 \
  mono
```

This produces:

- `build-mono-click-probe/mono_click_probe_mono.wav`
- `build-mono-click-probe/mono_click_probe_mono.spikes.tsv`
- `build-mono-click-probe/mono_click_probe_mono.ppm`

Run in poly mode:

```bash
build-mono-click-probe/mono_click_probe \
  thirdparty/libOPNMIDI/fm_banks/gs-by-papiezak-and-sneakernets.wopn \
  tools/mono_click_probe/temp_midi_bass_excerpt.tsv \
  build-mono-click-probe/mono_click_probe_poly.wav \
  10 \
  poly
```

Run against Nuked instead of MAME:

```bash
build-mono-click-probe/mono_click_probe \
  thirdparty/libOPNMIDI/fm_banks/gs-by-papiezak-and-sneakernets.wopn \
  tools/mono_click_probe/temp_midi_bass_excerpt.tsv \
  build-mono-click-probe/mono_click_probe_nuked.wav \
  10 \
  mono \
  nuked
```

## Interpretation

If mono and poly produce the same spike profile on this excerpt, the immediate failure is likely a more general note-edge problem for this patch/range, not just the mono handoff path. If emulator choice materially changes peak size, backend behavior is part of the bug surface and not just the note scheduler.

Future work should keep this probe around as a regression harness and add a second event fixture with explicit overlapping mono-note transitions so mono-only failures can be isolated from generic patch clicks.
