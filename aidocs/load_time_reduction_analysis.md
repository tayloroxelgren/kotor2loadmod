# Load-Time Reduction Analysis — Deep Dive

Date: 2026-09-07
Sources: current working tree (transition profiler changes), `kotor2_log.txt` from the
game directory (5-transition run, 2026-07-12 build), `base_timing_log.txt`,
`report.md`, `resource_manager_reimplementation_report.md`, and fresh Ghidra
decompilation of the load path.

---

## 1. Executive summary

The premise is correct: the load path is a 2004-era architecture with **zero caching
layers, a single-threaded serialized pipeline, and a full GUI rebuild on every module
transition**. Each of those is individually attackable with the hook infrastructure
already in this repo.

But the fresh measurements change the priorities. Two corrections to earlier beliefs:

1. **The old per-call profiler inflated the numbers.** The legacy hooks logged a line
   per call; under that instrumentation `GUI_FindAndBindControlByTag` measured
   ~670 ms/session. In the current clean build it is **~39 ms per module load**
   (584 binds). GUI *binding* is no longer a top-tier target; GUI *construction as a
   whole* still is (`ModuleChunkLoadCore` ≈ 250 ms warm / 618 ms cold per load).
2. **Archive byte caching is real but small.** The A/B run already showed this:
   77% off the ERF read path, but only ~2.5% off total load time, because ERF sync
   reads are a minor slice and the parse callbacks (mandatory) dominate.

Current per-transition budget (warm, caches off, clean build):

| Component | Time | Evidence |
|---|---:|---|
| `ModuleChunkLoadCore` (GUI rebuild, ~30 panels) | ~250 ms | 5 calls, avg 328 ms (618 cold / ~253 warm) |
| Transition wall outside the GUI chunk | ~167 ms | `LoadTransitionWallTime` median 166.8 ms |
| Loading-screen presented frames (overlaps the above) | ~370 ms | 190 frames / 5 loads × 9.77 ms |
| **Perceived per-transition total** | **~420–550 ms** | |

The three structural mechanisms behind most of that time, verified in Ghidra:

- **M1 — The loading screen is a vsync-paced render loop doing real present work**
  (~38 frames/transition at ~9.8 ms each: clear, *two* full `GUIContext_UpdateAndRender`
  walks per call, gamma post-process, `SwapBuffers`, `GlobalMemoryStatus` syscall).
- **M2 — Every transition destroys and rebuilds ~30 GUI panels from raw GFF bytes.**
  The engine itself ships an eager/lazy flag (`g_eagerLoadGuiPanels` @ `0x00a10760`)
  gating 12 of them and an idempotency flag (`CClientExoApp+0x128`) that skips the
  whole rebuild — evidence the original team knew this was heavy.
- **M3 — Nothing is cached at the byte layer.** Every sync resource read cycles the
  archive reader refcount 0→1→0, which re-`fopen`s the archive (5,333 `fopen` calls in
  the baseline session; 14,147 `LooseFileRead`s). The ERF cache proved the concept;
  BIF/KEY and loose opens are still uncached.

Ranked plan (detail in §5): **(1)** enable and A/B the already-implemented present
throttle; **(2)** skip the three debug-menu constructors and the module-directory
scan they trigger; **(3)** extend the byte cache to BIF/KEY + loose-file opens;
**(4)** upgrade the GUI cache from field-lookup to a full tag→node map; **(5)** the
endgame — make GUI construction once-per-process instead of once-per-load.
Realistic outcome: warm module transition ~420 ms → **~150–200 ms**, initial load
into game roughly halved. The "majority of load time" claim is achievable for the
*removable* categories (pacing + rebuild + I/O), but a hard floor remains: GFF parse
callbacks construct live per-module game objects, and that work must happen at least
once per module.

---

## 2. What the current local changes give us

The working tree adds an end-to-end transition profiler (`LoadTransitionWallTime`)
hooking `Engine`, `loadingscreen`, `LoadingScreenUpdateFrame`, `ModuleChunkLoadCore`,
`ResourceLoadFromArchive`, `Worker_SubmitJob`, and `ProcessResourceQueue`, plus a
parser (`loadingscreen_timeparse.py`) that prints per-transition phase breakdowns.
This is the right measurement tool and it is what produced the numbers above.

The optimization toggles all exist but ship **off** in the current build:
`ENABLE_ARCHIVE_RESOURCE_CACHE 0`, `ENABLE_GUI_CONTROLS_LOOKUP_CACHE 0`,
`THROTTLE_LOADING_SCREEN_PRESENTS 0`, `SKIP_DEBUG_GUI_CONSTRUCTION 0`,
`DEFER_INGAME_TAB_CONSTRUCTION 0`, `ENABLE_NATIVE_LAZY_GUI_MODE 0` (permanently —
see §6). One instrumentation-bias note: the old `report.md` targets were set using
per-call-logging numbers; re-baseline everything with the transition profiler before
spending effort (e.g., "GUI/GFF tag lookup cache: 300–600 ms expected" is really
~40 ms warm).

Latest clean run (`kotor2_log.txt`, 5 transitions, all caches off):

```
LoadTransitionWallTime : 5 valid, avg 150.5 ms, median 166.8 ms
  Engine               : 104.5 ms/transition
  Load coordinator     :  43.7 ms/transition
  ResourceLoadFromArchive: 0.53 ms/transition (64 calls ≈ 8.5 ms per load, all fallbacks)
ModuleChunkLoadCore    : 618 ms cold, then 250–261 ms warm (×5)
GUI_FindAndBindControlByTag: ~39.5 ms / 584 binds / 16,521 CONTROLS field lookups per load
LoadingScreenUpdateFrame: 190 calls × 9.77 ms across the session (~371 ms per load)
```

---

## 3. Ghidra findings — how the load path actually works

### 3.1 `loadingscreen` (0x00533830) is a per-tick state machine, not a delay

Per tick it drains the resource queue, then either drives the next load stage
(`LoadOrCreateAreaAndInit`, `ModuleLoad_FinalizeAndQueueReady`) or, at state 0, runs
the final drain (`ProcessResourceQueue(...,1)`, `InitGraphicsCache`,
`InitShadowCache`, autosave branch). **There is no artificial minimum display
time**: `LoadingScreen_UpdateTimeoutCountdown`/`UpdateFrameCountdown` are watchdogs
(return values unused). Every millisecond is real work — so every millisecond
removed is a real win.

### 3.2 The pipeline is serialized and client-"networked"

All load work is dispatched as packets ('P'/'S'/'BN') through a 64 KB ring buffer
and processed on the main thread by `ProcessResourceQueue` → `ResourceQueue_UnpackAndTrace`
→ handlers. `LoadingScreenUpdateFrame(...,1,·)` pumps exactly one `loadingscreen`
tick plus a queue drain per frame. Wall time = Σ(packet costs) + per-tick overhead +
present pacing. The worker thread (`Worker_SubmitJob`) exists but the measured
submit-wait is ~0.01 ms/transition — the client load path is effectively
single-threaded, and the loading screen exists to mask that serialization.

### 3.3 `ModuleChunkLoadCore` (0x007be4c0) — the GUI rebuild hub

Guarded only by `CClientExoApp+0x128` (zeroed by teardown packet each transition),
it allocates and constructs ~30 panels in five groups, calling
`LoadingScreenUpdateFrame(dt,0,0)` (i.e. **present enabled**) between groups.
Key structure verified:

- **12 panels are already gated by the engine's own eager flag**
  `g_eagerLoadGuiPanels` (0x00a10760): DialogComputer, Messages, Store, Equip,
  Inventory, Character, Map, Abilities, Journal, Options, PartySelection, GalaxyMap.
  The Xbox build shipped with lazy mode; PC defaults eager.
- **5 slots are null-guarded** (0xa0/0xa4/0xac/0xb0/0xb4: MessageBox ×2, SkillInfoBox,
  TutorialBox, ControllerLossBox) — the engine tolerates them being absent.
- **Three debug-only menus are built unconditionally**: `DebugMenuConstructor`,
  `CSWGuiLoadModuleDebugMenu_Ctor`, `CSWGuiPowersFeatsSkillsDebugMenu_Ctor`
  (slots 0x58/0x70/0x74). The load-module one walks and filters the entire modules
  directory — this is what drives `ModuleDirectoryScanner` (144 ms in the baseline
  session).
- `InitializeGameUI` (the 87 KB `CSWGuiMainInterface`) loads `mipc28x6_p` and then
  issues ~35 `GUI_BindNamedWidget` calls (BTN_EQU/INV/CHAR/…, TB_*, LBL_*).

### 3.4 Widget binding is an O(controls × binds) rescan

`GUI_FindAndBindControlByTag` (0x00418df0) restarts a linear scan of the CONTROLS
list for **every** requested widget; each iteration re-resolves the list element,
reads its `TAG` string through the GFF field API (two `CExoString` heap alloc/free
per iteration), and string-compares. 584 binds × ~28 average scan positions =
16,521 field lookups per load. The fix is a one-pass tag→node map per CONTROLS list
(called with the list pointer in arg 4; on hit, invoke the same panel bind vfunc at
`vtable+0x48` the original would call). The existing `ENABLE_GUI_CONTROLS_LOOKUP_CACHE`
only caches the inner field-label lookup — worth enabling, but the map is the real fix.

### 3.5 `LoadingScreenUpdateFrame` (0x00409ed0) — present pacing dominates

Each call: `FrameMetricsAndMemoryUpdate` (**`GlobalMemoryStatus` syscall every
frame**), buffer clear, `GUIContext_UpdateAndRender`, gamma post-process, `GetDC`/
`Window_CanPresentFrame`/`SwapBuffers`/`ReleaseDC` (**vsync-blocked**), message
pump, then a **second** full `GUIContext_UpdateAndRender`. At ~38 presented frames
per transition × 9.77 ms this is ~370 ms per load of pacing/render overhead — the
single largest removable block, and `THROTTLE_LOADING_SCREEN_PRESENTS` (already
implemented: forces `suppressPresent=1` inside the interval while keeping GUI
update/message pump/audio) is precisely the right lever. Note `suppressPresent=1`
still runs both GUIContext walks — a further win is skipping the redundant second
walk when suppressed.

### 3.6 Byte acquisition: refcount cycling with zero reuse

Sync archive reads go `AddRefSyncOpen → (open if ref 0) → GetResourceSize →
AllocateLoadBuffer → ReadResourceSync → ReleaseSyncClose → (close at ref 0)`, so
single-resource loads reopen the archive file each time (baseline: 5,333 `fopen`,
1,462 `LooseFileOpen`, 14,147 `LooseFileRead` per session). The working ERF cache
serves repeat reads as `memcpy` + parse callback (measured 421→95 ms over 8 loads).
Remaining uncached surfaces: `CExoResFile_*` (BIF/KEY, 2,472 loads in the focused
run — its table layout is already documented in
`resource_manager_reimplementation_report.md`) and loose-file opens. `ResourceLoadMemoryBacked`
(RIM image loads) is already a memory copy and fine.

### 3.7 Tracer overhead (verify, then kill)

`ResourceQueue_UnpackAndTrace` formats two printf-style strings and **hex-dumps the
entire packet** to the trace file for every packet when tracing is enabled (gate:
`*(*(ctx+4)+0x88)` via `FUN_0073f2b0`; `FlushTracer` was called 650,205 times in the
baseline session). If the Steam build writes trace output during load, forcing the
gate to 0 during transitions is a cheap 50–150 ms win. Measure first with the
transition profiler.

---

## 4. Where the time goes (reconciled numbers)

Baseline session (old full profiler, inflated by per-call logging — use as upper
bounds): `loadingscreen` 2,964 ms; `ModuleChunkLoadCore` 1,640 ms; loading frames
731 ms; `GameObjUpdate` 330 ms; `fopen` 184 ms; `ModuleDirectoryScanner` 144 ms;
`LevelLoaderAndInitializer` 149 ms.

Current clean build per warm transition (~420–550 ms total):
- GUI rebuild (`ModuleChunkLoadCore`): **~250 ms** — binding 39 ms, ~30 ctor
  GFF loads + object construction + textures ~160 ms, 5 inter-group frames ~50 ms.
- Non-GUI transition wall: **~167 ms** — engine ticks, area/object staging,
  final drain (`InitGraphicsCache`/`InitShadowCache`).
- Loading-frame pacing overlaps both: **~370 ms** at ~38 frames × 9.77 ms.
- Archive reads: ~8.5 ms/load (ERF only; BIF/loose additional).

First load of a session is 2–3× worse (618 ms GUI chunk, cold file cache, texture
uploads).

---

## 5. Ranked plan

### Tier 1 — enable what's built, low risk (est. −250–350 ms/transition)

1. **Turn on `THROTTLE_LOADING_SCREEN_PRESENTS`** (interval 100 ms → try 33/50/100).
   Keep the first and last frames presented. Verify: window responsiveness, load bar
   still animates, no watchdog trip (`LoadingScreen_UpdateTimeoutCountdown` resets
   the queue on expiry — with 100 ms interval the watchdog budget must stay above
   it; watch `valid=1` in `LoadTransitionWallTime`). Expected: −200–300 ms/transition.
2. **Turn on `ENABLE_ARCHIVE_RESOURCE_CACHE`** permanently (it has already proven
   stable in the 8-load A/B) — the README's "next candidate" framing undersells it;
   it's a free 77% off its slice.
3. **Skip the three debug-menu constructors** (`SKIP_DEBUG_GUI_CONSTRUCTION`) and
   with them the module-directory scan per load. Validate slot-null tolerance at
   0x58/0x70/0x74 (they are debug UI; the engine's own guarded slots show the null
   pattern is tolerated, but confirm each access path). Expected: −50–150 ms on
   loads that rebuild GUI (and kills the 144 ms scanner).
4. **Run every change as an A/B with the transition profiler**, one toggle at a
   time; `LoadTransitionWallTime` is now the source of truth, not the old per-call
   totals.

### Tier 2 — extend the proven patterns (est. further −100–200 ms/transition)

5. **BIF/KEY byte cache.** Port the two-level ERF design to
   `CExoResFile_ReadResourceSync` + serve `ResourceLoadFromArchiveSlot` from cache
   (structures already reversed: header at reader+0x30, table at +0x34, 0x10-byte
   entries, offset at +4/size at +8). Same rules: game allocator, game parse
   callback, no refcount mutation, disable-on-mismatch.
6. **Loose-open dedup.** 1,462 opens/14k reads per session; cache by resolved path
   (first-open wins, serve reads from memory for read-only resource extensions) or
   simply keep our own `FILE*`/mapping keyed by path.
7. **Full tag→node bind map** replacing the field-label-only cache. Build the map on
   first bind per CONTROLS list, clear on module teardown. ~−40 ms warm, more on
   first load; also removes 16k GFF field lookups/load.
8. **Defer the 8 in-game tabs** via the engine's own `CSWGuiInGamePanel_LazyInitTab`
   (`DEFER_INGAME_TAB_CONSTRUCTION` — implemented, untested). One tab at a time,
   verifying open/close/reopen/transition. The 12-panel eager flag itself stays
   alone (see §6).
9. **Tracer gate** — if Tier-1 profiling shows trace writes, force
   `FUN_0073f2b0`-equivalent gate to 0 during transitions.

### Tier 3 — structural (the "drastic" tier; high effort, gated by Tier-1/2 data)

10. **Once-per-process GUI construction.** The correct shape is *not* preserving
    objects through teardown (failed twice). Two safer designs:
    a. **Parsed-layout cache:** cache the post-parse GFF state per panel resource
       (field values per control) and fast-construct widgets from it, skipping GFF
       reads/field scans entirely; still allocate fresh objects each load through
       the game's constructors where possible.
    b. **Scoped teardown interception:** let teardown run, but intercept only the
       GUI-slot zeroing inside the packet-8 path (`FUN_00785410`) so panels survive
       *without* vtable swaps — the previous crash came from no-op'ing destructors
       and restoring pointers around a full state reset; intercepting the reset
       itself is narrower. Treat as experimental; needs the shadow-compare
       discipline from the resource-manager report.
11. **Prefetch thread** (mynotes plan): on `ModuleHandler` entry, warm the byte
    cache for the module's RIM/ERF set in parallel with main-thread CPU work.
    Value shows up mostly on first loads; only after 5/6 make hits free.
12. **Frame-work trimming:** skip the second `GUIContext_UpdateAndRender` walk and
    the `GlobalMemoryStatus` call when presents are suppressed (hook-level: the
    throttle already skips clear/present; suppressing the redundant walk is a small
    patch to the same hook's suppressed path via a lightweight pump of our own).

### Expected end state

| Stage | Warm transition |
|---|---:|
| Today | ~420–550 ms |
| + Tier 1 | ~200–300 ms |
| + Tier 2 | ~150–220 ms |
| + Tier 3 (once-per-process GUI + prefetch) | ~80–150 ms |

Remaining floor: per-module GFF parse callbacks, object staging (`GameObjUpdate`,
`LevelLoaderAndInitializer` ≈ 300 + 150 ms *per session* in the baseline), and the
engine's own finalization — real content work that must run once per module.

---

## 6. What not to revisit (already falsified)

- **Archive handle pinning** (`KEEP_ARCHIVE_OPEN_DURING_LOAD`): crashed on second
  load / hung a load; refcount lifecycle is ownership-sensitive. The byte cache
  achieves the same goal without touching lifecycle.
- **GUI object preservation via vtable swaps** (`PRESERVE_GUI_OBJECTS_ACROSS_LOADS`):
  teardown resets broader state than GUI slots; stale references crash. Keep off;
  use §5.10 designs instead if pursued at all.
- **`ENABLE_NATIVE_LAZY_GUI_MODE`** (zeroing `g_eagerLoadGuiPanels`): PC build leaves
  app-owned slots null with no reconstruction path. The engine's per-tab lazy path
  (item 8) is the supported subset.
- **Per-call logging at scale:** any hook that logs per invocation corrupts the
  measurement (670 ms → 39 ms discrepancy). Aggregate in memory, batch-flush.

---

## 7. Bottom line

"Yes, drastically" — but through the boring levers first. The architecture offers
three free lunches the engine never took: it re-renders a vsync-paced loading screen
while doing serialized CPU work (~370 ms/load), it rebuilds an identical 30-panel
GUI on every transition (~250 ms/load), and it re-opens and re-reads the same
archives thousands of times per session with no byte cache. Tier 1 alone — three
macro flips that are already implemented plus one small constructor-skip — should
take a warm module transition from ~0.5 s to ~0.25 s with low risk. The path to
sub-150 ms runs through once-per-process GUI construction, which is genuinely
hard, but everything up to it is incremental, measurable with the new transition
profiler, and reversible.
