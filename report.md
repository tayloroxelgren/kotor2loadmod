# KOTOR2 Load Optimization Project Report

Prepared for: Project Owner  
Prepared by: Codex  
Date: 2026-06-25

## Executive Summary

The project is at a transition point. We have enough timing data and Ghidra analysis to say that the original instinct, optimizing `ResourceLoadFromArchive`, is technically valid but probably not the highest-value first target anymore.

Owner feedback after this report draft: several of the proposed directions have already been attempted in practice and either proved unstable or did not produce a meaningful performance improvement. That changes the operating posture of the project. The next phase should not be another speculative hook or lifecycle shortcut. The next phase should be a controlled audit that separates:

- Ideas already attempted and rejected.
- Ideas that were attempted but may have been invalidated by instrumentation noise or unstable surrounding experiments.
- Ideas that remain untested and have a measurable exclusive-time target.

Until that audit is complete, the expected near-term speedup should be treated as lower and less certain than the optimistic ranges below.

The latest resource-specific timing shows `ResourceLoadFromArchive` taking 310.91 ms total across 1,274 calls. That is meaningful, but it is also a hard ceiling: even a perfect replacement of that function cannot save more than about 311 ms from that measured run. A realistic safe implementation would save less because allocation, parser callbacks, and engine resource-state updates still need to run.

The current stronger candidates for load-time improvement are:

1. Replace the current inclusive, per-call flushed logging with an exclusive aggregate profiler.
2. Remove or debounce redundant loading-screen frame work inside `ModuleChunkLoadCore`.
3. Cache GUI/GFF tag lookups used by `GUI_FindAndBindControlByTag`.
4. Reduce or defer expensive GUI construction done inside `ModuleChunkLoadCore`.
5. Only after the above, implement a safer `ResourceLoadFromArchive` wrapper that avoids engine archive handle pinning.

My recommendation is to stop pursuing archive handle pinning, GUI object preservation, or broad loading-screen/GUI lifecycle shortcuts unless fresh exclusive profiling proves a very specific and isolated target. Both attempted archive designs crossed engine ownership boundaries and caused crashes. Ghidra confirms the handle path is delicate and the expected gain is smaller than initially estimated.

## Current Project State

The project currently implements a `dinput8.dll` proxy that loads the real system DirectInput DLL and installs MinHook hooks into the Steam Windows build of KOTOR2.

The codebase currently contains:

- A large timing hook surface in `dinput8.cpp`.
- Reverse engineering notes in `README.md`.
- Historical timing summaries in `base_timing_log.txt`.
- A parser script in `loadingscreen_timeparse.py`.
- Built output files including `dinput8.dll`.

Important current flags in `dinput8.cpp`:

```cpp
#define SKIP_PRELOAD_INITIAL_ASSETS_WRAPPER 1
#define SKIP_LOADING_SCREEN_UPDATE_FRAME_IN_MODULE_CHUNK_LOAD_CORE 0
#define KEEP_ARCHIVE_OPEN_DURING_LOAD 0
#define TRACE_ARCHIVE_REFCOUNTS 1
```

Current meaning:

- Initial splash/preload skip is enabled.
- Loading-screen frame skipping is disabled.
- Archive handle pinning is disabled.
- Archive refcount tracing is still enabled.

Important active risk:

The source still contains GUI preservation logic around `ModuleHandler(8)` teardown and `ModuleChunkLoadCore` snapshotting. This code swaps GUI object vtables to suppress destructor behavior, restores old GUI pointers, and sets the game flag at `CClientExoApp + 0x128` so the next `ModuleChunkLoadCore` skips construction.

This is high risk. Ghidra shows packet-8/module teardown resets broader engine state, not only GUI slot pointers. Preserved GUI objects may keep stale pointers into old layout data, resource data, GUI context data, or module-owned state.

## Most Recent Timing Data

Latest focused resource timing provided:

| Function | Total Time | Calls | Average |
|---|---:|---:|---:|
| `ResourceEnsureLoaded` | 613.13 ms | 5,691 | 107.74 us |
| `ResourceLoadFromArchive` | 310.91 ms | 1,274 | 244.04 us |
| `ResourceLoadFromArchiveSlot` | 204.71 ms | 2,472 | 82.81 us |
| `LooseFileOpen` | 188.39 ms | 1,462 | 128.86 us |
| `LooseFileRead` | 118.26 ms | 14,147 | 8.36 us |
| `ResourceLoadFromLooseFile` | 28.49 ms | 89 | 320.13 us |
| `ResourceLoadMemoryBacked` | 0.12 ms | 13 | 8.92 us |
| `ResourceFinalizeAsyncLoad` | no data | 0 | n/a |

Interpretation:

- The most frequently used backend is `ResourceLoadFromArchiveSlot`, with 2,472 samples.
- The most expensive resource backend by total time is `ResourceLoadFromArchive`, with 310.91 ms.
- `ResourceLoadFromLooseFile` has the worst average among backends, but only 89 samples.
- `ResourceFinalizeAsyncLoad` did not appear, which strongly suggests the measured path is synchronous.
- `ResourceLoadFromArchive` is about 50.7% of `ResourceEnsureLoaded` time in this run, but still only about 311 ms absolute.

Older broader timing data also showed large inclusive totals in:

| Function | Total Time |
|---|---:|
| `loadingscreen` | 2,964.01 ms |
| `ProcessResourceQueue` | 2,841.91 ms |
| `ResourceQueue_UnpackAndTrace` | 2,640.89 ms |
| `PpacketHandler` | 2,639.37 ms |
| `ModuleHandler` | 1,962.44 ms |
| `ModuleChunkLoadCore` | 1,639.51 ms |
| `LoadingScreenUpdateFrame` | 730.97 ms |
| `InitializeGameUI` | 522.59 ms |
| `GameObjUpdate` | 330.17 ms |
| `ModuleDirectoryScanner` | 144.28 ms |
| `fopen` | 184.27 ms |

These older totals are inclusive. They are useful for attribution, but they should not be added together directly.

## Ghidra Findings

### `ResourceEnsureLoaded`

`ResourceEnsureLoaded` is a dispatcher. It checks whether a resource entry is already resident. If not, it dispatches based on the high bits of the packed resource id:

```text
ResourceEnsureLoaded
  case 0 -> ResourceLoadFromArchiveSlot
  case 1 -> ResourceLoadMemoryBacked
  case 2 -> ResourceLoadFromArchive
  case 3 -> ResourceLoadFromLooseFile
```

If the load succeeds, it bumps the resource refcount and returns the loaded data pointer.

The async path exists, but the current timing data shows no `ResourceFinalizeAsyncLoad` samples, and Ghidra shows `ResourceEnsureLoaded` normally calling the resource backend with `asyncFlag = 0`.

### `ResourceLoadFromArchive`

`ResourceLoadFromArchive` handles ERF, MOD, and HAK encapsulated archive resources. It is used when the top resource id bits select source case 2.

Architecture:

```text
ResourceLoadFromArchive(resourceEntry, asyncFlag)
  validate entry
  walk resource-manager source list
  find source whose archive id matches resourceEntry packed id
  AddRefSyncOpen archive reader
  GetResourceSize(resourceIndex)
  Resource_AllocateLoadBuffer(resourceEntry)
  ReadResourceSync(packedResourceId, destination, size)
  ReleaseSyncClose archive reader
  call resource entry parse callback
  mark loaded flag depending on parse result
```

Important details:

- The archive list walk finds the archive by comparing the source id at `source + 0x28` to bits from the packed resource id.
- The archive reader object is stored through the source node at `source + 0x30`.
- `GetResourceSize` is just an indexed table lookup.
- `ReadResourceSync` masks the packed id down to a 14-bit resource index, seeks to the table offset, and calls `LooseFileRead`.
- The type-specific parse callback happens after read and release.

### Archive Reader Vtable Path

For `CExoEncapsulatedFile`, the relevant vtable slots are:

| Vtable Offset | Behavior |
|---:|---|
| `+0x04` | `ArchiveReaderShared_AddRefSyncOpen` |
| `+0x0c` | close sync handle |
| `+0x14` | `ReleaseSyncClose` |
| `+0x24` | `GetResourceSize` |
| `+0x2c` | `OpenSyncHandle` |
| `+0x34` | `ReadResourceSync` |

`ArchiveReaderShared_AddRefSyncOpen`:

```cpp
if (reader->syncRefCount < 1) {
    reader->syncRefCount = 0;
    reader->OpenSyncHandle();
}
reader->syncRefCount++;
```

`ReleaseSyncClose`:

```cpp
if (reader->syncRefCount < 2) {
    reader->syncRefCount = 0;
    reader->CloseSyncHandle();
} else {
    reader->syncRefCount--;
}
```

This explains the repeated open/close pattern. In synchronous single-resource loads, the refcount often cycles:

```text
0 -> open -> 1 -> close -> 0
```

### Correction To Earlier Archive Hypothesis

Earlier notes suggested `OpenSyncHandle` might rebuild or reparse the full ERF/MOD/HAK resource table per resource. Ghidra does not support that.

The archive header/resource table appears to be built elsewhere and held in the reader object. In the hot load path:

- `GetResourceSize` reads from `reader + 0x3c`.
- `ReadResourceSync` reads offset/size from the same table.
- `OpenSyncHandle` opens a file wrapper and validates that the file handle exists.

This means handle pinning can remove repeated open/close overhead, but it cannot remove resource table construction per resource because that table construction is not happening in the hot path.

That lowers the realistic speedup estimate.

### `ModuleChunkLoadCore`

`ModuleChunkLoadCore` is one of the largest visible load-phase functions. It constructs many GUI objects, writes them into `CClientExoApp` slots, updates the load bar, and calls `LoadingScreenUpdateFrame` between construction groups.

High-level structure:

```text
ModuleChunkLoadCore
  if appState + 0x128 == 0:
    ensure GUI context
    construct debug/load menus
    update load bar
    LoadingScreenUpdateFrame(..., 0, 0)

    construct dialog/container/messagebox/tutorial panels
    update load bar
    LoadingScreenUpdateFrame(..., 0, 0)

    construct in-game menu/equip/inventory/store/messages panels
    update load bar
    LoadingScreenUpdateFrame(..., 0, 0)

    construct character/status/main UI
    InitializeGameUI
    update load bar
    LoadingScreenUpdateFrame(..., 0, 0)

    construct map/abilities/journal/options/party/galaxy map panels
    update load bar
    LoadingScreenUpdateFrame(..., 0, 0)

    appState + 0x128 = 1
```

This explains why GUI construction and loading-screen frame work are strong candidates.

### `LoadingScreenUpdateFrame`

`LoadingScreenUpdateFrame` does more than draw a progress screen:

- Updates frame/memory state.
- Updates/render GUI context.
- Optionally performs renderer/present work.
- Pumps Windows messages.
- If `param2 == 1`, it also calls the resource queue path and drains work.
- Calls subsystem readiness/update work.
- Performs another GUI context update/render at the end.

Inside `ModuleChunkLoadCore`, Ghidra shows calls using:

```cpp
LoadingScreenUpdateFrame(_DAT_00986da8, 0, 0);
```

Because `param2 == 0`, those calls do not take the explicit resource queue pumping branch. They still do render, message pump, GUI update, and subsystem work.

This creates a possible speedup target: skip, throttle, or partially replace only the `param2 == 0` frame updates inside `ModuleChunkLoadCore`.

Important implementation issue:

The current code has a `g_moduleChunkLoadCoreDepth` variable intended to detect calls from inside `ModuleChunkLoadCore`, but the hook does not currently increment/decrement it. Therefore, simply enabling `SKIP_LOADING_SCREEN_UPDATE_FRAME_IN_MODULE_CHUNK_LOAD_CORE` would not work correctly until that depth tracking is fixed.

### `GUI_FindAndBindControlByTag`

`GUI_BindNamedWidget` calls `GUI_FindAndBindControlByTag`.

Ghidra shows `GUI_FindAndBindControlByTag`:

- Reads the control count.
- Loops every control.
- Gets each control node.
- Reads the `TAG` field through GFF helper functions.
- Compares the tag against the requested widget name.
- Calls the panel bind virtual function when found.

This is a repeated linear search through GFF control lists.

From prior timings:

- `GUI_BindNamedWidget`: 717.59 ms over 2,997 calls.
- `GUI_FindAndBindControlByTag`: 670.19 ms over 2,997 calls.

These are nested, not additive. The repeated tag lookup appears to be most of the cost.

## Why Current Attempts Did Not Work

### Archive Handle Pinning

Two approaches were attempted:

1. Skipping `ReleaseSyncClose` during load and flushing later.
2. Taking an extra anchor ref with `AddRefSyncOpen` and later releasing that anchor.

The first approach crashed on second load. The second made the game unable to complete one load.

Based on Ghidra, this makes sense. The archive reader owns internal state:

- Sync refcount.
- Open file wrapper pointer.
- Open-state flag.
- Archive table pointer.
- File type/state fields.

Holding or suppressing close operations across module teardown risks stale internal state. The game expects its own refcount/open lifecycle to match exact resource load calls. Even if the pointer stays valid, its file wrapper or teardown expectations may not.

Conclusion:

Archive handle pinning is a poor risk/reward target right now. It is cheap in concept, but not cheap in engine semantics.

### GUI Preservation

The current GUI preservation experiment tries to save load time by preventing GUI destructors and restoring old GUI pointers after teardown.

The idea is attractive because `ModuleChunkLoadCore` spends a lot of time constructing GUI panels. However, the implementation crosses a dangerous lifecycle boundary:

- It swaps object vtables to no-op destructors.
- It allows teardown to run while object internals are preserved.
- It restores old pointers after teardown resets broader state.
- It forces `CClientExoApp + 0x128 = 1` so constructors are skipped.

This can crash because GUI objects are not self-contained. They likely reference:

- GFF layout nodes.
- Resource buffers.
- GUI context structures.
- Child controls.
- Engine arrays/lists.
- Callback registrations.
- Module or app-state data reset during teardown.

Conclusion:

The concept may save time, but this specific object preservation method is too invasive. A safer version would defer construction or cache lookup data, not preserve live GUI instances across teardown.

## Speedup Propositions

### Proposition 1: Exclusive Aggregate Profiler

Priority: Highest  
Difficulty: Medium-low  
Risk: Low  
Expected improvement by itself: None directly, but prevents bad decisions  
Expected project value: Very high

Current hook logging is per-call and flushes frequently. Parent timings are inclusive, so nested work is counted multiple times. This makes it hard to distinguish real exclusive cost from instrumentation noise.

Implement a thread-local scoped profiler:

- On hook entry, push a timing frame.
- On hook exit, pop it.
- Measure total elapsed.
- Add elapsed time to the parent frame's child time.
- Exclusive time equals elapsed minus child time.
- Aggregate totals in memory.
- Write one summary at unload or load-session end.

This should include:

- Total time.
- Exclusive time.
- Inclusive time.
- Call count.
- Average inclusive.
- Average exclusive.
- Max call.

Why this matters:

Before more invasive work, we need to know whether `ResourceLoadFromArchive`, GUI binding, loading-frame rendering, parse callbacks, or logging itself is the true wall-clock target.

### Proposition 2: Disable Risky Experimental Behavior For Baseline

Priority: Highest  
Difficulty: Low  
Risk: Low  
Expected improvement: Stability and clean data

Before running more timing tests:

1. Keep `KEEP_ARCHIVE_OPEN_DURING_LOAD` set to `0`.
2. Set `TRACE_ARCHIVE_REFCOUNTS` to `0` unless doing a dedicated diagnostic run.
3. Put GUI preservation behind a new macro, for example:

```cpp
#define PRESERVE_GUI_OBJECTS_ACROSS_LOADS 0
```

4. Ensure all vtable-swapping and pointer restoration behavior is disabled when that macro is `0`.
5. Rebuild and confirm the game can complete repeated loads without the experimental preservation path.

This is not a speedup itself, but it is required to prevent misattributing crashes and timing changes.

### Proposition 3: Loading Screen Frame Debounce

Priority: High  
Difficulty: Medium  
Risk: Medium-low if guarded carefully  
Expected improvement: 200-800 ms, possibly more on systems with expensive present/render work

Ghidra shows `ModuleChunkLoadCore` calls `LoadingScreenUpdateFrame(..., 0, 0)` five times during GUI construction. The `param2 == 0` form does not run the explicit resource queue pump, but it still renders and pumps messages.

Safe implementation approach:

1. Fix `g_moduleChunkLoadCoreDepth`:

```cpp
g_moduleChunkLoadCoreDepth++;
uint32_t result = g_originalModuleChunkLoadCore(param1);
g_moduleChunkLoadCoreDepth--;
```

Use a `try/finally`-style guard pattern if possible, or a small RAII struct.

2. In `Hook_LoadingScreenUpdateFrame`, only consider skipping if:

```cpp
g_moduleChunkLoadCoreDepth > 0 && param2 == 0
```

3. Start with throttling instead of full skipping:

- Allow first frame.
- Skip frames occurring within a short threshold, such as 50-100 ms.
- Always allow the last frame if we can identify it, or allow every Nth call.

4. Keep `WindowsMessagePump` concern in mind. If skipping causes the window to freeze or Windows reports not responding, replace full skip with a lighter custom path that pumps messages but does not render.

5. Measure:

- Load time.
- Visual behavior.
- Whether resources still load correctly.
- Whether the progress bar behaves acceptably.

Why this is promising:

It avoids resource ownership and GUI object lifetime. It changes how often the loading screen renders during a synchronous construction phase.

### Proposition 4: GUI/GFF Tag Lookup Cache

Priority: High  
Difficulty: Medium  
Risk: Medium  
Expected improvement: 300-600 ms if prior timing holds

`GUI_FindAndBindControlByTag` performs repeated linear searches across GFF control lists. This is a classic cache target.

Safer version:

- Cache lookup results for `(controlsListPtr, requestedTag) -> controlNode`.
- Keep the cache only for the current GUI layout/load phase.
- Clear it on module teardown, GUI context change, or load-session boundary.
- Do not preserve GUI objects.
- Do not bypass the panel bind virtual function.
- Only bypass the repeated search.

Implementation outline:

1. Identify the stable representation of `requestedTag`.
2. Convert or hash the requested tag to a normal string or stable engine-string key.
3. On `GUI_FindAndBindControlByTag` entry:
   - Build key from `controlsListPtr` plus tag.
   - If cached, call the same bind vfunc the original would call:

```cpp
(**(code **)(*panel + 0x48))(param2, cachedControlNode);
```

4. If not cached:
   - Call original.
   - Ideally capture which control matched.

Hard part:

The current hook wraps the whole function, but the function does not return the matched control node. To cache precisely, we may need to:

- Reimplement the search in our hook using the same GFF helper functions, or
- Hook the panel bind vfunc/callback to observe the matched node, or
- Add deeper hooks around the GFF field read/compare path.

Best first experiment:

Reimplement only the search logic in the hook for a small controlled subset, then fall back to original on any uncertainty.

Why this is promising:

It targets repeated CPU work without changing resource ownership, archive handles, or GUI object lifetime.

### Proposition 5: Defer Some GUI Construction

Priority: Medium-high  
Difficulty: Medium-high  
Risk: Medium-high  
Expected improvement: 300-900 ms depending on how many panels can be deferred

`ModuleChunkLoadCore` pre-constructs many panels so gameplay avoids UI stutter later. README notes a function `CSWGuiInGamePanel_LazyInitTab` that lazily initializes in-game tabs. That indicates the engine already has some concept of deferred GUI construction.

Safer direction:

- Do not preserve old GUI objects.
- Let teardown happen normally.
- Skip construction of panels that are not needed immediately after load.
- Allow the game's existing lazy-init path to construct them on first use.

Candidate panels:

- Galaxy map.
- Journal.
- Options.
- Party selection.
- Abilities.
- Store, if not needed immediately.
- Debug/load module menus in non-debug usage.

Implementation approach:

1. Map each `CClientExoApp` GUI slot to panel type.
2. Identify which panels are required immediately after load.
3. For non-required panels, patch or hook constructor call sites to leave the slot null.
4. Confirm later access paths check for null and call lazy initializer.
5. If not, hook the access path to create on demand.

Risk:

Some code may assume these slots are non-null after `ModuleChunkLoadCore`. This needs careful per-panel validation.

Why this is better than GUI preservation:

It respects teardown and object ownership. The cost moves from loading screen to first UI access instead of keeping stale objects alive.

### Proposition 6: Module Directory Scanner Cache

Priority: Medium  
Difficulty: Low-medium  
Risk: Low-medium  
Expected improvement: 50-150 ms based on older timing

`ModuleDirectoryScanner` showed 144.28 ms in older timing. If it is rescanning the same directories repeatedly, cache results per directory/filter.

Implementation approach:

1. Log input parameters and resolved directory/filter behavior.
2. Confirm whether calls repeat identical scans.
3. Cache output list for repeated inputs.
4. Invalidate only on module list change or process start.

This is smaller than GUI work, but safer.

### Proposition 7: Safer `ResourceLoadFromArchive` Wrapper

Priority: Medium  
Difficulty: Medium-high to high  
Risk: Medium-high  
Expected improvement: 100-250 ms in the latest measured run

Do not pin game archive handles.

Instead, write a wrapper that keeps engine semantics intact:

- Use the game resource entry.
- Use the game allocation path.
- Use the game parse callback.
- Avoid only the repeated engine file open/read path when safe.

Possible architecture:

```text
Hook_ResourceLoadFromArchive
  validate sync path only
  locate archive source node as original does
  resolve archive filename/type from CExoEncapsulatedFile
  use our own cached OS file handle or memory map
  read bytes at archive table offset into game-allocated buffer
  call original parse callback
  set loaded flag exactly as original does
  fall back to original on any uncertainty
```

Important:

- This must not use or mutate the game's archive sync refcount.
- This must not reuse game file wrapper objects.
- This must preserve `Resource_AllocateLoadBuffer`.
- This must preserve the parser callback at `resourceEntry->vtable + 0x10`.
- This must preserve loaded flag behavior.

The safest first version may hook lower:

```text
CExoEncapsulatedFile_ReadResourceSync
  if cache hit:
    memcpy cached bytes into destination
    return requested size
  else:
    call original
```

However, this lower hook does not avoid `AddRefSyncOpen`/`OpenSyncHandle`, so the speedup may be limited. A full wrapper saves more but is riskier.

## Expected Speedup Ranges

These are rough estimates based on current data and Ghidra structure:

| Strategy | Expected Gain | Risk | Confidence |
|---|---:|---|---|
| Exclusive aggregate profiler | 0 ms direct | Low | High |
| Disable risky experiments for baseline | Stability | Low | High |
| Loading-screen frame debounce | 200-800 ms | Medium-low | Medium |
| GUI/GFF tag lookup cache | 300-600 ms | Medium | Medium |
| GUI construction deferral | 300-900 ms | Medium-high | Medium-low |
| Module directory scanner cache | 50-150 ms | Low-medium | Medium |
| Safer archive read wrapper | 100-250 ms | Medium-high | Medium |
| Archive handle pinning | 0-180 ms theoretical | High | Low after crashes |

Best realistic near-term target:

```text
500 ms to 1.2 s improvement
```

This would likely come from combining loading-frame debounce, GUI/GFF lookup caching, and small directory/debug-menu improvements.

More aggressive target:

```text
1.0 s to 2.0 s improvement
```

This likely requires deferring GUI panel construction and/or implementing a safe archive read wrapper.

## Recommended Step-by-Step Plan

### Phase 0: Stabilize The Build

Goal: Get back to a clean, repeatable baseline.

Steps:

1. Add macro:

```cpp
#define PRESERVE_GUI_OBJECTS_ACROSS_LOADS 0
```

2. Wrap all GUI preservation behavior in that macro:

- Pre-teardown vtable swap in `Hook_ModuleHandler`.
- Post-teardown pointer restore in `Hook_ModuleHandler`.
- GUI slot snapshot in `Hook_ModuleChunkLoadCore`.
- Any code that forces `CClientExoApp + 0x128 = 1`.

3. Set:

```cpp
#define KEEP_ARCHIVE_OPEN_DURING_LOAD 0
#define TRACE_ARCHIVE_REFCOUNTS 0
```

4. Rebuild.
5. Run:
   - First load.
   - Same-save reload.
   - Module transition.
6. Confirm no crash.

Deliverable:

- Stable DLL baseline.
- Clean timing log with no experimental behavior.

### Phase 1: Implement Exclusive Profiler

Goal: Stop making decisions from inclusive totals.

Steps:

1. Define a small profiler record:

```cpp
struct ProfileStat {
    uint64_t inclusiveUs;
    uint64_t exclusiveUs;
    uint64_t maxInclusiveUs;
    uint64_t calls;
};
```

2. Define a thread-local stack frame:

```cpp
struct ProfileFrame {
    const char* name;
    uint64_t startUs;
    uint64_t childUs;
};
```

3. On scope entry:
   - Push frame with current timestamp.

4. On scope exit:
   - Compute elapsed.
   - exclusive = elapsed - childUs.
   - Add elapsed to parent childUs.
   - Aggregate stats.

5. Replace hot per-call log lines with profiler scopes.
6. Dump summary once per load session or DLL detach.

Minimum hooks to include:

- `loadingscreen`
- `ProcessResourceQueue`
- `ResourceQueue_UnpackAndTrace`
- `PpacketHandler`
- `ModuleHandler`
- `ModuleChunkLoadCore`
- `LoadingScreenUpdateFrame`
- `ResourceEnsureLoaded`
- `ResourceLoadFromArchive`
- `ResourceLoadFromArchiveSlot`
- `ResourceLoadFromLooseFile`
- `LooseFileOpen`
- `LooseFileRead`
- `GUI_BindNamedWidget`
- `GUI_FindAndBindControlByTag`
- `InitializeGameUI`
- Major GUI constructors
- `ModuleDirectoryScanner`

Deliverable:

- `profile_summary.txt` or appended summary in `kotor2_log.txt`.
- Exclusive and inclusive timing table.

### Phase 2: Loading Screen Frame Debounce Experiment

Goal: Reduce repeated render/update work inside `ModuleChunkLoadCore`.

Steps:

1. Fix `g_moduleChunkLoadCoreDepth`.
2. Add a macro:

```cpp
#define DEBOUNCE_MODULE_LOAD_SCREEN_FRAMES 1
```

3. In `Hook_LoadingScreenUpdateFrame`, when inside `ModuleChunkLoadCore` and `param2 == 0`:
   - Allow the first frame.
   - Skip or throttle frames that occur too soon after the last allowed frame.
   - Always fall back to original when not inside `ModuleChunkLoadCore`.

4. Test visually:
   - Loading screen still appears.
   - Game does not hang.
   - Window remains responsive.

5. Measure exclusive time saved.

Rollback condition:

- If resource loading stalls, the window hangs, or the game becomes unstable, replace full skip with a light message-pump-only path.

Deliverable:

- Timing comparison with and without debounce.

### Phase 3: GUI/GFF Lookup Cache

Goal: Remove repeated linear GFF tag scans.

Steps:

1. Instrument `GUI_FindAndBindControlByTag` more deeply:
   - Log/control count distribution.
   - Log requested tag distribution.
   - Identify repeated `(controlsListPtr, tag)` pairs.

2. Build a temporary diagnostic cache that only records keys and hit potential. Do not change behavior yet.

3. If hit rate is high, implement lookup cache:
   - Key: `controlsListPtr + requestedTag`.
   - Value: matched control node.
   - Lifetime: current GUI layout/load phase.

4. On cache hit:
   - Call the same bind vfunc the original calls.
   - Return without running the linear scan.

5. On cache miss:
   - Run original or reimplemented safe search.
   - Store match if known.

6. Clear cache on:
   - Module teardown.
   - GUI context change.
   - Load session start/end.

Deliverable:

- Cache hit-rate report.
- Timing comparison for `GUI_FindAndBindControlByTag` exclusive time.

### Phase 4: Low-Risk Cleanup Wins

Goal: Pick up safe milliseconds after the big two experiments.

Steps:

1. `ModuleDirectoryScanner`
   - Log repeated scan inputs.
   - Cache stable outputs if repeated.

2. Debug menu constructors
   - Confirm whether debug-only panels are needed in normal play.
   - Replace with no-op only if downstream code accepts null.

3. Logging overhead
   - Keep aggregate profiler.
   - Avoid flushing every line.
   - Optionally write only load-session summaries.

Deliverable:

- Small-win patch set with low crash risk.

### Phase 5: GUI Construction Deferral

Goal: Stop building panels during load that are not needed immediately.

Steps:

1. Produce a slot map:
   - `CClientExoApp` offset.
   - Panel constructor.
   - Whether it is required immediately.
   - Whether a lazy initializer exists.

2. Start with one non-critical panel:
   - Galaxy map or journal is a good candidate.

3. Prevent construction during `ModuleChunkLoadCore`.
4. Ensure first access constructs it safely.
5. Test:
   - Load.
   - Open the relevant UI.
   - Close/reopen.
   - Module transition.
   - Same-save reload.

6. Repeat panel by panel.

Deliverable:

- Per-panel deferral table.
- Timing gain per deferred panel.

### Phase 6: Safer Resource Archive Wrapper

Goal: Improve archive load time without touching game archive handle lifecycle.

Steps:

1. Reimplement only enough of `ResourceLoadFromArchive` to locate:
   - Archive source node.
   - Reader object.
   - Resource index.
   - Resource offset and size from reader table.

2. Open our own file handle or file mapping for the archive.
3. Keep our handle cache separate from game reader state.
4. Let the game allocate:

```cpp
Resource_AllocateLoadBuffer(resourceEntry)
```

5. Read bytes directly into the game allocation.
6. Call the resource entry parse callback.
7. Set loaded flag exactly as original does.
8. Fall back to original on any uncertain state.

Validation:

- Compare return values.
- Compare loaded flag behavior.
- Compare resource data pointer behavior.
- Test many resource types.
- Test repeated loads and module changes.

Deliverable:

- Archive wrapper behind a macro.
- Timing report with fallback counts and hit counts.

## Suggested Immediate Next Actions

I recommend the next work session do the following in order:

1. Add `PRESERVE_GUI_OBJECTS_ACROSS_LOADS 0` and disable the active GUI preservation behavior.
2. Set `TRACE_ARCHIVE_REFCOUNTS 0`.
3. Build and confirm repeated loads are stable.
4. Implement the exclusive aggregate profiler.
5. Run three clean timing captures:
   - First load.
   - Same-save reload.
   - Module transition.
6. Choose between loading-frame debounce and GUI/GFF lookup cache based on exclusive data.

My current preferred first optimization after profiling is loading-frame debounce because it is easier to isolate and has less ownership risk than GUI object reuse or archive handle manipulation.

## Final Assessment

The project has made real progress because we now understand the loading path better:

- `ResourceEnsureLoaded` is a dispatcher.
- `ResourceLoadFromArchive` is synchronous and real, but has a limited total ceiling.
- Archive handle pinning is risky and no longer looks worth pursuing first.
- `ModuleChunkLoadCore` is a major load-phase coordinator and GUI construction hub.
- `LoadingScreenUpdateFrame` inside `ModuleChunkLoadCore` is likely a removable or reducible cost.
- `GUI_FindAndBindControlByTag` is a repeated linear GFF lookup and likely a strong cache target.

The fastest path to a meaningful improvement is not a full resource system replacement. It is disciplined measurement, then removing repeated main-thread work that does not own fragile engine resources.

Recommended direction:

```text
Stabilize -> exclusive profiler -> loading-frame debounce -> GUI/GFF lookup cache -> selective GUI deferral -> archive wrapper
```

This path gives us the best chance of getting a real load-time improvement without continuing to crash the game during teardown or second-load scenarios.
