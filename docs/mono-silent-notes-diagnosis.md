# Bug: Mono mode — notes silently fail to play

**Branch:** `codex/mono-portamento-legato`  
**File:** `sources/plugin_processor.cc`, `handle_midi()` mono NoteOn section  
**Symptom:** Notes sometimes don't produce audio in mono mode, as if legato were active (but the
behavior is wrong even with Legato off). Especially noticeable with Logic's Session Player which
repeats pitches.

---

## Root cause 1 (primary): Same-note retrigger after NoteOff is silently dropped

### The broken logic

```cpp
int &last = mono_last_note_[channel];
int old_note = (sound >= 0) ? sound : last;   // last = previously-played pitch

if (old_note >= 0 && old_note != (int)pitch) {
    // crossfade to new pitch
} else if (old_note < 0) {
    // first note ever — send NoteOn directly
} // else: old_note == pitch → comment says "already sounding, do nothing"
sound = pitch;
last  = pitch;
```

### Why it breaks

The "do nothing" implicit `else` fires whenever `old_note == pitch`.  
`old_note` is `last` when `sound < 0` (i.e. the current note has already been NoteOff'd).  
So when Session Player plays the same pitch twice in a row:

```
NoteOn  A  → sound=-1, last=-1  → old_note=-1          → direct NoteOn sent  ✓  sound=A, last=A
NoteOff A  →                                            → NoteOff sent         ✓  sound=-1, last=A
NoteOn  A  → sound=-1, last=A   → old_note=A, A==A     → NOTHING SENT         ✗  sound=A, last=A
                                                          note silently dropped
```

The note is not sounding (`sound < 0`) but the code assumes it is, because it can't
distinguish "same note held" from "same note re-pressed after release."

### Minimal fix

Inside the NoteOn block, replace the implicit "do nothing" with a check on `sound`:

```cpp
if (old_note >= 0 && old_note != (int)pitch) {
    // crossfade path (unchanged)
} else if (old_note < 0) {
    // first note ever (unchanged)
} else {
    // old_note == pitch
    if (sound < 0) {
        // Same pitch re-pressed AFTER its NoteOff — must retrigger.
        // No crossfade needed (old voice is already releasing quietly).
        send_midi3(pl, 0xb0 | channel, 65, port ? 127 : 0);
        if (port) send_midi3(pl, 0xb0 | channel, 5, (uint8_t)portT);
        send_midi3(pl, 0x90 | channel, pitch, vel);
    }
    // If sound >= 0, note is truly still held — already sounding, do nothing.
}
sound = pitch;
last  = pitch;
```

---

## Root cause 2 (secondary): Multiple NoteOns per segment — only the last is honoured

### The broken logic

In `process()`, the loop structure is:

```
while (midi events exist for this segment)
    handle_midi(event)          ← may set pending_handoff_ multiple times

if (pending_handoff_.active)    ← checked ONCE, after all MIDI events
    fade-out / mono_handoff / fade-in
else
    normal generate
```

If two NoteOns arrive in the same 256-sample window (e.g. very fast passages or jitter in
Session Player's timing), `pending_handoff_` is overwritten by the second NoteOn. Only the
second note ever starts; the first is silently dropped.

### Suggested fix

Two options:

**A. Queue of pending handoffs** — replace `PendingHandoff pending_handoff_` with
`std::vector<PendingHandoff> pending_handoffs_` (or a small fixed-size ring). In `process()`,
consume them in order: fade-out → handoff → (no fade-in until next entry or end of segment).
This is more correct but more invasive.

**B. Drain MIDI one-event-at-a-time** — change the segment loop to interleave one MIDI event
with a mini-generate, similar to how many soft-synth hosts work. This eliminates batching
entirely. More correct musically but changes the fundamental loop structure.

For most real-world use (Session Player tempos, normal playing), option A with a queue of 2–4
entries should be sufficient.

---

## Files to change

| File | Change |
|------|--------|
| `sources/plugin_processor.cc` | Fix `handle_midi()` NoteOn same-pitch branch (Root cause 1) |
| `sources/plugin_processor.cc` | Optionally: queue multiple pending handoffs (Root cause 2) |
| `sources/plugin_processor.h`  | If queuing: replace `PendingHandoff pending_handoff_` with `PendingHandoff pending_handoffs_[4]` + `int pending_handoff_count_` |

Root cause 1 is a one-function change (~10 lines) and fixes the most common failure mode.
Root cause 2 is optional for now; it only manifests at very high note rates.

---

## Verification

1. Build: `cd build-opn-au-arm64 && cmake --build .`
2. Install: `cp -R au/OPNplug.component ~/Library/Audio/Plug-Ins/Components/`
3. `auval -v aumu OPNM JPCm` — must pass
4. In Logic Pro with Mono on:
   - Session Player repeating same note → every note plays (Root cause 1 test)
   - Fast chromatic runs → no silent notes dropped (both root causes)
   - Long bass lines → clean transitions, no clicks
