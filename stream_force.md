# Forced area streaming during load (scenario G)

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

## Result

Not run yet.
