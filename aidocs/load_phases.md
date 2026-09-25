# Load-phase attribution (log only)

The pixel timeline (`VisualLoad`/`FrameRun`) says how long each visible stretch of a
reload lasted. `LoadPhases` says why, using engine events stamped on the same QPC
clock and emitted from `EmitVisualLoad`, so the two can be compared line for line.
It changes no behaviour: every detour forwards all arguments and returns the
original's EAX untouched. `ENABLE_LOAD_PHASES_LOG` (top of the LP block in
`dinput8.cpp`) is tied to `LOGGING_ENABLED`; a build with `LOGGING_ENABLED=0`
leaves the whole thing out.

## Hooked functions

Conventions were read from each prologue and `RET` in Ghidra on 2026-09-24.

| Event | Address | Convention |
|---|---|---|
| `ModuleLoad_FinalizeAndQueueReady` | 0x0055a650 | thiscall, `RET` |
| `Server_SendStateToClient` | 0x00647af0 | thiscall, `RET 4` |
| `HandleNetEvents` | 0x00812350 | thiscall, `RET 4` (arg = minor byte) |
| `Server_StartGame_State1to2` | 0x005361d0 | thiscall, `RET` |
| `Server_UpdateClient_Throttle200ms` | 0x00537590 | thiscall, `RET 0x10` (player, force, timeLo, timeHi) |
| `Server_BuildClientObjectUpdate_2000B` | 0x0063caa0 | thiscall, **`RET 8` (2 stack args)** |
| `Message_GetWriteMessage` (bytes) | 0x005e47e0 | thiscall, `RET 8` (out: data ptr, size) |
| `Server_WriteAreaLoadProgressW` | 0x0063cee0 | thiscall, `RET 0x10` (obj, flag, stage, total) |
| `Send_P_AreaLoaded_4_3` | 0x008798a0 | thiscall, `RET` |
| `Server_PlacePlayerInArea` | 0x00535a10 | thiscall, `RET 4` |
| `SetGlobalFadeOut` / `SetGlobalFadeIn` / `SetFadeUntilScript` | 0x00696b00 / 0x006969e0 / 0x00699db0 | thiscall, `RET 8` |
| `CSWGuiFade_SetTransitionState` | 0x007bc8f0 | existing hook, now also records `progress` |

Each new hook is signature-checked separately; a mismatch skips that one hook and logs
`LoadPhases: signature mismatch, hook skipped: <name>` instead of disabling the mod.

Two things the disassembly showed that the plan did not assume:

- The build function takes 2 stack args, not 1.
- `Server_SendStateToClient` and `Server_PlacePlayerInArea` are both called from inside
  `Server_StartGame_State1to2`. Events carry `b=1` when they fired inside State1to2, and
  the OnEnter-relevant placement is the first one at or after the client's area-loaded
  ack (`place_after_area=1`), not the first one seen.

## What one load logs

- `LoadPhases:` one key=value line (all `_us` unless noted; `-1` = event not seen).
- `LoadPhaseMarks:` offset of each first event from the click anchor (same `t0` as `VisualLoad`).
- `LoadPhaseEvent:` chronological raw events (capped at 96), so every derived number is checkable.
- `LoadPhaseMsg:` one per object-update message build: time, duration, bytes.
- `LoadPhaseTick:` one per *sent* tick, with `held_before` = held ticks since the previous sent tick.

The tick hook runs every server tick for the whole session. It does not log per call:
outside a load window it is a flag test and a forward; inside one it bumps counters and
fills fixed arrays (64 sent ticks, 64 messages, 128 events).

### Definitions

Ticks and messages are bucketed by handshake phase: `hs_*` = finalize until the server
is running (State1to2 exit); `stream_*` = server running until the client's area-loaded
ack; `post_*` = after the ack.

- `work_us`: click anchor to finalize exit.
- `handshake_us`: finalize exit to State1to2 exit.
- `stream_us`: State1to2 exit to area-loaded ack.
- `throttle_wait_us`: `stream_us` minus time inside sent ticks. Approximate: main-thread
  rendering also falls in this window.
- A tick is "sent" when `player+0x2c/+0x30` (the throttle stamp) changed across the call.
  The engine rewrites it only when the tick passes the 200 ms gate (0x00537723). "Held"
  also includes early-outs for a player that is not updatable yet; both mean no message.
- `area_to_onenter_us`: ack to `PlacePlayerInArea` entry (OnEnter is queued there).
- `script_delay_us`: first `SetGlobalFadeOut` (else the place) to `SetGlobalFadeIn`. The three
  script-fade events are stamped on handler entry, because the handler itself starts the GUI fade.
- `fade_hold_s` / `fade_s`: `progress` and duration of the first GUI fade transition at
  or after the fade-in call (both 0 if none fired; `LoadPhaseEvent gui_fade` rows show what did).
- `script_hold_us`: place to fade-in call plus `fade_hold_s`. `residual_us` is
  place-to-gameplay minus `script_hold_us`. The fade-in duration is not subtracted: the
  visual timeline's "first gameplay frame" is the first non-black frame, i.e. the START
  of the fade-in, so the picture is fully lit about `fade_s` later.
- Window: opened when the visual tracker arms and closed by its final emit. A re-arm inside
  one load (the perceived-load tracker restarts on short state-activation transitions)
  keeps the window open instead of wiping it.
- `work_us` is click anchor to finalize exit. It is the remainder of the load that none of
  the hooks explain, not a measured engine phase.

## Prediction (written before the first run)

On the 101PER quicksave reload, from the static trace in the notes:

- `stream_msgs` about 9, `msg_gap_mean_us` about 200,000, `stream_bytes` about 17 KB and at most
  2000 per message.
- `throttle_wait_us` about 1.7 s, `stream_forced` = 0, `area_to_onenter_us` under 100 ms.
- `script_delay_us` about 2.0 s, `fade_hold_s` 1.0, `fade_s` 1.0, `last_progress` 14 of 14.

## Result (2026-09-24, scenario F, three loads plus repeats)

All 13 hooks installed and fired; no signature mismatches.

| | Predicted | Measured (steady state) |
|---|---|---|
| Messages before area-loaded | ~9 | 9 |
| Gap between messages | ~200 ms | 202-203 ms |
| Throttle wait | ~1.7 s | 1.90-1.91 s (first message ~273 ms after server running, then 8 gaps) |
| Forced sends | 0 | 0 |
| Area-loaded to OnEnter | <100 ms | ~1 ms |
| Script delay / fade hold / fade | 2.0 / 1.0 / 1.0 s | 2.002 / 1.000 / 1.000 s |
| Bytes | <=2000 per message | 1,977-2,244 per message, 18,705 total: the cap is checked between stages, so it is soft |

The phases tile the visual timeline exactly (remainder 37-41 ms), e.g. 14.39 s = 9.44 work +
0.006 handshake + 1.90 stream + 0.001 + 3.04 place-to-gameplay.

- Sending is not the cost: total time inside the 9 send ticks is about 2.2 ms. The stream is
  all throttle wait.
- The 3.0 s black window is the script (2.0 s DelayCommand + 1.0 s fade hold), after the ack.
- `work_us` is 1.4 s from the main menu but 9.4-11.4 s on in-game reloads: about two thirds of a
  reload, and nothing here says what it is. That is the next thing to hook.
- The first load had `script_delay_us` 2.21 s instead of 2.00 s (cold load; not investigated).
- A cold load can stall the main thread inside the stream window, so `throttle_wait_us` is an
  upper bound there.
