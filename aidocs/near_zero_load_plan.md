# Near-Zero Load Times — Design Analysis

Date: 2026-09-07. Supersedes the tier plan in `load_time_reduction_analysis.md`.
Everything below is grounded in the instrumented A/B runs (A1–A7, B1–B2) and
Ghidra-verified engine structure. Hand timing convention (owner-corrected):
stopwatch starts at the click that initiates the load, stops at playable.

---

## 1. The complete, measured anatomy of a save reload (warm, same save)

Target scenario: pause menu → Load → click save → playable. Hand-timed
**~3.5 s** (3.8 s first). Fully decomposed:

| # | Phase | Wall | Busy (CPU) | Mechanism |
|---|---|---:|---:|---|
| 1 | Click → engine load-state activation | **~3.0 s** | ~0.9 s | Save-load advances through the packet pipeline (`ProcessResourceQueue`, ~2,400 calls/cycle) one stage per coordinator tick, gated on queue-empty; coordinator also re-runs `InitGraphicsCache`/`InitShadowCache` object scans every tick; fade animations tick alongside |
| 1a | — save read + world-restore packets | (in 1) | ~360 ms | 'P'/'S' packet handlers deserialize the save into objects |
| 1b | — per-tick cache/scans (mostly no-ops) | (in 1) | ~300–570 ms | state==0 branch re-scans every object every tick |
| 1c | — freeze right before activation | 117 ms | — | last present → activation |
| 1d | — non-busy wall (fades, stage-per-tick pacing, queue-empty waits, render gaps) | **~2.0 s** | — | the engine waits by design, not by work |
| 2 | Activation → final drain (the "load screen") | ~320 ms | ~320 ms | GUI rebuild ~260 ms (`ModuleChunkLoadCore`) overlapping ~270 ms module packets |
| 3 | Drain → first playable frame | ~2.5 ms | ~2.5 ms | effectively zero — the engine is tight here |

Session-stable facts: same-save reload reconstructs identical world state and
identical GUI layout every time; module assets for the same module are already
resident after the first load; archive bytes are cacheable (proven: ERF cache,
−92.7% archive time in B).

## 2. What is the true floor?

Work that must happen at least once per load, in the worst case:

1. Deserializing the save into the live object graph (~360 ms CPU once).
2. Re-binding the client to the module (some packet exchange; tens of ms once
   assets are resident).
3. A handful of engine ticks for the state machine (tens of ms).
4. One presented frame (~2 ms, measured).

**Theoretical warm floor: roughly 0.4–0.6 s** for a naive-but-optimal engine.
With speculative prefetch (loading while the user browses the save list) or
same-save snapshot reuse, the click-to-playable floor drops to **tens of
milliseconds plus activation overhead — well under 0.5 s**. The goal
("almost completely remove") is therefore structurally reachable for the
reload path. Cold first-loads (new module, cold OS cache) keep a disk+parse
floor of a few hundred ms even in the best design.

## 3. The four-layer attack plan

### Layer 1 — Delete the waiting (no work removed, pacing removed) — est. 3.5 s → ~1.6 s

The single largest component is ~2 s of *deliberate* pacing. Nothing about it
is required by the content.

1a. **Clamp load-transition fades.** Hook `CSWGuiFade_SetTransitionState`
    (0x007bc8f0): log durations first (shadow), then rewrite the duration
    argument to ~0 during load transitions. Also covers `LoadingScreenFadeUpdateFrame`
    (0x0040dac0) pacing. Gate behind a macro; A/B with perceived metrics.
    *Risk: low-medium (fade completion may gate some state transitions —
    shadow data will tell us before we change anything).*

1b. **Pump the queue to busy-bound.** In the coordinator's state==0 path the
    hook calls `ProcessResourceQueue` repeatedly (until empty or a ~10 ms/tick
    budget) instead of the engine's single stage-per-tick. This converts the
    ~3 s pre-activation wall toward its ~0.9 s busy time.
    *Risk: medium — stages may assume tick boundaries. Test as a
    first-launch experiment; fall back per-tick on any anomaly.*

1c. **Throttle pre-activation presents** (the proven B-scenario throttle,
    applied to the pre-load menu/fade frames too) — removes per-tick render
    cost from the critical path. *Risk: low, already A/B-proven in-transition.*

Checkpoint: click→playable ≈ 1.4–1.8 s, with pre-activation wall ≈ busy+ε.

### Layer 2 — Delete redundant busy work — est. → ~1.0 s

2a. ~~Gate the per-tick object scans~~ **REMOVED BY MEASUREMENT (B3 run
    2026-09-07): `InitGraphicsCache` = 5 µs and `InitShadowCache` = 156 µs
    busy per load window across ~1,400 calls. The scans are free; the
    "300-570 ms" earlier estimate was mis-attributed loose-file reads.**

2b. **Byte-cache the save read — PROMOTED TO TOP PRIORITY.** Directly
    measured (B3, cold-cache session): the save/load cycle performs
    **~50-60 MB of `LooseFileRead` I/O in ~5,900 reads**; warm OS cache hides
    it (~16 ms) but cold it costs **~2.3 s busy**, dominating the
    pre-activation wait. A mod-owned cache (same design as the proven ERF
    cache) makes repeat loads immune to OS cache state. Also capture
    loose-open filenames next instrumentation round to split save vs other
    loose files.

2c. **Extend the archive cache to BIF/KEY + loose files** (planned in the
    original analysis; table layouts already reversed). Removes remaining
    in-transition I/O (~25 ms measured warm, more on cold loads).

Checkpoint: click→playable ≈ 0.9–1.1 s.

### Layer 3 — Delete the transition — est. → ~0.6 s

3a. **Once-per-process GUI construction.** The measured ~260 ms rebuild on
    every reload builds identical panels from identical layout data. Safest
    design remains the parsed-layout cache (fast-construct widgets from cached
    GFF field data); scoped teardown interception (skip only the GUI-slot
    zeroing in the packet-8 path) is the higher-risk alternative. The
    vtable-swap preservation approach is falsified — do not revisit.

3b. **Present throttle in-transition** (proven, B scenario) — keep enabled.

Checkpoint: click→playable ≈ 0.5–0.7 s, dominated by the (now unpaced)
deserialization.

### Layer 4 — Make the click free (speculation + snapshot) — est. → < 0.5 s, potentially ~0.2 s

4a. **Speculative prefetch while browsing.** The user spends seconds in the
    save list after opening the Load menu; the engine is idle. Speculatively
    read + deserialize the highlighted (or most recent) save in background
    coordinator ticks. By click time the world state is resident; the load
    becomes a state swap. *This is the highest-leverage idea in the plan: it
    converts the only remaining serial cost (deserialization) into time the
    player was going to spend anyway.* Risk: medium (must not disturb the live
    world while deserializing into shadow structures; needs a scratch object
    graph or double-buffer).

4b. **Same-save snapshot reuse.** For reload-of-the-same-save (our test route,
    and the common "retry" pattern), keep the previous deserialized world
    graph alive until the new load commits, then swap instead of rebuild.
    Equivalent of an emulator savestate at the object-graph level. The save
    file is identical; only player position/time/choices differ — those are
    small structures that must be re-read, the bulk (areas, statics, scripts)
    is identical. Risk: high (ownership boundaries — the same class of risk
    that killed GUI preservation, but at a layer with no vtables, per the
    mynotes analysis). Design, shadow-verify, then attempt last.

Checkpoint: warm same-save reload ≈ 0.2–0.4 s (activation + swap + frame).

## 4. Roadmap with falsifiable gates

| Phase | Ships | Expected click→playable | Abort/rollback criterion |
|---|---|---:|---|
| R1 | fade shadow → fade clamp → queue pump → pre-throttle | ≤ 1.8 s | any crash/hang on pump; fade artifacts |
| R2 | scan gating → save byte-cache → BIF/loose cache | ≤ 1.1 s | visual glitches (textures), cache mismatch disables |
| R3 | GUI parsed-layout cache | ≤ 0.7 s | panel misbehavior on 2nd+ load |
| R4a | speculative prefetch | ≤ 0.5 s | world-state corruption signs |
| R4b | same-save snapshot swap | ~0.2–0.4 s | any teardown-adjacent crash → drop permanently |

Every phase is measured with the instrumentation already built
(`PerceivedLoadWallTime` + `PacketProfile` + per-phase stamps), one toggle per
A/B run, void-run checks active.

## 5. What cannot be removed

- First load of a session (cold caches, one-time GUI build, module parse):
  a hard floor of ~0.5–1 s even with everything above.
- Loads into a *different* module pay that module's parse cost once per
  session; subsequent loads of it are warm.
- The engine's minimum state-machine handshake (a few ticks) and one frame.

## 6. Immediate next actions

1. Fade shadow instrumentation (`CSWGuiFade_SetTransitionState` durations +
   completion times during loads) — safe, one run, quantifies exactly how
   much of the 2.0 s pacing wall is fade-gated before we touch it.
2. Queue-pump experiment behind a macro (first-launch-experiment protocol).
3. Gate `InitGraphicsCache`/`InitShadowCache` behind a dirty-flag macro.
These three are independent, can ship as separate A/B toggles in one build,
and together attack ~2.5 s of the 3.5 s.
