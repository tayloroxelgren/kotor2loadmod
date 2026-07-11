#include <Windows.h>
#include <fstream>
#include <string>
#include <chrono>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstring>
#include <mutex>
#include "minhook/include/MinHook.h"
#include <Windows.h>

#define LOGGING_ENABLED 1
#define LOG_LOADSCREEN_ONLY 0
// Stability build: install only the small, signature-checked hook set below.  The
// legacy profiler contains many unrelated detours and is intentionally opt-in.
#define PERFORMANCE_HOOK_SET_ONLY 1
#define SKIP_PRELOAD_INITIAL_ASSETS_WRAPPER 1
#define SKIP_LOADING_SCREEN_UPDATE_FRAME_IN_MODULE_CHUNK_LOAD_CORE 0
// Never force the engine's platform GUI mode.  On the PC build it leaves app-owned
// GUI slots null that have no compatible lazy reconstruction path.
#define ENABLE_NATIVE_LAZY_GUI_MODE 0
// Stable optimization toggles.  For an A/B run, leave the hook set unchanged and
// change only these values between 0 (baseline) and 1 (optimized).
#define ENABLE_GUI_CONTROLS_LOOKUP_CACHE 0
#define THROTTLE_LOADING_SCREEN_PRESENTS 0
#define LOADING_SCREEN_PRESENT_INTERVAL_MS 100
#define ENABLE_ARCHIVE_RESOURCE_CACHE 0
#define ARCHIVE_CACHE_MAX_BYTES (256u * 1024u * 1024u)
#define SKIP_DEBUG_GUI_CONSTRUCTION 0
#define DEFER_INGAME_TAB_CONSTRUCTION 0
#define PRESERVE_GUI_OBJECTS_ACROSS_LOADS 0
#define HOOK_GUI_DEEP_GFF_TIMING 0
#define HOOK_APPSTATE_GET_GUI_CONTEXT_TIMING 0
#define HOOK_APPSTATE_GET_LOAD_PROGRESS_BYTE_TIMING 0
#define HOOK_RUNTIME_FLOAT_TO_INT_ST0_TIMING 0
#define HOOK_APPSTATE_SET_LOAD_BAR_VALUE_TIMING 0
#define HOOK_CSWGUIFADE_SET_TRANSITION_STATE_TIMING 0
#define HOOK_LOADING_SCREEN_FADE_UPDATE_FRAME_TIMING 0
#define HOOK_PARSE_TXI_AND_BUILD_TEXTURE_CONTROLLER_TIMING 0
#define HOOK_TEXTURE_CACHE_TIMING 0
#define HOOK_CEXOSTRING_CLEAR_TIMING 0
#define KEEP_ARCHIVE_OPEN_DURING_LOAD 0
#define TRACE_ARCHIVE_REFCOUNTS 0

// DirectInput8 proxy
typedef HRESULT(WINAPI *DICREATE)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DICREATE realCreate = nullptr;

// Simple logging
std::ofstream g_logFile;
std::mutex g_logMutex;

bool LogHasPrefix(const std::string& msg, const char* prefix) {
    return msg.compare(0, std::strlen(prefix), prefix) == 0;
}

bool ShouldLogMessage(const std::string& msg) {
#if LOG_LOADSCREEN_ONLY
    return LogHasPrefix(msg, "loadingscreen:");
#else
    return true;
#endif
}

void Log(const std::string& msg) {
    if(LOGGING_ENABLED){
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (g_logFile.is_open() && ShouldLogMessage(msg)) {
            SYSTEMTIME st;
            GetSystemTime(&st);
            g_logFile << st.wHour << ":" << st.wMinute << ":" << st.wSecond 
                      << " - " << msg << std::endl;
            g_logFile.flush();
        }
    }
}

typedef int (__thiscall* LoadAndInitializePtr_t)(void* thisPtr, uint32_t param1, int param2);
LoadAndInitializePtr_t g_originalLoadAndInitializePtr = nullptr;

int __fastcall Hook_LoadAndInitializePtr(
    void* thisPtr, void* edxDummy, uint32_t param1, int param2) {
    auto start = std::chrono::high_resolution_clock::now();
    int result = g_originalLoadAndInitializePtr(thisPtr, param1, param2);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("LoadAndInitialize: " + std::to_string(duration.count()) + " μs");
    return result;
}

// LoadingScreen
typedef int (__fastcall* loadingscreenPtr_t)(int param1);
loadingscreenPtr_t g_originalLoadingScreenPtr = nullptr;


int __fastcall Hook_loadingscreenPtr(int param1) {
    auto start = std::chrono::high_resolution_clock::now();

    int result = g_originalLoadingScreenPtr(param1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("loadingscreen: " + std::to_string(duration.count()) + " μs");

    return result;
}

typedef uint32_t (__fastcall* HandleBNPacketPtr_t)(int thisPtr,int edxdummy,uint32_t param1,char* packet,uint32_t length);
HandleBNPacketPtr_t g_originalHandleBNPacketPtr = nullptr;

uint32_t __fastcall Hook_HandleBNPacket(int thisPtr,int edxdummy,uint32_t param1,char* packet,uint32_t length){
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result=g_originalHandleBNPacketPtr(thisPtr,edxdummy,param1,packet,length);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("HandleBNPacket: " + std::to_string(duration.count()) + " μs");

    return result;
}


typedef uint32_t (__thiscall* ResourcePacketDispatcherPtr_t)(
    void* thisPtr, uint32_t param1, char* param2, uint32_t param3, int param4);
ResourcePacketDispatcherPtr_t g_originalResourcePacketPtr=nullptr;

uint32_t __fastcall Hook_ResourcePacketDispatcher(
    void* thisPtr, void* edxDummy,
    uint32_t param1, char* param2, uint32_t param3, int param4) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalResourcePacketPtr(thisPtr, param1, param2, param3, param4);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ResourcePacketDispatcher: " + std::to_string(duration.count()) + " μs");

    return result;
}

typedef uint32_t (__fastcall *ResourceQueue_UnpackAndTracePtr_t)(void* thisPtr, void* edxdummy, uint32_t param1, char* param2, uint32_t param3, int param4);
ResourceQueue_UnpackAndTracePtr_t g_originalResourceQueue_UnpackAndTracePtr=nullptr;

uint32_t __fastcall Hook_ResourceQueue_UnpackAndTrace(void* thisPtr, void* edxdummy, uint32_t param1, char* param2, uint32_t param3, int param4){
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result=g_originalResourceQueue_UnpackAndTracePtr(thisPtr,edxdummy,param1,param2,param3,param4);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ResourceQueue_UnpackAndTrace: " + std::to_string(duration.count()) + " μs");

    return result;
}



typedef void (__fastcall* ProcessResourceQueuePtr_t)(int param1, void*,int param2);
ProcessResourceQueuePtr_t g_originalProcessResourceQueuePtr = nullptr;


// void __fastcall Hook_ProcessResourceQueue(int param1, void*,int param2){
//     auto start = std::chrono::high_resolution_clock::now();
//     g_originalProcessResourceQueuePtr(param1,nullptr,param2);

//     auto end = std::chrono::high_resolution_clock::now();
//     auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
//     Log("ProcessResourceQueue: " + std::to_string(duration.count()) + " μs");
// }
void __fastcall Hook_ProcessResourceQueue(int param1, void*, int param2){
    auto start = std::chrono::high_resolution_clock::now();

    // Profiling must not replace the queue algorithm. The old handwritten walker
    // advanced an extra four bytes for already-aligned packets and could run past
    // the ring buffer, which made the full profiling build crash during loads.
    g_originalProcessResourceQueuePtr(param1, nullptr, param2);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ProcessResourceQueue: " + std::to_string(duration.count()) + " μs");
    
}


typedef void (__fastcall* InitShadowCachePtr_t)(uint32_t param1);
InitShadowCachePtr_t g_originalInitShadowCachePtr = nullptr;

void __fastcall Hook_InitShadowCache(uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();

    g_originalInitShadowCachePtr(param1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("InitShadowCache: " + std::to_string(duration.count()) + " μs");
}

typedef uint32_t* (__thiscall* LoadResourceBlockOrFallbackPtr_t)(
    void*       thisPtr,   // ECX – object / resource-manager
    uint32_t*   param1,
    int         param2,
    int         param3,
    uint32_t*   param4,
    uint32_t*   param5
);

LoadResourceBlockOrFallbackPtr_t g_originalLoadResourceBlockOrFallbackPtr = nullptr;

uint32_t* __fastcall Hook_LoadResourceBlockOrFallback(
    void*       thisPtr,
    void*       _edx,      // dummy for EDX – not used
    uint32_t*   param1,
    int         param2,
    int         param3,
    uint32_t*   param4,
    uint32_t*   param5){
    auto start = std::chrono::high_resolution_clock::now();


    uint32_t* result = g_originalLoadResourceBlockOrFallbackPtr(thisPtr,param1,param2,param3,param4,param5);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("LoadResourceBlockOrFallback: " + std::to_string(duration.count()) + " μs");

    return result;
}


typedef void (__fastcall* PreloadInitialAssetsWrapperPtr_t)(uint32_t param1);
PreloadInitialAssetsWrapperPtr_t g_originalPreloadInitialAssetsWrapperPtr=nullptr;

// Skips splash screens
void __fastcall Hook_PreloadInitialAssetsWrapper(uint32_t param1){
#if SKIP_PRELOAD_INITIAL_ASSETS_WRAPPER
    return;
#else
    g_originalPreloadInitialAssetsWrapperPtr(param1);
#endif
}

typedef void (__fastcall* CExoString_ClearAndFreePtr_t)(int *stringBuffer);
CExoString_ClearAndFreePtr_t g_originalCExoString_ClearAndFree=nullptr;

void __fastcall Hook_CExoString_ClearAndFree(int *stringBuffer){
    // This is an engine string destructor, not a trace flush. It must run or every
    // temporary CExoString-style buffer leaks. The performance hook set omits this
    // detour entirely; the pass-through remains for the legacy profiling hook set.
    g_originalCExoString_ClearAndFree(stringBuffer);
}



typedef uint32_t (__stdcall* PpacketHandlerPtr_t)(char *param1,int param2);
PpacketHandlerPtr_t g_originalPpacketHandler= nullptr;

uint32_t __stdcall Hook_PpacketHandler(char *param1,int param2){
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result=g_originalPpacketHandler(param1,param2);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("PpacketHandler: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__stdcall* SpacketHandlerPtr_t)(char *param1,int param2);
SpacketHandlerPtr_t g_originalSpacketHandler= nullptr;

uint32_t __stdcall Hook_SpacketHandler(char *param1,int param2){
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result=g_originalSpacketHandler(param1,param2);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("SpacketHandler: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__thiscall* ModuleHandlerPtr_t)(void* thisPtr, byte param1);
ModuleHandlerPtr_t g_originalModuleHandler=nullptr;

#if KEEP_ARCHIVE_OPEN_DURING_LOAD
static bool g_archiveLoadInProgress = false;
static std::vector<int*> g_pinnedEncapsulatedArchives;
static const int CExoEncapsulatedFileVtable = 0x0099c6fc;

typedef void (__thiscall* CExoEncapsulatedFile_ReleaseSyncClosePtr_t)(int* thisPtr);
extern CExoEncapsulatedFile_ReleaseSyncClosePtr_t g_originalCExoEncapsulatedFile_ReleaseSyncClose;

typedef void (__thiscall* ArchiveReaderShared_AddRefSyncOpenPtr_t)(int* thisPtr);
extern ArchiveReaderShared_AddRefSyncOpenPtr_t g_originalArchiveReaderShared_AddRefSyncOpen;

static bool ArchiveLoad_IsEncapsulatedReader(int* archive) {
    return archive != nullptr && archive[0] == CExoEncapsulatedFileVtable;
}

static bool ArchiveLoad_IsPinned(int* archive) {
    for (int* pinned : g_pinnedEncapsulatedArchives) {
        if (pinned == archive) {
            return true;
        }
    }
    return false;
}

static void ArchiveLoad_PinReader(int* archive) {
    if (!g_archiveLoadInProgress || !ArchiveLoad_IsEncapsulatedReader(archive) || ArchiveLoad_IsPinned(archive)) {
        return;
    }
    if (g_originalArchiveReaderShared_AddRefSyncOpen == nullptr) {
        return;
    }

    g_originalArchiveReaderShared_AddRefSyncOpen(archive);
    g_pinnedEncapsulatedArchives.push_back(archive);
    Log("ArchiveLoad: pinned encapsulated archive ref=" + std::to_string(archive[7]));
}

static void ArchiveLoad_FlushPinnedHandles() {
    if (g_pinnedEncapsulatedArchives.empty()) {
        return;
    }

    int pinnedCount = (int)g_pinnedEncapsulatedArchives.size();
    for (int* archive : g_pinnedEncapsulatedArchives) {
        if (ArchiveLoad_IsEncapsulatedReader(archive) && g_originalCExoEncapsulatedFile_ReleaseSyncClose != nullptr) {
            g_originalCExoEncapsulatedFile_ReleaseSyncClose(archive);
        }
    }
    g_pinnedEncapsulatedArchives.clear();
    Log("ArchiveLoad: flushed " + std::to_string(pinnedCount) + " pinned encapsulated archives");
}
#endif

// Forward declarations of GUI preservation state (full definitions are below near
// Hook_ModuleChunkLoadCore). Hook_ModuleHandler needs them for the packet-8 teardown hook.
extern int* g_cclientExoApp;
extern bool g_guiBuiltSuccessfully;
extern int g_guiObjCount;
extern const int k_numGuiSlots;
extern int g_guiSlotValues[];
extern void* g_fakeVtable[];
struct GuiObjRecord { int objAddr; int origVtable; };
extern GuiObjRecord g_guiObjs[];
extern const int MAX_GUI_OBJS;
extern const int k_guiSlots[];

uint32_t __fastcall Hook_ModuleHandler(void* thisPtr, void* edxDummy, byte param1){
    auto start = std::chrono::high_resolution_clock::now();

#if KEEP_ARCHIVE_OPEN_DURING_LOAD
    if (param1 == 8) {
        ArchiveLoad_FlushPinnedHandles();
    }
#endif

    // Packet 8 = teardown (case 7 in the switch: FUN_0073f250 → FUN_00785410).
    // Empirically confirmed: this packet zeros all GUI slot pointers and flag128 in CClientExoApp.
    // Arm vtable swaps BEFORE so virtual destructor dispatch hits a no-op (objects survive),
    // then restore pointers + flag128=1 AFTER so the next ModuleChunkLoadCore skips construction.
#if PRESERVE_GUI_OBJECTS_ACROSS_LOADS
    bool isTeardown = (param1 == 8) && g_guiBuiltSuccessfully && g_cclientExoApp != nullptr;
    if (isTeardown) {
        g_guiObjCount = 0;
        for (int i = 0; i < k_numGuiSlots; i++) {
            int objAddr = g_guiSlotValues[i];
            if (objAddr == 0) continue;
            int origVtable = *(int*)objAddr;
            *(int*)objAddr = (int)g_fakeVtable;
            if (g_guiObjCount < MAX_GUI_OBJS) {
                g_guiObjs[g_guiObjCount++] = { objAddr, origVtable };
            }
        }
        Log("ModuleHandler[8]: armed " + std::to_string(g_guiObjCount) + " GUI objects pre-teardown");
    }
#endif

    uint32_t result = g_originalModuleHandler(thisPtr, param1);

#if PRESERVE_GUI_OBJECTS_ACROSS_LOADS
    if (isTeardown) {
        // Restore vtables so virtual method calls during gameplay work normally.
        for (int i = 0; i < g_guiObjCount; i++) {
            *(int*)g_guiObjs[i].objAddr = g_guiObjs[i].origVtable;
        }
        // Restore slot pointers and set flag128=1 so the next ModuleChunkLoadCore call skips.
        for (int i = 0; i < k_numGuiSlots; i++) {
            g_cclientExoApp[k_guiSlots[i] / 4] = g_guiSlotValues[i];
        }
        g_cclientExoApp[0x128 / 4] = 1;
        Log("ModuleHandler[8]: restored " + std::to_string(g_guiObjCount) + " GUI objects post-teardown");
    }
#endif

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ModuleHandler[" + std::to_string((int)param1) + "]: " + std::to_string(duration.count()) + " μs");

    return result;
}

typedef uint32_t (__thiscall* GameObjUpdatePtr_t)(void* thisPtr, byte param1);
GameObjUpdatePtr_t g_originalGameObjUpdate=nullptr;

uint32_t __fastcall Hook_GameObjUpdate(void* thisPtr, void* edxDummy, byte param1){
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalGameObjUpdate(thisPtr, param1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("GameObjUpdate: " + std::to_string(duration.count()) + " μs");
    return result;
}

thread_local int g_moduleChunkLoadCoreDepth = 0;
thread_local uint32_t g_deferredInGameTabMask = 0;
thread_local bool g_lazyTabConstructionInProgress = false;

// GUI_FindAndBindControlByTag repeatedly asks the GFF layer to resolve the same
// CONTROLS field while walking one list.  Keep one result only for the duration of
// that call.  Nothing survives a GUI_Find boundary, so no engine-owned pointer is
// retained across a layout load or module transition.
struct GuiControlsLookupCacheState {
    int guiFindDepth;
    bool valid;
    int gffPtr;
    int structPtr;
    int result;
    uint64_t cacheHits;
    uint64_t originalLookups;
    uint64_t guiFindCalls;
    int64_t guiFindTimeUs;
};

thread_local GuiControlsLookupCacheState g_guiControlsLookupCache = {};

static void ResetGuiControlsProfile() {
    g_guiControlsLookupCache.cacheHits = 0;
    g_guiControlsLookupCache.originalLookups = 0;
    g_guiControlsLookupCache.guiFindCalls = 0;
    g_guiControlsLookupCache.guiFindTimeUs = 0;
}

static volatile LONG g_lazyTabHooksReady = FALSE;
static volatile LONG g_nativeLazyGuiModeState = 0;
static volatile LONG* const g_eagerLoadGuiPanels = (volatile LONG*)0x00a10760;

static bool ActivateNativeLazyGuiMode() {
#if ENABLE_NATIVE_LAZY_GUI_MODE
    LONG state = InterlockedCompareExchange(&g_nativeLazyGuiModeState, 0, 0);
    if (state != 0) {
        return state > 0;
    }

    LONG eagerMode = InterlockedCompareExchange(g_eagerLoadGuiPanels, 0, 0);
    if (eagerMode != 0 && eagerMode != 1) {
        if (InterlockedCompareExchange(&g_nativeLazyGuiModeState, -1, 0) == 0) {
            Log("Native lazy GUI mode refused: unexpected engine flag value=" +
                std::to_string(eagerMode));
        }
        return false;
    }

    // Switch only at the ModuleChunkLoadCore entry boundary. Changing this global
    // while the stock constructor is halfway through its conditional blocks could
    // leave a mixed GUI set. The zero path is the engine's own supported lazy mode.
    InterlockedExchange(g_eagerLoadGuiPanels, 0);
    if (InterlockedCompareExchange(&g_nativeLazyGuiModeState, 1, 0) == 0) {
        Log("Native lazy GUI mode enabled");
    }
    return true;
#else
    return false;
#endif
}

struct ScopedModuleChunkDepth {
    ScopedModuleChunkDepth() { ++g_moduleChunkLoadCoreDepth; }
    ~ScopedModuleChunkDepth() { --g_moduleChunkLoadCoreDepth; }
};

struct ScopedBoolValue {
    bool& target;
    bool saved;
    ScopedBoolValue(bool& targetRef, bool value) : target(targetRef), saved(targetRef) {
        target = value;
    }
    ~ScopedBoolValue() { target = saved; }
};

struct ScopedIntValue {
    int* target;
    int saved;
    ScopedIntValue(int* targetPtr, int value) : target(targetPtr), saved(*targetPtr) {
        *target = value;
    }
    ~ScopedIntValue() { *target = saved; }
};

// Game's own _free (0x0091c6b5), matched to the _malloc inside AllocateMemoryOrThrow.
// Used to release pre-allocated blocks that we skip constructing, avoiding heap leaks.
// Must use the game's CRT free — calling our DLL's free on game-malloc'd memory corrupts the heap.
typedef void (*GameFree_t)(void*);
static const GameFree_t GameFree = (GameFree_t)(0x0091c6b5);

typedef uint32_t (__fastcall* ModuleChunkLoadCorePtr_t)(int param1);
ModuleChunkLoadCorePtr_t g_originalModuleChunkLoadCore = nullptr;

// No-op virtual destructor. Written into each GUI object's vtable slot so the teardown's
// virtual destructor dispatch does nothing — neither freeing internal state nor calling
// GameFree on the object block. The object remains valid in memory across loads.
// __fastcall here acts as __thiscall: thisPtr in ECX, edxDummy in EDX, deleteFlag on stack.
static void __fastcall GuiNoOpDtor(void* thisPtr, void* edxDummy, int deleteFlag) {}

// Fake vtable: one entry — the no-op destructor. We write a pointer to this array into
// each GUI object's first word, replacing its real vtable ptr for the duration of teardown.
void* g_fakeVtable[1] = { (void*)&GuiNoOpDtor };

const int MAX_GUI_OBJS = 40;
GuiObjRecord g_guiObjs[MAX_GUI_OBJS];
int g_guiObjCount = 0;

// All unguarded slot offsets in CClientExoApp that ModuleChunkLoadCore always overwrites.
// Guarded slots (0xa0, 0xa4, 0xac, 0xb0, 0xb4) are NOT in this list — they have null-checks
// in the original and persist naturally.
const int k_guiSlots[] = {
    0x08, 0x0c, 0x10, 0x14, 0x18, 0x1c, 0x20, 0x24, 0x28,
    0x38, 0x3c, 0x40, 0x44, 0x48, 0x4c, 0x50, 0x54, 0x58,
    0x5c, 0x60, 0x64, 0x68, 0x6c, 0x70, 0x74, 0x78, 0x7c,
    0x80, 0x84, 0x94, 0x98, 0x9c, 0xb8
};
const int k_numGuiSlots = (int)(sizeof(k_guiSlots) / sizeof(k_guiSlots[0]));

int g_guiSlotValues[k_numGuiSlots] = {};
bool g_guiBuiltSuccessfully = false;

// After the first successful build: save all slot pointers and swap each object's vtable
// to the no-op fake so the teardown's virtual destructor dispatch does nothing.
static void GuiPreserve_SaveAndArmVtables(int* state) {
    g_guiObjCount = 0;
    for (int i = 0; i < k_numGuiSlots; i++) {
        int slotOff = k_guiSlots[i];
        int objAddr = state[slotOff / 4];
        g_guiSlotValues[i] = objAddr;
        if (objAddr == 0) continue;

        int origVtable = *(int*)objAddr;
        // Swap the instance's vtable pointer to the fake (heap is writable, no VirtualProtect needed).
        *(int*)objAddr = (int)g_fakeVtable;
        if (g_guiObjCount < MAX_GUI_OBJS) {
            g_guiObjs[g_guiObjCount++] = { objAddr, origVtable };
        }
    }
    Log("GuiPreserve: armed " + std::to_string(g_guiObjCount) + " objects with no-op vtable");
}

// Before the second call: restore each object's real vtable, restore slot pointers, set flag128=1.
static void GuiPreserve_RestoreAndSkip(int* state) {
    // Restore real vtables on each object so gameplay code can call virtual methods.
    for (int i = 0; i < g_guiObjCount; i++) {
        *(int*)g_guiObjs[i].objAddr = g_guiObjs[i].origVtable;
    }
    // Restore slot pointers into CClientExoApp.
    for (int i = 0; i < k_numGuiSlots; i++) {
        state[k_guiSlots[i] / 4] = g_guiSlotValues[i];
    }
    // Set the game's own idempotency flag — original sees 1 and skips all 31 constructors.
    state[0x128 / 4] = 1;
}

uint32_t __fastcall Hook_ModuleChunkLoadCore(int param1, void* edx) {
    auto start = std::chrono::high_resolution_clock::now();

    const bool outermostModuleChunkLoad = (g_moduleChunkLoadCoreDepth == 0);
    if (outermostModuleChunkLoad) {
        g_deferredInGameTabMask = 0;
        ResetGuiControlsProfile();
    }
    if (!g_cclientExoApp) g_cclientExoApp = (int*)param1;

#if KEEP_ARCHIVE_OPEN_DURING_LOAD
    bool wasArchiveLoadInProgress = g_archiveLoadInProgress;
    if (!wasArchiveLoadInProgress) {
        ArchiveLoad_FlushPinnedHandles();
    }
    g_archiveLoadInProgress = true;
#endif

    uint32_t result;
    {
        ScopedModuleChunkDepth depthScope;
        result = g_originalModuleChunkLoadCore(param1);
    }

#if KEEP_ARCHIVE_OPEN_DURING_LOAD
    g_archiveLoadInProgress = wasArchiveLoadInProgress;
    if (!g_archiveLoadInProgress) {
        ArchiveLoad_FlushPinnedHandles();
    }
#endif

#if PRESERVE_GUI_OBJECTS_ACROSS_LOADS
    // On first successful build, snapshot slot pointers so we can restore them after teardown.
    // Vtable arming happens in Hook_ModuleHandler when packet 8 (the teardown packet) fires.
    if (result == 1 && !g_guiBuiltSuccessfully) {
        for (int i = 0; i < k_numGuiSlots; i++) {
            g_guiSlotValues[i] = ((int*)param1)[k_guiSlots[i] / 4];
        }
        g_guiBuiltSuccessfully = true;
        Log("ModuleChunkLoadCore: snapshot saved for GUI preservation");
    }
#endif

#if DEFER_INGAME_TAB_CONSTRUCTION
    if (outermostModuleChunkLoad && g_deferredInGameTabMask != 0) {
        Log("ModuleChunkLoadCore: deferred in-game tab mask=" +
            std::to_string(g_deferredInGameTabMask));
    }
#endif

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    if (outermostModuleChunkLoad) {
        Log("GUI_FindAndBindControlByTag: " +
            std::to_string(g_guiControlsLookupCache.guiFindTimeUs) +
            " us count=" + std::to_string(g_guiControlsLookupCache.guiFindCalls));
        Log("GFF_CONTROLS_Lookup: original=" +
            std::to_string(g_guiControlsLookupCache.originalLookups) +
            " cache_hits=" + std::to_string(g_guiControlsLookupCache.cacheHits));
    }
    Log("ModuleChunkLoadCore: " + std::to_string(duration.count()) + " μs");
    return result;
}

static bool DeferInGameTabConstructor(void* thisPtr, uint32_t tabIndex) {
#if DEFER_INGAME_TAB_CONSTRUCTION
    if (InterlockedCompareExchange(&g_lazyTabHooksReady, FALSE, FALSE) != FALSE &&
        g_moduleChunkLoadCoreDepth > 0 && !g_lazyTabConstructionInProgress &&
        thisPtr != nullptr && tabIndex < 8) {
        // ModuleChunkLoadCore allocates each object immediately before invoking its
        // constructor. Returning null makes the engine store a null tab slot; release
        // the unused block with the same CRT heap that allocated it.
        GameFree(thisPtr);
        g_deferredInGameTabMask |= (1u << tabIndex);
        return true;
    }
#endif
    return false;
}

typedef void (__thiscall* Texture_ApplyTXIAndBuildControllerPtr_t)(int* thisPtr, uint32_t textureName);
Texture_ApplyTXIAndBuildControllerPtr_t g_originalTexture_ApplyTXIAndBuildController = nullptr;

void __fastcall Hook_Texture_ApplyTXIAndBuildController(int* thisPtr, void* edxDummy, uint32_t textureName) {
    auto start = std::chrono::high_resolution_clock::now();

    g_originalTexture_ApplyTXIAndBuildController(thisPtr, textureName);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("Texture_ApplyTXIAndBuildController: " + std::to_string(duration.count()) + " μs");
}

#if HOOK_TEXTURE_CACHE_TIMING
typedef int (__cdecl* Texture_FindExistingPtr_t)(char* primaryName, char* variantName, int variantArray, int variantCount);
Texture_FindExistingPtr_t g_originalTexture_FindExisting = nullptr;

int __cdecl Hook_Texture_FindExisting(char* primaryName, char* variantName, int variantArray, int variantCount) {
    auto start = std::chrono::high_resolution_clock::now();

    int result = g_originalTexture_FindExisting(primaryName, variantName, variantArray, variantCount);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("Texture_FindExisting: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef int (__cdecl* Texture_AcquireAndReleasePtr_t)(char* textureName, char* overrideName);
Texture_AcquireAndReleasePtr_t g_originalTexture_AcquireAndRelease = nullptr;

int __cdecl Hook_Texture_AcquireAndRelease(char* textureName, char* overrideName) {
    auto start = std::chrono::high_resolution_clock::now();

    int result = g_originalTexture_AcquireAndRelease(textureName, overrideName);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("Texture_AcquireAndRelease: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef int (__cdecl* Texture_GetOrCreatePtr_t)(char* textureName, int existingTexture, char* overrideName, int variantArray, int variantCount);
Texture_GetOrCreatePtr_t g_originalTexture_GetOrCreate = nullptr;

int __cdecl Hook_Texture_GetOrCreate(char* textureName, int existingTexture, char* overrideName, int variantArray, int variantCount) {
    auto start = std::chrono::high_resolution_clock::now();

    int result = g_originalTexture_GetOrCreate(textureName, existingTexture, overrideName, variantArray, variantCount);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("Texture_GetOrCreate: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef int (__cdecl* Texture_UpdateResourceBindingPtr_t)(int* textureRefObj, char* currentName, int param3, char* replacementName, int param5, int param6);
Texture_UpdateResourceBindingPtr_t g_originalTexture_UpdateResourceBinding = nullptr;

int __cdecl Hook_Texture_UpdateResourceBinding(int* textureRefObj, char* currentName, int param3, char* replacementName, int param5, int param6) {
    auto start = std::chrono::high_resolution_clock::now();

    int result = g_originalTexture_UpdateResourceBinding(textureRefObj, currentName, param3, replacementName, param5, param6);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("Texture_UpdateResourceBinding: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef int (__cdecl* Texture_ReplaceAcrossUsersPtr_t)(int collectionA, int collectionB, char* newTextureName, char* oldTextureName);
Texture_ReplaceAcrossUsersPtr_t g_originalTexture_ReplaceAcrossUsers = nullptr;

int __cdecl Hook_Texture_ReplaceAcrossUsers(int collectionA, int collectionB, char* newTextureName, char* oldTextureName) {
    auto start = std::chrono::high_resolution_clock::now();

    int result = g_originalTexture_ReplaceAcrossUsers(collectionA, collectionB, newTextureName, oldTextureName);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("Texture_ReplaceAcrossUsers: " + std::to_string(duration.count()) + " μs");
    return result;
}
#endif

#if HOOK_PARSE_TXI_AND_BUILD_TEXTURE_CONTROLLER_TIMING
typedef void (__thiscall* ParseTXIAndBuildTextureControllerPtr_t)(int* thisPtr, char* txiLine);
ParseTXIAndBuildTextureControllerPtr_t g_originalParseTXIAndBuildTextureController = nullptr;

void __fastcall Hook_ParseTXIAndBuildTextureController(int* thisPtr, void* edxDummy, char* txiLine) {
    auto start = std::chrono::high_resolution_clock::now();

    g_originalParseTXIAndBuildTextureController(thisPtr, txiLine);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ParseTXIAndBuildTextureController: " + std::to_string(duration.count()) + " μs");
}
#endif

typedef void (__thiscall* Texture_ApplyTXIBlendingModePtr_t)(int* thisPtr, uint32_t textureName, int materialState);
Texture_ApplyTXIBlendingModePtr_t g_originalTexture_ApplyTXIBlendingMode = nullptr;

void __fastcall Hook_Texture_ApplyTXIBlendingMode(int* thisPtr, void* edxDummy, uint32_t textureName, int materialState) {
    auto start = std::chrono::high_resolution_clock::now();

    g_originalTexture_ApplyTXIBlendingMode(thisPtr, textureName, materialState);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("Texture_ApplyTXIBlendingMode: " + std::to_string(duration.count()) + " μs");
}

typedef void (__thiscall* Texture_ApplyTXIMaterialDirectivesPtr_t)(int* thisPtr, uint32_t textureName);
Texture_ApplyTXIMaterialDirectivesPtr_t g_originalTexture_ApplyTXIMaterialDirectives = nullptr;

void __fastcall Hook_Texture_ApplyTXIMaterialDirectives(int* thisPtr, void* edxDummy, uint32_t textureName) {
    auto start = std::chrono::high_resolution_clock::now();

    g_originalTexture_ApplyTXIMaterialDirectives(thisPtr, textureName);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("Texture_ApplyTXIMaterialDirectives: " + std::to_string(duration.count()) + " Î¼s");
}

typedef void* (__fastcall* CSWGuiLoadModuleDebugMenu_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiLoadModuleDebugMenu_CtorPtr_t g_originalCSWGuiLoadModuleDebugMenu_Ctor = nullptr;

void* __fastcall Hook_CSWGuiLoadModuleDebugMenu_Ctor(void* thisPtr, void* edx, uint32_t param1){
#if SKIP_DEBUG_GUI_CONSTRUCTION
    if (g_moduleChunkLoadCoreDepth > 0) {
        GameFree(thisPtr);
        return nullptr;
    }
#endif
    return g_originalCSWGuiLoadModuleDebugMenu_Ctor(thisPtr, edx, param1);
}

typedef uint32_t* (__fastcall* CSWGuiPowersFeatsSkillsDebugMenu_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiPowersFeatsSkillsDebugMenu_CtorPtr_t g_originalCSWGuiPowersFeatsSkillsDebugMenu_Ctor = nullptr;

uint32_t* __fastcall Hook_CSWGuiPowersFeatsSkillsDebugMenu_Ctor(void* thisPtr, void* edx, uint32_t param1){
#if SKIP_DEBUG_GUI_CONSTRUCTION
    if (g_moduleChunkLoadCoreDepth > 0) {
        GameFree(thisPtr);
        return nullptr;
    }
#endif
    return g_originalCSWGuiPowersFeatsSkillsDebugMenu_Ctor(thisPtr, edx, param1);
}

typedef void* (__fastcall* CSWGuiDialogCinematic_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiDialogCinematic_CtorPtr_t g_originalCSWGuiDialogCinematic_Ctor = nullptr;

void* __fastcall Hook_CSWGuiDialogCinematic_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();

    void* result = g_originalCSWGuiDialogCinematic_Ctor(thisPtr,edx,param1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiDialogCinematic_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiDialogComputerCamera_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiDialogComputerCamera_CtorPtr_t g_originalCSWGuiDialogComputerCamera_Ctor = nullptr;

uint32_t* __fastcall Hook_CSWGuiDialogComputerCamera_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t* result = g_originalCSWGuiDialogComputerCamera_Ctor(thisPtr,edx,param1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiDialogComputerCamera_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiDialogComputer_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiDialogComputer_CtorPtr_t g_originalCSWGuiDialogComputer_Ctor = nullptr;

uint32_t* __fastcall Hook_CSWGuiDialogComputer_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t* result = g_originalCSWGuiDialogComputer_Ctor(thisPtr,edx,param1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiDialogComputer_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiContainer_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiContainer_CtorPtr_t g_originalCSWGuiContainer_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiContainer_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t* result = g_originalCSWGuiContainer_Ctor(thisPtr,edx,param1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiContainer_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiExamine_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiExamine_CtorPtr_t g_originalCSWGuiExamine_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiExamine_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t* result = g_originalCSWGuiExamine_Ctor(thisPtr,edx,param1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiExamine_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiCreateDebugItemSubMenu_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiCreateDebugItemSubMenu_CtorPtr_t g_originalCSWGuiCreateDebugItemSubMenu_Ctor = nullptr;

uint32_t* __fastcall Hook_CSWGuiCreateDebugItemSubMenu_Ctor(void* thisPtr, void* edx, uint32_t param1){
#if SKIP_DEBUG_GUI_CONSTRUCTION
    if (g_moduleChunkLoadCoreDepth > 0) {
        GameFree(thisPtr);
        return nullptr;
    }
#endif
    return g_originalCSWGuiCreateDebugItemSubMenu_Ctor(thisPtr, edx, param1);
}

typedef uint32_t* (__fastcall* CSWGuiTutorialBox_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiTutorialBox_CtorPtr_t g_originalCSWGuiTutorialBox_Ctor = nullptr;

uint32_t* __fastcall Hook_CSWGuiTutorialBox_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t* result = g_originalCSWGuiTutorialBox_Ctor(thisPtr,edx,param1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiTutorialBox_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiSkillInfoBox_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiSkillInfoBox_CtorPtr_t g_originalCSWGuiSkillInfoBox_Ctor = nullptr;

uint32_t* __fastcall Hook_CSWGuiSkillInfoBox_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t* result = g_originalCSWGuiSkillInfoBox_Ctor(thisPtr,edx,param1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiSkillInfoBox_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiBarkBubble_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiBarkBubble_CtorPtr_t g_originalCSWGuiBarkBubble_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiBarkBubble_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiBarkBubble_Ctor(thisPtr,edx,param1);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiBarkBubble_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiMessageBox_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiMessageBox_CtorPtr_t g_originalCSWGuiMessageBox_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiMessageBox_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiMessageBox_Ctor(thisPtr,edx,param1);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiMessageBox_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__thiscall* CSWGuiMessageBoxVariant_CtorPtr_t)(
    void* thisPtr, uint32_t guiContext, int variant);
CSWGuiMessageBoxVariant_CtorPtr_t g_originalCSWGuiMessageBoxVariant_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiMessageBoxVariant_Ctor(
    void* thisPtr, void* edxDummy, uint32_t guiContext, int variant) {
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiMessageBoxVariant_Ctor(thisPtr, guiContext, variant);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiMessageBoxVariant_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiDialogLetterbox_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiDialogLetterbox_CtorPtr_t g_originalCSWGuiDialogLetterbox_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiDialogLetterbox_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiDialogLetterbox_Ctor(thisPtr,edx,param1);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiDialogLetterbox_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiFade_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiFade_CtorPtr_t g_originalCSWGuiFade_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiFade_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiFade_Ctor(thisPtr,edx,param1);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiFade_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiInGameMenu_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiInGameMenu_CtorPtr_t g_originalCSWGuiInGameMenu_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiInGameMenu_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiInGameMenu_Ctor(thisPtr,edx,param1);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiInGameMenu_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiInGamePause_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiInGamePause_CtorPtr_t g_originalCSWGuiInGamePause_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiInGamePause_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiInGamePause_Ctor(thisPtr,edx,param1);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiInGamePause_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiInGameSoloModeQuery_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiInGameSoloModeQuery_CtorPtr_t g_originalCSWGuiInGameSoloModeQuery_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiInGameSoloModeQuery_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiInGameSoloModeQuery_Ctor(thisPtr,edx,param1);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiInGameSoloModeQuery_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiInGameAreaTransition_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiInGameAreaTransition_CtorPtr_t g_originalCSWGuiInGameAreaTransition_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiInGameAreaTransition_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiInGameAreaTransition_Ctor(thisPtr,edx,param1);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiInGameAreaTransition_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiInGameMessages_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiInGameMessages_CtorPtr_t g_originalCSWGuiInGameMessages_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiInGameMessages_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiInGameMessages_Ctor(thisPtr,edx,param1);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiInGameMessages_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiStore_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiStore_CtorPtr_t g_originalCSWGuiStore_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiStore_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiStore_Ctor(thisPtr,edx,param1);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiStore_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiInGameEquip_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiInGameEquip_CtorPtr_t g_originalCSWGuiInGameEquip_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiInGameEquip_Ctor(void* thisPtr, void* edx, uint32_t param1){
    if (DeferInGameTabConstructor(thisPtr, 0)) return nullptr;
    return g_originalCSWGuiInGameEquip_Ctor(thisPtr, edx, param1);
}

typedef uint32_t* (__fastcall* CSWGuiInGameInventory_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiInGameInventory_CtorPtr_t g_originalCSWGuiInGameInventory_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiInGameInventory_Ctor(void* thisPtr, void* edx, uint32_t param1){
    if (DeferInGameTabConstructor(thisPtr, 1)) return nullptr;
    return g_originalCSWGuiInGameInventory_Ctor(thisPtr, edx, param1);
}

typedef uint32_t* (__fastcall* CSWGuiInGameCharacter_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiInGameCharacter_CtorPtr_t g_originalCSWGuiInGameCharacter_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiInGameCharacter_Ctor(void* thisPtr, void* edx, uint32_t param1){
    if (DeferInGameTabConstructor(thisPtr, 2)) return nullptr;
    return g_originalCSWGuiInGameCharacter_Ctor(thisPtr, edx, param1);
}

typedef uint32_t* (__fastcall* CSWGuiStatusSummary_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiStatusSummary_CtorPtr_t g_originalCSWGuiStatusSummary_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiStatusSummary_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiStatusSummary_Ctor(thisPtr,edx,param1);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiStatusSummary_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiInGameMap_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiInGameMap_CtorPtr_t g_originalCSWGuiInGameMap_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiInGameMap_Ctor(void* thisPtr, void* edx, uint32_t param1){
    if (DeferInGameTabConstructor(thisPtr, 6)) return nullptr;
    return g_originalCSWGuiInGameMap_Ctor(thisPtr, edx, param1);
}

typedef uint32_t* (__fastcall* CSWGuiInGameAbilities_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiInGameAbilities_CtorPtr_t g_originalCSWGuiInGameAbilities_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiInGameAbilities_Ctor(void* thisPtr, void* edx, uint32_t param1){
    if (DeferInGameTabConstructor(thisPtr, 3)) return nullptr;
    return g_originalCSWGuiInGameAbilities_Ctor(thisPtr, edx, param1);
}

typedef uint32_t* (__fastcall* CSWGuiInGameJournal_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiInGameJournal_CtorPtr_t g_originalCSWGuiInGameJournal_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiInGameJournal_Ctor(void* thisPtr, void* edx, uint32_t param1){
    if (DeferInGameTabConstructor(thisPtr, 5)) return nullptr;
    return g_originalCSWGuiInGameJournal_Ctor(thisPtr, edx, param1);
}

typedef uint32_t* (__fastcall* CSWGuiInGameOptions_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiInGameOptions_CtorPtr_t g_originalCSWGuiInGameOptions_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiInGameOptions_Ctor(void* thisPtr, void* edx, uint32_t param1){
    if (DeferInGameTabConstructor(thisPtr, 7)) return nullptr;
    return g_originalCSWGuiInGameOptions_Ctor(thisPtr, edx, param1);
}

typedef uint32_t* (__fastcall* CSWGuiPartySelection_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiPartySelection_CtorPtr_t g_originalCSWGuiPartySelection_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiPartySelection_Ctor(void* thisPtr, void* edx, uint32_t param1){
    if (DeferInGameTabConstructor(thisPtr, 4)) return nullptr;
    return g_originalCSWGuiPartySelection_Ctor(thisPtr, edx, param1);
}

typedef uint32_t* (__fastcall* CSWGuiInGameGalaxyMap_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiInGameGalaxyMap_CtorPtr_t g_originalCSWGuiInGameGalaxyMap_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiInGameGalaxyMap_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiInGameGalaxyMap_Ctor(thisPtr,edx,param1);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiInGameGalaxyMap_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef void (__thiscall* CSWGuiInGamePanel_LazyInitTabPtr_t)(int thisPtr, int previousTab, int requestedTab);
CSWGuiInGamePanel_LazyInitTabPtr_t g_originalCSWGuiInGamePanel_LazyInitTab = nullptr;

void __fastcall Hook_CSWGuiInGamePanel_LazyInitTab(
    int thisPtr, void* edxDummy, int previousTab, int requestedTab) {
#if DEFER_INGAME_TAB_CONSTRUCTION
    if (thisPtr != 0 && requestedTab >= 0 && requestedTab < 8) {
        int* requestedSlot = (int*)(thisPtr + 0x0c + requestedTab * 4);
        if (*requestedSlot == 0) {
            // The stock lazy dispatcher is gated off when the PC eager-load mode is
            // enabled. Temporarily expose the engine's own lazy path, and pass -1 as
            // the previous tab so it constructs the missing target without evicting
            // any already-created tab object.
            int* eagerLoadGuiPanels = (int*)0x00a10760;
            if (*eagerLoadGuiPanels == 0) {
                g_originalCSWGuiInGamePanel_LazyInitTab(thisPtr, previousTab, requestedTab);
                return;
            }

            ScopedIntValue lazyMode(eagerLoadGuiPanels, 0);
            ScopedBoolValue constructionScope(g_lazyTabConstructionInProgress, true);
            g_originalCSWGuiInGamePanel_LazyInitTab(thisPtr, -1, requestedTab);
            return;
        }
    }
#endif
    g_originalCSWGuiInGamePanel_LazyInitTab(thisPtr, previousTab, requestedTab);
}

typedef void (__cdecl* LoadingScreenUpdateFramePtr_t)(
    float deltaTime, int runLoadingScreenWork, int suppressPresent);
LoadingScreenUpdateFramePtr_t g_originalLoadingScreenUpdateFrame=nullptr;
thread_local DWORD g_lastLoadingScreenPresentTick = 0;
static volatile LONG g_loadingScreenThrottleLogged = 0;

void __cdecl Hook_LoadingScreenUpdateFrame(
    float deltaTime, int runLoadingScreenWork, int suppressPresent) {
    auto start = std::chrono::high_resolution_clock::now();

    int effectiveSuppressPresent = suppressPresent;
#if THROTTLE_LOADING_SCREEN_PRESENTS
    if (suppressPresent == 0) {
        DWORD now = GetTickCount();
        DWORD elapsed = now - g_lastLoadingScreenPresentTick;
        if (g_lastLoadingScreenPresentTick != 0 &&
            elapsed < LOADING_SCREEN_PRESENT_INTERVAL_MS) {
            // The original function still updates GUI state, pumps Windows
            // messages, advances resource work, services audio, and performs its
            // final cleanup.  Only the clear/SwapBuffers presentation is omitted.
            effectiveSuppressPresent = 1;
            if (InterlockedCompareExchange(
                    &g_loadingScreenThrottleLogged, 1, 0) == 0) {
                Log("LoadingScreenPresentThrottle: active interval_ms=" +
                    std::to_string(LOADING_SCREEN_PRESENT_INTERVAL_MS));
            }
        } else {
            g_lastLoadingScreenPresentTick = now;
        }
    }
#endif

#if SKIP_LOADING_SCREEN_UPDATE_FRAME_IN_MODULE_CHUNK_LOAD_CORE
    bool calledFromModuleChunkLoadCore = (g_moduleChunkLoadCoreDepth > 0);
    // Skipping LoadingScreenUpdateFrame when called from ModuleChunkLoadCore with param2 == 0, as this seems to be redundant.
    bool skipLoadingScreenUpdate = calledFromModuleChunkLoadCore && runLoadingScreenWork == 0;

    if (skipLoadingScreenUpdate) {
        Log("LoadingScreenUpdateFrame: skipped inside ModuleChunkLoadCore");
    }
    else{
        g_originalLoadingScreenUpdateFrame(
            deltaTime, runLoadingScreenWork, effectiveSuppressPresent);
    }
#else
    g_originalLoadingScreenUpdateFrame(
        deltaTime, runLoadingScreenWork, effectiveSuppressPresent);
#endif

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("LoadingScreenUpdateFrame: " + std::to_string(duration.count()) + " μs");
}

typedef uint32_t (__fastcall* AppState_GetGuiContextPtr_t)(void* thisPtr, void* edx);
AppState_GetGuiContextPtr_t g_originalAppState_GetGuiContext = nullptr;

uint32_t __fastcall Hook_AppState_GetGuiContext(void* thisPtr, void* edx){
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t result = g_originalAppState_GetGuiContext(thisPtr, edx);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("AppState_GetGuiContext: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef unsigned char (__fastcall* AppState_GetLoadProgressBytePtr_t)(void* thisPtr, void* edx, int index);
AppState_GetLoadProgressBytePtr_t g_originalAppState_GetLoadProgressByte = nullptr;

unsigned char __fastcall Hook_AppState_GetLoadProgressByte(void* thisPtr, void* edx, int index){
    auto start = std::chrono::high_resolution_clock::now();
    unsigned char result = g_originalAppState_GetLoadProgressByte(thisPtr, edx, index);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("AppState_GetLoadProgressByte: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef int (__cdecl* Runtime_FloatToInt_ST0Ptr_t)();
Runtime_FloatToInt_ST0Ptr_t g_originalRuntime_FloatToInt_ST0 = nullptr;

int __cdecl Hook_Runtime_FloatToInt_ST0(){
    auto start = std::chrono::high_resolution_clock::now();
    int result = g_originalRuntime_FloatToInt_ST0();
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("Runtime_FloatToInt_ST0: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef void (__fastcall* AppState_SetLoadBarValuePtr_t)(void* thisPtr, void* edx, int value, int updateFlag);
AppState_SetLoadBarValuePtr_t g_originalAppState_SetLoadBarValue = nullptr;

void __fastcall Hook_AppState_SetLoadBarValue(void* thisPtr, void* edx, int value, int updateFlag){
    auto start = std::chrono::high_resolution_clock::now();
    g_originalAppState_SetLoadBarValue(thisPtr, edx, value, updateFlag);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("AppState_SetLoadBarValue: " + std::to_string(duration.count()) + " μs");
}

typedef void (__fastcall* CSWGuiFade_SetTransitionStatePtr_t)(void* thisPtr, void* edx, int mode, uint32_t progress, uint32_t duration, uint32_t* targetColor);
CSWGuiFade_SetTransitionStatePtr_t g_originalCSWGuiFade_SetTransitionState = nullptr;

void __fastcall Hook_CSWGuiFade_SetTransitionState(void* thisPtr, void* edx, int mode, uint32_t progress, uint32_t duration, uint32_t* targetColor){
    auto start = std::chrono::high_resolution_clock::now();
    g_originalCSWGuiFade_SetTransitionState(thisPtr, edx, mode, progress, duration, targetColor);
    auto end = std::chrono::high_resolution_clock::now();
    auto durationTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
}

typedef void (__fastcall* LoadingScreenFadeUpdateFramePtr_t)(void* thisPtr, void* edx);
LoadingScreenFadeUpdateFramePtr_t g_originalLoadingScreenFadeUpdateFrame = nullptr;

void __fastcall Hook_LoadingScreenFadeUpdateFrame(void* thisPtr, void* edx){
    auto start = std::chrono::high_resolution_clock::now();
    g_originalLoadingScreenFadeUpdateFrame(thisPtr, edx);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("LoadingScreenFadeUpdateFrame: " + std::to_string(duration.count()) + " Î¼s");
}

typedef void (__cdecl* ConfigParsePtr_t)(char* filename);
ConfigParsePtr_t g_originalConfigParse=nullptr;

void __cdecl Hook_ConfigParse(char* filename){
    auto start = std::chrono::high_resolution_clock::now();
    
    g_originalConfigParse(filename);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ConfigParse: " + std::to_string(duration.count()) + " μs");
}

typedef void (__cdecl* LevelLoaderAndInitializerPtr_t)(char* filename,char* param_2,int param_3,uint32_t param_4);
LevelLoaderAndInitializerPtr_t g_originalLevelLoaderAndInitializer=nullptr;

void __cdecl Hook_LevelLoaderAndInitializer(char* filename,char* param_2,int param_3,uint32_t param_4){
    auto start = std::chrono::high_resolution_clock::now();
    
    Log(std::string("Loading from file: ") + (filename ? filename : "(null)"));
    g_originalLevelLoaderAndInitializer(filename,param_2,param_3,param_4);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("LevelLoaderAndInitializer: " + std::to_string(duration.count()) + " μs");
}

typedef FILE* (__cdecl* _fopenptr_t)(char* filename, char* mode);
_fopenptr_t g_originalfopen=nullptr;

// Real CClientExoApp pointer, captured from the first Hook_ModuleChunkLoadCore call.
// The pointer-chain *(*(0x00a1b4a4) + 0x4) read garbage in testing — the real pointer
// is whatever ECX holds when the game calls ModuleChunkLoadCore.
int* g_cclientExoApp = nullptr;

static int g_lastFlag128 = -1, g_lastSlot08 = -1, g_lastSlot14 = -1;
static void CheckGuiTeardown(const char* tag) {
    if (!g_cclientExoApp) return;
    int f128 = g_cclientExoApp[0x128/4];
    int s08 = g_cclientExoApp[0x08/4];
    int s14 = g_cclientExoApp[0x14/4];
    if (f128 != g_lastFlag128 || s08 != g_lastSlot08 || s14 != g_lastSlot14) {
        Log(std::string("[TEARDOWN-WATCH ") + tag + "] flag128 " +
            std::to_string(g_lastFlag128) + "->" + std::to_string(f128) +
            " slot08 " + std::to_string(g_lastSlot08) + "->" + std::to_string(s08) +
            " slot14 " + std::to_string(g_lastSlot14) + "->" + std::to_string(s14));
        g_lastFlag128 = f128; g_lastSlot08 = s08; g_lastSlot14 = s14;
    }
}

FILE* __cdecl Hook_fopen(char* filename, char* mode){
    auto start = std::chrono::high_resolution_clock::now();

    CheckGuiTeardown(filename ? filename : "(null)");

    if (filename == nullptr || *filename == '\0') {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        Log("fopen: " + std::to_string(duration.count()) + " μs");
        return nullptr;
    }

    Log("fopening file: " + std::string(filename));

    FILE* result=g_originalfopen(filename,mode);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("fopen: " + std::to_string(duration.count()) + " μs");
    return result;
}


typedef uint32_t* (__thiscall* DebugMenuContructorPtr_t)(void* thisPtr, uint32_t param1);
DebugMenuContructorPtr_t g_originalDebugMenuConstructor=nullptr;
uint32_t* __fastcall Hook_DebugMenuConstructor(void* thisPtr, void* edxDummy, uint32_t param1){
#if SKIP_DEBUG_GUI_CONSTRUCTION
    if (g_moduleChunkLoadCoreDepth > 0) {
        GameFree(thisPtr);
        return nullptr;
    }
#endif
    return g_originalDebugMenuConstructor(thisPtr, param1);
}

typedef void* (__fastcall* gobconstructorPtr_t)(uint32_t* thisptr, void* edx,char* name);
gobconstructorPtr_t g_originalgobconstructor=nullptr;

void* __fastcall Hook_gobconstructor(uint32_t* thisptr, void* edx,char* name){
    auto start = std::chrono::high_resolution_clock::now();

    void* result=g_originalgobconstructor(thisptr,edx,name);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("gobconstructor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef void (__thiscall* Gob_LoadFromFileOrStreamPtr_t)(uint32_t* thisptr, uint32_t resourceName);
Gob_LoadFromFileOrStreamPtr_t g_originalGob_LoadFromFileOrStream = nullptr;

void __fastcall Hook_Gob_LoadFromFileOrStream(uint32_t* thisptr, void* edx, uint32_t resourceName){
    auto start = std::chrono::high_resolution_clock::now();

    g_originalGob_LoadFromFileOrStream(thisptr, resourceName);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("Gob_LoadFromFileOrStream: " + std::to_string(duration.count()) + " μs");
}

typedef void* (__fastcall* AreaConstructorPtr_t)(uint32_t* thisptr, void* edx,uint32_t param2,uint32_t param3,int param4);
AreaConstructorPtr_t g_originalAreaConstructor=nullptr;

void* __fastcall Hook_AreaConstructor(uint32_t* thisptr, void* edx,uint32_t param2,uint32_t param3,int param4){
    auto start = std::chrono::high_resolution_clock::now();

    void* result=g_originalAreaConstructor(thisptr,edx,param2,param3,param4);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("areaconstructor: " + std::to_string(duration.count()) + " μs");
    return result;
}



typedef void* (__fastcall* InitializeGameUIPtr_t)(void* param1,void* edxdummy,int param2);
InitializeGameUIPtr_t g_originalInitializeGameUIPtr_t = nullptr;

void* __fastcall Hook_InitializeGameUI(void* param1,void* edxdummy,int param2){
    auto start = std::chrono::high_resolution_clock::now();

    void* result=g_originalInitializeGameUIPtr_t(param1,edxdummy,param2);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("InitializeGameUI: " + std::to_string(duration.count()) + " μs");
    return result;
}


typedef void* (__fastcall* GUI_Update3DSceneViewPtr_t)(int param1,void* edxdummy,int* param2,unsigned int param3,int param4);
GUI_Update3DSceneViewPtr_t g_originalGUI_Update3DSceneViewPtr_t = nullptr;

void* __fastcall Hook_GUI_Update3DSceneView(int param1,void* edxdummy,int* param2,unsigned int param3,int param4){
    auto start = std::chrono::high_resolution_clock::now();

    void* result=g_originalGUI_Update3DSceneViewPtr_t(param1,edxdummy,param2,param3,param4);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("GUI_Update3DSceneView: " + std::to_string(duration.count()) + " μs");
    return result;
}

// typedef int (__cdecl* AllocateMemoryOrThrowPtr_t)(size_t param_1);
// AllocateMemoryOrThrowPtr_t g_originalAllocateMemoryOrThrow = nullptr;

// int __cdecl Hook_AllocateMemoryOrThrow(size_t param_1){
//     auto start = std::chrono::high_resolution_clock::now();

//     int result=g_originalAllocateMemoryOrThrow(param_1);

//     auto end = std::chrono::high_resolution_clock::now();
//     auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
//     Log("AllocateMemoryOrThrow: " + std::to_string(duration.count()) + " μs");
//     return result;
// }

typedef int (__thiscall* ModuleDirectoryScannerPtr_t)(
    void* thisPtr, int param1, uint32_t param2, uint32_t param3, int param4, int param5);
ModuleDirectoryScannerPtr_t g_originalModuleDirectoryScanner = nullptr;

int __fastcall Hook_ModuleDirectoryScanner(
    void* thisPtr, void* edxDummy,
    int param1, uint32_t param2, uint32_t param3, int param4, int param5) {
    auto start = std::chrono::high_resolution_clock::now();

    int result = g_originalModuleDirectoryScanner(
        thisPtr, param1, param2, param3, param4, param5);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ModuleDirectoryScanner: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef void (__fastcall* ArrayAddPtr_t)(int* param1,void* edxdummy,uint32_t param2);
ArrayAddPtr_t g_originalArrayAdd = nullptr;

void __fastcall Hook_ArrayAdd(int* param1,void* edxdummy,uint32_t param2){
    auto start = std::chrono::high_resolution_clock::now();

    g_originalArrayAdd(param1,edxdummy,param2);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ArrayAdd: " + std::to_string(duration.count()) + " μs");
}

typedef void* (__cdecl* OpenOrStreamGameFilePtr_t)(char *param1, char *param2, uint32_t *param3, char param4);
OpenOrStreamGameFilePtr_t g_originalOpenOrStreamGameFile = nullptr;

void* __cdecl Hook_OpenOrStreamGameFile(char *param1, char *param2, uint32_t *param3, char param4){
    auto start = std::chrono::high_resolution_clock::now();

    void* result = g_originalOpenOrStreamGameFile(param1, param2, param3, param4);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("OpenOrStreamGameFile: " + std::to_string(duration.count()) + " μs");

    return result;
}

typedef void (__thiscall* GUI_FindAndBindControlByTagPtr_t)(
    int thisPtr, int param1, int gffPtr, int controlsListPtr, void* requestedTag);
GUI_FindAndBindControlByTagPtr_t g_originalGUI_FindAndBindControlByTag = nullptr;

void __fastcall Hook_GUI_FindAndBindControlByTag(int thisPtr, void* edxDummy, int param1, int gffPtr, int controlsListPtr, void* requestedTag) {
    auto start = std::chrono::high_resolution_clock::now();

    bool outermostGuiFind = (g_guiControlsLookupCache.guiFindDepth == 0);
    if (outermostGuiFind) {
        g_guiControlsLookupCache.valid = false;
    }
    ++g_guiControlsLookupCache.guiFindDepth;

    g_originalGUI_FindAndBindControlByTag(
        thisPtr, param1, gffPtr, controlsListPtr, requestedTag);

    --g_guiControlsLookupCache.guiFindDepth;
    if (outermostGuiFind) {
        g_guiControlsLookupCache.valid = false;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    ++g_guiControlsLookupCache.guiFindCalls;
    g_guiControlsLookupCache.guiFindTimeUs += duration.count();
}

typedef void (__fastcall* GUI_BindNamedWidgetPtr_t)(int thisPtr, void* edxDummy, int* param1, unsigned int param2, int param3, int param4);
GUI_BindNamedWidgetPtr_t g_originalGUI_BindNamedWidget = nullptr;

void __fastcall Hook_GUI_BindNamedWidget(int thisPtr, void* edxDummy, int* param1, unsigned int param2, int param3, int param4){
    auto start = std::chrono::high_resolution_clock::now();

    g_originalGUI_BindNamedWidget(thisPtr, edxDummy, param1, param2, param3, param4);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("GUI_BindNamedWidget: " + std::to_string(duration.count()) + " μs");
}

typedef void (__thiscall* GUI_InitWidgetFromGFFPtr_t)(
    int* thisPtr, uint32_t param1, int param2, int param3);
GUI_InitWidgetFromGFFPtr_t g_originalGUI_InitWidgetFromGFF = nullptr;

void __fastcall Hook_GUI_InitWidgetFromGFF(
    int* thisPtr, void* edxDummy, uint32_t param1, int param2, int param3) {
    auto start = std::chrono::high_resolution_clock::now();

    g_originalGUI_InitWidgetFromGFF(thisPtr, param1, param2, param3);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("GUI_InitWidgetFromGFF: " + std::to_string(duration.count()) + " μs");
}

typedef void (__thiscall* GUI_BindChildStructByNamePtr_t)(int thisPtr, int param1, int param2, int param3, int param4);
GUI_BindChildStructByNamePtr_t g_originalGUI_BindChildStructByName = nullptr;

void __fastcall Hook_GUI_BindChildStructByName(int thisPtr, void* edxDummy, int param1, int param2, int param3, int param4) {
    auto start = std::chrono::high_resolution_clock::now();

    g_originalGUI_BindChildStructByName(thisPtr, param1, param2, param3, param4);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("GUI_BindChildStructByName: " + std::to_string(duration.count()) + " Î¼s");
}

typedef void (__thiscall* GUI_BaseControlSetupPtr_t)(int thisPtr, int param1, int param2);
GUI_BaseControlSetupPtr_t g_originalGUI_BaseControlSetup = nullptr;

void __fastcall Hook_GUI_BaseControlSetup(int thisPtr, void* edxDummy, int param1, int param2) {
    auto start = std::chrono::high_resolution_clock::now();

    g_originalGUI_BaseControlSetup(thisPtr, param1, param2);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("GUI_BaseControlSetup: " + std::to_string(duration.count()) + " Î¼s");
}

typedef void (__thiscall* GUI_CommonBaseBinderPtr_t)(int thisPtr, int param1, int param2);
GUI_CommonBaseBinderPtr_t g_originalGUI_CommonBaseBinder = nullptr;

void __fastcall Hook_GUI_CommonBaseBinder(int thisPtr, void* edxDummy, int param1, int param2) {
    auto start = std::chrono::high_resolution_clock::now();

    g_originalGUI_CommonBaseBinder(thisPtr, param1, param2);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("GUI_CommonBaseBinder: " + std::to_string(duration.count()) + " Î¼s");
}

typedef void (__thiscall* GUI_ListBoxBindProtoItemPtr_t)(int thisPtr, int param1, int param2);
GUI_ListBoxBindProtoItemPtr_t g_originalGUI_ListBoxBindProtoItem = nullptr;

void __fastcall Hook_GUI_ListBoxBindProtoItem(int thisPtr, void* edxDummy, int param1, int param2) {
    auto start = std::chrono::high_resolution_clock::now();

    g_originalGUI_ListBoxBindProtoItem(thisPtr, param1, param2);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("GUI_ListBoxBindProtoItem: " + std::to_string(duration.count()) + " Î¼s");
}

typedef unsigned char (__thiscall* GFF_ReadBoolFieldByNamePtr_t)(int gffPtr, int structPtr, const char* fieldName, int* foundOut, unsigned char defaultValue);
GFF_ReadBoolFieldByNamePtr_t g_originalGFF_ReadBoolFieldByName = nullptr;

unsigned char __fastcall Hook_GFF_ReadBoolFieldByName(int gffPtr, void* edxDummy, int structPtr, const char* fieldName, int* foundOut, unsigned char defaultValue) {
    auto start = std::chrono::high_resolution_clock::now();

    unsigned char result = g_originalGFF_ReadBoolFieldByName(gffPtr, structPtr, fieldName, foundOut, defaultValue);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("GFF_ReadBoolFieldByName: " + std::to_string(duration.count()) + " Î¼s");
    return result;
}

typedef int (__thiscall* GFF_ReadIntFieldByNamePtr_t)(int gffPtr, int structPtr, const char* fieldName, int* foundOut, int defaultValue);
GFF_ReadIntFieldByNamePtr_t g_originalGFF_ReadIntFieldByName = nullptr;

int __fastcall Hook_GFF_ReadIntFieldByName(int gffPtr, void* edxDummy, int structPtr, const char* fieldName, int* foundOut, int defaultValue) {
    auto start = std::chrono::high_resolution_clock::now();

    int result = g_originalGFF_ReadIntFieldByName(gffPtr, structPtr, fieldName, foundOut, defaultValue);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("GFF_ReadIntFieldByName: " + std::to_string(duration.count()) + " Î¼s");
    return result;
}

typedef uint32_t* (__thiscall* GFF_ReadVector3FieldByNamePtr_t)(int gffPtr, uint32_t* outVec, int structPtr, const char* fieldName, int* foundOut, uint32_t* defaultVec);
GFF_ReadVector3FieldByNamePtr_t g_originalGFF_ReadVector3FieldByName = nullptr;

uint32_t* __fastcall Hook_GFF_ReadVector3FieldByName(int gffPtr, void* edxDummy, uint32_t* outVec, int structPtr, const char* fieldName, int* foundOut, uint32_t* defaultVec) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t* result = g_originalGFF_ReadVector3FieldByName(gffPtr, outVec, structPtr, fieldName, foundOut, defaultVec);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("GFF_ReadVector3FieldByName: " + std::to_string(duration.count()) + " Î¼s");
    return result;
}

typedef int (__thiscall* GFF_LookupFieldLabelByNamePtr_t)(int gffPtr, int structPtr, const char* fieldName);
GFF_LookupFieldLabelByNamePtr_t g_originalGFF_LookupFieldLabelByName = nullptr;

static bool IsControlsGffLabel(const char* fieldName) {
    static const char expected[] = "controls";
    if (fieldName == nullptr) {
        return false;
    }
    for (int i = 0; i < 8; ++i) {
        char ch = fieldName[i];
        if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch - 'A' + 'a');
        }
        if (ch != expected[i]) {
            return false;
        }
    }
    return fieldName[8] == '\0';
}

int __fastcall Hook_GFF_LookupFieldLabelByName(int gffPtr, void* edxDummy, int structPtr, const char* fieldName) {
#if HOOK_GUI_DEEP_GFF_TIMING
    auto start = std::chrono::high_resolution_clock::now();
#endif

#if ENABLE_GUI_CONTROLS_LOOKUP_CACHE
    if (g_guiControlsLookupCache.guiFindDepth > 0 && IsControlsGffLabel(fieldName)) {
        if (g_guiControlsLookupCache.valid &&
            g_guiControlsLookupCache.gffPtr == gffPtr &&
            g_guiControlsLookupCache.structPtr == structPtr) {
            ++g_guiControlsLookupCache.cacheHits;
            return g_guiControlsLookupCache.result;
        }

        int result = g_originalGFF_LookupFieldLabelByName(gffPtr, structPtr, fieldName);
        ++g_guiControlsLookupCache.originalLookups;
        g_guiControlsLookupCache.valid = true;
        g_guiControlsLookupCache.gffPtr = gffPtr;
        g_guiControlsLookupCache.structPtr = structPtr;
        g_guiControlsLookupCache.result = result;
        return result;
    }
#else
    if (g_guiControlsLookupCache.guiFindDepth > 0 && IsControlsGffLabel(fieldName)) {
        ++g_guiControlsLookupCache.originalLookups;
    }
#endif

    int result = g_originalGFF_LookupFieldLabelByName(gffPtr, structPtr, fieldName);

#if HOOK_GUI_DEEP_GFF_TIMING
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("GFF_LookupFieldLabelByName: " + std::to_string(duration.count()) + " Î¼s");
#endif
    return result;
}

typedef uint32_t (__thiscall* ResourceEnsureLoadedPtr_t)(int thisPtr, int resourceEntry);
ResourceEnsureLoadedPtr_t g_originalResourceEnsureLoaded = nullptr;

uint32_t __fastcall Hook_ResourceEnsureLoaded(int thisPtr, void* edxDummy, int resourceEntry) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalResourceEnsureLoaded(thisPtr, resourceEntry);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ResourceEnsureLoaded: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef int (__thiscall* ResourceLoadFromArchiveSlotPtr_t)(int thisPtr, int* resourceEntry, int asyncFlag);
ResourceLoadFromArchiveSlotPtr_t g_originalResourceLoadFromArchiveSlot = nullptr;

int __fastcall Hook_ResourceLoadFromArchiveSlot(int thisPtr, void* edxDummy, int* resourceEntry, int asyncFlag) {
    auto start = std::chrono::high_resolution_clock::now();

    int result = g_originalResourceLoadFromArchiveSlot(thisPtr, resourceEntry, asyncFlag);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ResourceLoadFromArchiveSlot: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__thiscall* ResourceLoadMemoryBackedPtr_t)(int thisPtr, int* resourceEntry, int unusedFlag);
ResourceLoadMemoryBackedPtr_t g_originalResourceLoadMemoryBacked = nullptr;

uint32_t __fastcall Hook_ResourceLoadMemoryBacked(int thisPtr, void* edxDummy, int* resourceEntry, int unusedFlag) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalResourceLoadMemoryBacked(thisPtr, resourceEntry, unusedFlag);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ResourceLoadMemoryBacked: " + std::to_string(duration.count()) + " μs");
    return result;
}

struct ArchiveCacheKey {
    std::string archiveName;
    uint32_t archiveType;
    uint32_t archiveId;
    uint32_t resourceIndex;
    uint32_t resourceOffset;
    uint32_t resourceSize;

    bool operator==(const ArchiveCacheKey& other) const {
        return archiveType == other.archiveType &&
            archiveId == other.archiveId &&
            resourceIndex == other.resourceIndex &&
            resourceOffset == other.resourceOffset &&
            resourceSize == other.resourceSize &&
            archiveName == other.archiveName;
    }
};

struct ArchiveCacheKeyHash {
    size_t operator()(const ArchiveCacheKey& key) const {
        size_t value = std::hash<std::string>()(key.archiveName);
        value ^= (size_t)key.archiveType + 0x9e3779b9u + (value << 6) + (value >> 2);
        value ^= (size_t)key.archiveId + 0x9e3779b9u + (value << 6) + (value >> 2);
        value ^= (size_t)key.resourceIndex + 0x9e3779b9u + (value << 6) + (value >> 2);
        value ^= (size_t)key.resourceOffset + 0x9e3779b9u + (value << 6) + (value >> 2);
        value ^= (size_t)key.resourceSize + 0x9e3779b9u + (value << 6) + (value >> 2);
        return value;
    }
};

typedef std::shared_ptr<const std::vector<BYTE>> ArchiveCacheBytesPtr;
static std::unordered_map<ArchiveCacheKey, ArchiveCacheBytesPtr, ArchiveCacheKeyHash>
    g_archiveResourceCache;
static std::mutex g_archiveResourceCacheMutex;
static size_t g_archiveResourceCacheBytes = 0;
static volatile LONG g_archiveResourceCacheDisabled = 0;
static volatile LONG g_archiveCacheStores = 0;
static volatile LONG g_archiveCacheHits = 0;
static volatile LONG g_archiveCacheMisses = 0;
__declspec(align(8)) static volatile LONG64 g_archiveCacheBytesServed = 0;

struct ArchiveCacheTimingBatch {
    int64_t lookupKeyUs;
    int64_t originalFallbackUs;
    int64_t captureKeyUs;
    int64_t captureCopyUs;
    int64_t mapInsertUs;
    int64_t hitFastPathUs;
};
thread_local ArchiveCacheTimingBatch g_archiveCacheTimingBatch = {};

static const int kCExoEncapsulatedFileVtable = 0x0099c6fc;

typedef int (__thiscall* ArchiveSourceListBeginPtr_t)(void* list);
typedef int (__thiscall* ArchiveSourceListGetPtr_t)(void* list, int iterator);
typedef int (__thiscall* ArchiveSourceListNextPtr_t)(void* list, int* iterator);
typedef int (__thiscall* ResourceAllocateLoadBufferPtr_t)(void* manager, int* resourceEntry);

static const ArchiveSourceListBeginPtr_t ArchiveSourceListBegin =
    (ArchiveSourceListBeginPtr_t)0x007a1720;
static const ArchiveSourceListGetPtr_t ArchiveSourceListGet =
    (ArchiveSourceListGetPtr_t)0x00561430;
static const ArchiveSourceListNextPtr_t ArchiveSourceListNext =
    (ArchiveSourceListNextPtr_t)0x0058c370;
static const ResourceAllocateLoadBufferPtr_t ResourceAllocateLoadBuffer =
    (ResourceAllocateLoadBufferPtr_t)0x00712f30;

static int* FindEncapsulatedArchiveReader(void* manager, uint32_t packedResourceId) {
    if (manager == nullptr) {
        return nullptr;
    }
    void* sourceList = (BYTE*)manager + 0x18;
    int iterator = ArchiveSourceListBegin(sourceList);
    int source = iterator != 0 ? ArchiveSourceListGet(sourceList, iterator) : 0;
    uint32_t archiveId = (packedResourceId & 0x000fc000u) >> 14;
    while (iterator != 0) {
        if (source != 0 &&
            ((*(uint32_t*)(source + 0x28) & 0x0fffffffu) == archiveId)) {
            int* readerHolder = *(int**)(source + 0x30);
            return readerHolder != nullptr ? (int*)*readerHolder : nullptr;
        }
        source = ArchiveSourceListNext(sourceList, &iterator);
    }
    return nullptr;
}

static bool BuildArchiveCacheKey(
    int* reader, uint32_t resourceId, uint32_t requestedSize,
    ArchiveCacheKey& key, uint32_t& fullResourceSize) {
    if (reader == nullptr || reader[0] != kCExoEncapsulatedFileVtable) {
        return false;
    }
    int header = *(int*)((BYTE*)reader + 0x38);
    int table = *(int*)((BYTE*)reader + 0x3c);
    if (header == 0 || table == 0 || *(int*)((BYTE*)reader + 0x2c) == 0) {
        return false;
    }
    uint32_t resourceIndex = resourceId & 0x3fffu;
    uint32_t resourceCount = *(uint32_t*)(header + 0x10);
    if (resourceIndex >= resourceCount) {
        return false;
    }
    uint32_t resourceOffset = *(uint32_t*)(table + resourceIndex * 8);
    uint32_t tableSize = *(uint32_t*)(table + resourceIndex * 8 + 4);
    if (tableSize == 0 || (requestedSize != 0 && requestedSize < tableSize)) {
        return false;
    }
    char* archiveName = *(char**)((BYTE*)reader + 0x04);
    uint32_t archiveNameCapacity = *(uint32_t*)((BYTE*)reader + 0x08);
    if (archiveName == nullptr || archiveNameCapacity < 2 || archiveNameCapacity > 4096) {
        return false;
    }
    size_t archiveNameLength = 0;
    while (archiveNameLength < archiveNameCapacity && archiveName[archiveNameLength] != '\0') {
        ++archiveNameLength;
    }
    if (archiveNameLength == 0 || archiveNameLength == archiveNameCapacity) {
        return false;
    }
    key.archiveName.assign(archiveName, archiveNameLength);
    key.archiveType = *(BYTE*)((BYTE*)reader + 0x40);
    key.archiveId = (resourceId & 0x000fc000u) >> 14;
    key.resourceIndex = resourceIndex;
    key.resourceOffset = resourceOffset;
    key.resourceSize = tableSize;
    fullResourceSize = tableSize;
    return true;
}

static void DisableArchiveResourceCache(const char* reason) {
    if (InterlockedExchange(&g_archiveResourceCacheDisabled, 1) == 0) {
        {
            std::lock_guard<std::mutex> lock(g_archiveResourceCacheMutex);
            g_archiveResourceCache.clear();
            g_archiveResourceCacheBytes = 0;
        }
        Log(std::string("ArchiveCache disabled: ") + reason);
    }
}

static ArchiveCacheBytesPtr FindArchiveCacheEntry(const ArchiveCacheKey& key) {
    if (InterlockedCompareExchange(&g_archiveResourceCacheDisabled, 0, 0) != 0) {
        return ArchiveCacheBytesPtr();
    }
    std::lock_guard<std::mutex> lock(g_archiveResourceCacheMutex);
    auto found = g_archiveResourceCache.find(key);
    return found == g_archiveResourceCache.end() ? ArchiveCacheBytesPtr() : found->second;
}

static void StoreArchiveCacheEntry(
    const ArchiveCacheKey& key, const BYTE* source, uint32_t size) {
    if (source == nullptr || size == 0 ||
        InterlockedCompareExchange(&g_archiveResourceCacheDisabled, 0, 0) != 0) {
        return;
    }
    try {
        auto copyStart = std::chrono::high_resolution_clock::now();
        ArchiveCacheBytesPtr bytes =
            std::make_shared<const std::vector<BYTE>>(source, source + size);
        auto copyEnd = std::chrono::high_resolution_clock::now();
        g_archiveCacheTimingBatch.captureCopyUs +=
            std::chrono::duration_cast<std::chrono::microseconds>(
                copyEnd - copyStart).count();

        bool mismatch = false;
        auto insertStart = std::chrono::high_resolution_clock::now();
        {
            std::lock_guard<std::mutex> lock(g_archiveResourceCacheMutex);
            auto found = g_archiveResourceCache.find(key);
            if (found != g_archiveResourceCache.end()) {
                mismatch = found->second->size() != size ||
                    std::memcmp(found->second->data(), source, size) != 0;
            } else if (g_archiveResourceCacheBytes + size <= ARCHIVE_CACHE_MAX_BYTES) {
                g_archiveResourceCache.emplace(key, bytes);
                g_archiveResourceCacheBytes += size;
                InterlockedIncrement(&g_archiveCacheStores);
            }
        }
        auto insertEnd = std::chrono::high_resolution_clock::now();
        g_archiveCacheTimingBatch.mapInsertUs +=
            std::chrono::duration_cast<std::chrono::microseconds>(
                insertEnd - insertStart).count();
        if (mismatch) {
            DisableArchiveResourceCache("same key produced different bytes");
        }
    } catch (...) {
        DisableArchiveResourceCache("allocation or map failure");
    }
}

struct ArchiveProfileBatch {
    uint32_t calls;
    uint32_t hits;
    uint32_t fallbacks;
    int64_t totalUs;
};
thread_local ArchiveProfileBatch g_archiveProfileBatch = {};

static void RecordArchiveLoadProfile(int64_t durationUs, bool cacheHit) {
    ++g_archiveProfileBatch.calls;
    g_archiveProfileBatch.totalUs += durationUs;
    if (cacheHit) ++g_archiveProfileBatch.hits;
    else ++g_archiveProfileBatch.fallbacks;
    if (g_archiveProfileBatch.calls >= 64) {
        size_t entryCount;
        size_t cachedBytes;
        {
            std::lock_guard<std::mutex> lock(g_archiveResourceCacheMutex);
            entryCount = g_archiveResourceCache.size();
            cachedBytes = g_archiveResourceCacheBytes;
        }
        Log("ResourceLoadFromArchive: " + std::to_string(g_archiveProfileBatch.totalUs) +
            " us count=" + std::to_string(g_archiveProfileBatch.calls) +
            " cache_hits=" + std::to_string(g_archiveProfileBatch.hits) +
            " fallbacks=" + std::to_string(g_archiveProfileBatch.fallbacks));
        Log("ArchiveCache: entries=" + std::to_string(entryCount) +
            " cached_bytes=" + std::to_string(cachedBytes) +
            " stores=" + std::to_string(InterlockedCompareExchange(&g_archiveCacheStores, 0, 0)) +
            " hits=" + std::to_string(InterlockedCompareExchange(&g_archiveCacheHits, 0, 0)) +
            " misses=" + std::to_string(InterlockedCompareExchange(&g_archiveCacheMisses, 0, 0)) +
            " served_bytes=" + std::to_string(InterlockedCompareExchange64(&g_archiveCacheBytesServed, 0, 0)));
        Log("ArchiveCacheTiming: count=" + std::to_string(g_archiveProfileBatch.calls) +
            " lookup_key_us=" + std::to_string(g_archiveCacheTimingBatch.lookupKeyUs) +
            " original_fallback_us=" + std::to_string(g_archiveCacheTimingBatch.originalFallbackUs) +
            " capture_key_us=" + std::to_string(g_archiveCacheTimingBatch.captureKeyUs) +
            " capture_copy_us=" + std::to_string(g_archiveCacheTimingBatch.captureCopyUs) +
            " map_insert_us=" + std::to_string(g_archiveCacheTimingBatch.mapInsertUs) +
            " hit_fast_path_us=" + std::to_string(g_archiveCacheTimingBatch.hitFastPathUs));
        g_archiveProfileBatch = {};
        g_archiveCacheTimingBatch = {};
    }
}

typedef int (__thiscall* ResourceLoadFromArchivePtr_t)(int thisPtr, int* resourceEntry, int asyncFlag);
ResourceLoadFromArchivePtr_t g_originalResourceLoadFromArchive = nullptr;

int __fastcall Hook_ResourceLoadFromArchive(int thisPtr, void* edxDummy, int* resourceEntry, int asyncFlag) {
    auto start = std::chrono::high_resolution_clock::now();

    int result = 0;
    bool cacheHit = false;

#if ENABLE_ARCHIVE_RESOURCE_CACHE
    if (resourceEntry != nullptr && asyncFlag == 0 &&
        (resourceEntry[3] & 4) == 0 &&
        InterlockedCompareExchange(&g_archiveResourceCacheDisabled, 0, 0) == 0) {
        ArchiveCacheBytesPtr cached;
        uint32_t resourceSize = 0;
        auto lookupStart = std::chrono::high_resolution_clock::now();
        try {
            int* reader = FindEncapsulatedArchiveReader(
                (void*)thisPtr, (uint32_t)resourceEntry[2]);
            ArchiveCacheKey key;
            if (BuildArchiveCacheKey(
                    reader, (uint32_t)resourceEntry[2], 0, key, resourceSize)) {
                cached = FindArchiveCacheEntry(key);
                if (cached == nullptr || cached->size() != resourceSize) {
                    InterlockedIncrement(&g_archiveCacheMisses);
                }
            }
        } catch (...) {
            DisableArchiveResourceCache("fast-path exception");
        }
        auto lookupEnd = std::chrono::high_resolution_clock::now();
        g_archiveCacheTimingBatch.lookupKeyUs +=
            std::chrono::duration_cast<std::chrono::microseconds>(
                lookupEnd - lookupStart).count();

        if (cached != nullptr && cached->size() == resourceSize &&
            InterlockedCompareExchange(&g_archiveResourceCacheDisabled, 0, 0) == 0) {
            auto hitStart = std::chrono::high_resolution_clock::now();
            resourceEntry[6] = (int)resourceSize;
            if (ResourceAllocateLoadBuffer((void*)thisPtr, resourceEntry) != 0) {
                std::memcpy((void*)resourceEntry[4], cached->data(), resourceSize);
                typedef int (__thiscall* ResourceParseCallbackPtr_t)(int* entry);
                ResourceParseCallbackPtr_t parseCallback =
                    (ResourceParseCallbackPtr_t)*(int*)(resourceEntry[0] + 0x10);
                result = parseCallback(resourceEntry);
                resourceEntry[3] =
                    (resourceEntry[3] & ~4) | (result != 0 ? 4 : 0);
                InterlockedIncrement(&g_archiveCacheHits);
                InterlockedExchangeAdd64(&g_archiveCacheBytesServed, resourceSize);
            }
            // Allocation failure is also a completed fast path: the original
            // would return zero after the same allocator fails.
            cacheHit = true;
            auto hitEnd = std::chrono::high_resolution_clock::now();
            g_archiveCacheTimingBatch.hitFastPathUs +=
                std::chrono::duration_cast<std::chrono::microseconds>(
                    hitEnd - hitStart).count();
        }
    }
#endif

    if (!cacheHit) {
        auto originalStart = std::chrono::high_resolution_clock::now();
        result = g_originalResourceLoadFromArchive(thisPtr, resourceEntry, asyncFlag);
        auto originalEnd = std::chrono::high_resolution_clock::now();
        g_archiveCacheTimingBatch.originalFallbackUs +=
            std::chrono::duration_cast<std::chrono::microseconds>(
                originalEnd - originalStart).count();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    RecordArchiveLoadProfile(duration.count(), cacheHit);
    return result;
}

typedef int (__thiscall* ResourceLoadFromLooseFilePtr_t)(int thisPtr, int* resourceEntry, int asyncFlag);
ResourceLoadFromLooseFilePtr_t g_originalResourceLoadFromLooseFile = nullptr;

int __fastcall Hook_ResourceLoadFromLooseFile(int thisPtr, void* edxDummy, int* resourceEntry, int asyncFlag) {
    auto start = std::chrono::high_resolution_clock::now();

    int result = g_originalResourceLoadFromLooseFile(thisPtr, resourceEntry, asyncFlag);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ResourceLoadFromLooseFile: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__thiscall* LooseFileOpenPtr_t)(uint32_t* thisPtr, uint32_t param1, uint16_t param2, uint32_t param3);
LooseFileOpenPtr_t g_originalLooseFileOpen = nullptr;

uint32_t* __fastcall Hook_LooseFileOpen(uint32_t* thisPtr, void* edxDummy, uint32_t param1, uint16_t param2, uint32_t param3) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t* result = g_originalLooseFileOpen(thisPtr, param1, param2, param3);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("LooseFileOpen: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef int (__thiscall* LooseFileReadPtr_t)(int* thisPtr, void* buffer, size_t elementSize, size_t elementCount);
LooseFileReadPtr_t g_originalLooseFileRead = nullptr;

int __fastcall Hook_LooseFileRead(int* thisPtr, void* edxDummy, void* buffer, size_t elementSize, size_t elementCount) {
    auto start = std::chrono::high_resolution_clock::now();

    int result = g_originalLooseFileRead(thisPtr, buffer, elementSize, elementCount);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("LooseFileRead: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__thiscall* ResourceFinalizeAsyncLoadPtr_t)(int thisPtr);
ResourceFinalizeAsyncLoadPtr_t g_originalResourceFinalizeAsyncLoad = nullptr;

uint32_t __fastcall Hook_ResourceFinalizeAsyncLoad(int thisPtr, void* edxDummy) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalResourceFinalizeAsyncLoad(thisPtr);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ResourceFinalizeAsyncLoad: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef int (__thiscall* Resource_AllocateLoadBufferPtr_t)(int thisPtr, int* resourceEntry);
Resource_AllocateLoadBufferPtr_t g_originalResource_AllocateLoadBuffer = nullptr;

int __fastcall Hook_Resource_AllocateLoadBuffer(int thisPtr, void* edxDummy, int* resourceEntry) {
    auto start = std::chrono::high_resolution_clock::now();

    int result = g_originalResource_AllocateLoadBuffer(thisPtr, resourceEntry);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("Resource_AllocateLoadBuffer: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef void (__thiscall* CExoResFile_AddRefSyncOpenPtr_t)(int* thisPtr);
CExoResFile_AddRefSyncOpenPtr_t g_originalCExoResFile_AddRefSyncOpen = nullptr;

void __fastcall Hook_CExoResFile_AddRefSyncOpen(int* thisPtr, void* edxDummy) {
    auto start = std::chrono::high_resolution_clock::now();

    g_originalCExoResFile_AddRefSyncOpen(thisPtr);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResFile_AddRefSyncOpen: " + std::to_string(duration.count()) + " μs");
}

typedef void (__thiscall* CExoResFile_AddRefAsyncOpenPtr_t)(int* thisPtr);
CExoResFile_AddRefAsyncOpenPtr_t g_originalCExoResFile_AddRefAsyncOpen = nullptr;

void __fastcall Hook_CExoResFile_AddRefAsyncOpen(int* thisPtr, void* edxDummy) {
    auto start = std::chrono::high_resolution_clock::now();

    g_originalCExoResFile_AddRefAsyncOpen(thisPtr);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResFile_AddRefAsyncOpen: " + std::to_string(duration.count()) + " μs");
}

typedef void (__thiscall* CExoResFile_ReleaseSyncClosePtr_t)(int* thisPtr);
CExoResFile_ReleaseSyncClosePtr_t g_originalCExoResFile_ReleaseSyncClose = nullptr;

void __fastcall Hook_CExoResFile_ReleaseSyncClose(int* thisPtr, void* edxDummy) {
    auto start = std::chrono::high_resolution_clock::now();

    g_originalCExoResFile_ReleaseSyncClose(thisPtr);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResFile_ReleaseSyncClose: " + std::to_string(duration.count()) + " μs");
}

typedef void (__thiscall* CExoResFile_ReleaseAsyncClosePtr_t)(int* thisPtr);
CExoResFile_ReleaseAsyncClosePtr_t g_originalCExoResFile_ReleaseAsyncClose = nullptr;

void __fastcall Hook_CExoResFile_ReleaseAsyncClose(int* thisPtr, void* edxDummy) {
    auto start = std::chrono::high_resolution_clock::now();

    g_originalCExoResFile_ReleaseAsyncClose(thisPtr);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResFile_ReleaseAsyncClose: " + std::to_string(duration.count()) + " μs");
}

typedef uint32_t (__thiscall* CExoResFile_GetResourceSizePtr_t)(int thisPtr, uint32_t resourceId);
CExoResFile_GetResourceSizePtr_t g_originalCExoResFile_GetResourceSize = nullptr;

uint32_t __fastcall Hook_CExoResFile_GetResourceSize(int thisPtr, void* edxDummy, uint32_t resourceId) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoResFile_GetResourceSize(thisPtr, resourceId);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResFile_GetResourceSize: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__thiscall* CExoResFile_ReadResourceSyncPtr_t)(
    int thisPtr, uint32_t resourceId, int buffer, uint32_t size, uint32_t readContext);
CExoResFile_ReadResourceSyncPtr_t g_originalCExoResFile_ReadResourceSync = nullptr;

uint32_t __fastcall Hook_CExoResFile_ReadResourceSync(
    int thisPtr, void* edxDummy,
    uint32_t resourceId, int buffer, uint32_t size, uint32_t readContext) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoResFile_ReadResourceSync(
        thisPtr, resourceId, buffer, size, readContext);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResFile_ReadResourceSync: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__thiscall* CExoResFile_ReadResourceAsyncPtr_t)(
    int thisPtr, uint32_t resourceId, int buffer, uint32_t size, uint32_t asyncContext);
CExoResFile_ReadResourceAsyncPtr_t g_originalCExoResFile_ReadResourceAsync = nullptr;

uint32_t __fastcall Hook_CExoResFile_ReadResourceAsync(
    int thisPtr, void* edxDummy,
    uint32_t resourceId, int buffer, uint32_t size, uint32_t asyncContext) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoResFile_ReadResourceAsync(
        thisPtr, resourceId, buffer, size, asyncContext);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResFile_ReadResourceAsync: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__thiscall* CExoResFile_OpenSyncHandlePtr_t)(int thisPtr);
CExoResFile_OpenSyncHandlePtr_t g_originalCExoResFile_OpenSyncHandle = nullptr;

uint32_t __fastcall Hook_CExoResFile_OpenSyncHandle(int thisPtr, void* edxDummy) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoResFile_OpenSyncHandle(thisPtr);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResFile_OpenSyncHandle: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__thiscall* CExoResFile_OpenAsyncHandlePtr_t)(int thisPtr);
CExoResFile_OpenAsyncHandlePtr_t g_originalCExoResFile_OpenAsyncHandle = nullptr;

uint32_t __fastcall Hook_CExoResFile_OpenAsyncHandle(int thisPtr, void* edxDummy) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoResFile_OpenAsyncHandle(thisPtr);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResFile_OpenAsyncHandle: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef void (__thiscall* ArchiveReaderShared_AddRefSyncOpenPtr_t)(int* thisPtr);
ArchiveReaderShared_AddRefSyncOpenPtr_t g_originalArchiveReaderShared_AddRefSyncOpen = nullptr;

void __fastcall Hook_ArchiveReaderShared_AddRefSyncOpen(int* thisPtr, void* edxDummy) {
    auto start = std::chrono::high_resolution_clock::now();

#if TRACE_ARCHIVE_REFCOUNTS
    int refBefore = thisPtr ? thisPtr[7] : -1;
    int openBefore = thisPtr ? thisPtr[9] : -1;
#endif

    g_originalArchiveReaderShared_AddRefSyncOpen(thisPtr);
#if KEEP_ARCHIVE_OPEN_DURING_LOAD
    ArchiveLoad_PinReader(thisPtr);
#endif

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ArchiveReaderShared_AddRefSyncOpen: " + std::to_string(duration.count())+ " μs");
#if TRACE_ARCHIVE_REFCOUNTS
    if (thisPtr != nullptr && thisPtr[0] == 0x0099c6fc) {
        Log("ArchiveRef: AddRef ptr=" + std::to_string((int)thisPtr) +
            " ref=" + std::to_string(refBefore) + "->" + std::to_string(thisPtr[7]) +
            " open=" + std::to_string(openBefore) + "->" + std::to_string(thisPtr[9]));
    }
#endif
}

typedef void (__thiscall* CExoEncapsulatedFile_AddRefAsyncOpenPtr_t)(int* thisPtr);
CExoEncapsulatedFile_AddRefAsyncOpenPtr_t g_originalCExoEncapsulatedFile_AddRefAsyncOpen = nullptr;

void __fastcall Hook_CExoEncapsulatedFile_AddRefAsyncOpen(int* thisPtr, void* edxDummy) {
    auto start = std::chrono::high_resolution_clock::now();

    g_originalCExoEncapsulatedFile_AddRefAsyncOpen(thisPtr);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoEncapsulatedFile_AddRefAsyncOpen: " + std::to_string(duration.count())+ " μs");
}

typedef void (__thiscall* CExoEncapsulatedFile_ReleaseSyncClosePtr_t)(int* thisPtr);
CExoEncapsulatedFile_ReleaseSyncClosePtr_t g_originalCExoEncapsulatedFile_ReleaseSyncClose = nullptr;

void __fastcall Hook_CExoEncapsulatedFile_ReleaseSyncClose(int* thisPtr, void* edxDummy) {
    auto start = std::chrono::high_resolution_clock::now();
#if TRACE_ARCHIVE_REFCOUNTS
    int refBefore = thisPtr ? thisPtr[7] : -1;
    int openBefore = thisPtr ? thisPtr[9] : -1;
#endif
    g_originalCExoEncapsulatedFile_ReleaseSyncClose(thisPtr);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoEncapsulatedFile_ReleaseSyncClose: " + std::to_string(duration.count()) + " μs");
#if TRACE_ARCHIVE_REFCOUNTS
    if (thisPtr != nullptr && thisPtr[0] == 0x0099c6fc) {
        Log("ArchiveRef: Release ptr=" + std::to_string((int)thisPtr) +
            " ref=" + std::to_string(refBefore) + "->" + std::to_string(thisPtr[7]) +
            " open=" + std::to_string(openBefore) + "->" + std::to_string(thisPtr[9]));
    }
#endif
}

typedef void (__thiscall* CExoEncapsulatedFile_ReleaseAsyncClosePtr_t)(int* thisPtr);
CExoEncapsulatedFile_ReleaseAsyncClosePtr_t g_originalCExoEncapsulatedFile_ReleaseAsyncClose = nullptr;

void __fastcall Hook_CExoEncapsulatedFile_ReleaseAsyncClose(int* thisPtr, void* edxDummy) {
    auto start = std::chrono::high_resolution_clock::now();

    g_originalCExoEncapsulatedFile_ReleaseAsyncClose(thisPtr);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoEncapsulatedFile_ReleaseAsyncClose: " + std::to_string(duration.count()) + " μs");
}

typedef uint32_t (__thiscall* CExoEncapsulatedFile_GetResourceSizePtr_t)(int thisPtr, uint32_t resourceId);
CExoEncapsulatedFile_GetResourceSizePtr_t g_originalCExoEncapsulatedFile_GetResourceSize = nullptr;

uint32_t __fastcall Hook_CExoEncapsulatedFile_GetResourceSize(int thisPtr, void* edxDummy, uint32_t resourceId) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoEncapsulatedFile_GetResourceSize(thisPtr, resourceId);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoEncapsulatedFile_GetResourceSize: " + std::to_string(duration.count())+ " μs");
    return result;
}

typedef uint32_t (__thiscall* CExoEncapsulatedFile_ReadResourceSyncPtr_t)(
    int thisPtr, uint32_t resourceId, int buffer, uint32_t size, uint32_t readContext);
CExoEncapsulatedFile_ReadResourceSyncPtr_t g_originalCExoEncapsulatedFile_ReadResourceSync = nullptr;

uint32_t __fastcall Hook_CExoEncapsulatedFile_ReadResourceSync(
    int thisPtr, void* edxDummy,
    uint32_t resourceId, int buffer, uint32_t size, uint32_t readContext) {
    uint32_t result = g_originalCExoEncapsulatedFile_ReadResourceSync(
        thisPtr, resourceId, buffer, size, readContext);

#if ENABLE_ARCHIVE_RESOURCE_CACHE
    if (result != 0 && buffer != 0 && readContext == 0 &&
        InterlockedCompareExchange(&g_archiveResourceCacheDisabled, 0, 0) == 0) {
        try {
            ArchiveCacheKey key;
            uint32_t fullResourceSize = 0;
            auto captureKeyStart = std::chrono::high_resolution_clock::now();
            bool keyBuilt = BuildArchiveCacheKey(
                (int*)thisPtr, resourceId, size, key, fullResourceSize);
            auto captureKeyEnd = std::chrono::high_resolution_clock::now();
            g_archiveCacheTimingBatch.captureKeyUs +=
                std::chrono::duration_cast<std::chrono::microseconds>(
                    captureKeyEnd - captureKeyStart).count();
            if (keyBuilt &&
                result == fullResourceSize) {
                StoreArchiveCacheEntry(key, (const BYTE*)buffer, fullResourceSize);
            }
        } catch (...) {
            DisableArchiveResourceCache("read-capture exception");
        }
    }
#endif
    return result;
}

typedef uint32_t (__thiscall* CExoEncapsulatedFile_ReadResourceAsyncPtr_t)(
    int thisPtr, uint32_t resourceId, int buffer, uint32_t size, uint32_t asyncContext);
CExoEncapsulatedFile_ReadResourceAsyncPtr_t g_originalCExoEncapsulatedFile_ReadResourceAsync = nullptr;

uint32_t __fastcall Hook_CExoEncapsulatedFile_ReadResourceAsync(
    int thisPtr, void* edxDummy,
    uint32_t resourceId, int buffer, uint32_t size, uint32_t asyncContext) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoEncapsulatedFile_ReadResourceAsync(
        thisPtr, resourceId, buffer, size, asyncContext);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoEncapsulatedFile_ReadResourceAsync: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__thiscall* CExoEncapsulatedFile_OpenSyncHandlePtr_t)(int thisPtr);
CExoEncapsulatedFile_OpenSyncHandlePtr_t g_originalCExoEncapsulatedFile_OpenSyncHandle = nullptr;

uint32_t __fastcall Hook_CExoEncapsulatedFile_OpenSyncHandle(int thisPtr, void* edxDummy) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoEncapsulatedFile_OpenSyncHandle(thisPtr);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoEncapsulatedFile_OpenSyncHandle: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__thiscall* CExoEncapsulatedFile_OpenAsyncHandlePtr_t)(int thisPtr);
CExoEncapsulatedFile_OpenAsyncHandlePtr_t g_originalCExoEncapsulatedFile_OpenAsyncHandle = nullptr;

uint32_t __fastcall Hook_CExoEncapsulatedFile_OpenAsyncHandle(int thisPtr, void* edxDummy) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoEncapsulatedFile_OpenAsyncHandle(thisPtr);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoEncapsulatedFile_OpenAsyncHandle: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef void (__thiscall* CExoResourceImageFile_ReleaseSyncClosePtr_t)(int* thisPtr);
CExoResourceImageFile_ReleaseSyncClosePtr_t g_originalCExoResourceImageFile_ReleaseSyncClose = nullptr;

void __fastcall Hook_CExoResourceImageFile_ReleaseSyncClose(int* thisPtr, void* edxDummy) {
    auto start = std::chrono::high_resolution_clock::now();

    g_originalCExoResourceImageFile_ReleaseSyncClose(thisPtr);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResourceImageFile_ReleaseSyncClose: " + std::to_string(duration.count()) + " μs");
}

typedef uint32_t (__thiscall* CExoResourceImageFile_GetResourceSizePtr_t)(int thisPtr, uint32_t resourceId);
CExoResourceImageFile_GetResourceSizePtr_t g_originalCExoResourceImageFile_GetResourceSize = nullptr;

uint32_t __fastcall Hook_CExoResourceImageFile_GetResourceSize(int thisPtr, void* edxDummy, uint32_t resourceId) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoResourceImageFile_GetResourceSize(thisPtr, resourceId);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResourceImageFile_GetResourceSize: " + std::to_string(duration.count())+ " μs");
    return result;
}

typedef uint32_t (__thiscall* CExoResourceImageFile_ReadResourceSyncPtr_t)(
    int thisPtr, uint32_t resourceId, int buffer, uint32_t size, uint32_t readContext);
CExoResourceImageFile_ReadResourceSyncPtr_t g_originalCExoResourceImageFile_ReadResourceSync = nullptr;

uint32_t __fastcall Hook_CExoResourceImageFile_ReadResourceSync(
    int thisPtr, void* edxDummy,
    uint32_t resourceId, int buffer, uint32_t size, uint32_t readContext) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoResourceImageFile_ReadResourceSync(
        thisPtr, resourceId, buffer, size, readContext);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResourceImageFile_ReadResourceSync: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__thiscall* CExoResourceImageFile_ReadResourceAsyncPtr_t)(
    int thisPtr, uint32_t resourceId, int buffer, uint32_t size, uint32_t asyncContext);
CExoResourceImageFile_ReadResourceAsyncPtr_t g_originalCExoResourceImageFile_ReadResourceAsync = nullptr;

uint32_t __fastcall Hook_CExoResourceImageFile_ReadResourceAsync(
    int thisPtr, void* edxDummy,
    uint32_t resourceId, int buffer, uint32_t size, uint32_t asyncContext) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoResourceImageFile_ReadResourceAsync(
        thisPtr, resourceId, buffer, size, asyncContext);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResourceImageFile_ReadResourceAsync: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__thiscall* CExoResourceImageFile_LoadImagePtr_t)(int thisPtr);
CExoResourceImageFile_LoadImagePtr_t g_originalCExoResourceImageFile_LoadImage = nullptr;

uint32_t __fastcall Hook_CExoResourceImageFile_LoadImage(int thisPtr, void* edxDummy) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoResourceImageFile_LoadImage(thisPtr);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResourceImageFile_LoadImage: " + std::to_string(duration.count()) + " us");
    return result;
}

static bool InstallCheckedHook(
    DWORD address, LPVOID detour, LPVOID* original, const char* name) {
    LPVOID target = (LPVOID)address;
    if (MH_CreateHook(target, detour, original) != MH_OK) {
        Log(std::string("Failed to create hook: ") + name);
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log(std::string("Failed to enable hook: ") + name);
        return false;
    }
    Log(std::string("Installed performance hook: ") + name);
    return true;
}

static bool MatchesExecutableBytes(DWORD address, const BYTE* expected, size_t length) {
    MEMORY_BASIC_INFORMATION memoryInfo = {};
    BYTE* target = (BYTE*)address;
    if (VirtualQuery(target, &memoryInfo, sizeof(memoryInfo)) != sizeof(memoryInfo) ||
        memoryInfo.State != MEM_COMMIT ||
        (memoryInfo.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }

    BYTE* regionEnd = (BYTE*)memoryInfo.BaseAddress + memoryInfo.RegionSize;
    return target + length <= regionEnd && std::memcmp(target, expected, length) == 0;
}

static bool IsSupportedSteamExecutable() {
    if ((DWORD)GetModuleHandle(nullptr) != 0x00400000) {
        return false;
    }

    // Guard only the functions used by the stability hook set.  This prevents
    // a wrong executable build from receiving a detour while avoiding dependencies
    // on the large disabled legacy profiler.
    static const BYTE moduleChunkSignature[] = {
        0x55,0x8b,0xec,0x6a,0xff,0x68,0x84,0x99,0x96,0x00
    };
    static const BYTE loadingFrameSignature[] = {
        0x55,0x8b,0xec,0x83,0xec,0x50
    };
    static const BYTE loadScreenSignature[] = {
        0x55,0x8b,0xec,0x6a,0xff,0x68,0x4a,0xeb,0x94,0x00
    };
    static const BYTE guiFindSignature[] = {
        0x55,0x8b,0xec,0x6a,0xff,0x68,0xa8,0x89,0x94,0x00
    };
    static const BYTE gffLookupSignature[] = {
        0x55,0x8b,0xec,0x83,0xec,0x34,0xa1,0x20,0x1f,0xa1,0x00
    };
    static const BYTE resourceLoadArchiveSignature[] = {
        0x55,0x8b,0xec,0x83,0xec,0x14
    };
    static const BYTE encapsulatedReadSignature[] = {
        0x55,0x8b,0xec,0x83,0xec,0x0c
    };
    static const BYTE allocateBufferSignature[] = {
        0x55,0x8b,0xec,0x83,0xec,0x10
    };
    static const BYTE sourceListBeginSignature[] = {
        0x55,0x8b,0xec,0x83,0xec,0x0c
    };
    static const BYTE sourceListAccessSignature[] = {
        0x55,0x8b,0xec,0x51,0x89,0x4d,0xfc
    };
    if (!MatchesExecutableBytes(
            0x007be4c0, moduleChunkSignature, sizeof(moduleChunkSignature)) ||
        !MatchesExecutableBytes(
            0x00409ed0, loadingFrameSignature, sizeof(loadingFrameSignature)) ||
        !MatchesExecutableBytes(
            0x00533830, loadScreenSignature, sizeof(loadScreenSignature)) ||
        !MatchesExecutableBytes(
            0x00418df0, guiFindSignature, sizeof(guiFindSignature)) ||
        !MatchesExecutableBytes(
            0x007178e0, gffLookupSignature, sizeof(gffLookupSignature)) ||
        !MatchesExecutableBytes(
            0x00713bf0, resourceLoadArchiveSignature, sizeof(resourceLoadArchiveSignature)) ||
        !MatchesExecutableBytes(
            0x00729370, encapsulatedReadSignature, sizeof(encapsulatedReadSignature)) ||
        !MatchesExecutableBytes(
            0x00712f30, allocateBufferSignature, sizeof(allocateBufferSignature)) ||
        !MatchesExecutableBytes(
            0x007a1720, sourceListBeginSignature, sizeof(sourceListBeginSignature)) ||
        !MatchesExecutableBytes(
            0x00561430, sourceListAccessSignature, sizeof(sourceListAccessSignature)) ||
        !MatchesExecutableBytes(
            0x0058c370, sourceListAccessSignature, sizeof(sourceListAccessSignature))) {
        return false;
    }

#if SKIP_PRELOAD_INITIAL_ASSETS_WRAPPER
    static const BYTE preloadSignature[] = {
        0x55,0x8b,0xec,0x51,0x89,0x4d,0xfc,0x8b,0x45,0xfc,0x8b,0x48,0x04
    };
    if (!MatchesExecutableBytes(0x0073f050, preloadSignature, sizeof(preloadSignature))) {
        return false;
    }
#endif
    return true;
}

static void InstallPerformanceHooks() {
#if SKIP_PRELOAD_INITIAL_ASSETS_WRAPPER
    InstallCheckedHook(0x0073f050, (LPVOID)&Hook_PreloadInitialAssetsWrapper,
        (LPVOID*)&g_originalPreloadInitialAssetsWrapperPtr, "PreloadInitialAssetsWrapper");
#endif

    // Read-only coordinator timing and scope tracking.  This detour does not alter
    // the CClientExoApp object graph or any engine ownership state.
    bool moduleChunkHookReady = InstallCheckedHook(0x007be4c0, (LPVOID)&Hook_ModuleChunkLoadCore,
        (LPVOID*)&g_originalModuleChunkLoadCore, "ModuleChunkLoadCore");

    InstallCheckedHook(0x00409ed0, (LPVOID)&Hook_LoadingScreenUpdateFrame,
        (LPVOID*)&g_originalLoadingScreenUpdateFrame, "LoadingScreenUpdateFrame");
    InstallCheckedHook(0x00533830, (LPVOID)&Hook_loadingscreenPtr,
        (LPVOID*)&g_originalLoadingScreenPtr, "loadingscreen");
    InstallCheckedHook(0x00713bf0, (LPVOID)&Hook_ResourceLoadFromArchive,
        (LPVOID*)&g_originalResourceLoadFromArchive, "ResourceLoadFromArchive");
    InstallCheckedHook(0x00729370, (LPVOID)&Hook_CExoEncapsulatedFile_ReadResourceSync,
        (LPVOID*)&g_originalCExoEncapsulatedFile_ReadResourceSync,
        "CExoEncapsulatedFile_ReadResourceSync");
#if ENABLE_ARCHIVE_RESOURCE_CACHE
    Log("Archive resource cache enabled; max_bytes=" +
        std::to_string((size_t)ARCHIVE_CACHE_MAX_BYTES));
#else
    Log("Archive resource cache disabled (baseline)");
#endif
    InstallCheckedHook(0x00418df0, (LPVOID)&Hook_GUI_FindAndBindControlByTag,
        (LPVOID*)&g_originalGUI_FindAndBindControlByTag, "GUI_FindAndBindControlByTag");
    InstallCheckedHook(0x007178e0, (LPVOID)&Hook_GFF_LookupFieldLabelByName,
        (LPVOID*)&g_originalGFF_LookupFieldLabelByName, "GFF_LookupFieldLabelByName");

#if ENABLE_NATIVE_LAZY_GUI_MODE
    if (moduleChunkHookReady) {
        Log("Native lazy GUI mode armed for ModuleChunkLoadCore entry");
    }
#endif

#if SKIP_DEBUG_GUI_CONSTRUCTION
    InstallCheckedHook(0x008c19f0, (LPVOID)&Hook_DebugMenuConstructor,
        (LPVOID*)&g_originalDebugMenuConstructor, "CSWGuiCreateItemMenu_Ctor");
    InstallCheckedHook(0x008c0cb0, (LPVOID)&Hook_CSWGuiLoadModuleDebugMenu_Ctor,
        (LPVOID*)&g_originalCSWGuiLoadModuleDebugMenu_Ctor, "CSWGuiLoadModuleDebugMenu_Ctor");
    InstallCheckedHook(0x008bfb60, (LPVOID)&Hook_CSWGuiPowersFeatsSkillsDebugMenu_Ctor,
        (LPVOID*)&g_originalCSWGuiPowersFeatsSkillsDebugMenu_Ctor,
        "CSWGuiPowersFeatsSkillsDebugMenu_Ctor");
    InstallCheckedHook(0x008bee10, (LPVOID)&Hook_CSWGuiCreateDebugItemSubMenu_Ctor,
        (LPVOID*)&g_originalCSWGuiCreateDebugItemSubMenu_Ctor,
        "CSWGuiCreateDebugItemSubMenu_Ctor");
#endif

#if DEFER_INGAME_TAB_CONSTRUCTION
    bool allTabConstructorHooksReady = true;
    allTabConstructorHooksReady &= InstallCheckedHook(0x008a92d0, (LPVOID)&Hook_CSWGuiInGameEquip_Ctor,
        (LPVOID*)&g_originalCSWGuiInGameEquip_Ctor, "CSWGuiInGameEquip_Ctor");
    allTabConstructorHooksReady &= InstallCheckedHook(0x008a6170, (LPVOID)&Hook_CSWGuiInGameInventory_Ctor,
        (LPVOID*)&g_originalCSWGuiInGameInventory_Ctor, "CSWGuiInGameInventory_Ctor");
    allTabConstructorHooksReady &= InstallCheckedHook(0x0084c3a0, (LPVOID)&Hook_CSWGuiInGameCharacter_Ctor,
        (LPVOID*)&g_originalCSWGuiInGameCharacter_Ctor, "CSWGuiInGameCharacter_Ctor");
    allTabConstructorHooksReady &= InstallCheckedHook(0x008a25c0, (LPVOID)&Hook_CSWGuiInGameAbilities_Ctor,
        (LPVOID*)&g_originalCSWGuiInGameAbilities_Ctor, "CSWGuiInGameAbilities_Ctor");
    allTabConstructorHooksReady &= InstallCheckedHook(0x0089cf30, (LPVOID)&Hook_CSWGuiPartySelection_Ctor,
        (LPVOID*)&g_originalCSWGuiPartySelection_Ctor, "CSWGuiPartySelection_Ctor");
    allTabConstructorHooksReady &= InstallCheckedHook(0x007fae60, (LPVOID)&Hook_CSWGuiInGameJournal_Ctor,
        (LPVOID*)&g_originalCSWGuiInGameJournal_Ctor, "CSWGuiInGameJournal_Ctor");
    allTabConstructorHooksReady &= InstallCheckedHook(0x00893950, (LPVOID)&Hook_CSWGuiInGameMap_Ctor,
        (LPVOID*)&g_originalCSWGuiInGameMap_Ctor, "CSWGuiInGameMap_Ctor");
    allTabConstructorHooksReady &= InstallCheckedHook(0x008a1170, (LPVOID)&Hook_CSWGuiInGameOptions_Ctor,
        (LPVOID*)&g_originalCSWGuiInGameOptions_Ctor, "CSWGuiInGameOptions_Ctor");
    bool lazyTabHookReady = InstallCheckedHook(0x007d0760, (LPVOID)&Hook_CSWGuiInGamePanel_LazyInitTab,
        (LPVOID*)&g_originalCSWGuiInGamePanel_LazyInitTab, "CSWGuiInGamePanel_LazyInitTab");
    if (moduleChunkHookReady && allTabConstructorHooksReady && lazyTabHookReady) {
        InterlockedExchange(&g_lazyTabHooksReady, TRUE);
        Log("Lazy in-game tab deferral enabled");
    } else {
        Log("Lazy in-game tab deferral disabled: required hook unavailable");
    }
#endif
}

void InstallHook() {
    
    if (MH_Initialize() != MH_OK) {
        Log("MinHook init failed");
        return;
    }

    if (!IsSupportedSteamExecutable()) {
        Log("Hooks not installed: unsupported swkotor2.exe build");
        return;
    }

    InstallPerformanceHooks();

#if PERFORMANCE_HOOK_SET_ONLY
    return;
#endif
    
    HMODULE hModule = GetModuleHandle(nullptr);
    DWORD baseAddr = (DWORD)hModule;
    
    
    void* targetAddr = (void*)(0x533830); //Just putting in actual address
    if (MH_CreateHook(targetAddr, &Hook_loadingscreenPtr, 
        (LPVOID*)&g_originalLoadingScreenPtr) == MH_OK) {
            if (MH_EnableHook(targetAddr) == MH_OK) {
                Log("LoadScreen hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }


        
    void* targetAddr_LoadAndInitialize = (void*)(0x5582f0); //Just putting in actual address
    if (MH_CreateHook(targetAddr_LoadAndInitialize, &Hook_LoadAndInitializePtr, 
        (LPVOID*)&g_originalLoadAndInitializePtr) == MH_OK) {
            if (MH_EnableHook(targetAddr_LoadAndInitialize) == MH_OK) {
                Log("LoadAndInitialize hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_ProcessResourceQueue = (void*)(0x703f30); //Just putting in actual address
    if (MH_CreateHook(targetAddr_ProcessResourceQueue, &Hook_ProcessResourceQueue, 
        (LPVOID*)&g_originalProcessResourceQueuePtr) == MH_OK) {
            if (MH_EnableHook(targetAddr_ProcessResourceQueue) == MH_OK) {
                Log("ProcessResourceQueue hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_HandleBNPacket = (void*)(0x704880); //Just putting in actual address
    if (MH_CreateHook(targetAddr_HandleBNPacket, &Hook_HandleBNPacket, 
        (LPVOID*)&g_originalHandleBNPacketPtr) == MH_OK) {
            if (MH_EnableHook(targetAddr_HandleBNPacket) == MH_OK) {
                Log("HandleBNPacket hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }


    void* targetAddr_InitShadowCache = (void*)(0x53a8b0); //Just putting in actual address
    if (MH_CreateHook(targetAddr_InitShadowCache, &Hook_InitShadowCache, 
        (LPVOID*)&g_originalInitShadowCachePtr) == MH_OK) {
            if (MH_EnableHook(targetAddr_InitShadowCache) == MH_OK) {
                Log("InitShadowCache hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_LoadResourceBlockOrFallback = (void*)(0x718e40); //Just putting in actual address
    if (MH_CreateHook(targetAddr_LoadResourceBlockOrFallback, &Hook_LoadResourceBlockOrFallback, 
        (LPVOID*)&g_originalLoadResourceBlockOrFallbackPtr) == MH_OK) {
            if (MH_EnableHook(targetAddr_LoadResourceBlockOrFallback) == MH_OK) {
                Log("LoadResourceBlockOrFallback hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }


#if HOOK_CEXOSTRING_CLEAR_TIMING
    void* targetAddr_CExoString_ClearAndFree = (void*)(0x733780);
    if (MH_CreateHook(targetAddr_CExoString_ClearAndFree, &Hook_CExoString_ClearAndFree,
        (LPVOID*)&g_originalCExoString_ClearAndFree) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoString_ClearAndFree) == MH_OK) {
                Log("CExoString_ClearAndFree hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }
#endif

    void* targetAddr_ResourcePacketDispatcher = (void*)(0x5314e0); //Just putting in actual address
    if (MH_CreateHook(targetAddr_ResourcePacketDispatcher, &Hook_ResourcePacketDispatcher, 
        (LPVOID*)&g_originalResourcePacketPtr) == MH_OK) {
            if (MH_EnableHook(targetAddr_ResourcePacketDispatcher) == MH_OK) {
                Log("ResourcePacketDispatcher hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_ResourceQueue_UnpackAndTrace = (void*)(0x781840); //Just putting in actual address
    if (MH_CreateHook(targetAddr_ResourceQueue_UnpackAndTrace, &Hook_ResourceQueue_UnpackAndTrace, 
        (LPVOID*)&g_originalResourceQueue_UnpackAndTracePtr) == MH_OK) {
            if (MH_EnableHook(targetAddr_ResourceQueue_UnpackAndTrace) == MH_OK) {
                Log("ResourceQueue_UnpackAndTrace hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }


    void* targetAddr_PpacketHandler = (void*)(0x810cf0); //Just putting in actual address
    if (MH_CreateHook(targetAddr_PpacketHandler, &Hook_PpacketHandler, 
        (LPVOID*)&g_originalPpacketHandler) == MH_OK) {
            if (MH_EnableHook(targetAddr_PpacketHandler) == MH_OK) {
                Log("PpacketHandler hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }


    void* targetAddr_SpacketHandler = (void*)(0x884530); //Just putting in actual address
    if (MH_CreateHook(targetAddr_SpacketHandler, &Hook_SpacketHandler, 
        (LPVOID*)&g_originalSpacketHandler) == MH_OK) {
            if (MH_EnableHook(targetAddr_SpacketHandler) == MH_OK) {
                Log("PpacketHandler hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }


    void* targetAddr_ModuleHandler = (void*)(0x811450); //Just putting in actual address
    if (MH_CreateHook(targetAddr_ModuleHandler, &Hook_ModuleHandler, 
        (LPVOID*)&g_originalModuleHandler) == MH_OK) {
            if (MH_EnableHook(targetAddr_ModuleHandler) == MH_OK) {
                Log("ModuleHandler hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_GameObjUpdate = (void*)(0x80deb0); //Just putting in actual address
    if (MH_CreateHook(targetAddr_GameObjUpdate, &Hook_GameObjUpdate, 
        (LPVOID*)&g_originalGameObjUpdate) == MH_OK) {
            if (MH_EnableHook(targetAddr_GameObjUpdate) == MH_OK) {
                Log("GameObjUpdate hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

#if SKIP_DEBUG_GUI_CONSTRUCTION
    void* targetAddr_CSWGuiLoadModuleDebugMenu_Ctor = (void*)(0x8c0cb0); //Just putting in actual address
    if (MH_CreateHook(targetAddr_CSWGuiLoadModuleDebugMenu_Ctor, &Hook_CSWGuiLoadModuleDebugMenu_Ctor, 
        (LPVOID*)&g_originalCSWGuiLoadModuleDebugMenu_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiLoadModuleDebugMenu_Ctor) == MH_OK) {
                Log("CSWGuiLoadModuleDebugMenu_Ctor hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_CSWGuiPowersFeatsSkillsDebugMenu_Ctor = (void*)(0x8bfb60); //Just putting in actual address
    if (MH_CreateHook(targetAddr_CSWGuiPowersFeatsSkillsDebugMenu_Ctor, &Hook_CSWGuiPowersFeatsSkillsDebugMenu_Ctor, 
        (LPVOID*)&g_originalCSWGuiPowersFeatsSkillsDebugMenu_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiPowersFeatsSkillsDebugMenu_Ctor) == MH_OK) {
                Log("CSWGuiPowersFeatsSkillsDebugMenu_Ctor hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }
#endif

    void* targetAddr_CSWGuiDialogCinematic_Ctor = (void*)(0x8bba80); //Just putting in actual address
    if (MH_CreateHook(targetAddr_CSWGuiDialogCinematic_Ctor, &Hook_CSWGuiDialogCinematic_Ctor, 
        (LPVOID*)&g_originalCSWGuiDialogCinematic_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiDialogCinematic_Ctor) == MH_OK) {
                Log("CSWGuiDialogCinematic_Ctor hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }


    void* targetAddr_CSWGuiDialogComputerCamera_Ctor = (void*)(0x8bd910); //Just putting in actual address
    if (MH_CreateHook(targetAddr_CSWGuiDialogComputerCamera_Ctor, &Hook_CSWGuiDialogComputerCamera_Ctor, 
        (LPVOID*)&g_originalCSWGuiDialogComputerCamera_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiDialogComputerCamera_Ctor) == MH_OK) {
                Log("CSWGuiDialogComputerCamera_Ctor hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_CSWGuiSkillInfoBox_Ctor = (void*)(0x89c5f0); //Just putting in actual address
    if (MH_CreateHook(targetAddr_CSWGuiSkillInfoBox_Ctor, &Hook_CSWGuiSkillInfoBox_Ctor, 
        (LPVOID*)&g_originalCSWGuiSkillInfoBox_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiSkillInfoBox_Ctor) == MH_OK) {
                Log("CSWGuiSkillInfoBox_Ctor hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_CSWGuiDialogComputer_Ctor = (void*)(0x8bc620); //Just putting in actual address
    if (MH_CreateHook(targetAddr_CSWGuiDialogComputer_Ctor, &Hook_CSWGuiDialogComputer_Ctor, 
        (LPVOID*)&g_originalCSWGuiDialogComputer_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiDialogComputer_Ctor) == MH_OK) {
                Log("CSWGuiDialogComputer_Ctor hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_CSWGuiContainer_Ctor = (void*)(0x8b1ea0); //Just putting in actual address
    if (MH_CreateHook(targetAddr_CSWGuiContainer_Ctor, &Hook_CSWGuiContainer_Ctor, 
        (LPVOID*)&g_originalCSWGuiContainer_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiContainer_Ctor) == MH_OK) {
                Log("CSWGuiContainer_Ctor hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_CSWGuiExamine_Ctor = (void*)(0x8becf0); //Just putting in actual address
    if (MH_CreateHook(targetAddr_CSWGuiExamine_Ctor, &Hook_CSWGuiExamine_Ctor, 
        (LPVOID*)&g_originalCSWGuiExamine_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiExamine_Ctor) == MH_OK) {
                Log("CSWGuiExamine_Ctor hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

#if SKIP_DEBUG_GUI_CONSTRUCTION
    void* targetAddr_CSWGuiCreateDebugItemSubMenu_Ctor = (void*)(0x8bee10); //Just putting in actual address
    if (MH_CreateHook(targetAddr_CSWGuiCreateDebugItemSubMenu_Ctor, &Hook_CSWGuiCreateDebugItemSubMenu_Ctor, 
        (LPVOID*)&g_originalCSWGuiCreateDebugItemSubMenu_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiCreateDebugItemSubMenu_Ctor) == MH_OK) {
                Log("CSWGuiCreateDebugItemSubMenu_Ctor hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }
#endif
    
    void* targetAddr_CSWGuiTutorialBox_Ctor = (void*)(0x898ca0);
    if (MH_CreateHook(targetAddr_CSWGuiTutorialBox_Ctor, &Hook_CSWGuiTutorialBox_Ctor, 
        (LPVOID*)&g_originalCSWGuiTutorialBox_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiTutorialBox_Ctor) == MH_OK) {
                Log("CSWGuiTutorialBox_Ctor hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_CSWGuiBarkBubble_Ctor = (void*)(0x8bdc90);
    if (MH_CreateHook(targetAddr_CSWGuiBarkBubble_Ctor, &Hook_CSWGuiBarkBubble_Ctor,
        (LPVOID*)&g_originalCSWGuiBarkBubble_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiBarkBubble_Ctor) == MH_OK) {
                Log("CSWGuiBarkBubble_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CSWGuiMessageBox_Ctor = (void*)(0x75ae40);
    if (MH_CreateHook(targetAddr_CSWGuiMessageBox_Ctor, &Hook_CSWGuiMessageBox_Ctor,
        (LPVOID*)&g_originalCSWGuiMessageBox_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiMessageBox_Ctor) == MH_OK) {
                Log("CSWGuiMessageBox_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CSWGuiMessageBoxVariant_Ctor = (void*)(0x75b370);
    if (MH_CreateHook(targetAddr_CSWGuiMessageBoxVariant_Ctor, &Hook_CSWGuiMessageBoxVariant_Ctor,
        (LPVOID*)&g_originalCSWGuiMessageBoxVariant_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiMessageBoxVariant_Ctor) == MH_OK) {
                Log("CSWGuiMessageBoxVariant_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CSWGuiDialogLetterbox_Ctor = (void*)(0x8ba980);
    if (MH_CreateHook(targetAddr_CSWGuiDialogLetterbox_Ctor, &Hook_CSWGuiDialogLetterbox_Ctor,
        (LPVOID*)&g_originalCSWGuiDialogLetterbox_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiDialogLetterbox_Ctor) == MH_OK) {
                Log("CSWGuiDialogLetterbox_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CSWGuiFade_Ctor = (void*)(0x7bc600);
    if (MH_CreateHook(targetAddr_CSWGuiFade_Ctor, &Hook_CSWGuiFade_Ctor,
        (LPVOID*)&g_originalCSWGuiFade_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiFade_Ctor) == MH_OK) {
                Log("CSWGuiFade_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CSWGuiInGameMenu_Ctor = (void*)(0x754ed0);
    if (MH_CreateHook(targetAddr_CSWGuiInGameMenu_Ctor, &Hook_CSWGuiInGameMenu_Ctor,
        (LPVOID*)&g_originalCSWGuiInGameMenu_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiInGameMenu_Ctor) == MH_OK) {
                Log("CSWGuiInGameMenu_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CSWGuiInGamePause_Ctor = (void*)(0x8b91f0);
    if (MH_CreateHook(targetAddr_CSWGuiInGamePause_Ctor, &Hook_CSWGuiInGamePause_Ctor,
        (LPVOID*)&g_originalCSWGuiInGamePause_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiInGamePause_Ctor) == MH_OK) {
                Log("CSWGuiInGamePause_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CSWGuiInGameSoloModeQuery_Ctor = (void*)(0x8b8c40);
    if (MH_CreateHook(targetAddr_CSWGuiInGameSoloModeQuery_Ctor, &Hook_CSWGuiInGameSoloModeQuery_Ctor,
        (LPVOID*)&g_originalCSWGuiInGameSoloModeQuery_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiInGameSoloModeQuery_Ctor) == MH_OK) {
                Log("CSWGuiInGameSoloModeQuery_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CSWGuiInGameAreaTransition_Ctor = (void*)(0x8b82e0);
    if (MH_CreateHook(targetAddr_CSWGuiInGameAreaTransition_Ctor, &Hook_CSWGuiInGameAreaTransition_Ctor,
        (LPVOID*)&g_originalCSWGuiInGameAreaTransition_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiInGameAreaTransition_Ctor) == MH_OK) {
                Log("CSWGuiInGameAreaTransition_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CSWGuiInGameMessages_Ctor = (void*)(0x757c40);
    if (MH_CreateHook(targetAddr_CSWGuiInGameMessages_Ctor, &Hook_CSWGuiInGameMessages_Ctor,
        (LPVOID*)&g_originalCSWGuiInGameMessages_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiInGameMessages_Ctor) == MH_OK) {
                Log("CSWGuiInGameMessages_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CSWGuiStore_Ctor = (void*)(0x8b4270);
    if (MH_CreateHook(targetAddr_CSWGuiStore_Ctor, &Hook_CSWGuiStore_Ctor,
        (LPVOID*)&g_originalCSWGuiStore_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiStore_Ctor) == MH_OK) {
                Log("CSWGuiStore_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

#if DEFER_INGAME_TAB_CONSTRUCTION
    void* targetAddr_CSWGuiInGameEquip_Ctor = (void*)(0x8a92d0);
    if (MH_CreateHook(targetAddr_CSWGuiInGameEquip_Ctor, &Hook_CSWGuiInGameEquip_Ctor,
        (LPVOID*)&g_originalCSWGuiInGameEquip_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiInGameEquip_Ctor) == MH_OK) {
                Log("CSWGuiInGameEquip_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CSWGuiInGameInventory_Ctor = (void*)(0x8a6170);
    if (MH_CreateHook(targetAddr_CSWGuiInGameInventory_Ctor, &Hook_CSWGuiInGameInventory_Ctor,
        (LPVOID*)&g_originalCSWGuiInGameInventory_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiInGameInventory_Ctor) == MH_OK) {
                Log("CSWGuiInGameInventory_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CSWGuiInGameCharacter_Ctor = (void*)(0x84c3a0);
    if (MH_CreateHook(targetAddr_CSWGuiInGameCharacter_Ctor, &Hook_CSWGuiInGameCharacter_Ctor,
        (LPVOID*)&g_originalCSWGuiInGameCharacter_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiInGameCharacter_Ctor) == MH_OK) {
                Log("CSWGuiInGameCharacter_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }
#endif

    void* targetAddr_CSWGuiStatusSummary_Ctor = (void*)(0x75cec0);
    if (MH_CreateHook(targetAddr_CSWGuiStatusSummary_Ctor, &Hook_CSWGuiStatusSummary_Ctor,
        (LPVOID*)&g_originalCSWGuiStatusSummary_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiStatusSummary_Ctor) == MH_OK) {
                Log("CSWGuiStatusSummary_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

#if DEFER_INGAME_TAB_CONSTRUCTION
    void* targetAddr_CSWGuiInGameMap_Ctor = (void*)(0x893950);
    if (MH_CreateHook(targetAddr_CSWGuiInGameMap_Ctor, &Hook_CSWGuiInGameMap_Ctor,
        (LPVOID*)&g_originalCSWGuiInGameMap_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiInGameMap_Ctor) == MH_OK) {
                Log("CSWGuiInGameMap_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CSWGuiInGameAbilities_Ctor = (void*)(0x8a25c0);
    if (MH_CreateHook(targetAddr_CSWGuiInGameAbilities_Ctor, &Hook_CSWGuiInGameAbilities_Ctor,
        (LPVOID*)&g_originalCSWGuiInGameAbilities_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiInGameAbilities_Ctor) == MH_OK) {
                Log("CSWGuiInGameAbilities_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CSWGuiInGameJournal_Ctor = (void*)(0x7fae60);
    if (MH_CreateHook(targetAddr_CSWGuiInGameJournal_Ctor, &Hook_CSWGuiInGameJournal_Ctor,
        (LPVOID*)&g_originalCSWGuiInGameJournal_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiInGameJournal_Ctor) == MH_OK) {
                Log("CSWGuiInGameJournal_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CSWGuiInGameOptions_Ctor = (void*)(0x8a1170);
    if (MH_CreateHook(targetAddr_CSWGuiInGameOptions_Ctor, &Hook_CSWGuiInGameOptions_Ctor,
        (LPVOID*)&g_originalCSWGuiInGameOptions_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiInGameOptions_Ctor) == MH_OK) {
                Log("CSWGuiInGameOptions_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CSWGuiPartySelection_Ctor = (void*)(0x89cf30);
    if (MH_CreateHook(targetAddr_CSWGuiPartySelection_Ctor, &Hook_CSWGuiPartySelection_Ctor,
        (LPVOID*)&g_originalCSWGuiPartySelection_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiPartySelection_Ctor) == MH_OK) {
                Log("CSWGuiPartySelection_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }
#endif

    void* targetAddr_CSWGuiInGameGalaxyMap_Ctor = (void*)(0x8973d0);
    if (MH_CreateHook(targetAddr_CSWGuiInGameGalaxyMap_Ctor, &Hook_CSWGuiInGameGalaxyMap_Ctor,
        (LPVOID*)&g_originalCSWGuiInGameGalaxyMap_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiInGameGalaxyMap_Ctor) == MH_OK) {
                Log("CSWGuiInGameGalaxyMap_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_LoadingScreenUpdateFrame = (void*)(0x409ed0); //Just putting in actual address
    if (MH_CreateHook(targetAddr_LoadingScreenUpdateFrame, &Hook_LoadingScreenUpdateFrame, 
        (LPVOID*)&g_originalLoadingScreenUpdateFrame) == MH_OK) {
            if (MH_EnableHook(targetAddr_LoadingScreenUpdateFrame) == MH_OK) {
                Log("LoadingScreenUpdateFrame hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

#if HOOK_APPSTATE_GET_GUI_CONTEXT_TIMING
    void* targetAddr_AppState_GetGuiContext = (void*)(0x73fea0);
    if (MH_CreateHook(targetAddr_AppState_GetGuiContext, &Hook_AppState_GetGuiContext,
        (LPVOID*)&g_originalAppState_GetGuiContext) == MH_OK) {
            if (MH_EnableHook(targetAddr_AppState_GetGuiContext) == MH_OK) {
                Log("AppState_GetGuiContext hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }
#endif

#if HOOK_APPSTATE_GET_LOAD_PROGRESS_BYTE_TIMING
    void* targetAddr_AppState_GetLoadProgressByte = (void*)(0x740c60);
    if (MH_CreateHook(targetAddr_AppState_GetLoadProgressByte, &Hook_AppState_GetLoadProgressByte,
        (LPVOID*)&g_originalAppState_GetLoadProgressByte) == MH_OK) {
            if (MH_EnableHook(targetAddr_AppState_GetLoadProgressByte) == MH_OK) {
                Log("AppState_GetLoadProgressByte hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }
#endif

#if HOOK_RUNTIME_FLOAT_TO_INT_ST0_TIMING
    void* targetAddr_Runtime_FloatToInt_ST0 = (void*)(0x91c860);
    if (MH_CreateHook(targetAddr_Runtime_FloatToInt_ST0, &Hook_Runtime_FloatToInt_ST0,
        (LPVOID*)&g_originalRuntime_FloatToInt_ST0) == MH_OK) {
            if (MH_EnableHook(targetAddr_Runtime_FloatToInt_ST0) == MH_OK) {
                Log("Runtime_FloatToInt_ST0 hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }
#endif

#if HOOK_APPSTATE_SET_LOAD_BAR_VALUE_TIMING
    void* targetAddr_AppState_SetLoadBarValue = (void*)(0x7405d0);
    if (MH_CreateHook(targetAddr_AppState_SetLoadBarValue, &Hook_AppState_SetLoadBarValue,
        (LPVOID*)&g_originalAppState_SetLoadBarValue) == MH_OK) {
            if (MH_EnableHook(targetAddr_AppState_SetLoadBarValue) == MH_OK) {
                Log("AppState_SetLoadBarValue hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }
#endif

#if HOOK_CSWGUIFADE_SET_TRANSITION_STATE_TIMING
    void* targetAddr_CSWGuiFade_SetTransitionState = (void*)(0x7bc8f0);
    if (MH_CreateHook(targetAddr_CSWGuiFade_SetTransitionState, &Hook_CSWGuiFade_SetTransitionState,
        (LPVOID*)&g_originalCSWGuiFade_SetTransitionState) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiFade_SetTransitionState) == MH_OK) {
                Log("CSWGuiFade_SetTransitionState hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }
#endif

#if HOOK_LOADING_SCREEN_FADE_UPDATE_FRAME_TIMING
    void* targetAddr_LoadingScreenFadeUpdateFrame = (void*)(0x40dac0);
    if (MH_CreateHook(targetAddr_LoadingScreenFadeUpdateFrame, &Hook_LoadingScreenFadeUpdateFrame,
        (LPVOID*)&g_originalLoadingScreenFadeUpdateFrame) == MH_OK) {
            if (MH_EnableHook(targetAddr_LoadingScreenFadeUpdateFrame) == MH_OK) {
                Log("LoadingScreenFadeUpdateFrame hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }
#endif

    void* targetAddr_ConfigParse = (void*)(0x4763b0);
    if (MH_CreateHook(targetAddr_ConfigParse, &Hook_ConfigParse, 
        (LPVOID*)&g_originalConfigParse) == MH_OK) {
            if (MH_EnableHook(targetAddr_ConfigParse) == MH_OK) {
                Log("ConfigParse hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_LevelLoaderAndInitializer = (void*)(0x462320);
    if (MH_CreateHook(targetAddr_LevelLoaderAndInitializer, &Hook_LevelLoaderAndInitializer, 
        (LPVOID*)&g_originalLevelLoaderAndInitializer) == MH_OK) {
            if (MH_EnableHook(targetAddr_LevelLoaderAndInitializer) == MH_OK) {
                Log("LevelLoaderAndInitializer hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

#if SKIP_DEBUG_GUI_CONSTRUCTION
    void* targetAddr_DebugMenuConstructor = (void*)(0x8C19F0);
    if (MH_CreateHook(targetAddr_DebugMenuConstructor, &Hook_DebugMenuConstructor, 
        (LPVOID*)&g_originalDebugMenuConstructor) == MH_OK) {
            if (MH_EnableHook(targetAddr_DebugMenuConstructor) == MH_OK) {
                Log("DebugMenuConstructor hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }
#endif

    void* targetAddr_fopen = (void*)(0x91caeb);
    if (MH_CreateHook(targetAddr_fopen, &Hook_fopen, 
        (LPVOID*)&g_originalfopen) == MH_OK) {
            if (MH_EnableHook(targetAddr_fopen) == MH_OK) {
                Log("fopen hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_gobconstructor = (void*)(0x458b70);
    if (MH_CreateHook(targetAddr_gobconstructor, &Hook_gobconstructor, 
        (LPVOID*)&g_originalgobconstructor) == MH_OK) {
            if (MH_EnableHook(targetAddr_gobconstructor) == MH_OK) {
                Log("gobconstructor hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_Gob_LoadFromFileOrStream = (void*)(0x45a030);
    if (MH_CreateHook(targetAddr_Gob_LoadFromFileOrStream, &Hook_Gob_LoadFromFileOrStream,
        (LPVOID*)&g_originalGob_LoadFromFileOrStream) == MH_OK) {
            if (MH_EnableHook(targetAddr_Gob_LoadFromFileOrStream) == MH_OK) {
                Log("Gob_LoadFromFileOrStream hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_AreaConstructor = (void*)(0x521360);
    if (MH_CreateHook(targetAddr_AreaConstructor, &Hook_AreaConstructor, 
        (LPVOID*)&g_originalAreaConstructor) == MH_OK) {
            if (MH_EnableHook(targetAddr_AreaConstructor) == MH_OK) {
                Log("AreaConstructor hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_InitializeGameUI = (void*)(0x747210);
    if (MH_CreateHook(targetAddr_InitializeGameUI, &Hook_InitializeGameUI, 
        (LPVOID*)&g_originalInitializeGameUIPtr_t) == MH_OK) {
            if (MH_EnableHook(targetAddr_InitializeGameUI) == MH_OK) {
                Log("InitializeGameUI hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }


    void* targetAddr_GUI_Update3DSceneViewPtr_t = (void*)(0x410530);
    if (MH_CreateHook(targetAddr_GUI_Update3DSceneViewPtr_t, &Hook_GUI_Update3DSceneView, 
        (LPVOID*)&g_originalGUI_Update3DSceneViewPtr_t) == MH_OK) {
            if (MH_EnableHook(targetAddr_GUI_Update3DSceneViewPtr_t) == MH_OK) {
                Log("GUI_Update3DSceneView hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }
        
        // void* targetAddr_AllocateMemoryOrThrow = (void*)(0x919723);
        // if (MH_CreateHook(targetAddr_AllocateMemoryOrThrow, &Hook_AllocateMemoryOrThrow, 
        //     (LPVOID*)&g_originalAllocateMemoryOrThrow) == MH_OK) {
            //         if (MH_EnableHook(targetAddr_AllocateMemoryOrThrow) == MH_OK) {
                //             Log("AllocateMemoryOrThrow hook installed successfully");
                //         } else {
                    //             Log("Failed to enable hook");
                    //         }
                    //     } else {
                        //         Log("Failed to create hook");
                        //     }
                        
    void* targetAddr_ModuleDirectoryScanner = (void*)(0x737540);
    if (MH_CreateHook(targetAddr_ModuleDirectoryScanner, &Hook_ModuleDirectoryScanner, 
        (LPVOID*)&g_originalModuleDirectoryScanner) == MH_OK) {
            if (MH_EnableHook(targetAddr_ModuleDirectoryScanner) == MH_OK) {
                Log("ModuleDirectoryScanner hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_ArrayAdd = (void*)(0x83ea60);
    if (MH_CreateHook(targetAddr_ArrayAdd, &Hook_ArrayAdd, 
        (LPVOID*)&g_originalArrayAdd) == MH_OK) {
            if (MH_EnableHook(targetAddr_ArrayAdd) == MH_OK) {
                Log("ArrayAdd hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_OpenOrStreamGameFile = (void*)(0x475ab0);
    if (MH_CreateHook(targetAddr_OpenOrStreamGameFile, &Hook_OpenOrStreamGameFile,
        (LPVOID*)&g_originalOpenOrStreamGameFile) == MH_OK) {
            if (MH_EnableHook(targetAddr_OpenOrStreamGameFile) == MH_OK) {
                Log("OpenOrStreamGameFile hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_Texture_ApplyTXIAndBuildController = (void*)(0x424b10);
    if (MH_CreateHook(targetAddr_Texture_ApplyTXIAndBuildController, &Hook_Texture_ApplyTXIAndBuildController,
        (LPVOID*)&g_originalTexture_ApplyTXIAndBuildController) == MH_OK) {
            if (MH_EnableHook(targetAddr_Texture_ApplyTXIAndBuildController) == MH_OK) {
                Log("Texture_ApplyTXIAndBuildController hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

#if HOOK_TEXTURE_CACHE_TIMING
    void* targetAddr_Texture_FindExisting = (void*)(0x4269f0);
    if (MH_CreateHook(targetAddr_Texture_FindExisting, &Hook_Texture_FindExisting,
        (LPVOID*)&g_originalTexture_FindExisting) == MH_OK) {
            if (MH_EnableHook(targetAddr_Texture_FindExisting) == MH_OK) {
                Log("Texture_FindExisting hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_Texture_UpdateResourceBinding = (void*)(0x426d00);
    if (MH_CreateHook(targetAddr_Texture_UpdateResourceBinding, &Hook_Texture_UpdateResourceBinding,
        (LPVOID*)&g_originalTexture_UpdateResourceBinding) == MH_OK) {
            if (MH_EnableHook(targetAddr_Texture_UpdateResourceBinding) == MH_OK) {
                Log("Texture_UpdateResourceBinding hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_Texture_ReplaceAcrossUsers = (void*)(0x426f20);
    if (MH_CreateHook(targetAddr_Texture_ReplaceAcrossUsers, &Hook_Texture_ReplaceAcrossUsers,
        (LPVOID*)&g_originalTexture_ReplaceAcrossUsers) == MH_OK) {
            if (MH_EnableHook(targetAddr_Texture_ReplaceAcrossUsers) == MH_OK) {
                Log("Texture_ReplaceAcrossUsers hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_Texture_AcquireAndRelease = (void*)(0x4274e0);
    if (MH_CreateHook(targetAddr_Texture_AcquireAndRelease, &Hook_Texture_AcquireAndRelease,
        (LPVOID*)&g_originalTexture_AcquireAndRelease) == MH_OK) {
            if (MH_EnableHook(targetAddr_Texture_AcquireAndRelease) == MH_OK) {
                Log("Texture_AcquireAndRelease hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_Texture_GetOrCreate = (void*)(0x427520);
    if (MH_CreateHook(targetAddr_Texture_GetOrCreate, &Hook_Texture_GetOrCreate,
        (LPVOID*)&g_originalTexture_GetOrCreate) == MH_OK) {
            if (MH_EnableHook(targetAddr_Texture_GetOrCreate) == MH_OK) {
                Log("Texture_GetOrCreate hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }
#endif

#if HOOK_PARSE_TXI_AND_BUILD_TEXTURE_CONTROLLER_TIMING
    void* targetAddr_ParseTXIAndBuildTextureController = (void*)(0x423ab0);
    if (MH_CreateHook(targetAddr_ParseTXIAndBuildTextureController, &Hook_ParseTXIAndBuildTextureController,
        (LPVOID*)&g_originalParseTXIAndBuildTextureController) == MH_OK) {
            if (MH_EnableHook(targetAddr_ParseTXIAndBuildTextureController) == MH_OK) {
                Log("ParseTXIAndBuildTextureController hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }
#endif

    void* targetAddr_Texture_ApplyTXIBlendingMode = (void*)(0x45bf50);
    if (MH_CreateHook(targetAddr_Texture_ApplyTXIBlendingMode, &Hook_Texture_ApplyTXIBlendingMode,
        (LPVOID*)&g_originalTexture_ApplyTXIBlendingMode) == MH_OK) {
            if (MH_EnableHook(targetAddr_Texture_ApplyTXIBlendingMode) == MH_OK) {
                Log("Texture_ApplyTXIBlendingMode hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_Texture_ApplyTXIMaterialDirectives = (void*)(0x4da3f0);
    if (MH_CreateHook(targetAddr_Texture_ApplyTXIMaterialDirectives, &Hook_Texture_ApplyTXIMaterialDirectives,
        (LPVOID*)&g_originalTexture_ApplyTXIMaterialDirectives) == MH_OK) {
            if (MH_EnableHook(targetAddr_Texture_ApplyTXIMaterialDirectives) == MH_OK) {
                Log("Texture_ApplyTXIMaterialDirectives hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

    void* targetAddr_GUI_FindAndBindControlByTag = (void*)(0x00418df0);
    if (MH_CreateHook(targetAddr_GUI_FindAndBindControlByTag, &Hook_GUI_FindAndBindControlByTag,
        (LPVOID*)&g_originalGUI_FindAndBindControlByTag) == MH_OK) {
            if (MH_EnableHook(targetAddr_GUI_FindAndBindControlByTag) == MH_OK) {
                Log("GUI_FindAndBindControlByTag hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_GUI_BindNamedWidget = (void*)(0x0040f620);
    if (MH_CreateHook(targetAddr_GUI_BindNamedWidget, &Hook_GUI_BindNamedWidget,
        (LPVOID*)&g_originalGUI_BindNamedWidget) == MH_OK) {
            if (MH_EnableHook(targetAddr_GUI_BindNamedWidget) == MH_OK) {
                Log("GUI_BindNamedWidget hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_GUI_InitWidgetFromGFF = (void*)(0x0040ee40);
    if (MH_CreateHook(targetAddr_GUI_InitWidgetFromGFF, &Hook_GUI_InitWidgetFromGFF,
        (LPVOID*)&g_originalGUI_InitWidgetFromGFF) == MH_OK) {
            if (MH_EnableHook(targetAddr_GUI_InitWidgetFromGFF) == MH_OK) {
                Log("GUI_InitWidgetFromGFF hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_GUI_BindChildStructByName = (void*)(0x00418da0);
    if (MH_CreateHook(targetAddr_GUI_BindChildStructByName, &Hook_GUI_BindChildStructByName,
        (LPVOID*)&g_originalGUI_BindChildStructByName) == MH_OK) {
            if (MH_EnableHook(targetAddr_GUI_BindChildStructByName) == MH_OK) {
                Log("GUI_BindChildStructByName hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_GUI_BaseControlSetup = (void*)(0x004188e0);
    if (MH_CreateHook(targetAddr_GUI_BaseControlSetup, &Hook_GUI_BaseControlSetup,
        (LPVOID*)&g_originalGUI_BaseControlSetup) == MH_OK) {
            if (MH_EnableHook(targetAddr_GUI_BaseControlSetup) == MH_OK) {
                Log("GUI_BaseControlSetup hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_GUI_CommonBaseBinder = (void*)(0x00418f20);
    if (MH_CreateHook(targetAddr_GUI_CommonBaseBinder, &Hook_GUI_CommonBaseBinder,
        (LPVOID*)&g_originalGUI_CommonBaseBinder) == MH_OK) {
            if (MH_EnableHook(targetAddr_GUI_CommonBaseBinder) == MH_OK) {
                Log("GUI_CommonBaseBinder hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_GUI_ListBoxBindProtoItem = (void*)(0x00420180);
    if (MH_CreateHook(targetAddr_GUI_ListBoxBindProtoItem, &Hook_GUI_ListBoxBindProtoItem,
        (LPVOID*)&g_originalGUI_ListBoxBindProtoItem) == MH_OK) {
            if (MH_EnableHook(targetAddr_GUI_ListBoxBindProtoItem) == MH_OK) {
                Log("GUI_ListBoxBindProtoItem hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

#if HOOK_GUI_DEEP_GFF_TIMING
    void* targetAddr_GFF_ReadBoolFieldByName = (void*)(0x00718a00);
    if (MH_CreateHook(targetAddr_GFF_ReadBoolFieldByName, &Hook_GFF_ReadBoolFieldByName,
        (LPVOID*)&g_originalGFF_ReadBoolFieldByName) == MH_OK) {
            if (MH_EnableHook(targetAddr_GFF_ReadBoolFieldByName) == MH_OK) {
                Log("GFF_ReadBoolFieldByName hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_GFF_ReadIntFieldByName = (void*)(0x00718c80);
    if (MH_CreateHook(targetAddr_GFF_ReadIntFieldByName, &Hook_GFF_ReadIntFieldByName,
        (LPVOID*)&g_originalGFF_ReadIntFieldByName) == MH_OK) {
            if (MH_EnableHook(targetAddr_GFF_ReadIntFieldByName) == MH_OK) {
                Log("GFF_ReadIntFieldByName hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_GFF_ReadVector3FieldByName = (void*)(0x00719520);
    if (MH_CreateHook(targetAddr_GFF_ReadVector3FieldByName, &Hook_GFF_ReadVector3FieldByName,
        (LPVOID*)&g_originalGFF_ReadVector3FieldByName) == MH_OK) {
            if (MH_EnableHook(targetAddr_GFF_ReadVector3FieldByName) == MH_OK) {
                Log("GFF_ReadVector3FieldByName hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

#endif

#if HOOK_GUI_DEEP_GFF_TIMING
    void* targetAddr_GFF_LookupFieldLabelByName = (void*)(0x007178e0);
    if (MH_CreateHook(targetAddr_GFF_LookupFieldLabelByName, &Hook_GFF_LookupFieldLabelByName,
        (LPVOID*)&g_originalGFF_LookupFieldLabelByName) == MH_OK) {
            if (MH_EnableHook(targetAddr_GFF_LookupFieldLabelByName) == MH_OK) {
                Log("GFF_LookupFieldLabelByName hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }
#endif

    void* targetAddr_ResourceEnsureLoaded = (void*)(0x711c20);
    if (MH_CreateHook(targetAddr_ResourceEnsureLoaded, &Hook_ResourceEnsureLoaded,
        (LPVOID*)&g_originalResourceEnsureLoaded) == MH_OK) {
            if (MH_EnableHook(targetAddr_ResourceEnsureLoaded) == MH_OK) {
                Log("ResourceEnsureLoaded hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_ResourceLoadFromArchiveSlot = (void*)(0x713fb0);
    if (MH_CreateHook(targetAddr_ResourceLoadFromArchiveSlot, &Hook_ResourceLoadFromArchiveSlot,
        (LPVOID*)&g_originalResourceLoadFromArchiveSlot) == MH_OK) {
            if (MH_EnableHook(targetAddr_ResourceLoadFromArchiveSlot) == MH_OK) {
                Log("ResourceLoadFromArchiveSlot hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_ResourceLoadMemoryBacked = (void*)(0x713e80);
    if (MH_CreateHook(targetAddr_ResourceLoadMemoryBacked, &Hook_ResourceLoadMemoryBacked,
        (LPVOID*)&g_originalResourceLoadMemoryBacked) == MH_OK) {
            if (MH_EnableHook(targetAddr_ResourceLoadMemoryBacked) == MH_OK) {
                Log("ResourceLoadMemoryBacked hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_ResourceLoadFromArchive = (void*)(0x713bf0);
    if (MH_CreateHook(targetAddr_ResourceLoadFromArchive, &Hook_ResourceLoadFromArchive,
        (LPVOID*)&g_originalResourceLoadFromArchive) == MH_OK) {
            if (MH_EnableHook(targetAddr_ResourceLoadFromArchive) == MH_OK) {
                Log("ResourceLoadFromArchive hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_ResourceLoadFromLooseFile = (void*)(0x7133a0);
    if (MH_CreateHook(targetAddr_ResourceLoadFromLooseFile, &Hook_ResourceLoadFromLooseFile,
        (LPVOID*)&g_originalResourceLoadFromLooseFile) == MH_OK) {
            if (MH_EnableHook(targetAddr_ResourceLoadFromLooseFile) == MH_OK) {
                Log("ResourceLoadFromLooseFile hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_LooseFileOpen = (void*)(0x73da40);
    if (MH_CreateHook(targetAddr_LooseFileOpen, &Hook_LooseFileOpen,
        (LPVOID*)&g_originalLooseFileOpen) == MH_OK) {
            if (MH_EnableHook(targetAddr_LooseFileOpen) == MH_OK) {
                Log("LooseFileOpen hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_LooseFileRead = (void*)(0x73dd20);
    if (MH_CreateHook(targetAddr_LooseFileRead, &Hook_LooseFileRead,
        (LPVOID*)&g_originalLooseFileRead) == MH_OK) {
            if (MH_EnableHook(targetAddr_LooseFileRead) == MH_OK) {
                Log("LooseFileRead hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_ResourceFinalizeAsyncLoad = (void*)(0x715a60);
    if (MH_CreateHook(targetAddr_ResourceFinalizeAsyncLoad, &Hook_ResourceFinalizeAsyncLoad,
        (LPVOID*)&g_originalResourceFinalizeAsyncLoad) == MH_OK) {
            if (MH_EnableHook(targetAddr_ResourceFinalizeAsyncLoad) == MH_OK) {
                Log("ResourceFinalizeAsyncLoad hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_Resource_AllocateLoadBuffer = (void*)(0x712f30);
    if (MH_CreateHook(targetAddr_Resource_AllocateLoadBuffer, &Hook_Resource_AllocateLoadBuffer,
        (LPVOID*)&g_originalResource_AllocateLoadBuffer) == MH_OK) {
            if (MH_EnableHook(targetAddr_Resource_AllocateLoadBuffer) == MH_OK) {
                Log("Resource_AllocateLoadBuffer hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoResFile_AddRefSyncOpen = (void*)(0x7270a0);
    if (MH_CreateHook(targetAddr_CExoResFile_AddRefSyncOpen, &Hook_CExoResFile_AddRefSyncOpen,
        (LPVOID*)&g_originalCExoResFile_AddRefSyncOpen) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoResFile_AddRefSyncOpen) == MH_OK) {
                Log("CExoResFile_AddRefSyncOpen hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoResFile_AddRefAsyncOpen = (void*)(0x7270f0);
    if (MH_CreateHook(targetAddr_CExoResFile_AddRefAsyncOpen, &Hook_CExoResFile_AddRefAsyncOpen,
        (LPVOID*)&g_originalCExoResFile_AddRefAsyncOpen) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoResFile_AddRefAsyncOpen) == MH_OK) {
                Log("CExoResFile_AddRefAsyncOpen hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoResFile_ReleaseSyncClose = (void*)(0x7272f0);
    if (MH_CreateHook(targetAddr_CExoResFile_ReleaseSyncClose, &Hook_CExoResFile_ReleaseSyncClose,
        (LPVOID*)&g_originalCExoResFile_ReleaseSyncClose) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoResFile_ReleaseSyncClose) == MH_OK) {
                Log("CExoResFile_ReleaseSyncClose hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoResFile_ReleaseAsyncClose = (void*)(0x727340);
    if (MH_CreateHook(targetAddr_CExoResFile_ReleaseAsyncClose, &Hook_CExoResFile_ReleaseAsyncClose,
        (LPVOID*)&g_originalCExoResFile_ReleaseAsyncClose) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoResFile_ReleaseAsyncClose) == MH_OK) {
                Log("CExoResFile_ReleaseAsyncClose hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoResFile_GetResourceSize = (void*)(0x727390);
    if (MH_CreateHook(targetAddr_CExoResFile_GetResourceSize, &Hook_CExoResFile_GetResourceSize,
        (LPVOID*)&g_originalCExoResFile_GetResourceSize) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoResFile_GetResourceSize) == MH_OK) {
                Log("CExoResFile_GetResourceSize hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoResFile_ReadResourceSync = (void*)(0x727930);
    if (MH_CreateHook(targetAddr_CExoResFile_ReadResourceSync, &Hook_CExoResFile_ReadResourceSync,
        (LPVOID*)&g_originalCExoResFile_ReadResourceSync) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoResFile_ReadResourceSync) == MH_OK) {
                Log("CExoResFile_ReadResourceSync hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoResFile_ReadResourceAsync = (void*)(0x7279f0);
    if (MH_CreateHook(targetAddr_CExoResFile_ReadResourceAsync, &Hook_CExoResFile_ReadResourceAsync,
        (LPVOID*)&g_originalCExoResFile_ReadResourceAsync) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoResFile_ReadResourceAsync) == MH_OK) {
                Log("CExoResFile_ReadResourceAsync hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoResFile_OpenSyncHandle = (void*)(0x727450);
    if (MH_CreateHook(targetAddr_CExoResFile_OpenSyncHandle, &Hook_CExoResFile_OpenSyncHandle,
        (LPVOID*)&g_originalCExoResFile_OpenSyncHandle) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoResFile_OpenSyncHandle) == MH_OK) {
                Log("CExoResFile_OpenSyncHandle hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoResFile_OpenAsyncHandle = (void*)(0x7275b0);
    if (MH_CreateHook(targetAddr_CExoResFile_OpenAsyncHandle, &Hook_CExoResFile_OpenAsyncHandle,
        (LPVOID*)&g_originalCExoResFile_OpenAsyncHandle) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoResFile_OpenAsyncHandle) == MH_OK) {
                Log("CExoResFile_OpenAsyncHandle hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_ArchiveReaderShared_AddRefSyncOpen = (void*)(0x7295b0);
    if (MH_CreateHook(targetAddr_ArchiveReaderShared_AddRefSyncOpen, &Hook_ArchiveReaderShared_AddRefSyncOpen,
        (LPVOID*)&g_originalArchiveReaderShared_AddRefSyncOpen) == MH_OK) {
            if (MH_EnableHook(targetAddr_ArchiveReaderShared_AddRefSyncOpen) == MH_OK) {
                Log("ArchiveReaderShared_AddRefSyncOpen hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoEncapsulatedFile_AddRefAsyncOpen = (void*)(0x727bb0);
    if (MH_CreateHook(targetAddr_CExoEncapsulatedFile_AddRefAsyncOpen, &Hook_CExoEncapsulatedFile_AddRefAsyncOpen,
        (LPVOID*)&g_originalCExoEncapsulatedFile_AddRefAsyncOpen) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoEncapsulatedFile_AddRefAsyncOpen) == MH_OK) {
                Log("CExoEncapsulatedFile_AddRefAsyncOpen hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoEncapsulatedFile_ReleaseSyncClose = (void*)(0x727d10);
    if (MH_CreateHook(targetAddr_CExoEncapsulatedFile_ReleaseSyncClose, &Hook_CExoEncapsulatedFile_ReleaseSyncClose,
        (LPVOID*)&g_originalCExoEncapsulatedFile_ReleaseSyncClose) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoEncapsulatedFile_ReleaseSyncClose) == MH_OK) {
                Log("CExoEncapsulatedFile_ReleaseSyncClose hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoEncapsulatedFile_ReleaseAsyncClose = (void*)(0x727d50);
    if (MH_CreateHook(targetAddr_CExoEncapsulatedFile_ReleaseAsyncClose, &Hook_CExoEncapsulatedFile_ReleaseAsyncClose,
        (LPVOID*)&g_originalCExoEncapsulatedFile_ReleaseAsyncClose) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoEncapsulatedFile_ReleaseAsyncClose) == MH_OK) {
                Log("CExoEncapsulatedFile_ReleaseAsyncClose hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoEncapsulatedFile_GetResourceSize = (void*)(0x727d90);
    if (MH_CreateHook(targetAddr_CExoEncapsulatedFile_GetResourceSize, &Hook_CExoEncapsulatedFile_GetResourceSize,
        (LPVOID*)&g_originalCExoEncapsulatedFile_GetResourceSize) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoEncapsulatedFile_GetResourceSize) == MH_OK) {
                Log("CExoEncapsulatedFile_GetResourceSize hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoEncapsulatedFile_ReadResourceSync = (void*)(0x729370);
    if (MH_CreateHook(targetAddr_CExoEncapsulatedFile_ReadResourceSync, &Hook_CExoEncapsulatedFile_ReadResourceSync,
        (LPVOID*)&g_originalCExoEncapsulatedFile_ReadResourceSync) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoEncapsulatedFile_ReadResourceSync) == MH_OK) {
                Log("CExoEncapsulatedFile_ReadResourceSync hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoEncapsulatedFile_ReadResourceAsync = (void*)(0x729420);
    if (MH_CreateHook(targetAddr_CExoEncapsulatedFile_ReadResourceAsync, &Hook_CExoEncapsulatedFile_ReadResourceAsync,
        (LPVOID*)&g_originalCExoEncapsulatedFile_ReadResourceAsync) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoEncapsulatedFile_ReadResourceAsync) == MH_OK) {
                Log("CExoEncapsulatedFile_ReadResourceAsync hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoEncapsulatedFile_OpenSyncHandle = (void*)(0x727e30);
    if (MH_CreateHook(targetAddr_CExoEncapsulatedFile_OpenSyncHandle, &Hook_CExoEncapsulatedFile_OpenSyncHandle,
        (LPVOID*)&g_originalCExoEncapsulatedFile_OpenSyncHandle) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoEncapsulatedFile_OpenSyncHandle) == MH_OK) {
                Log("CExoEncapsulatedFile_OpenSyncHandle hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoEncapsulatedFile_OpenAsyncHandle = (void*)(0x728230);
    if (MH_CreateHook(targetAddr_CExoEncapsulatedFile_OpenAsyncHandle, &Hook_CExoEncapsulatedFile_OpenAsyncHandle,
        (LPVOID*)&g_originalCExoEncapsulatedFile_OpenAsyncHandle) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoEncapsulatedFile_OpenAsyncHandle) == MH_OK) {
                Log("CExoEncapsulatedFile_OpenAsyncHandle hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoResourceImageFile_ReleaseSyncClose = (void*)(0x729650);
    if (MH_CreateHook(targetAddr_CExoResourceImageFile_ReleaseSyncClose, &Hook_CExoResourceImageFile_ReleaseSyncClose,
        (LPVOID*)&g_originalCExoResourceImageFile_ReleaseSyncClose) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoResourceImageFile_ReleaseSyncClose) == MH_OK) {
                Log("CExoResourceImageFile_ReleaseSyncClose hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoResourceImageFile_GetResourceSize = (void*)(0x7296e0);
    if (MH_CreateHook(targetAddr_CExoResourceImageFile_GetResourceSize, &Hook_CExoResourceImageFile_GetResourceSize,
        (LPVOID*)&g_originalCExoResourceImageFile_GetResourceSize) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoResourceImageFile_GetResourceSize) == MH_OK) {
                Log("CExoResourceImageFile_GetResourceSize hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoResourceImageFile_ReadResourceSync = (void*)(0x729ae0);
    if (MH_CreateHook(targetAddr_CExoResourceImageFile_ReadResourceSync, &Hook_CExoResourceImageFile_ReadResourceSync,
        (LPVOID*)&g_originalCExoResourceImageFile_ReadResourceSync) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoResourceImageFile_ReadResourceSync) == MH_OK) {
                Log("CExoResourceImageFile_ReadResourceSync hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoResourceImageFile_ReadResourceAsync = (void*)(0x729b80);
    if (MH_CreateHook(targetAddr_CExoResourceImageFile_ReadResourceAsync, &Hook_CExoResourceImageFile_ReadResourceAsync,
        (LPVOID*)&g_originalCExoResourceImageFile_ReadResourceAsync) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoResourceImageFile_ReadResourceAsync) == MH_OK) {
                Log("CExoResourceImageFile_ReadResourceAsync hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

    void* targetAddr_CExoResourceImageFile_LoadImage = (void*)(0x729790);
    if (MH_CreateHook(targetAddr_CExoResourceImageFile_LoadImage, &Hook_CExoResourceImageFile_LoadImage,
        (LPVOID*)&g_originalCExoResourceImageFile_LoadImage) == MH_OK) {
            if (MH_EnableHook(targetAddr_CExoResourceImageFile_LoadImage) == MH_OK) {
                Log("CExoResourceImageFile_LoadImage hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }


}

HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst, DWORD dwVersion, REFIID riid, LPVOID* ppvOut, LPUNKNOWN punkOuter)
{
    if (!realCreate) {
        char sysPath[MAX_PATH];
        GetSystemDirectoryA(sysPath, MAX_PATH);
        strcat_s(sysPath, "\\dinput8.dll");
        HMODULE realDLL = LoadLibraryA(sysPath);
        if (!realDLL) return E_FAIL;
        realCreate = (DICREATE)GetProcAddress(realDLL, "DirectInput8Create");
        if (!realCreate) return E_FAIL;
        
    }

    return realCreate(hinst, dwVersion, riid, ppvOut, punkOuter);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            g_logFile.open("kotor2_log.txt", std::ios::app);
            
            // Install hook after delay
            CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                Sleep(10);
                InstallHook();
                return 0;
            }, nullptr, 0, nullptr);
            break;
            
        case DLL_PROCESS_DETACH:
            MH_Uninitialize();
            if (g_logFile.is_open()) g_logFile.close();
            break;
    }
    return TRUE;
}
