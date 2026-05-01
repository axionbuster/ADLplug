# Plan: Fix mono mode click artifacts

## Context

Mono mode works but clicks on note transitions. A previous AI implemented a `mono_handoff` approach in libOPNMIDI that does `Upd_OffMute` (hard-mutes the old note via `touchNote(c, 0)` — sets all carrier TL registers to max attenuation in one sample), then starts the new note, then applies a post-generate fade-in ramp. This still clicks.

### Why the current approach clicks

The click source is inside `generate()`: the library mutes the old note instantly (TL registers → 127) at sample offset 0, then starts the new note. By the time the ramp code runs (after `generate()`), the damage is done — the old waveform jumped from its current amplitude to zero within a single sample inside the emulator. The ramp only affects the output buffer *after* that discontinuity was already rendered.

## Recommended approach: Pre-generate fade-out, then handoff

Instead of muting inside the library and ramping after, **fade out the old note's audio before the handoff** by generating a short segment with a gain ramp *before* sending the mono_handoff:

### How it works

In `process()` / `handle_midi()`:

1. When a mono NoteOn transition triggers, **don't call mono_handoff yet**
2. Instead, record that a handoff is pending: store `{channel, old_pitch, new_pitch, new_velocity}`
3. In `process()`, when a pending handoff exists:
   - Generate a short segment (16-48 samples ≈ 0.3-1ms at 44.1kHz) normally — the old note is still playing
   - Apply a fade-out ramp (1→0) to that segment
   - Now call `mono_handoff()` — the old note is muted at zero-crossing (or near-zero thanks to ramp), so no click
   - Generate the rest of the segment — the new note plays
   - Apply a fade-in ramp (0→1) to the post-handoff samples
   - This creates a smooth volume-envelope crossfade

### Files to modify

1. **`sources/plugin_processor.h`** — Add pending handoff state:
   ```cpp
   struct PendingHandoff {
       bool active = false;
       uint8_t channel;
       uint8_t old_pitch;
       uint8_t new_pitch;
       uint8_t new_velocity;
   };
   PendingHandoff pending_handoff_;
   ```
   Remove `mono_handoff_ramp_remaining_` and `mono_handoff_ramp_total_` (no longer needed).

2. **`sources/plugin_processor.cc`** — Two changes:
   
   **In `handle_midi()`**: Instead of calling `pl->mono_handoff()` + arming ramp, set `pending_handoff_`:
   ```cpp
   if (sound >= 0 && sound != (int)pitch) {
       pending_handoff_ = {true, (uint8_t)channel, (uint8_t)sound, pitch, vel};
   }
   ```

   **In `process()`**: Before the main generate loop, check for pending handoff and split the segment:
   ```cpp
   // Inside the iframe loop, after handling MIDI events:
   if (pending_handoff_.active) {
       auto &h = pending_handoff_;
       constexpr unsigned fade_samples = 32;
       unsigned fade_out = std::min(fade_samples, segment_nframes);
       
       // 1. Generate fade_out samples with old note still playing
       pl->generate(&left[iframe], &right[iframe], fade_out, 1);
       // Apply fade-out ramp (1→0)
       for (unsigned i = 0; i < fade_out; ++i) {
           float gain = 1.0f - (float)(i + 1) / (float)fade_out;
           left[iframe + i] *= gain;
           right[iframe + i] *= gain;
       }
       
       // 2. Now do the handoff (old note at ~zero amplitude)
       pl->mono_handoff(h.channel, h.old_pitch, h.new_pitch, h.new_velocity);
       
       // 3. Generate remaining samples with new note
       unsigned remaining = segment_nframes - fade_out;
       if (remaining > 0) {
           pl->generate(&left[iframe + fade_out], &right[iframe + fade_out], remaining, 1);
           // Apply fade-in ramp (0→1)
           unsigned fade_in = std::min(fade_samples, remaining);
           for (unsigned i = 0; i < fade_in; ++i) {
               float gain = (float)(i + 1) / (float)fade_in;
               left[iframe + fade_out + i] *= gain;
               right[iframe + fade_out + i] *= gain;
           }
       }
       
       pending_handoff_.active = false;
       iframe += segment_nframes;
       continue; // skip the normal generate call for this segment
   }
   ```

   Remove the old post-generate ramp code.

### Tradeoffs
- **Pro**: Eliminates the click — audio fades to zero before the mute happens, so there's no discontinuity
- **Pro**: The crossfade is ~0.7ms — imperceptible as a gap, but long enough to prevent clicks
- **Con**: Adds a tiny latency to note transitions (32 samples ≈ 0.7ms)
- **Con**: Slightly more complex process loop

---

## Alternative approach: Modify carrier operator envelope (commercial synth style)

Instead of output-level crossfading, **change the YM2612 carrier operators' release rate to maximum** before keying off the old note. The chip's built-in envelope generator then does a fast-but-smooth release. This is how commercial FM synths (DX7, TX81Z) handle mono mode.

### How it would work

1. On mono NoteOn transition:
   - For each carrier operator of the old note's chip channel, write RR=15 (fastest release) to register 0x80+op
   - Send NoteOff to the old chip channel (key-off via reg 0x28)
   - Wait ~1ms (the fastest RR at the chip clock rate ≈ 10-20 samples)
   - Start the new note on a new or same chip channel
   - Restore the original RR values on the old channel (for correct behavior if the channel is reused)

### Scope of changes

This is significantly more invasive:

1. **`thirdparty/libOPNMIDI/src/opnmidi_opn2.cpp`** — Add a new method `OPN2::fastRelease(size_t c)` that:
   - Reads the current instrument's algorithm to determine which operators are carriers
   - Saves the carrier operators' current RR values
   - Writes RR=15 (fastest) to carrier operator registers (0x80 + op*4 + cc)
   - Optionally provides a `restoreRelease()` method

2. **`thirdparty/libOPNMIDI/src/opnmidi_opn2.hpp`** — Declare `fastRelease()` and add per-channel saved-RR storage

3. **`thirdparty/libOPNMIDI/src/opnmidi_midiplay.cpp`** — Modify `realTime_MonoHandoff()` to use `fastRelease()` instead of `Upd_OffMute`, and introduce a delay (deferred new-note-on)

4. **`sources/plugin_processor.cc`** — The process loop would need to handle the delayed note-on (generate a short segment after key-off with fast RR, then start the new note)

5. **`thirdparty/libOPNMIDI/include/opnmidi.h`** — Possibly expose new API functions

### Tradeoffs
- **Pro**: Most authentic — uses the chip's own envelope hardware like real FM synths
- **Pro**: No output-level gain manipulation, so the release sounds natural
- **Con**: Touches the library internals (5+ files across the library boundary)
- **Con**: Requires understanding which operators are carriers per algorithm (the `alg_do` table in touchNote already has this — can be reused)
- **Con**: The "wait for release" timing is tricky — depends on chip clock rate vs sample rate
- **Con**: Modifying a vendored third-party library makes future updates harder

---

## Recommendation

**Go with the pre-generate fade approach** (Approach 1). It's 2 files, ~40 lines of changes, no library modifications, and achieves click-free transitions. The carrier envelope approach is more authentic but touches 5+ files in the vendored library for marginal sonic benefit — the 0.7ms crossfade is indistinguishable from a fast FM release to the listener.

## Verification

1. Build: `cd build-opn-au-arm64 && cmake --build .`
2. Install to `~/Library/Audio/Plug-Ins/Components/`
3. `auval -v aumu OPNM JPCm` — must pass
4. In Logic Pro: play overlapping bass notes with Mono on → no clicks
5. Fast chromatic runs → clean transitions
6. Release last note → clean release, no hanging
