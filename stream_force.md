# Forced area streaming during load (scenario G)

**Merged to `main` as the default in every scenario on 2026-09-25.** Scenario G no longer
exists; set `FORCE_AREA_STREAM_DURING_LOAD 0` for a control run.

Single-variable experiment against scenario A: every other optimization toggle is off.
`FORCE_AREA_STREAM_DURING_LOAD` is 1 only in `AB_SCENARIO_G`, and `ProfilerRunStart`
logs `scenario=g_streamforce ... stream_force=1`.

## What it changes

`load_phases.md` measured the loading-screen tail on the 101PER reload: 9 object-update
messages, 202-203 ms apart, 1.90 s of throttle wait and about 2 ms of send work. The
server builds each message only when `Server_UpdateClient_Throttle200ms` (0x00537590)
sees 200 ms since the last one, unless its `force` argument is 1.

The existing LoadPhases tick hook now passes `force = 1` while `player+0x24 == 1`:

- `+0x24` is the player's area-load state. It is 1 while the area streams, and
  `Server_HandleAreaMsg` (0x00660600) sets it to 2 when the client's area-loaded ack
  (P(4,3)) arrives. Gameplay (state 2) keeps the 200 ms rate.
- Force skips only the time compare. The function still checks that the player is
  updatable first (`+0x7c == 1`, creature `+0x350 != 0x7f000000`), so nothing is sent
  earlier than the engine allows. It only stops waiting between messages.
- The engine passes force = 1 itself from `FUN_0089fbd0`, so this path is already used.

MinHook allows one detour per address, so the force lives in a shared helper
(`StreamForceFlag`) that two detours call: the LoadPhases tick hook when logging is
on, and a bare `Hook_UpdateClientForce` when `LOGGING_ENABLED=0`. The force works in
both builds.

## Prediction (written before the first run)

On the 101PER quicksave reload, compared with the scenario-F numbers in `load_phases.md`:

- `stream_forced` > 0 (it was 0). `stream_sent` and `stream_msgs` about 9, same as before.
- `msg_gap_mean_us` about one frame instead of ~202,000 (≈6 ms at 170 fps, ≈33 ms at 30 fps).
- `stream_us` / `throttle_wait_us` from ~1.9 s to about 0.1-0.3 s. The first message
  was ~273 ms after the server started running; that part may stay if the player isn't
  updatable yet.
- `stream_bytes` about 18.7 KB, the same data sent sooner. `last_progress` still 14 of 14.
- `work_us`, `area_to_onenter_us` and the scripted black window (`script_delay_us` 2.0 s,
  fade hold 1.0 s) unchanged.
- `VisualLoad` total down by about 1.6 s per reload, taken out of the `loading_stale` run.

## What to watch

- A few long frames during the stream instead of a steady bar: the client may build
  models and textures per message, and those would now land in back-to-back frames.
  Check `LoadPhaseMsg dur_us` and the `FrameRun` rows.
- `hs_*` ticks can also be forced (the state is already 1 in the handshake); `hs_msgs`
  shows whether any message went out before the server was running.
- After the reload: party members, placeables and creatures in place, no missing objects,
  no desync on the first area transition. Same data should mean the same game state.
- Levels with more objects need more messages, so the saving should grow with level size.
  One reload of a large level (e.g. Nar Shaddaa) is worth logging.

## Result (2026-09-25, run_id 151561689651388, one load from the menu plus three reloads)

`ProfilerRunStart` confirms `scenario=g_streamforce stream_force=1`. All hooks installed.

| | Before (scenario F, `load_phases.md`) | Scenario G |
|---|---|---|
| `stream_us` (server running → area-loaded ack) | 1.90-1.91 s | 0.548-0.565 s |
| `stream_msgs` / `stream_bytes` | 9 / 18,705 | 9 / 18,705-18,727 |
| `stream_forced` | 0 | 10 (every stream tick) |
| Gap between messages 2-8 | ~202 ms | 9-16 ms |
| `area_to_onenter_us` | ~1 ms | ~1 ms |
| Script delay / fade hold / fade | 2.00 / 1.0 / 1.0 s | 2.00 / 1.0 / 1.0 s (2.21 s on the first load, as before) |
| Messages after the ack | 200 ms apart | 200 ms apart (`held_before` ~34), so gameplay is unaffected |

The stream is 1.34 s shorter, about 70%. The prediction of 0.1-0.3 s missed: two stretches
are not the throttle, and together they make up about 470 ms of the remaining 560 ms.

- About 270 ms from server running to the first message, with only one tick in that time.
  The tick was held, meaning the player was not yet updatable.
- About 185-200 ms between message 0 and message 1 (both carry stage 7), with **no** server
  ticks at all (`held_before=0`). The loading screen still presented ~16 frames in that time.

With force on, every server tick sends. No ticks means the server loop (`loadingscreen`,
0x00533830, the only caller of `Server_UpdateAllClients` besides `FUN_0089fbd0`) is not
running. Something else holds the main thread while redrawing the loading screen, most
likely the client processing the first large message (stage 7). The gap varies from 184 to
200 ms, so it looks like work rather than a timer. The throttle used to hide it: it ran in
parallel with the 200 ms waits.

`work_us` here is 0.83-0.85 s per reload, against 9.4-11.4 s in the scenario F run, even
though the bytes show the same area. The change cannot affect anything before finalize, so
that difference comes from the other branch's build or the machine state, not this
experiment. **The total `VisualLoad` numbers (4.43-4.46 s per reload) need a scenario A run
from this branch before they can be compared.** Only the stream phase is compared above.

Game state after the reloads: nothing looked off in play (checked by hand, 2026-09-25).
Not yet checked: a larger level.

A second run the same night (run_id 115398065698256) repeated it: stream 0.548-0.583 s,
10 forced ticks per load.

## Remaining gaps: where the main thread is (instrumented and run 2026-09-25)

`GameMain` (0x00408120) does, per frame: `CClientExoApp_MainLoopTick` (client), then
`loadingscreenwrapper` → `loadingscreen` (0x00533830, the server tick, which calls
`Server_UpdateAllClients`), then `SwapBuffers`. With the force on, every server tick in
the stream sends a message, so a stretch with **no** tick means `GameMain` is stuck inside
one client tick. The loading screen keeps drawing because the client work calls
`LoadingScreenUpdateFrame` (0x00409ed0) itself. That function runs the server tick only
when its `runLoadingScreenWork` argument is 1, and it has ~50 call sites, including five
in `ModuleChunkLoadCore` and one in `Client_OnAreaLoadProgressW`.

New log-only instrumentation (`LP_SAMPLE_MAIN_THREAD`):

- A side thread suspends the main thread about every 1 ms from finalize to the area-loaded
  ack, and records EIP and up to 16 return addresses in the game image. It uses the EBP
  chain, or a call-validated stack scan when the chain breaks inside a driver. Nothing is
  allocated while the thread is suspended. Output: `LoadPhaseSamples:` (count plus total
  suspended time) and one `LoadPhaseSample:` line per sample.
- `modchunk_enter` / `modchunk_exit` events for the outermost `ModuleChunkLoadCore` call.
- `python lp_samples.py [log]` splits the samples into handshake / gap1 / gap2 / rest and
  lists the return addresses present in the most samples, to resolve in Ghidra.

Prediction:

- gap1 (~270 ms, server running → first message): `ModuleChunkLoadCore` (~258 ms per load
  in the logs) runs inside it. `modchunk_enter`/`exit` bracket most of gap1, and most gap1
  samples have a `ModuleChunkLoadCore` return address on the stack.
- gap2 (~190 ms, message 0 → message 1): the client handling message 0 (stage 7, 2.2 KB).
  Samples sit under the client's object-update handler, most likely loading models or
  textures for the objects in that message, not in `ModuleChunkLoadCore`.
- Sampling cost: `suspended_us` a few ms per load. The stream should stay about 0.55 s.

### Result (run_id 77636713844027, four 101PER loads)

The sampler was stable: 292-313 samples per load (one per ~1.93 ms), 11-13 ms of total
suspension per load, stream 0.56-0.60 s, the same as without it. `modchunk_enter`/`exit`
land 10 ms after the server starts running and end ~270 ms later, 1-2 ms before the first
message. So gap 1 **is** `ModuleChunkLoadCore`, as predicted. But inside it, most of the
time is texture uploads, not module loading.

Mean per load, in ms (`lp_samples.py --funcs`, categories by stack content):

| | gap 1 (286) | gap 2 (193) | rest (95) |
|---|---|---|---|
| `gluBuild2DMipmaps` (CPU mipmaps) | **149** | 0 | 0 |
| other pending-texture uploads | 34 | 58 | 10 |
| waiting in `SwapBuffers` (loading-screen redraw) | 10 | 31 | 13 |
| rest of `ModuleChunkLoadCore` | 27 | - | - |
| area scene build (`FUN_007a2fb0`: mainscene, grass, anim loops) | - | 33 | - |
| other (file reads, malloc, packet handling) | 67 | 70 | 72 |

How the texture work gets there: `LoadingScreenUpdateFrame` → `FrameMetricsAndMemoryUpdate`
→ `FUN_00427f30` → `TexturePoolCleanupAndRefresh` (0x00427810). That last function is not
idle cleanup: it drains the pending-texture queue (`DAT_00a1bc30`) and uploads each entry
through `FUN_004260e0` → … → `Texture_UploadToGL` (0x00433cd0, renamed). So every load-bar
redraw pays for the textures queued since the previous one. In gap 1 the redraws come from
`ModuleChunkLoadCore` via `AppState_SetLoadBarValue`; in gap 2 from the scene build.

**The CPU mipmap path is a stale capability check.** For uncompressed mipmapped textures,
`Texture_UploadToGL` calls `GL_CanUseHardwareMipmapGen` (0x00484a60, renamed; its only
caller). If that returns 1 it sets `GL_GENERATE_MIPMAP` and calls `glTexImage2D`, so the
driver builds the mips. If it returns 0 it calls `gluBuild2DMipmaps`, which builds every
level on the CPU. The check needs `GL_SGIS_generate_mipmap` (bit 0x80000) and **not** bit
0x100000. `GL_DetectExtensions` (0x004317d0) sets 0x100000 for `GL_ATI_text_fragment_shader`
and also for `GL_ARB_fragment_program`, which every modern driver exposes. So current
hardware always gets the CPU path, most likely an old ATI-era workaround.

Gap 2's texture uploads are a different path (no `gluBuild2DMipmaps` samples there), and the
~54 ms per load in `SwapBuffers` is loading-screen redraws waiting on present.

### Candidates, largest first

1. **Force `GL_CanUseHardwareMipmapGen` to return 1** when `GL_SGIS_generate_mipmap` is present.
   It has one caller, and the engine already has the GPU path. Expected: most of the ~149 ms
   in gap 1. The load before finalize (`work_us`, ~0.84 s, not sampled) probably uploads
   textures the same way, so the total saving may be larger. Risk: mip quality differs
   slightly (driver box filter vs GLU's), and `gluBuild2DMipmaps` also rescales
   non-power-of-two images, which `glTexImage2D` now uploads as-is (fine on modern GPUs).
2. **Throttle loading-screen presents** during the stream (the existing
   `THROTTLE_LOADING_SCREEN_PRESENTS` toggle from scenario B). It keeps the redraw work but
   skips clear/swap: up to ~54 ms per load.
3. Extend the sampler window to click → ack to see how much of `work_us` is the same texture path.
