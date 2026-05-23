import sys
import re
import statistics
from concurrent.futures import ProcessPoolExecutor

def metric_pattern(name):
    return re.compile(rf'{re.escape(name)}:\s*(\d+)')

def parse_loadingscreen_times(file_path,pattern):
    """
    Reads the file at `file_path`, finds all lines containing 'loadingscreen',
    extracts the time in microseconds, and returns (total_time_us, average_time_us, count).
    """
    times = []

    with open(file_path, 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            m = pattern.search(line)
            if m:
                times.append(int(m.group(1)))

    count = len(times)
    if count == 0:
        return 0, 0.0, 0, 0.0
    
    std_dev= statistics.stdev(times) if len(times)>1 else 0.0

    total = sum(times)
    average = total / count
    return total, average, count,std_dev


def parse_named_metric(args):
    name, file_path = args
    pattern = metric_pattern(name)
    return name, parse_loadingscreen_times(file_path, pattern)

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <logfile>")
        sys.exit(1)

    logfile = sys.argv[1]

    patterns = [
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

    with ProcessPoolExecutor() as executor:
        results = executor.map(parse_named_metric, ((name, logfile) for name in patterns))

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
if __name__ == "__main__":
    main()
