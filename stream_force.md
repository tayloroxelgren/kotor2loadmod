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

It rides on the LoadPhases tick hook because MinHook allows one detour per address.
Building with the force on and `ENABLE_LOAD_PHASES_LOG 0` is a compile error.

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
