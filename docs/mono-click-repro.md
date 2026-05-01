# Mono click repro and offline probe

## Current findings

- The AU installed by `./postpull` was verified to match the just-built binary, so the observed clicks are coming from the current code, not a stale install.
- The user repro uses:
  - bank: `thirdparty/libOPNMIDI/fm_banks/gs-by-papiezak-and-sneakernets.wopn`
  - patch: `M056 Trumpet` (program 56)
  - range: low notes
  - source material: the first few seconds of `temp-midi-bass.mid`
- A stripped offline probe was used to replay only the first **3.5s** worth of channel events from that MIDI file and score sample-to-sample discontinuity spikes around note edges.
- For that first 3.5s slice, the largest spikes were identical in **mono** and **poly** playback, so this repro slice is **not currently mono-exclusive**.

## Measured spike summary

The probe computes a median sample-to-sample delta baseline, then measures the largest local peak near each event boundary.

Observed worst spikes on the current build:

| Time (s) | Peak delta | Ratio vs median delta | Nearest event |
| --- | ---: | ---: | --- |
| 0.993061 | 0.00241096 | 19.75x | `note_off ch=1 note=50 vel=64` |
| 1.300000 | 0.00219733 | 18.00x | `note_off ch=1 note=50 vel=64` |
| 0.306939 | 0.00189215 | 15.50x | `note_off ch=1 note=50 vel=64` |
| 1.000000 | 0.00183111 | 15.00x | `note_on ch=1 note=50 vel=89` |
| 1.640270 | 0.00137333 | 11.25x | `note_off ch=1 note=50 vel=64` |
| 0.856939 | 0.000946074 | 7.75x | `note_on ch=1 note=50 vel=89` |

## What is checked in

To avoid checking in the full melody, only the extracted repro event slice is stored:

- harness source: `tools/mono_click_probe/mono_click_probe.cpp`
- extracted event slice: `tools/mono_click_probe/temp_midi_bass_excerpt.tsv`

The full `temp-midi-bass.mid` file is intentionally **not** checked in.

## Probe behavior

The probe:

1. Loads the Papiezak/Sneakernets DMXOPN2 bank.
2. Selects program 56 on channel 1.
3. Replays the extracted event slice for 3.5 seconds.
4. Can run in `mono` or `poly` mode.
5. Writes a WAV file and prints the largest discontinuity spikes.

The `mono` mode in this probe mirrors the current plugin-side mono scheduling model closely enough to compare mono-vs-poly behavior on the same event sequence.

## Example usage

Build after the normal project build:

```bash
clang++ -std=c++17 \
  -Ithirdparty/libOPNMIDI/include \
  tools/mono_click_probe/mono_click_probe.cpp \
  build-opn-au-arm64/libOPNMIDI.a \
  -o build-opn-au-arm64/mono_click_probe
```

Run in mono mode:

```bash
build-opn-au-arm64/mono_click_probe \
  thirdparty/libOPNMIDI/fm_banks/gs-by-papiezak-and-sneakernets.wopn \
  tools/mono_click_probe/temp_midi_bass_excerpt.tsv \
  build-opn-au-arm64/mono_click_probe_mono.wav \
  3.5 \
  mono
```

Run in poly mode:

```bash
build-opn-au-arm64/mono_click_probe \
  thirdparty/libOPNMIDI/fm_banks/gs-by-papiezak-and-sneakernets.wopn \
  tools/mono_click_probe/temp_midi_bass_excerpt.tsv \
  build-opn-au-arm64/mono_click_probe_poly.wav \
  3.5 \
  poly
```

## Interpretation

If mono and poly produce the same spike profile on this excerpt, the immediate failure is likely a more general note-edge problem for this patch/range, not just the mono handoff path.

Future work should keep this probe around as a regression harness and add a second event fixture with explicit overlapping mono-note transitions so mono-only failures can be isolated from generic patch clicks.
