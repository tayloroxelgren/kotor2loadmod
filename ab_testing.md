# A/B Testing Protocol

One scenario per game session. The DLL logs `ProfilerRunStart: ... scenario=a_baseline|b_optimized`
plus every toggle value at startup, so each saved log self-identifies. The parser
(`loadingscreen_timeparse.py`) automatically analyzes the latest run in a log file.

## Switching scenarios

Set `AB_SCENARIO` at the top of `dinput8.cpp`:

```cpp
#define AB_SCENARIO AB_SCENARIO_A   // baseline: all optimization toggles off
#define AB_SCENARIO AB_SCENARIO_B   // throttle + archive cache + debug-GUI skip
#define AB_SCENARIO AB_SCENARIO_C   // baseline + long-fade clamp (1.0s -> 1ms)
#define AB_SCENARIO AB_SCENARIO_G   // baseline + forced area streaming during load (stream_force.md)
```

Then build (from any shell):

```
cmd /c build_scenario.bat
```

and copy `dinput8.dll` into the game directory.

- Scenario A build: 2026-09-07, deployed as `dinput8.dll` (69,632 bytes).
  Previous in-game build preserved as `dinput8_backup_20260712.dll`.
- Round-1 B composition (documented in the source block): present throttle
  (100 ms interval), archive resource cache, debug-GUI construction skip.
  Excluded from B for attribution: GUI controls lookup cache, in-game tab deferral.

- Round-2 C composition: baseline toggles + `CLAMP_LONG_FADES` — every fade
  of 0.25 s or longer is clamped to 1 ms before the engine starts it. The
  measured load-transition fade is exactly 1.0 s once per load, so expected
  effect is about -1.0 s on every load, visible by hand timing alone.
  Visual note for the run: all long fades (load fade-out AND any post-load
  fade-in) will be near-instant; that is expected, not a bug.

## Run checklist (same for both scenarios)

1. Confirm the game directory has the right `dinput8.dll` and no `kotor2_log.txt`
   (rename the previous one if present).
2. Launch the game normally (Steam).
3. Load the SAME save / follow the SAME route. Standard run:
   - 1 initial load into the game (cold),
   - 4 module transitions through doors (warm) — same doors, same order,
   - avoid opening GUI panels mid-run (keep first-use costs out of the numbers).
4. Quit the game.
5. Save the log into the repo:
   ```
   cp "<game dir>/kotor2_log.txt" timelogs/ab_<a|b>_<date>.txt
   ```
6. Parse it:
   ```
   python loadingscreen_timeparse.py timelogs/ab_<a|b>_<date>.txt
   ```
7. Compare `LoadTransitionWallTime` (per-transition wall, median), then the nested
   `ModuleChunkLoadCore` / `LoadingScreenUpdateFrame` / `ResourceLoadFromArchive`
   rows, then `loadingscreen` totals.

## Reading the results

- `LoadTransitionWallTime ... valid=1` on every transition; `valid=0` means phase
  markers arrived out of order — do not use that transition in comparisons.
- Compare medians, not means (first/cold load is a separate population).
- `presented_frame_calls` should drop sharply in scenario B (throttle active);
  if it does not, the throttle macro did not take effect — check the
  `ProfilerRunStart` line.
- Scenario B stability checks during the run: loading bar still animates, window
  stays responsive, no hang on the final drain, saves still work after loading.
- If scenario B crashes or hangs, the throttle interval is the first suspect
  (watchdog interaction); fall back to 150-200 ms and retest.

## Standard test route

Load one save, then reload the same save 3 more times (4 transitions total:
1 cold + 3 warm). Keep GUI panels closed except the load menu needed to reload.

## Robustness rules (added 2026-09-07 after two bad runs)

1. **Void-run check.** After every run, before comparing anything:
   `python loadingscreen_timeparse.py <log>` must print `Installed hooks: N`
   (N > 0) and must NOT print `VOID RUN`. A run with `Hooks not installed` or
   `Signature check failed` lines was vanilla gameplay — discard it. (This
   happened once: one wrong signature byte silently disabled the whole mod,
   including the intro skip; the gate now names the failing check.)
2. **Dead-anchor check.** The parser warns if the save-load anchor never fired
   (`loadgame_calls=0` on every PerceivedLoad line). In that state the
   perceived window starts at engine state activation and excludes
   click-to-activation time — the wall numbers are then a lower bound.
   Known limitation: the Load button's save-load flow is server-driven through
   the packet queue; the client-side `LoadGame` (0x006310d0) is not on that
   path and never fires during save loads.
3. **Stopwatch calibration (once per session).** Hand-time two loads with a
   stopwatch during the run and note the values. After parsing, compare them
   to the matching `PerceivedLoadWallTime` walls: agreement within human
   reaction time (~0.2-0.3 s) validates the instrumented window; a larger gap
   means real unmeasured time exists (chase the anchor before trusting data).
4. **Warm-cache order bias.** The Windows file cache warms up during the first
   run of a session, so a scenario that runs second gets cheaper disk I/O.
   Only compare runs performed back-to-back in the same session (warm vs
   warm), or alternate order A-B-B-A. The original A(20:13)-B(20:17) pair has
   this bias in B's favor; the A2 rerun fixes it (B ran at 20:39, A2 runs
   immediately after).
5. **Same everything.** Same save slot, same load order, same route, GUI
   panels closed, same in-game location, game restarted between scenarios.

## Results

### Scenario A baseline — 2026-09-07 (`timelogs/ab_a_20260907.txt`, run_id=121169631425111)

| Transition | Wall | ModuleChunkLoadCore | Archive | Frames |
|---|---:|---:|---:|---:|
| 1 (cold, initial load) | 764.4 ms | 582.0 ms | 136.6 ms | 8 |
| 2 (reload) | 333.8 ms | 261.9 ms | 26.3 ms | 8 |
| 3 (reload) | 331.6 ms | 259.6 ms | 25.9 ms | 8 |
| 4 (reload) | 321.6 ms | 263.9 ms | 27.2 ms | 8 |

Warm medians: wall **332.7 ms**, ModuleChunkLoadCore **262.9 ms**,
LoadingScreenUpdateFrame (in-transition) **187.3 ms**, ResourceLoadFromArchive
**26.7 ms**. Session totals: GUI_FindAndBindControlByTag 280.5 ms / 2,336 binds
(584 per load); ResourceLoadFromArchive 1,183.9 ms / 1,728 calls (incl. outside
transitions).

Observations:
- GUI is fully rebuilt on every save reload (~262 ms each) — teardown clears
  `CClientExoApp+0x128` on every load, confirming the once-per-process GUI
  construction lever.
- Only 8 presented frames per transition; most LoadingScreenUpdateFrame time is
  the `runLoadingScreenWork=1` queue-drain form, so present throttling alone has
  a bounded (~70-100 ms/transition) ceiling on this route.

### Scenario B (throttle + archive cache + debug-GUI skip) — 2026-09-07 (`timelogs/ab_b_20260907.txt`, run_id=106051346806660)

| Transition | Wall | ModuleChunkLoadCore | Archive | Frames |
|---|---:|---:|---:|---:|
| 1 (cold, initial load) | 302.5 ms | 255.7 ms | 18.8 ms | 2 |
| 2 (reload) | 292.0 ms | 235.5 ms | 4.2 ms | 2 |
| 3 (reload) | 293.2 ms | 226.9 ms | 4.1 ms | 3 |
| 4 (reload) | 298.3 ms | 228.3 ms | 2.8 ms | 3 |

### A vs B comparison (medians)

| Metric | A | B | Delta |
|---|---:|---:|---:|
| Cold wall (transition 1) | 764.4 ms | 302.5 ms | **−60.4%** |
| Warm wall (reloads) | 332.7 ms | 295.7 ms | **−11.1%** |
| Warm ModuleChunkLoadCore | 262.9 ms | 231.9 ms | −11.8% |
| Warm ResourceLoadFromArchive | 26.7 ms | 4.1 ms | −84.7% |
| ResourceLoadFromArchive session total | 1,183.9 ms | 86.0 ms | −92.7% |
| GUI_FindAndBindControlByTag session | 280.5 ms / 2,336 | 125.1 ms / 2,304 | −55.4% |
| Presented frames per transition | 8 | 2–3 | −70% |
| Whole 4-load session | 1,751.3 ms | 1,186.0 ms | −32.3% |

Archive cache behavior: 235 entries / 27.4 MB retained, 1,493 hits vs 235
misses (~86% of served loads hit), ~176 MB served from RAM; no mismatch
disables, all 4 transitions valid, no stability issues observed.

Findings:
- The cold-load penalty (A: 764 vs 333 warm) almost vanished in B (302 vs 296) —
  it was mostly debug-GUI construction and uncached archive reads.
- Warm reload improvement is real but bounded: GUI rebuild is now 78% of the
  warm wall (232 of 296 ms). Next levers by size: once-per-process GUI
  construction (Tier 3), then BIF/KEY + loose-file caching (ERF archive reads
  are now only ~4 ms/transition).
- LoadingScreenUpdateFrame in-transition only fell 187→180 ms: as predicted,
  most of it is the queue-drain form doing real load work, not presentation.
- Scenario B left deployed and active in the source (`AB_SCENARIO AB_SCENARIO_B`).


### Perceived-load calibration and A reruns (2026-09-07, sessions A2/A3/A4)

| Session | Warm transition median | Frozen pre-load | Post-drain | Notes |
|---|---:|---:|---:|---|
| A2 (hook counters) | 314.9 ms | 115 ms (reloads) | ~3 ms | calibration: hand 3800/3500 ms vs logged 325/323 ms |
| A3 (+anchors/gaps) | 316.4 ms | 115 ms | ~2 ms, gaps 0 | save_request anchor fired once, inside the window |
| A4 (+save-list timer) | 319.9 ms | 116 ms | ~3 ms, gaps 0 | SaveListParse 14.3 ms total — save-menu parse ruled out |

**Accuracy verdict.** Within its window the profiler is highly accurate and
repeatable: warm-transition medians across three separate game sessions differ
by ~1.6% (314.9/316.4/319.9 ms), so the A2-vs-B2 ~6% scenario difference is a
real, resolvable signal. But coverage against hand timing is ~12-13%: of a
~3,500 ms hand-timed load, the instrumented phases account for only ~440 ms
(~320 transition + ~115 frozen pre-load + ~3 post-drain). Ruled out as the
missing ~3.1 s: post-load stalls (zero post-drain frame gaps), a frozen main
thread (only 115 ms of freeze before activation), and save-list parsing
(14 ms). The remainder is live-rendered time between the click and load-state
activation — cause not yet identified. Candidates: chunked per-tick save
deserialization through the packet pipeline, background-thread work, or
deliberate fade/animation pacing. Next probe: record inter-frame gaps of the
presents immediately BEFORE activation (rolling ring buffer) plus per-tick
Engine durations outside transitions, to distinguish busy-ticks from idle
animation.

### A5 — whole-load account via loadingscreen accumulator (2026-09-07, run_id=104230285445343)

| Load | Transition | Frozen pre | Post | ls_busy (all phases) | ls_span | ls_calls |
|---|---:|---:|---:|---:|---:|---:|
| 1 (initial) | 324 ms | 0.2 ms | 1 ms | 44 ms | 329 ms | 6 |
| 2 (reload) | 316 ms | 114 ms | 3 ms | 995 ms | 8.3 s | 1257 |
| 3 (reload) | 321 ms | 115 ms | 4 ms | 915 ms | 7.7 s | 1160 |
| 4 (reload) | 321 ms | 114 ms | 3 ms | 993 ms | 8.3 s | 1259 |

Complete per-reload account: ~437 ms state-active load (320 transition +
115 frozen + 3 post) + ~950 ms coordinator busy time outside the transition
(pre-activation phase) ≈ **1.4 s of measured engine work per reload**. The
remaining wall time to the ~3.5 s hand timing is pre-activation pacing:
coordinator ticks interleaved with rendering (~1200 ticks between loads),
plus whatever menu navigation the stopwatch included.

Structural explanation (from the `loadingscreen` decompile): while load state
== 0, every coordinator tick runs `ProcessResourceQueue(queue, 1)` — the
save-load packet pipeline (server-side save read + deserialization) is
drained through those ticks BEFORE the load state activates. That is the
phase the state-activation transition window structurally cannot see, and it
is exactly what the original sum-of-loadingscreen-durations metric captured.
ls_span includes gameplay/menu dwell between loads; use ls_busy for work,
span only for context.

**Optimization implication:** the pre-activation save-packet drain (~950 ms
busy + pacing) is now the single largest phase of a reload — bigger than
everything the current toggles touch (GUI rebuild 262 ms, archive 26 ms).
Next targets should aim there (e.g. byte-cache the save file read, or skip
redundant per-tick queue scans), not at further load-state-window trimming.

### A7 — queue-drain attribution, full reload account (2026-09-07, run_id=119434471914168)

Packet hooks disabled (crash postmortem: handlers are __thiscall per RET 0x8
and call sites; original cdecl detour corrupted the stack — convention now
fixed in source behind ENABLE_PACKET_HANDLER_TIMING, disabled).

| Load | ls_busy | prq_busy (queue drain) | prq calls | Transition | Frozen pre |
|---|---:|---:|---:|---:|---:|
| 1 (initial) | 43 ms | 279 ms | 225 | 327 ms | 0.1 ms |
| 2 | 1089 ms | 642 ms | 2641 | 333 ms | 121 ms |
| 3 | 960 ms | 622 ms | 2387 | 315 ms | 117 ms |
| 4 | 980 ms | 619 ms | 2427 | 317 ms | 117 ms |

**Complete per-reload account (loads 2-4):**
- Packet-pipeline busy: ~620-640 ms per cycle, split ~270 ms inside the
  transition (module-load packets) and **~350-370 ms pre-activation
  (save read + world restore packets)**.
- Coordinator non-packet busy: ls_busy(~970) minus overlap — several hundred
  ms of per-tick InitGraphicsCache/InitShadowCache/EnsureSubsystemReady scans
  across ~1,200 state==0 coordinator ticks (menu + pre-activation phases).
- Transition wall ~320 ms (GUI rebuild ~260 inside), pre-activation freeze
  ~117 ms, post-drain ~2.5 ms.

**New optimization targets, in order of measured size:**
1. Per-tick graphics/shadow cache scans during state==0 phases (hundreds of
   ms per load cycle of mostly no-op object iteration; gate to state changes).
2. Pre-activation save packet drain (~360 ms; byte-cache the .sav read /
   reduce packet round-trips).
3. The known in-transition GUI rebuild (~260 ms).
