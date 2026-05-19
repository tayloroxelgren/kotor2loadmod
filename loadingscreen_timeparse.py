import sys
import re
import statistics

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

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <logfile>")
        sys.exit(1)

    logfile = sys.argv[1]

    patterns = [
        ("loadingscreen", metric_pattern("loadingscreen")),
        ("LoadAndInitialize", metric_pattern("LoadAndInitialize")),
        ("ProcessResourceQueue", metric_pattern("ProcessResourceQueue")),
        ("InitShadowCache", metric_pattern("InitShadowCache")),
        ("LoadResourceBlockOrFallback", metric_pattern("LoadResourceBlockOrFallback")),
        ("HandleBNPacket", metric_pattern("HandleBNPacket")),
        ("FlushTracer", metric_pattern("FlushTracer")),
        ("Elsefunction", metric_pattern("Elsefunction")),
        ("ResourcePacketDispatcher", metric_pattern("ResourcePacketDispatcher")),
        ("ResourceQueue_UnpackAndTrace", metric_pattern("ResourceQueue_UnpackAndTrace")),
        ("PpacketHandler", metric_pattern("PpacketHandler")),
        ("SpacketHandler", metric_pattern("SpacketHandler")),
        ("ModuleHandler", metric_pattern("ModuleHandler")),
        ("ModuleChunkLoadCore", metric_pattern("ModuleChunkLoadCore")),
        ("LoadingScreenUpdateFrame", metric_pattern("LoadingScreenUpdateFrame")),
        ("GameObjUpdate", metric_pattern("GameObjUpdate")),
        ("LevelLoaderAndInitializer", metric_pattern("LevelLoaderAndInitializer")),
        ("DebugMenuConstructor", metric_pattern("DebugMenuConstructor")),
        ("fopen", metric_pattern("fopen")),
        ("gobconstructor", metric_pattern("gobconstructor")),
        ("areaconstructor", metric_pattern("areaconstructor")),
        ("InitializeGameUI", metric_pattern("InitializeGameUI")),
        ("GUI_Update3DSceneView", metric_pattern("GUI_Update3DSceneView")),
        ("AllocateMemoryOrThrow", metric_pattern("AllocateMemoryOrThrow")),
        ("ModuleDirectoryScanner", metric_pattern("ModuleDirectoryScanner")),
        ("ArrayAdd", metric_pattern("ArrayAdd")),
        ("CSWGuiLoadModuleDebugMenu_Ctor", metric_pattern("CSWGuiLoadModuleDebugMenu_Ctor")),
        ("CSWGuiPowersFeatsSkillsDebugMenu_Ctor", metric_pattern("CSWGuiPowersFeatsSkillsDebugMenu_Ctor")),
        ("CSWGuiDialogCinematic_Ctor", metric_pattern("CSWGuiDialogCinematic_Ctor")),
        ("CSWGuiDialogComputerCamera_Ctor", metric_pattern("CSWGuiDialogComputerCamera_Ctor")),
        ("CSWGuiComputerDialog_Ctor", metric_pattern("CSWGuiComputerDialog_Ctor")),
        ("CSWGuiSkillInfoBox_Ctor", metric_pattern("CSWGuiSkillInfoBox_Ctor")),
        ("CSWGuiContainer_Ctor", metric_pattern("CSWGuiContainer_Ctor")),
        ("CSWGuiExamine_Ctor", metric_pattern("CSWGuiExamine_Ctor")),
        ("CSWGuiCreateDebugItemSubMenu_Ctor", metric_pattern("CSWGuiCreateDebugItemSubMenu_Ctor")),
        ("CSWGuiTutorialBox_Ctor", metric_pattern("CSWGuiTutorialBox_Ctor")),
        ("OpenOrStreamGameFile", metric_pattern("OpenOrStreamGameFile")),
        ("CSWGuiBarkBubble_Ctor", metric_pattern("CSWGuiBarkBubble_Ctor")),
        ("CSWGuiMessageBox_Ctor", metric_pattern("CSWGuiMessageBox_Ctor")),
        ("CSWGuiMessageBoxVariant_Ctor", metric_pattern("CSWGuiMessageBoxVariant_Ctor")),
        ("CSWGuiDialogLetterbox_Ctor", metric_pattern("CSWGuiDialogLetterbox_Ctor")),
        ("CSWGuiFade_Ctor", metric_pattern("CSWGuiFade_Ctor")),
        ("CSWGuiInGameMenu_Ctor", metric_pattern("CSWGuiInGameMenu_Ctor")),
        ("CSWGuiInGamePause_Ctor", metric_pattern("CSWGuiInGamePause_Ctor")),
        ("CSWGuiInGameSoloModeQuery_Ctor", metric_pattern("CSWGuiInGameSoloModeQuery_Ctor")),
        ("CSWGuiInGameAreaTransition_Ctor", metric_pattern("CSWGuiInGameAreaTransition_Ctor")),
        ("CSWGuiInGameMessages_Ctor", metric_pattern("CSWGuiInGameMessages_Ctor")),
        ("CSWGuiStore_Ctor", metric_pattern("CSWGuiStore_Ctor")),
        ("CSWGuiInGameEquip_Ctor", metric_pattern("CSWGuiInGameEquip_Ctor")),
        ("CSWGuiInGameInventory_Ctor", metric_pattern("CSWGuiInGameInventory_Ctor")),
        ("CSWGuiInGameCharacter_Ctor", metric_pattern("CSWGuiInGameCharacter_Ctor")),
        ("CSWGuiStatusSummary_Ctor", metric_pattern("CSWGuiStatusSummary_Ctor")),
        ("CSWGuiInGameMap_Ctor", metric_pattern("CSWGuiInGameMap_Ctor")),
        ("CSWGuiInGameAbilities_Ctor", metric_pattern("CSWGuiInGameAbilities_Ctor")),
        ("CSWGuiInGameJournal_Ctor", metric_pattern("CSWGuiInGameJournal_Ctor")),
        ("CSWGuiInGameOptions_Ctor", metric_pattern("CSWGuiInGameOptions_Ctor")),
        ("CSWGuiPartySelection_Ctor", metric_pattern("CSWGuiPartySelection_Ctor")),
        ("CSWGuiInGameGalaxyMap_Ctor", metric_pattern("CSWGuiInGameGalaxyMap_Ctor")),
        ("GUI_InitWidgetFromGFF", metric_pattern("GUI_InitWidgetFromGFF")),
        ("GUI_FindAndBindControlByTag", metric_pattern("GUI_FindAndBindControlByTag")),
        ("GUI_BindNamedWidget", metric_pattern("GUI_BindNamedWidget")),
        ("ResourceEnsureLoaded", metric_pattern("ResourceEnsureLoaded")),
        ("ResourceLoadFromArchiveSlot", metric_pattern("ResourceLoadFromArchiveSlot")),
        ("ResourceLoadMemoryBacked", metric_pattern("ResourceLoadMemoryBacked")),
        ("ResourceLoadFromArchive", metric_pattern("ResourceLoadFromArchive")),
        ("ResourceLoadFromLooseFile", metric_pattern("ResourceLoadFromLooseFile")),
        ("LooseFileOpen", metric_pattern("LooseFileOpen")),
        ("LooseFileRead", metric_pattern("LooseFileRead")),
        ("ResourceFinalizeAsyncLoad", metric_pattern("ResourceFinalizeAsyncLoad")),
        ("Resource_AllocateLoadBuffer", metric_pattern("Resource_AllocateLoadBuffer")),
        ("CExoResFile_AddRefSyncOpen", metric_pattern("CExoResFile_AddRefSyncOpen")),
        ("CExoResFile_AddRefAsyncOpen", metric_pattern("CExoResFile_AddRefAsyncOpen")),
        ("CExoResFile_OpenSyncHandle", metric_pattern("CExoResFile_OpenSyncHandle")),
        ("CExoResFile_OpenAsyncHandle", metric_pattern("CExoResFile_OpenAsyncHandle")),
        ("CExoResFile_GetResourceSize", metric_pattern("CExoResFile_GetResourceSize")),
        ("CExoResFile_ReadResourceSync", metric_pattern("CExoResFile_ReadResourceSync")),
        ("CExoResFile_ReadResourceAsync", metric_pattern("CExoResFile_ReadResourceAsync")),
        ("CExoResFile_ReleaseSyncClose", metric_pattern("CExoResFile_ReleaseSyncClose")),
        ("CExoResFile_ReleaseAsyncClose", metric_pattern("CExoResFile_ReleaseAsyncClose")),
        ("ArchiveReaderShared_AddRefSyncOpen", metric_pattern("ArchiveReaderShared_AddRefSyncOpen")),
        ("CExoEncapsulatedFile_AddRefAsyncOpen", metric_pattern("CExoEncapsulatedFile_AddRefAsyncOpen")),
        ("CExoEncapsulatedFile_OpenSyncHandle", metric_pattern("CExoEncapsulatedFile_OpenSyncHandle")),
        ("CExoEncapsulatedFile_OpenAsyncHandle", metric_pattern("CExoEncapsulatedFile_OpenAsyncHandle")),
        ("CExoEncapsulatedFile_GetResourceSize", metric_pattern("CExoEncapsulatedFile_GetResourceSize")),
        ("CExoEncapsulatedFile_ReadResourceSync", metric_pattern("CExoEncapsulatedFile_ReadResourceSync")),
        ("CExoEncapsulatedFile_ReadResourceAsync", metric_pattern("CExoEncapsulatedFile_ReadResourceAsync")),
        ("CExoEncapsulatedFile_ReleaseSyncClose", metric_pattern("CExoEncapsulatedFile_ReleaseSyncClose")),
        ("CExoEncapsulatedFile_ReleaseAsyncClose", metric_pattern("CExoEncapsulatedFile_ReleaseAsyncClose")),
        ("CExoResourceImageFile_LoadImage", metric_pattern("CExoResourceImageFile_LoadImage")),
        ("CExoResourceImageFile_GetResourceSize", metric_pattern("CExoResourceImageFile_GetResourceSize")),
        ("CExoResourceImageFile_ReadResourceSync", metric_pattern("CExoResourceImageFile_ReadResourceSync")),
        ("CExoResourceImageFile_ReadResourceAsync", metric_pattern("CExoResourceImageFile_ReadResourceAsync")),
        ("CExoResourceImageFile_ReleaseSyncClose", metric_pattern("CExoResourceImageFile_ReleaseSyncClose")),
    ]

    for name, p in patterns:
        try:
            total_us, avg_us, count, std_us = parse_loadingscreen_times(logfile, p)
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
