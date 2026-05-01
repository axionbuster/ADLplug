# Mono bass-click remediation plan (OPN/AU focus)

Date: 2026-05-01
Scope target: OPN only, Logic Pro AU on personal MacBook, while keeping platform-agnostic harness metrics.

## What I inspected

- Prior mono-click work and measured outcomes in `docs/mono-click-repro.md`.
- Prior mono-silent-note postmortem in `docs/mono-silent-notes-diagnosis.md`.
- Current mono MIDI state machine and note handoff code in `sources/plugin_processor.cc`.
- Local harness source in `tools/mono_click_probe/mono_click_probe.cpp`.

## Current diagnosis

1. **The clicks are not mono-exclusive in the current probe fixture.**
   The checked-in 10s excerpt reports similar top discontinuity spikes in mono and poly, which means mono handoff alone cannot be the whole failure surface.
2. **The issue is at least partially emulator-sensitive.**
   Peak deltas change by backend (MAME/Nuked/Neko/GENS), so chip core behavior is involved, but all cores still show discontinuities.
3. **Previous fixes over-focused on transition timing mechanics** (crossfade/envelope staging) **without building a hard acceptance gate tied to event-boundary discontinuity metrics across emulators and mono/poly modes.**
4. **Current mono scheduler can still create abrupt edges** because it performs immediate NoteOff/NoteOn policy decisions without an explicit phase/edge continuity guard around the actual render sample where state flips happen.

## Why previous commits likely fell short

- **Crossfade was too local and policy-driven**: it smooths a selected handoff path, but not every edge class observed by the probe (especially pitchwheel-adjacent spikes and emulator-specific response).
- **Envelope manipulation lacked a deterministic entry/exit contract** per event boundary, so some transitions remained effectively hard-stepped.
- **No two-tier fixture strategy** was enforced:
  - fixture A: mono-specific overlap/retrigger stress,
  - fixture B: generic patch/range click stress (existing excerpt).
  As a result, regressions could move between causes without clear attribution.

---

## Plan 1 — Quick patch (high success probability, minimal code risk)

### Goal
Lower audible bass clicks in OPN mono mode on Logic/AU quickly, without emulator surgery.

### Scope
- OPN path only (`ADLPLUG_OPN2` guarded code).
- Mono mode note transitions only (NoteOn/NoteOff/retrigger).
- No emulator source changes.

### Implementation
1. **Add a short, sample-accurate de-click ramp around mono note state flips** inside processor render path (not only at MIDI policy layer):
   - `N` samples down-ramp before forced NoteOff of sounding note.
   - trigger new note.
   - `N` samples up-ramp after NoteOn.
   - Keep `N` tiny (e.g. 16–64 samples @44.1k) and configurable internally for tuning.
2. **Classify transition type explicitly** in mono handler:
   - same-pitch retrigger,
   - pitch change retrigger,
   - stack fallback on release,
   - pending-note overwrite.
   Apply ramp to all classes that produce a hard discontinuity.
3. **Pin default OPN emulator for this mode to Nuked for new instances** (already preferred) and verify whether that should be made explicit in mono documentation as a recommended baseline.
4. **Acceptance criteria (quick patch)**:
   - On harness fixture B (existing 10s excerpt): reduce top-10 spike deltas in mono by a meaningful threshold (e.g. >=20% on selected emulator).
   - In Logic Pro AU manual check: bass clicks become rare or clearly quieter during normal mono bass lines.

### What is different this time
- We smooth at the **audio-sample boundary where discontinuity happens**, not only via MIDI event choreography.
- We require measurable harness delta reduction before calling success.

---

## Plan 2 — Fundamental improvement (robust, architecture-level)

### Goal
Make mono transitions deterministic and emulator-tolerant, reducing edge artifacts across backends.

### Scope
- OPN mono engine behavior and scheduling model.
- Harness and fixtures expanded.
- Potentially medium refactor in processor mono subsystem.

### Implementation
1. **Introduce a dedicated mono transition state machine**:
   - states: `Idle`, `Hold`, `RampDown`, `Switch`, `RampUp`.
   - transitions driven by queued note intents (FIFO), not single mutable pending slot.
2. **Replace single pending note with bounded queue** per MIDI channel (e.g. ring size 8):
   - prevents overwrite/loss when multiple events land in one segment.
   - preserves temporal order.
3. **Render-coupled transition executor**:
   - execute state transitions in sub-blocks with exact sample offsets.
   - centralize all NoteOff/NoteOn/controller writes for mono into this executor.
4. **Fixture split in harness**:
   - Fixture A: explicit mono overlap/retrigger stress sequence.
   - Fixture B: existing musical bass excerpt.
   Track mono-vs-poly and multi-emulator metrics for both.
5. **Metrics upgrade**:
   - add percentile stats (P95/P99 delta), event-class tagging, and per-event-type spike tables.

### What is different this time
- We stop treating clicks as a single bug and model them as **event-classed discontinuities**.
- We solve scheduler correctness (queue + sample offsets) and de-click rendering together.

---

## Documentation updates for future agents

1. **Update `docs/mono-click-repro.md`**
   - Add fixture taxonomy (A mono-specific, B generic musical).
   - Add pass/fail thresholds and required emulator set for comparisons.
2. **Add a new implementation log section in `docs/mono-silent-notes-diagnosis.md` or successor file**
   - Record exactly which transition classes are smoothed.
   - Record before/after spike metrics per emulator.
3. **Add `docs/mono-click-remediation-plan-2026-05-01.md` (this file)**
   - Hand-off strategy, scoping, and sequencing.
4. **Agent checklist snippet (to paste into diagnosis docs)**
   - confirm binary freshness,
   - run harness fixture A/B mono+poly on at least MAME+Nuked,
   - attach PNG + spike table,
   - only then iterate policy changes.

## Local harness status in this environment

Attempted to build/run `tools/mono_click_probe`, but this checkout lacks `thirdparty/libOPNMIDI/CMakeLists.txt`, so the standalone harness cannot be configured here. Treat current findings as source-and-history diagnosis plus prior checked-in measurements, and run full metric validation on the MacBook checkout that includes libOPNMIDI.
