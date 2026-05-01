# Mono bass-click action plan (OPN, Logic Pro AU, personal MacBook target)

## What I inspected right now

1. Reviewed the existing repro and harness notes in `docs/mono-click-repro.md`.
2. Reviewed prior design notes (`docs/mono-mode-design.md`) and diagnosis notes (`docs/mono-silent-notes-diagnosis.md`).
3. Reviewed the branch history around the prior anti-click attempts (`576129f`, `61cb020`, `b48a390`, `7f77da2`, `2573cea`, `655c2e8`, `66ba559`, `88e1e31`).
4. Attempted to run the local probe harness, but this workspace snapshot lacks a populated `thirdparty/libOPNMIDI` checkout (empty submodule directory), so probe build fails here.

## Observed diagnosis (from code + prior measured artifacts)

### 1) The current click profile is not mono-exclusive on the checked-in 10s fixture
The existing probe report already shows mono and poly with essentially the same worst discontinuities on the checked-in excerpt, and backend choice changes peak size materially. That means the click surface is shared between:
- mono scheduling/transitions, and
- intrinsic chip-backend/note-edge behavior.

### 2) Previous attempts were directionally useful but partially mismatched to the failure surface

- **Post-handoff ramp only (`576129f`)**: attenuates output after the discontinuity has already been produced in the emulator, so it cannot fully remove hard jumps.
- **Pre-generate crossfade (`61cb020`)**: conceptually stronger, but if event batching, pending-note overwrite, and same-pitch retrigger corner cases are not fully controlled, edge clicks and note drops still appear.
- **Session Player ordering fixes (`b48a390`, `7f77da2`)**: improved correctness of mono note lifecycle, but they mostly address silent/dropped notes and sequencing integrity, not backend discontinuities seen equally in poly.
- **Harness and visualization work (`2573cea`, `655c2e8`, `66ba559`, `88e1e31`)**: excellent baseline and should remain the regression gate.

### 3) Why this can look like an emulator problem
Emulator choice clearly affects severity (MAME/Nuked/Neko/Gens differ), but all tested backends still show discontinuities on the same sequence. So emulator behavior is a contributor, not an excuse to skip host/plugin-side mitigation.

---

## Plan 1 — Quick patch (high probability, low scope)

### Goal
Minimize audible bass clicks in **Mono Mode** for **OPN AU in Logic Pro** with minimal architecture change.

### Scope
- Files: `sources/plugin_processor.cc`, `sources/plugin_processor.h`, `docs/mono-click-repro.md`.
- No third-party library API changes.
- Keep current parameter persistence behavior intact.

### Approach
1. **Replace single pending note with queue semantics inside block processing**
   - Prevent “last event wins” overwrite when multiple mono transitions land in one audio block.
   - Drain one pending mono transition at a time with bounded sub-segments.
2. **Deterministic micro-fade envelope around every mono handoff**
   - Keep fade-out before handoff and fade-in after handoff, but enforce sample-accurate envelope application even with dense event timing.
   - Add guard rails to avoid fade truncation artifacts when remaining segment length is shorter than fade length.
3. **Dezipper portamento + mono CC writes at transition points**
   - Ensure CC65/CC5 updates cannot coincide with zero-length pre-roll.
4. **Harness acceptance threshold**
   - Use the local probe as hard gate: mono worst-peak ratio should improve meaningfully vs current branch baseline on the same fixture.

### What this does differently vs previous commits
- Treats handoffs as a **queue**, not a singleton flag.
- Prioritizes **timing correctness under batched MIDI** as first-class, not incidental.
- Uses harness metrics as merge criterion, not subjective ear-only checks.

### Risk
- Very fast passages may still reveal backend-intrinsic transients.
- Small sonic softening around transitions due to short fades.

---

## Plan 2 — Fundamental improvement (lower risk of regressions long-term)

### Goal
Separate mono-note scheduling from backend rendering so clicks are controlled structurally, not patched per event.

### Scope
- Plugin processor mono engine refactor + optional OPN backend behavior tuning.
- Potentially includes `thirdparty/libOPNMIDI` integration strategy, but keep AU-facing behavior stable.

### Approach
1. **Introduce a dedicated mono voice-transition state machine**
   - States like `Idle`, `SustainOld`, `FadeOutOld`, `TriggerNew`, `FadeInNew`.
   - State advances per sample-span, not per host MIDI batch.
2. **Sample-accurate event slicing**
   - Split block by event boundaries and transition boundaries; render each slice with explicit state.
3. **Backend-adaptive transition policy**
   - Calibrate fade length/shape per emulator (MAME/Nuked/etc.) using probe data.
4. **Optional deeper path**
   - Prototype an OPN-side “fast release handoff” (carrier-aware) in libOPNMIDI as experimental mode, compare against processor-level fade strategy.
5. **Expand harness fixtures**
   - Keep current 10s musical fixture.
   - Add a synthetic overlap stress fixture for guaranteed mono-only transitions.

### What this does differently vs previous commits
- Moves from ad-hoc conditionals to a formal transition model.
- Makes behavior deterministic regardless of DAW event coalescing.
- Produces reusable measurement-driven policy rather than one-off fixes.

### Risk / cost
- Higher implementation complexity.
- Needs careful regression checks for legato/portamento feel.

---

## Success criteria (both plans)

1. No silent-note regressions in Logic Session Player patterns.
2. Audible click reduction on low bass material in Mono Mode.
3. Harness metrics improve on same fixture and remain stable across emulator choices.
4. Parameter save/restore behavior for mono controls remains unchanged.

---

## Documentation updates for future agents

When implementing either plan, update these docs in the same PR:

1. **`docs/mono-click-repro.md`**
   - Add exact before/after metrics table (peak delta + ratio) per emulator and mono/poly mode.
   - Add exact command lines used.
2. **`docs/mono-mode-design.md`**
   - Mark superseded assumptions and describe the final transition model.
3. **New troubleshooting subsection**
   - Record known environment pitfall: probe requires populated `thirdparty/libOPNMIDI`; include recovery/setup steps.
4. **Commit-message hygiene note**
   - Require each mono/click fix commit to state: target fixture, measured delta change, and whether changes are scheduler-only or backend-touching.

This will keep future agents from repeating already-failed “post-discontinuity smoothing” approaches and direct them to measurement-first fixes.
