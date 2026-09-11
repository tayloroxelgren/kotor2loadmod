import sys
import re
import statistics

def metric_pattern(name):
    return re.compile(rf'{re.escape(name)}:\s*(\d+)')

def read_latest_run(file_path):
    with open(file_path, 'r', encoding='utf-8', errors='replace') as f:
        lines = f.readlines()

    run_starts = [
        index for index, line in enumerate(lines)
        if "ProfilerRunStart:" in line
    ]
    if run_starts:
        selected = lines[run_starts[-1]:]
        match = re.search(r'\brun_id=(\d+)', selected[0])
        return selected, match.group(1) if match else "unknown"

    # Compatibility with logs produced before explicit run identifiers existed.
    legacy_starts = [
        index for index, line in enumerate(lines)
        if "Installed performance hook: PreloadInitialAssetsWrapper" in line
    ]
    if legacy_starts:
        return lines[legacy_starts[-1]:], "legacy-latest"
    return lines, "unsegmented"


def parse_loadingscreen_times(lines, pattern):
    """
    Reads the file at `file_path`, finds all lines containing 'loadingscreen',
    extracts the time in microseconds, and returns (total_time_us, average_time_us, count).
    """
    times = []
    total = 0
    count = 0

    for line in lines:
        m = pattern.search(line)
        if m:
            duration = int(m.group(1))
            aggregate_count = re.search(r'\bcount=(\d+)', line)
            weight = int(aggregate_count.group(1)) if aggregate_count else 1
            if weight < 1:
                weight = 1
            total += duration
            count += weight
            # Aggregate profiler lines contain a batch total. Represent every
            # member with the batch mean so total/count remain correct; the
            # resulting standard deviation is approximate for those batches.
            times.extend([duration / weight] * weight)

    if count == 0:
        return 0, 0.0, 0, 0.0
    
    std_dev= statistics.stdev(times) if len(times)>1 else 0.0

    average = total / count
    return total, average, count,std_dev


def parse_named_metric(args):
    name, lines = args
    pattern = metric_pattern(name)
    return name, parse_loadingscreen_times(lines, pattern)

def parse_transition_breakdown(lines):
    records = {}
    prefixes = (
        "LoadTransitionWallTime:",
        "LoadTransitionExclusiveTopLevel:",
        "LoadTransitionInclusiveDetail:",
        "LoadTransitionStateModes:",
    )
    for line in lines:
        prefix = next((p for p in prefixes if p in line), None)
        if prefix is None:
            continue
        values = {
            key: int(value)
            for key, value in re.findall(r'\b([a-zA-Z0-9_]+)=(-?\d+)', line)
        }
        transition_id = values.get("id")
        if transition_id is None:
            continue
        run_id = values.get("run_id", 0)
        record = records.setdefault((run_id, transition_id), {})
        record.update(values)
        for key in ("start_reason", "end_reason"):
            match = re.search(rf'\b{key}=([^\s]+)', line)
            if match:
                record[key] = match.group(1)
        if prefix == "LoadTransitionWallTime:":
            wall_match = re.search(r'LoadTransitionWallTime:\s*(\d+)', line)
            if wall_match:
                record["wall_us"] = int(wall_match.group(1))
    return records

def print_transition_breakdown(lines):
    records = parse_transition_breakdown(lines)
    finished = [record for record in records.values() if "wall_us" in record]
    valid = [record for record in finished if record.get("valid", 1) == 1]
    invalid = [record for record in finished if record.get("valid", 1) != 1]
    if not finished:
        return

    print(
        f"=== End-to-end load transitions "
        f"({len(valid)} valid, {len(invalid)} invalid) ==="
    )
    groups = (
        ("Wall-clock phases", (
            ("wall_us", "Request to ready"),
            ("first_frame_delay_us", "Request to first visible frame"),
            ("visible_screen_us", "Visible loading-screen span"),
            ("post_frame_us", "Last visible frame to ready"),
            ("state_activated_us", "Coordinator activation offset"),
            ("coordinator_clear_us", "Coordinator clear offset"),
            ("final_drain_start_us", "Final-drain offset"),
        )),
        ("Nested diagnostics (overlap wall time)", (
            ("engine_us", "Engine"),
            ("outer_loadingscreen_us", "Load coordinator"),
            ("module_chunk_us", "ModuleChunkLoadCore"),
            ("loading_frame_us", "LoadingScreenUpdateFrame"),
            ("archive_us", "ResourceLoadFromArchive"),
            ("worker_submit_wait_us", "Worker submit/fence"),
            ("queue_drain_us", "Resource queue drain"),
        )),
    )
    for heading, fields in groups:
        print(heading + ":")
        for key, label in fields:
            samples = [record[key] for record in valid if record.get(key, -1) >= 0]
            if samples:
                total = sum(samples)
                print(
                    f"  {label}: {total / 1000:.2f} ms total, "
                    f"{statistics.mean(samples) / 1000:.2f} ms/transition, "
                    f"median {statistics.median(samples) / 1000:.2f} ms"
                )
    print("Per transition:")
    for record in sorted(finished, key=lambda item: item.get("id", 0)):
        wall_ms = record["wall_us"] / 1000
        visible_ms = record.get("visible_screen_us", -1) / 1000
        print(
            f"  id={record.get('id')} valid={record.get('valid', 1)} "
            f"wall={wall_ms:.2f} ms visible={visible_ms:.2f} ms "
            f"frames={record.get('presented_frame_calls', 0)} "
            f"module={record.get('module_chunk_us', 0) / 1000:.2f} ms "
            f"archive={record.get('archive_us', 0) / 1000:.2f} ms "
            f"start={record.get('start_reason', 'legacy')} "
            f"end={record.get('end_reason', 'legacy')}"
        )
    print()

def print_perceived_breakdown(lines):
    records = []
    for line in lines:
        if "PerceivedLoadWallTime:" not in line:
            continue
        wall_match = re.search(r'PerceivedLoadWallTime:\s*(\d+)', line)
        if not wall_match:
            continue
        record = {
            key: int(value)
            for key, value in re.findall(r'\b([a-zA-Z0-9_]+)=(-?\d+)', line)
        }
        record["wall_us"] = int(wall_match.group(1))
        match = re.search(r'\bstart_source=([^\s]+)', line)
        record["start_source"] = match.group(1) if match else "legacy"
        match = re.search(r'\bend_reason=([^\s]+)', line)
        record["end_reason"] = match.group(1) if match else "legacy"
        records.append(record)
    if not records:
        return

    # Dead-anchor warning: if neither save-load entry hook ever fired, every
    # perceived window silently anchored at engine state activation and the
    # click-to-activation phase is excluded from the numbers.
    anchor_calls = [
        max(r.get("loadgame_calls", -1), r.get("save_request_calls", -1))
        for r in records
    ]
    if anchor_calls and max(anchor_calls) <= 0:
        print(
            "  WARNING: no save-load anchor fired (loadgame/save_request both 0);"
            " perceived windows start at engine state activation."
        )
        print(
            "  Validate the window by hand: stopwatch a load in-game and "
            "compare against the wall column below."
        )

    print(
        f"=== Perceived loads: action to first gameplay frame "
        f"({len(records)} total) ==="
    )
    for key, label in (
        ("pre_activation_us", "Load entry to engine activation"),
        ("frozen_pre_us", "Last frame before activation (frozen pre-load)"),
        ("transition_wall_us", "Engine transition window"),
        ("post_drain_us", "Final drain to first gameplay frame"),
        ("ls_busy_us", "Loadingscreen coordinator busy time (all phases)"),
        ("ls_span_us", "Loadingscreen coordinator wall span (since prev load)"),
        ("gslc_busy_us", "Save/load state machine busy (pre-activation)"),
        ("gslc_span_us", "Save/load state machine wall span"),
    ):
        samples = [r[key] for r in records if r.get(key, -1) >= 0]
        if samples:
            print(
                f"  {label}: {sum(samples) / 1000:.2f} ms total, "
                f"{statistics.mean(samples) / 1000:.2f} ms/load, "
                f"median {statistics.median(samples) / 1000:.2f} ms"
            )
    for record in records:
        gaps = "/".join(
            f"{record.get(k, -1) / 1000:.0f}"
            for k in ("post_gap1_us", "post_gap2_us", "post_gap3_us")
        )
        print(
            f"  id={record.get('id')} wall={record['wall_us'] / 1000:.2f} ms "
            f"pre={record.get('pre_activation_us', -1) / 1000:.2f} ms "
            f"frozen={record.get('frozen_pre_us', -1) / 1000:.2f} ms "
            f"transition={record.get('transition_wall_us', -1) / 1000:.2f} ms "
            f"post={record.get('post_drain_us', -1) / 1000:.2f} ms "
            f"ls={record.get('ls_busy_us', -1) / 1000:.0f}ms/"
            f"{record.get('ls_span_us', -1) / 1000:.0f}ms/"
            f"{record.get('ls_calls', 0)}c "
            f"gslc={record.get('gslc_busy_us', -1) / 1000:.0f}ms/"
            f"{record.get('gslc_span_us', -1) / 1000:.0f}ms/"
            f"{record.get('gslc_calls', 0)}c "
            f"max={record.get('gslc_max_us', -1) / 1000:.0f} "
            f"s1/s4/s5={record.get('gslc_s1_us', 0) / 1000:.0f}/"
            f"{record.get('gslc_s4_us', 0) / 1000:.0f}/"
            f"{record.get('gslc_s5_us', 0) / 1000:.0f} "
            f"postgaps={gaps} ms "
            f"presents={record.get('loading_presents', 0)}"
            f"+{record.get('post_drain_presents', 0)} "
            f"start={record['start_source']} end={record['end_reason']}"
        )
    print()

def print_click_to_control(lines):
    """Click-to-control: the player-experienced load.  Starts at the save/load
    click (save-load request path / state activation), ends when the client
    busy flag (client+0x90, the gate that blocks UpdatePlayerInputAndTargeting)
    clears after the first presented frame.  Phases:
    click->transition = pre-activation drain (save read + world-restore
    packets), transition->first_present = measured transition window,
    first_present->control = the black window (engine presents static black
    frames while it finishes streaming/restoring)."""
    records = []
    for line in lines:
        if "ClickToControl:" not in line:
            continue
        wall_match = re.search(r'ClickToControl:\s*(\d+)', line)
        if not wall_match:
            continue
        record = {
            key: int(value)
            for key, value in re.findall(r'\b([a-zA-Z0-9_]+)=(-?\d+)', line)
        }
        record["wall_us"] = int(wall_match.group(1))
        for key in ("click_source", "control_reason"):
            match = re.search(rf'\b{key}=([^\s]+)', line)
            record[key] = match.group(1) if match else "?"
        records.append(record)

    substates = []
    for line in lines:
        match = re.search(
            r'Load(?:Substate: value=(-?\d+)|Gate: substate=(-?\d+) client_busy=(-?\d+))'
            r'(?: at_us_after_click=(-?\d+))?', line)
        if match:
            if match.group(1) is not None:
                substates.append(("sub", int(match.group(1)), None,
                                  int(match.group(4) or 0)))
            else:
                substates.append(("gate", int(match.group(2)),
                                  int(match.group(3)),
                                  int(match.group(4) or 0)))

    action_lines = []
    for line in lines:
        match = re.search(
            r'SaveLoadRequestAction: action=0x([0-9A-Fa-f]+) param1=(\d+)', line)
        if match:
            action_lines.append((match.group(1), int(match.group(2))))

    if not records:
        print("=== Click to control ===")
        print(
            "  NO ClickToControl lines landed.  If LoadGate shadow lines are "
            "present they show what the input gate did; check control-reason "
            "expiries below in the raw log."
        )
        if substates:
            trace = " ".join(
                (f"sub={v}@{t}ms" if kind == "sub" else f"busy={b}@{t}ms")
                for kind, v, b, t in substates[:24]
            )
            print(f"  gate trace: {trace}")
        else:
            print(
                "  No gate trace either - the tracker never armed or the "
                "shadow-log cap hit. Check start_source on PerceivedLoad lines."
            )
        if action_lines:
            print(
                "  SaveLoadRequest actions seen: " +
                ", ".join(f"0x{a}(p1={p})" for a, p in action_lines[:12])
            )
        print()
        return

    print(f"=== Click to control: the player-experienced load ({len(records)} loads) ===")
    landed = [r for r in records if r.get("control_reason", "").startswith("control_substate")]
    for key, label in (
        ("click_to_transition_us", "Click to transition start (pre-activation drain: save read + world restore)"),
        ("transition_to_first_present_us", "Transition window (GUI rebuild etc.)"),
        ("first_present_to_control_us", "BLACK WINDOW (first presented frame to input unlocked)"),
        ("wall_us", "TOTAL click-to-control"),
    ):
        samples = [r[key] for r in records if r.get(key, -1) >= 0]
        if samples:
            extra = ""
            if key == "wall_us" and len(landed) > 1:
                medians = statistics.median([r["wall_us"] for r in landed])
                extra = f" (median of landed: {medians / 1000:.0f} ms)"
            print(
                f"  {label}: {sum(samples) / 1000:.0f} ms total, "
                f"{statistics.mean(samples) / 1000:.0f} ms/load, "
                f"median {statistics.median(samples) / 1000:.0f} ms{extra}"
            )
    for record in records:
        print(
            f"  id={record.get('id')} total={record['wall_us'] / 1000:.0f} ms "
            f"pre={record.get('click_to_transition_us', -1) / 1000:.0f} "
            f"transition={record.get('transition_to_first_present_us', -1) / 1000:.0f} "
            f"black={record.get('first_present_to_control_us', -1) / 1000:.0f} ms "
            f"click={record.get('click_source', '?')} "
            f"end={record.get('control_reason', '?')} "
            f"substate={record.get('control_substate', '?')}"
        )
    if substates:
        trace = " ".join(
            (f"sub={v}@{t // 1000}ms" if kind == "sub"
             else f"busy={b}@{t // 1000}ms")
            for kind, v, b, t in substates[:24]
        )
        print(f"  gate trace (value@ms-after-click): {trace}")
    print()


def print_visual_loads(lines):
    """Visual load timeline: what a camera pointed at the screen would measure.
    Every present is pixel-classified (loading screen / black / other); the
    report starts at the mouse click and ends at the first bright gameplay
    frame, so the loading-screen span and the black window are measured the
    same way the hand-timed video measured them."""
    records = []
    current = None
    for line in lines:
        if "VisualLoad:" in line:
            wall_match = re.search(r'VisualLoad:\s*(\d+)', line)
            if not wall_match:
                continue
            record = {
                key: int(value)
                for key, value in re.findall(r'\b([a-zA-Z0-9_]+)=(-?\d+)', line)
            }
            record["wall_us"] = int(wall_match.group(1))
            for key in ("ref", "end_reason"):
                match = re.search(rf'\b{key}=([^\s]+)', line)
                record[key] = match.group(1) if match else "?"
            record["click_source"] = "?"
            match = re.search(r'\bclick_source=(.)', line)
            if match:
                record["click_source"] = match.group(1)
            record["runs"] = []
            records.append(record)
            current = record
        elif "FrameRun:" in line and current is not None:
            match = re.search(
                r'FrameRun: id=\d+ n=(\d+) class=(\S+) start_us=(-?\d+) '
                r'dur_us=(-?\d+) frames=(\d+) static=(\d+) mean_lum=(\d+) '
                r'load_state=(-?\d+)', line)
            if match:
                current["runs"].append({
                    "n": int(match.group(1)),
                    "class": match.group(2),
                    "start_us": int(match.group(3)),
                    "dur_us": int(match.group(4)),
                    "frames": int(match.group(5)),
                    "static": int(match.group(6)),
                    "mean_lum": int(match.group(7)),
                    "load_state": int(match.group(8)),
                })
        elif "FrameRun:" not in line and "VisualLoad:" not in line:
            current = None

    print("=== Visual load timeline (pixel-classified, camera-equivalent) ===")
    if not records:
        print(
            "  NO VisualLoad lines.  Either the load never finished visually "
            "(quit during the black window - a 'detach' record should still "
            "land) or back-buffer sampling was unavailable on this build."
        )
        print()
        return

    usable = [r for r in records if r.get("first_gameplay_us", -1) >= 0]
    for key, label in (
        ("first_loading_us", "Click to loading screen visible"),
        ("loading_us", "LOADING SCREEN (first to last loading frame)"),
        ("black_us", "BLACK WINDOW (last loading frame to first gameplay frame)"),
        ("wall_us", "TOTAL click to first gameplay frame"),
    ):
        samples = [r[key] for r in usable if r.get(key, -1) >= 0]
        if samples:
            print(
                f"  {label}: {sum(samples) / 1000:.0f} ms total, "
                f"{statistics.mean(samples) / 1000:.0f} ms/load, "
                f"median {statistics.median(samples) / 1000:.0f} ms"
            )
    for record in records:
        print(
            f"  id={record.get('id')} total={record['wall_us'] / 1000:.0f} ms "
            f"click={record.get('click_seen', 0)}/{record.get('click_source', '?')} "
            f"loading={record.get('loading_us', -1) / 1000:.0f} "
            f"({record.get('loading_frames', 0)}f) "
            f"black={record.get('black_us', -1) / 1000:.0f} "
            f"({record.get('black_frames', 0)}f) "
            f"gameplay@={record.get('first_gameplay_us', -1) / 1000:.0f} ms "
            f"end={record.get('end_reason', '?')}"
        )
        activation = record.get("activation_us", -1)
        perceived = record.get("perceived_end_us", -1)
        fade = record.get("fade_us", -1)
        print(
            f"    anchors: engine-activation@{activation / 1000:.0f} ms "
            f"engine-end@{perceived / 1000:.0f} ms "
            f"fade@{fade / 1000:.0f} ms"
            + (f" (mode={record.get('fade_mode')}, "
               f"{record.get('fade_seconds', 0):.2f}s)" if fade >= 0 else "")
        )
        for run in record["runs"][:24]:
            print(
                f"    [{run['n']:2d}] {run['class']:<13} "
                f"start={run['start_us'] / 1000:8.1f} ms "
                f"dur={run['dur_us'] / 1000:8.1f} ms "
                f"frames={run['frames']:4d} static={run['static']:4d} "
                f"lum={run['mean_lum']:3d} ls={run['load_state']}"
            )
        if len(record["runs"]) > 24:
            print(f"    ... {len(record['runs']) - 24} more runs")
    print()


def print_packet_profiles(lines):
    records = []
    for line in lines:
        if "PacketProfile:" not in line:
            continue
        record = {
            key: int(value)
            for key, value in re.findall(r'\b([a-zA-Z0-9_]+)=(-?\d+)', line)
        }
        match = re.search(r'\btop1=(\S+)', line)
        record["top1"] = match.group(1) if match else "n/a"
        records.append(record)
    if not records:
        return
    print("=== Load diagnostics per load window ===")
    for record in records:
        def ms(key):
            return record.get(key, -1) / 1000
        print(
            f"  id={record.get('id')} "
            f"prq={ms('prq_busy_us'):.0f}ms/{record.get('prq_calls', 0)}c "
            f"fade={ms('fade_busy_us'):.1f}ms/{record.get('fade_calls', 0)}c"
            f"+{record.get('fade_clamps', 0)}clamp "
            f"igc={ms('igc_busy_us'):.0f}ms/{record.get('igc_calls', 0)}c "
            f"isc={ms('isc_busy_us'):.0f}ms/{record.get('isc_calls', 0)}c "
            f"lfopen={ms('lfopen_busy_us'):.0f}ms "
            f"lfread={ms('lfread_busy_us'):.0f}ms/"
            f"{record.get('lfread_bytes', 0) / (1024 * 1024):.1f}MB "
            f"top={record.get('top1', 'n/a')}"
        )
    print()

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <logfile>")
        sys.exit(1)

    logfile = sys.argv[1]
    lines, run_id = read_latest_run(logfile)
    print(f"Analyzing latest profiler run: {run_id} ({len(lines)} log lines)")

    # Validity gates: a run whose hooks failed to install is vanilla gameplay
    # and must never be compared as if it were instrumented data.
    broken = [
        line.strip()
        for line in lines
        if "Hooks not installed" in line or "Signature check failed" in line
    ]
    if broken:
        print()
        print("!!! VOID RUN - HOOKS DID NOT INSTALL !!!")
        for line in broken:
            print(f"    {line}")
        print("Do not use this run for comparisons; the game ran unmodified.")
        sys.exit(1)
    installed = sum(1 for line in lines if "Installed performance hook:" in line)
    print(f"Installed hooks: {installed}")
    if run_id not in ("unsegmented", "legacy-latest") and installed == 0:
        print("WARNING: no hook installation lines in this run.")
    print()

    patterns = [
        "LoadTransitionWallTime",
        "PerceivedLoadWallTime",
        "SaveListParse",
        "LoadSession",
        "loadingscreen",
        "LoadAndInitialize",
        "ProcessResourceQueue",
        "InitShadowCache",
        "LoadResourceBlockOrFallback",
        "HandleBNPacket",
        "FlushTracer",
        "Elsefunction",
        "ResourcePacketDispatcher",
        "ResourceQueue_UnpackAndTrace",
        "PpacketHandler",
        "SpacketHandler",
        "ModuleHandler",
        "ModuleChunkLoadCore",
        "LoadingScreenUpdateFrame",
        "SwapBuffers",
        "GUIContext_UpdateAndRender",
        "WindowsMessagePump",
        "OpenGL_GammaPostProcess",
        "FrameMetricsAndMemoryUpdate",
        "AppState_GetGuiContext",
        "AppState_GetLoadProgressByte",
        "Runtime_FloatToInt_ST0",
        "AppState_SetLoadBarValue",
        "CSWGuiFade_SetTransitionState",
        "LoadingScreenFadeUpdateFrame",
        "GameObjUpdate",
        "LevelLoaderAndInitializer",
        "DebugMenuConstructor",
        "fopen",
        "gobconstructor",
        "Gob_LoadFromFileOrStream",
        "areaconstructor",
        "InitializeGameUI",
        "GUI_Update3DSceneView",
        "AllocateMemoryOrThrow",
        "ModuleDirectoryScanner",
        "ArrayAdd",
        "CSWGuiLoadModuleDebugMenu_Ctor",
        "CSWGuiPowersFeatsSkillsDebugMenu_Ctor",
        "CSWGuiDialogCinematic_Ctor",
        "CSWGuiDialogComputerCamera_Ctor",
        "CSWGuiComputerDialog_Ctor",
        "CSWGuiSkillInfoBox_Ctor",
        "CSWGuiContainer_Ctor",
        "CSWGuiExamine_Ctor",
        "CSWGuiCreateDebugItemSubMenu_Ctor",
        "CSWGuiTutorialBox_Ctor",
        "OpenOrStreamGameFile",
        "Texture_ApplyTXIAndBuildController",
        "Texture_FindExisting",
        "Texture_UpdateResourceBinding",
        "Texture_ReplaceAcrossUsers",
        "Texture_AcquireAndRelease",
        "Texture_GetOrCreate",
        "ParseTXIAndBuildTextureController",
        "Texture_ApplyTXIBlendingMode",
        "Texture_ApplyTXIMaterialDirectives",
        "CSWGuiBarkBubble_Ctor",
        "CSWGuiMessageBox_Ctor",
        "CSWGuiMessageBoxVariant_Ctor",
        "CSWGuiDialogLetterbox_Ctor",
        "CSWGuiFade_Ctor",
        "CSWGuiInGameMenu_Ctor",
        "CSWGuiInGamePause_Ctor",
        "CSWGuiInGameSoloModeQuery_Ctor",
        "CSWGuiInGameAreaTransition_Ctor",
        "CSWGuiInGameMessages_Ctor",
        "CSWGuiStore_Ctor",
        "CSWGuiInGameEquip_Ctor",
        "CSWGuiInGameInventory_Ctor",
        "CSWGuiInGameCharacter_Ctor",
        "CSWGuiStatusSummary_Ctor",
        "CSWGuiInGameMap_Ctor",
        "CSWGuiInGameAbilities_Ctor",
        "CSWGuiInGameJournal_Ctor",
        "CSWGuiInGameOptions_Ctor",
        "CSWGuiPartySelection_Ctor",
        "CSWGuiInGameGalaxyMap_Ctor",
        "GUI_InitWidgetFromGFF",
        "GUI_FindAndBindControlByTag",
        "GUI_BindNamedWidget",
        "GUI_BindChildStructByName",
        "GUI_BaseControlSetup",
        "GUI_CommonBaseBinder",
        "GUI_ListBoxBindProtoItem",
        "GFF_ReadBoolFieldByName",
        "GFF_ReadIntFieldByName",
        "GFF_ReadVector3FieldByName",
        "GFF_LookupFieldLabelByName",
        "ResourceStreamer_Init",
        "ResourceLoader",
        "Worker_ProcessJob",
        "ResourceEnsureLoaded",
        "ResourceLoadFromArchiveSlot",
        "ResourceLoadMemoryBacked",
        "ResourceLoadFromArchive",
        "ResourceLoadFromLooseFile",
        "LooseFileOpen",
        "LooseFileRead",
        "ResourceFinalizeAsyncLoad",
        "Resource_AllocateLoadBuffer",
        "CExoResFile_AddRefSyncOpen",
        "CExoResFile_AddRefAsyncOpen",
        "CExoResFile_OpenSyncHandle",
        "CExoResFile_OpenAsyncHandle",
        "CExoResFile_GetResourceSize",
        "CExoResFile_ReadResourceSync",
        "CExoResFile_ReadResourceAsync",
        "CExoResFile_ReleaseSyncClose",
        "CExoResFile_ReleaseAsyncClose",
        "ArchiveReaderShared_AddRefSyncOpen",
        "CExoEncapsulatedFile_AddRefAsyncOpen",
        "CExoEncapsulatedFile_OpenSyncHandle",
        "CExoEncapsulatedFile_OpenAsyncHandle",
        "CExoEncapsulatedFile_GetResourceSize",
        "CExoEncapsulatedFile_ReadResourceSync",
        "CExoEncapsulatedFile_ReadResourceAsync",
        "CExoEncapsulatedFile_ReleaseSyncClose",
        "CExoEncapsulatedFile_ReleaseAsyncClose",
        "CExoResourceImageFile_LoadImage",
        "CExoResourceImageFile_GetResourceSize",
        "CExoResourceImageFile_ReadResourceSync",
        "CExoResourceImageFile_ReadResourceAsync",
        "CExoResourceImageFile_ReleaseSyncClose",
    ]

    results = [parse_named_metric((name, lines)) for name in patterns]

    for name, metric_result in results:
        try:
            total_us, avg_us, count, std_us = metric_result
            if count == 0:
                print(f"Function data not found for: {name}")
                continue

            total_ms = total_us / 1000
            avg_ms = avg_us / 1000
            std_ms = std_us / 1000

            print(f"=== {name} ===")
            print(f"Total time:   {total_us} us ({total_ms:.2f} ms)")
            print(f"Average time: {avg_us:.2f} us ({avg_ms:.2f} ms) over {count} samples")
            print(f"Std deviation: {std_us:.2f} us ({std_ms:.2f} ms)")
            print()
        except Exception as exc:
            print(f"Failed to parse {name}: {exc}")
    print_transition_breakdown(lines)
    print_perceived_breakdown(lines)
    print_click_to_control(lines)
    print_visual_loads(lines)
    print_packet_profiles(lines)
if __name__ == "__main__":
    main()
