# Mono bass click remediation plan (OPN/AU/Logic-first)

Date: 2026-05-01
Scope target: OPN only, AU in Logic Pro on personal macOS machine; keep harness-valid methodology platform-agnostic.

## Investigation summary

### What I could verify in this environment

- The current branch already contains multiple attempted mitigations (handoff mute, delayed retrigger, probe tooling, and docs) and the present mono path uses `note_off_fast()` followed by a delayed re-`NoteOn` scheduler in `plugin_processor.cc`.
- The local benchmark harness (`tools/mono_click_probe`) is designed correctly for repeatable click scoring and PNG waveform/discontinuity inspection, but I could not execute it here because `thirdparty/libOPNMIDI` is a missing git submodule and outbound clone is blocked in this environment.
- Existing repo diagnosis notes report that, on the checked 10-second excerpt, the largest discontinuity spikes were similar in mono and poly and varied by emulator backend; this strongly suggests the failure surface is not only mono scheduling and includes emulator/patch edge behavior.

### Why prior attempts were incomplete

From code and docs review:

1. **Post-event smoothing instead of pre-discontinuity prevention**
   - Prior ramp/crossfade attempts mostly shaped output after or around note transitions, but the hard discontinuity can already be emitted inside emulator state changes.
2. **Mono note lifecycle ambiguity**
   - Mono scheduler complexity (stack + pending delayed NoteOn + fast NoteOff) can still create edge conditions where event ordering and sounding-state assumptions diverge from host event batching.
3. **Single fixture overfitting risk**
   - Current harness excerpt is useful, but it is not mono-exclusive. Fixes targeting only that slice can regress true mono legato/retrigger transitions in real Logic playback.
4. **Backend variance unaccounted in acceptance criteria**
   - Since spikes vary by emulator, an emulator-agnostic fix needs per-backend thresholds/expectations, or a chosen backend policy for your AU use case.

---

## Plan 1 — Quick patch (high probability, low invasiveness)

### Goal
Reduce audible bass clicks quickly for your actual target workflow (Logic AU + OPN), even if not architecturally perfect.

### Scope
- Files: primarily `sources/plugin_processor.cc` (possibly tiny parameter/constants exposure).
- No deep libOPNMIDI surgery.
- OPN-only behavior gating is acceptable.

### Approach
1. **Replace hard handoff with micro overlap render in host wrapper**
   - On mono transition (`sound != newPitch`), do not immediately execute hard note-off + delayed note-on as today.
   - Render a short pre-handoff tail window (old note alive), apply deterministic fade-out.
   - Apply transition (note off old, note on new), render short post-handoff window with fade-in.
   - Keep window tiny (e.g., 24–64 samples) and sample-rate normalized.
2. **Guard same-pitch retrigger and batched event handling explicitly**
   - Ensure `sound < 0` + same pitch still triggers fresh NoteOn.
   - Process pending mono transitions in strict FIFO per frame to avoid overwrite in dense host batches.
3. **Backend policy for your use case**
   - Default OPN emulator to the one with the best click profile for your bass program in Logic (based on harness metrics and listening), while preserving manual override.

### Acceptance criteria (quick patch)
- Audible clicks on your trumpet-bass mono line are clearly reduced in Logic playback.
- Harness spike metric on mono fixture improves by a defined target (e.g., >30% reduction in top-5 peak deltas on selected emulator).
- No silent-note regressions in same-note retrigger or fast runs.

### What this does differently vs prior commits
- Prior fixes centered on delayed retrigger and isolated ramps; this plan adds **structured pre/post split rendering around the exact transition point**, with explicit FIFO transition semantics and retrigger guards.

### Estimated effort
- 0.5–1.5 days including regression pass.

---

## Plan 2 — Fundamental fix (robust, emulator-aware)

### Goal
Eliminate root discontinuity causes rather than masking them at plugin output.

### Scope
- `thirdparty/libOPNMIDI` mono-handoff internals + plugin integration.
- Add richer harness fixtures and scoring.
- Potentially larger change set across vendored library and wrapper.

### Approach
1. **Introduce discontinuity-safe mono handoff primitive in libOPNMIDI**
   - Replace hard TL mute semantics in handoff path with envelope-consistent release behavior (or controlled phase/state-safe handoff primitive), avoiding one-sample jumps.
2. **Event-time accurate transition scheduling inside synthesis layer**
   - Execute note transitions at precise sample offsets with internal micro-segment generation, not host-block coarse boundaries.
3. **Multi-fixture benchmark suite**
   - Keep current 10s excerpt, add:
     - true mono overlap/retrigger stress fixture,
     - same-note retrigger fixture,
     - low-register bass sustain-transition fixture.
4. **Dual validation: metric + visual + ear**
   - Metric: top-N delta peaks and percentile discontinuity stats.
   - Visual: PNG overlays old vs new for transition regions.
   - Listening: Logic AU A/B on your exact patch chain.

### Acceptance criteria (fundamental)
- Mono-specific fixtures pass stricter thresholds across at least your chosen production emulator and one alternate backend.
- No regressions in poly mode and no dropped notes under dense MIDI.

### What this does differently vs prior commits
- Prior work treated symptoms at wrapper level; this plan moves the **core anti-click guarantee into synthesis transition semantics**, then verifies across targeted fixtures instead of a single mixed-behavior excerpt.

### Estimated effort
- 3–7 days depending on libOPNMIDI internals and iteration count.

---

## Execution strategy to maximize success probability

1. Run Plan 1 first for immediate user value in Logic.
2. Lock objective thresholds using harness before coding.
3. Only then decide if Plan 2 is still required based on measured/audio outcomes.
4. If Plan 2 starts, keep Plan 1 guardrails behind feature flags for rollback safety.

---

## Documentation updates for future agents

1. **Update `docs/mono-click-repro.md`**
   - Add explicit prerequisite note: submodule availability requirement and exact build failure symptom when missing.
   - Add “primary target scenario” section (Logic AU + OPN + patch/program details).
2. **Add a new “decision log” section**
   - Record which quick-patch constants/window sizes were tested and with what metric outcomes.
3. **Add fixture intent table**
   - Document what bug class each fixture isolates (mono-only, poly edge, emulator sensitivity).
4. **Add “don’t repeat” notes**
   - Explicitly call out that post-discontinuity-only smoothing is insufficient, and that fixes must handle same-pitch retrigger and batched event ordering.

This ensures future agents converge on proven paths instead of re-running already-failed tactics.
