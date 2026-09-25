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
#define LOG_HIGH_FREQUENCY_CALLS 0
// Stability build: install only the small, signature-checked hook set below.  The
// legacy profiler contains many unrelated detours and is intentionally opt-in.
#define PERFORMANCE_HOOK_SET_ONLY 1
#define SKIP_PRELOAD_INITIAL_ASSETS_WRAPPER 1
#define SKIP_LOADING_SCREEN_UPDATE_FRAME_IN_MODULE_CHUNK_LOAD_CORE 0
// Never force the engine's platform GUI mode.  On the PC build it leaves app-owned
// GUI slots null that have no compatible lazy reconstruction path.
#define ENABLE_NATIVE_LAZY_GUI_MODE 0
// A/B test scenarios.  Build scenario A (baseline) or B (optimized) by setting
// AB_SCENARIO below and rebuilding.  The hook set is identical in both scenarios;
// only these optimization toggles differ, and the active scenario plus every
// toggle value is written to the ProfilerRunStart log line for post-run parsing.
#define AB_SCENARIO_A 0
#define AB_SCENARIO_B 1
#define AB_SCENARIO_C 2
// D/E/F are taken on the queue-pump / area-prefetch / ui-timer branches; G was
// the forced-streaming experiment, now on in every scenario (below).
#define AB_SCENARIO AB_SCENARIO_A

// Round-1 B scenario: present throttle + archive byte cache + debug-GUI skip.
// Deliberately excluded from B (isolate in a later round): GUI controls lookup
// cache (small measured win) and in-game tab deferral (stability experiment).
// Round-2 C scenario: baseline toggles + long-fade clamp (single-variable
// experiment vs A).  Load transitions run a 1.0-second fade animation
// (measured: duration_raw=0x3F800000, once per load); clamping it to 1 ms
// removes that second of deliberate pacing from every load.
#if AB_SCENARIO == AB_SCENARIO_B
#define ENABLE_GUI_CONTROLS_LOOKUP_CACHE 0
#define THROTTLE_LOADING_SCREEN_PRESENTS 1
#define LOADING_SCREEN_PRESENT_INTERVAL_MS 100
#define ENABLE_ARCHIVE_RESOURCE_CACHE 1
#define SKIP_DEBUG_GUI_CONSTRUCTION 1
#define DEFER_INGAME_TAB_CONSTRUCTION 0
#define CLAMP_LONG_FADES 0
#elif AB_SCENARIO == AB_SCENARIO_C
#define ENABLE_GUI_CONTROLS_LOOKUP_CACHE 0
#define THROTTLE_LOADING_SCREEN_PRESENTS 0
#define LOADING_SCREEN_PRESENT_INTERVAL_MS 100
#define ENABLE_ARCHIVE_RESOURCE_CACHE 0
#define SKIP_DEBUG_GUI_CONSTRUCTION 0
#define DEFER_INGAME_TAB_CONSTRUCTION 0
#define CLAMP_LONG_FADES 1
#else
#define ENABLE_GUI_CONTROLS_LOOKUP_CACHE 0
#define THROTTLE_LOADING_SCREEN_PRESENTS 0
#define LOADING_SCREEN_PRESENT_INTERVAL_MS 100
#define ENABLE_ARCHIVE_RESOURCE_CACHE 0
#define SKIP_DEBUG_GUI_CONSTRUCTION 0
#define DEFER_INGAME_TAB_CONSTRUCTION 0
#define CLAMP_LONG_FADES 0
#endif
// Forced area streaming: on in every scenario, so experiments build on it.
// After a load the server streams the client's object data in ~2 KB messages,
// at most one per 200 ms (Server_UpdateClient_Throttle200ms 0x00537590), and
// the loading screen stays up until the last one lands.  Passing the engine's
// own force flag while the player is still streaming sends one per tick:
// 101PER stream 1.90 s -> 0.56 s (scenario G, stream_force.md).  Set to 0 for
// a no-force control run; ProfilerRunStart logs it as stream_force=.
#define FORCE_AREA_STREAM_DURING_LOAD 1
#define ARCHIVE_CACHE_MAX_BYTES (256u * 1024u * 1024u)

#if AB_SCENARIO == AB_SCENARIO_C
static const char* const kScenarioName = "c_fadeclamp";
#elif AB_SCENARIO == AB_SCENARIO_B
static const char* const kScenarioName = "b_optimized";
#else
static const char* const kScenarioName = "a_baseline";
#endif
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
    return LogHasPrefix(msg, "loadingscreen:") ||
           LogHasPrefix(msg, "LoadTransition") ||
           LogHasPrefix(msg, "ProfilerRunStart:");
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

// End-to-end transition profiler.  The load-state object is only a coordinator
// handshake: it can clear before queued client/module work finishes.  A complete
// session therefore begins at coordinator preparation and ends only after the
// idle-state final object/event drain returns.
struct EngineLoadStateSnapshot {
    int state;
    int mode;
    int currentIndex;
    int targetOrCount;
    int beginPending;
    int finishPending;
    // Load-context substate at +0x38 (shadow diagnostics only: observed to
    // stay 0 through a full load, so it is NOT the input gate).
    int substate;
    // Client load-busy flag at *(client+0x04)+0x90 (0x0073f2f0).
    // UpdatePlayerInputAndTargeting (0x007ac470) early-returns while this is
    // set, making it the engine's actual "input blocked during load" gate.
    int clientBusy;
};

struct LoadCoordinatorSnapshot {
    int preparationPending;
    short managerStatus;
};

struct LoadTransitionProfile {
    bool active;
    DWORD ownerThreadId;
    unsigned int id;
    const char* startReason;
    LARGE_INTEGER start;
    EngineLoadStateSnapshot startState;
    bool stateActivationSeen;
    LARGE_INTEGER stateActivated;
    bool coordinatorClearSeen;
    LARGE_INTEGER coordinatorCleared;
    bool finalDrainSeen;
    LARGE_INTEGER finalDrainStarted;
    bool presentedFrameSeen;
    LARGE_INTEGER firstPresentedFrame;
    LARGE_INTEGER lastPresentedFrameEnd;
    unsigned int presentedFrameCalls;
    bool moduleWindowSeen;
    LARGE_INTEGER firstModuleStart;
    LARGE_INTEGER lastModuleEnd;
    long long moduleChunkMaxCallUs;
    bool archiveWindowSeen;
    LARGE_INTEGER firstArchiveStart;
    LARGE_INTEGER lastArchiveEnd;
    long long engineUs;
    unsigned int engineCalls;
    long long outerLoadingScreenUs;
    unsigned int outerLoadingScreenCalls;
    long long moduleChunkUs;
    unsigned int moduleChunkCalls;
    long long loadingFrameUs;
    unsigned int loadingFrameCalls;
    long long archiveUs;
    unsigned int archiveCalls;
    long long workerSubmitWaitUs;
    unsigned int workerSubmitCalls;
    long long resourceQueueDrainUs;
    unsigned int resourceQueueDrainCalls;
};

static LoadTransitionProfile g_loadTransition = {};
static unsigned int g_nextLoadTransitionId = 0;
static unsigned long long g_profilerRunId = 0;
thread_local int g_loadingscreenDepth = 0;
thread_local bool g_finishTransitionAtOutermostReturn = false;

static long long QpcElapsedUs(LARGE_INTEGER start, LARGE_INTEGER end) {
    static LARGE_INTEGER frequency = {};
    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
    }
    if (frequency.QuadPart <= 0 || end.QuadPart < start.QuadPart) {
        return 0;
    }
    return ((end.QuadPart - start.QuadPart) * 1000000LL) / frequency.QuadPart;
}

// Signed variant: negative when t precedes anchor (e.g. a timer already
// running before the click).
static long long QpcRelUs(LARGE_INTEGER anchor, LARGE_INTEGER t) {
    return t.QuadPart >= anchor.QuadPart ? QpcElapsedUs(anchor, t)
                                         : -QpcElapsedUs(t, anchor);
}

static bool TryReadEngineLoadState(EngineLoadStateSnapshot& snapshot) {
    __try {
        int root = *(int*)0x00a1b4a4;
        if (root == 0) {
            return false;
        }
        int loadState = *(int*)(root + 0x14);
        if (loadState == 0) {
            return false;
        }
        snapshot.state = *(int*)(loadState + 0x00);
        snapshot.mode = *(int*)(loadState + 0x04);
        snapshot.currentIndex = *(int*)(loadState + 0x08);
        snapshot.targetOrCount = *(int*)(loadState + 0x0c);
        snapshot.beginPending = *(int*)(loadState + 0x10);
        snapshot.finishPending = *(int*)(loadState + 0x14);
        snapshot.substate = *(int*)(loadState + 0x38);
        snapshot.clientBusy = 0;
        int client = *(int*)(root + 0x04);
        if (client != 0) {
            // FUN_0073f2f0(client) == *(*(client + 0x04) + 0x90): the flag
            // lives one object below the client, not on the client itself.
            int inner = *(int*)(client + 0x04);
            if (inner != 0) {
                snapshot.clientBusy = *(int*)(inner + 0x90);
            }
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool TryReadLoadCoordinator(
    int manager, LoadCoordinatorSnapshot& snapshot) {
    if (manager == 0) {
        return false;
    }
    __try {
        snapshot.preparationPending = *(int*)(manager + 0x10080);
        snapshot.managerStatus = *(short*)(manager + 0x10008);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool TryReadGlobalLoadCoordinator(
    int& manager, LoadCoordinatorSnapshot& snapshot) {
    __try {
        int root = *(int*)0x00a1b4a4;
        if (root == 0) {
            return false;
        }
        manager = *(int*)(root + 0x08);
        return TryReadLoadCoordinator(manager, snapshot);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// True perceived load window: from the player's load action (LoadGame entry,
// i.e. the Load button) to the first presented frame after the load transition
// ends.  The transition window above deliberately starts at engine load-state
// activation, which excludes the save read/deserialization before activation
// and the fade-in/first-frame work after the final drain; this tracker measures
// what the player actually waits through.
struct PerceivedLoadProfile {
    bool active;
    DWORD ownerThreadId;
    unsigned int id;
    const char* startSource;
    LARGE_INTEGER start;
    bool transitionStarted;
    LARGE_INTEGER transitionStart;
    bool transitionEnded;
    LARGE_INTEGER transitionEnd;
    unsigned int loadingPresents;
    unsigned int postDrainPresents;
    bool lastPresentBeforeTransitionSeen;
    LARGE_INTEGER lastPresentBeforeTransition;
    LARGE_INTEGER lastPostDrainPresent;
    long long postGap1Us;
    long long postGap2Us;
    long long postGap3Us;
};
static PerceivedLoadProfile g_perceivedLoad = {};
static unsigned int g_nextPerceivedLoadId = 0;

// Click-to-control tracker.  The perceived-load window ends at the first
// presented frame after the drain, but the 2026-09-11 frame-captured session
// proved that frame is BLACK: the engine then presents ~2.6 s of identical
// black frames while it finishes the module packet drain, the area 3D scene
// full-load, and the loose-file read storm, and input stays locked until the
// client busy flag (client+0x90) clears — the gate that early-outs
// UpdatePlayerInputAndTargeting.  This tracker starts at the same click
// anchor as the perceived window but ends at the first Engine tick where
// that flag reads 0, which is the moment the player can actually play.
// Substate/client-busy values are shadow-logged on change so the gate
// semantics stay verified against real builds.
struct ClickToControlProfile {
    bool armed;
    unsigned int id;
    const char* clickSource;
    LARGE_INTEGER click;
    bool transitionStartSeen;
    LARGE_INTEGER transitionStart;
    bool firstPresentSeen;
    LARGE_INTEGER firstPresent;
    int lastSubstate;
    int lastGateValue;
    bool lastSubstateValid;
    // Set once the gate has read nonzero after the click.  A 0 that was never
    // preceded by a busy reading is not an "unblock" and must not be reported
    // as one (that is how a wrong offset would masquerade as a 0 ms window).
    bool gateSeenBusy;
    unsigned int substateLogs;
};
static ClickToControlProfile g_clickToControl = {};

// Per-load loadingscreen accumulator.  The coordinator tick runs across ALL
// load phases (menu, prep, activation, drain) and its busy-time sum was the
// original crude-but-accurate whole-load metric: it lands far closer to hand
// timing than the state-activation window alone.  Accumulated continuously,
// stamped into each PerceivedLoadWallTime line, reset at each load's end.
static unsigned int g_lsCallsSinceLoad = 0;
static long long g_lsBusyUsSinceLoad = 0;
static LARGE_INTEGER g_lsFirstCallSinceLoad = {};

// Packet-pipeline accumulators (per load window): the pre-activation save
// load runs through ProcessResourceQueue -> P/S packet handlers on the main
// thread, so per-major P-packet busy time attributes the pre-activation work.
static unsigned int g_prqCallsSinceLoad = 0;
static long long g_prqBusyUsSinceLoad = 0;
static unsigned int g_ppCallsSinceLoad = 0;
static long long g_ppBusyUsSinceLoad = 0;
static unsigned int g_spCallsSinceLoad = 0;
static long long g_spBusyUsSinceLoad = 0;
static unsigned int g_ppMajorCalls[256] = {};
static long long g_ppMajorUs[256] = {};

// Load diagnostics (per load window): fade animations, per-tick graphics/
// shadow cache scans, loose-file I/O — the Layer-1/2 targets of the
// near-zero load plan.
static unsigned int g_fadeCallsSinceLoad = 0;
static long long g_fadeBusyUsSinceLoad = 0;
static unsigned int g_fadeClampsSinceLoad = 0;
static unsigned int g_igcCallsSinceLoad = 0;
static long long g_igcBusyUsSinceLoad = 0;
static unsigned int g_iscCallsSinceLoad = 0;
static long long g_iscBusyUsSinceLoad = 0;
static unsigned int g_lfOpenCallsSinceLoad = 0;
static long long g_lfOpenBusyUsSinceLoad = 0;
static unsigned int g_lfReadCallsSinceLoad = 0;
static long long g_lfReadBusyUsSinceLoad = 0;
static long long g_lfReadBytesSinceLoad = 0;

// Fire counters for validation.  A hook that installs but never fires (wrong
// call path, engine build change) must be visible in the log instead of being
// discovered by accident after a wasted test run.  The save-load anchor count
// is stamped into every PerceivedLoadWallTime line; all counters are dumped at
// DLL detach.
static volatile LONG64 g_hookCalls_LoadGame = 0;
static volatile LONG64 g_hookCalls_SaveLoadRequest = 0;
static volatile LONG64 g_hookCalls_PopulateSave = 0;
static volatile LONG64 g_hookCalls_GameSaveLoadCore = 0;
static volatile LONG64 g_hookCalls_SwapBuffers = 0;

// Save/load state-machine core accumulator (per load): stage-bucketed busy
// time plus first/last call stamps so busy-vs-span shows the pipeline gating.
struct GameSaveLoadCoreBatch {
    uint32_t calls;
    int64_t totalUs;
    int64_t maxUs;
    LARGE_INTEGER firstCall;
    LARGE_INTEGER lastCall;
    uint32_t stageCalls[8];
    int64_t stageUs[8];
};
static GameSaveLoadCoreBatch g_gslc = {};
static volatile LONG64 g_hookCalls_Loadingscreen = 0;
static volatile LONG64 g_hookCalls_Engine = 0;
// Timestamp of the most recent SwapBuffers completion.  If the main thread
// freezes (e.g. synchronous save read before load-state activation), the gap
// between this stamp and the transition start measures the frozen period.
static LARGE_INTEGER g_lastPresentQpc = {};

static void DumpProfilerHookCounts(const char* reason) {
    Log("ProfilerHookCounts: " + std::string(reason) +
        " loadgame=" + std::to_string(InterlockedCompareExchange64(&g_hookCalls_LoadGame, 0, 0)) +
        " save_load_request=" + std::to_string(InterlockedCompareExchange64(&g_hookCalls_SaveLoadRequest, 0, 0)) +
        " swapbuffers=" + std::to_string(InterlockedCompareExchange64(&g_hookCalls_SwapBuffers, 0, 0)) +
        " loadingscreen=" + std::to_string(InterlockedCompareExchange64(&g_hookCalls_Loadingscreen, 0, 0)) +
        " engine=" + std::to_string(InterlockedCompareExchange64(&g_hookCalls_Engine, 0, 0)) +
        " populate_save=" + std::to_string(InterlockedCompareExchange64(&g_hookCalls_PopulateSave, 0, 0)) +
        " gamesaveload_core=" + std::to_string(InterlockedCompareExchange64(&g_hookCalls_GameSaveLoadCore, 0, 0)));
}

// ---------------------------------------------------------------------------
// Visual load timeline.
//
// Every engine-state anchor tried so far (load-state clear, final drain,
// first post-drain present, load-context substate, client busy flag) closes
// the window at the FIRST BLACK FRAME: a 5 fps video of a reload showed the
// loading screen for ~2.4 s and then a byte-identical black screen for ~2.6 s
// before control returned, while the engine-anchored logs reported ~0.3-0.5 s.
// This tracker measures what the video measures.  At every present it samples
// four thin rows of the back buffer, classifies the frame (black / loading
// screen / other), polls the physical mouse button for the click, and once a
// load has ended AND several consecutive bright non-loading frames have been
// presented it emits one VisualLoad line with the frame-derived anatomy
// (click -> loading screen -> black -> first gameplay frame) plus compact
// FrameRun lines so the classification itself can be checked against video.
#define ENABLE_VISUAL_LOAD_TIMELINE 1

typedef void (APIENTRY* VlGlReadPixels_t)(int, int, int, int, unsigned int, unsigned int, void*);
typedef void (APIENTRY* VlGlGetIntegerv_t)(unsigned int, int*);
typedef void (APIENTRY* VlGlPixelStorei_t)(unsigned int, int);
typedef void (APIENTRY* VlGlReadBuffer_t)(unsigned int);
typedef unsigned int (APIENTRY* VlGlGetError_t)();
typedef HGLRC (WINAPI* VlWglGetCurrentContext_t)();

#define VL_GL_RGB 0x1907
#define VL_GL_UNSIGNED_BYTE 0x1401
#define VL_GL_VIEWPORT 0x0BA2
#define VL_GL_PACK_ALIGNMENT 0x0D05
#define VL_GL_READ_BUFFER 0x0C02
#define VL_GL_BACK 0x0405

enum VisualFrameClass : unsigned char {
    VL_FRAME_OTHER = 0,
    VL_FRAME_LOADING = 1,        // presented from inside LoadingScreenUpdateFrame
    VL_FRAME_LOADING_STALE = 2,  // pixel-identical to the last such frame
    VL_FRAME_BLACK = 3,
    VL_FRAME_UNSAMPLED = 4,
};

static const char* VisualFrameClassName(unsigned char cls) {
    switch (cls) {
        case VL_FRAME_OTHER: return "other";
        case VL_FRAME_LOADING: return "loading";
        case VL_FRAME_LOADING_STALE: return "loading_stale";
        case VL_FRAME_BLACK: return "black";
        default: return "unsampled";
    }
}

struct VisualFrameSample {
    LARGE_INTEGER qpc;
    unsigned int hash;
    unsigned char cls;
    unsigned char maxLum;
    unsigned char meanLum;
    unsigned char loadState;
};

struct VisualClickSample {
    LARGE_INTEGER qpc;
    char source;  // 'm' mouse left button, 'k' keyboard Return/Space
};

static const unsigned int VL_RING_SIZE = 1024;
static const unsigned int VL_CLICK_RING_SIZE = 32;
static VisualFrameSample g_vlRing[VL_RING_SIZE] = {};
static unsigned int g_vlRingWrite = 0;  // total presents recorded (index = n % size)
static VisualClickSample g_vlClicks[VL_CLICK_RING_SIZE] = {};
static unsigned int g_vlClickWrite = 0;
static bool g_vlPrevLButtonDown = false;
static bool g_vlPrevKeyDown = false;
static unsigned int g_vlLastLoadingHash = 0;
static bool g_vlLastLoadingHashValid = false;
static thread_local int g_vlLoadingScreenFrameDepth = 0;
static std::vector<unsigned char> g_vlRowBuffer;

static VlGlReadPixels_t g_vlGlReadPixels = nullptr;
static VlGlGetIntegerv_t g_vlGlGetIntegerv = nullptr;
static VlGlPixelStorei_t g_vlGlPixelStorei = nullptr;
static VlGlReadBuffer_t g_vlGlReadBuffer = nullptr;
static VlGlGetError_t g_vlGlGetError = nullptr;
static VlWglGetCurrentContext_t g_vlWglGetCurrentContext = nullptr;
static bool g_vlGlResolved = false;
static bool g_vlGlUnavailableLogged = false;

struct VisualLoadTracker {
    bool active;
    unsigned int id;
    LARGE_INTEGER armed;
    unsigned int armRingIndex;
    bool perceivedEndSeen;
    LARGE_INTEGER perceivedEnd;
    bool fadeSeen;
    LARGE_INTEGER fade;
    int fadeMode;
    float fadeSeconds;
    unsigned int consecutiveBrightOther;
    unsigned int emitted;
};
static VisualLoadTracker g_visualLoad = {};

static void ResolveVisualGl() {
    if (g_vlGlResolved) {
        return;
    }
    g_vlGlResolved = true;
    HMODULE gl = GetModuleHandleA("opengl32.dll");
    if (gl == nullptr) {
        return;
    }
    g_vlGlReadPixels = (VlGlReadPixels_t)GetProcAddress(gl, "glReadPixels");
    g_vlGlGetIntegerv = (VlGlGetIntegerv_t)GetProcAddress(gl, "glGetIntegerv");
    g_vlGlPixelStorei = (VlGlPixelStorei_t)GetProcAddress(gl, "glPixelStorei");
    g_vlGlReadBuffer = (VlGlReadBuffer_t)GetProcAddress(gl, "glReadBuffer");
    g_vlGlGetError = (VlGlGetError_t)GetProcAddress(gl, "glGetError");
    g_vlWglGetCurrentContext =
        (VlWglGetCurrentContext_t)GetProcAddress(gl, "wglGetCurrentContext");
}

// Samples four full-width rows (1/8, 3/8, 5/8, 7/8 of the height) of the
// buffer about to be presented.  Returns false when no GL context is current
// on this thread or the read fails; the frame is then recorded as unsampled.
static bool SampleBackBuffer(HDC hdc, unsigned char& maxLum, unsigned char& meanLum,
                             unsigned int& hash) {
    ResolveVisualGl();
    if (g_vlGlReadPixels == nullptr || g_vlGlGetIntegerv == nullptr ||
        g_vlWglGetCurrentContext == nullptr) {
        return false;
    }
    if (g_vlWglGetCurrentContext() == nullptr) {
        return false;
    }
    int width = 0;
    int height = 0;
    HWND window = WindowFromDC(hdc);
    RECT client = {};
    if (window != nullptr && GetClientRect(window, &client)) {
        width = client.right - client.left;
        height = client.bottom - client.top;
    }
    if (width <= 0 || height <= 0) {
        int viewport[4] = {};
        g_vlGlGetIntegerv(VL_GL_VIEWPORT, viewport);
        width = viewport[2];
        height = viewport[3];
    }
    if (width <= 0 || height < 8 || width > 8192) {
        return false;
    }
    const size_t rowBytes = (size_t)width * 3;
    if (g_vlRowBuffer.size() < rowBytes * 4) {
        g_vlRowBuffer.resize(rowBytes * 4);
    }
    int savedAlignment = 4;
    int savedReadBuffer = VL_GL_BACK;
    g_vlGlGetIntegerv(VL_GL_PACK_ALIGNMENT, &savedAlignment);
    g_vlGlGetIntegerv(VL_GL_READ_BUFFER, &savedReadBuffer);
    if (g_vlGlPixelStorei != nullptr) {
        g_vlGlPixelStorei(VL_GL_PACK_ALIGNMENT, 1);
    }
    if (g_vlGlReadBuffer != nullptr && savedReadBuffer != VL_GL_BACK) {
        g_vlGlReadBuffer(VL_GL_BACK);
    }
    if (g_vlGlGetError != nullptr) {
        g_vlGlGetError();  // clear any stale error so we see our own
    }
    for (int r = 0; r < 4; ++r) {
        int y = (height * (2 * r + 1)) / 8;
        g_vlGlReadPixels(0, y, width, 1, VL_GL_RGB, VL_GL_UNSIGNED_BYTE,
                         g_vlRowBuffer.data() + rowBytes * r);
    }
    bool ok = g_vlGlGetError == nullptr || g_vlGlGetError() == 0;
    if (g_vlGlReadBuffer != nullptr && savedReadBuffer != VL_GL_BACK) {
        g_vlGlReadBuffer((unsigned int)savedReadBuffer);
    }
    if (g_vlGlPixelStorei != nullptr) {
        g_vlGlPixelStorei(VL_GL_PACK_ALIGNMENT, savedAlignment);
    }
    if (!ok) {
        return false;
    }
    unsigned int h = 2166136261u;
    unsigned int maxV = 0;
    unsigned long long sum = 0;
    const size_t total = rowBytes * 4;
    const unsigned char* p = g_vlRowBuffer.data();
    for (size_t i = 0; i < total; ++i) {
        unsigned int v = p[i];
        sum += v;
        if (v > maxV) {
            maxV = v;
        }
        // Quantize so a one-LSB difference does not break stale-frame matching.
        h ^= (v >> 3);
        h *= 16777619u;
    }
    maxLum = (unsigned char)maxV;
    meanLum = (unsigned char)(sum / total);
    hash = h;
    return true;
}

static void PollInputForClicks(LARGE_INTEGER now) {
    SHORT lb = GetAsyncKeyState(VK_LBUTTON);
    bool down = (lb & 0x8000) != 0;
    // Bit 0: pressed since the previous poll — catches a press+release that
    // fit entirely inside one frame (e.g. during a stalled frame).
    bool edge = (down && !g_vlPrevLButtonDown) ||
                (!down && !g_vlPrevLButtonDown && (lb & 1) != 0);
    g_vlPrevLButtonDown = down;
    if (edge) {
        VisualClickSample& c = g_vlClicks[g_vlClickWrite % VL_CLICK_RING_SIZE];
        c.qpc = now;
        c.source = 'm';
        ++g_vlClickWrite;
    }
    bool keyDown = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0 ||
                   (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    if (keyDown && !g_vlPrevKeyDown) {
        VisualClickSample& c = g_vlClicks[g_vlClickWrite % VL_CLICK_RING_SIZE];
        c.qpc = now;
        c.source = 'k';
        ++g_vlClickWrite;
    }
    g_vlPrevKeyDown = keyDown;
}

static bool IsLoadingClass(unsigned char cls) {
    return cls == VL_FRAME_LOADING || cls == VL_FRAME_LOADING_STALE;
}

static void ArmVisualLoad(unsigned int id, LARGE_INTEGER now);

// ---------------------------------------------------------------------------
// Load-phase attribution (log only, no behaviour change).  See load_phases.md.
//
// The pixel timeline says HOW LONG each visible stretch lasted; these engine
// event hooks say WHY.  Every event is stamped on the same QPC clock as the
// visual timeline and the summary is emitted from EmitVisualLoad, so the two
// can be cross-checked line for line.  The window is the visual-load window
// (armed at the click / state activation, closed at first gameplay frames).
//
// Hot-path rule: the per-tick hook (0x00537590) runs for every server tick of
// the whole session.  Outside a window it is a flag test and a forward; inside
// one it only bumps counters and fills fixed arrays -- no per-call Log().
#define ENABLE_LOAD_PHASES_LOG 1
#if FORCE_AREA_STREAM_DURING_LOAD && !ENABLE_LOAD_PHASES_LOG
#error FORCE_AREA_STREAM_DURING_LOAD is applied inside the LoadPhases tick hook (0x00537590)
#endif
#define LP_EVENT_MAX 128
#define LP_TICK_MAX 64
#define LP_MSG_MAX 64
#define LP_GUI_FADE_MAX 16

enum LpKind : unsigned char {
    LP_FINALIZE_ENTER = 0,  // ModuleLoad_FinalizeAndQueueReady 0x0055a650
    LP_FINALIZE_EXIT,
    LP_ANNOUNCE,            // Server_SendStateToClient 0x00647af0; a = arg, b = inside State1to2
    LP_NET_EVENT,           // HandleNetEvents 0x00812350; a = minor (first per minor only)
    LP_STATE12_ENTER,       // Server_StartGame_State1to2 0x005361d0
    LP_STATE12_EXIT,        // a = return value
    LP_AREA_LOADED,         // Send_P_AreaLoaded_4_3 0x008798a0 (client says streaming finished)
    LP_PLACE_ENTER,         // Server_PlacePlayerInArea 0x00535a10; b = inside State1to2
    LP_PLACE_EXIT,
    LP_SCRIPT_FADE_OUT,     // SetGlobalFadeOut 0x00696b00 (stamped on entry)
    LP_SCRIPT_FADE_IN,      // SetGlobalFadeIn 0x006969e0 (stamped on entry)
    LP_SCRIPT_FADE_UNTIL,   // SetFadeUntilScript 0x00699db0
    LP_GUI_FADE,            // CSWGuiFade_SetTransitionState; a = mode, c = raw progress, f1 = progress s, f2 = duration s
    LP_PROGRESS,            // Server_WriteAreaLoadProgressW 0x0063cee0; a = stage, b = total, c = flag
};

static const char* LpKindName(unsigned char kind) {
    switch (kind) {
        case LP_FINALIZE_ENTER: return "finalize_enter";
        case LP_FINALIZE_EXIT: return "finalize_exit";
        case LP_ANNOUNCE: return "announce";
        case LP_NET_EVENT: return "net_event";
        case LP_STATE12_ENTER: return "state12_enter";
        case LP_STATE12_EXIT: return "state12_exit";
        case LP_AREA_LOADED: return "area_loaded";
        case LP_PLACE_ENTER: return "place_enter";
        case LP_PLACE_EXIT: return "place_exit";
        case LP_SCRIPT_FADE_OUT: return "script_fade_out";
        case LP_SCRIPT_FADE_IN: return "script_fade_in";
        case LP_SCRIPT_FADE_UNTIL: return "script_fade_until";
        case LP_GUI_FADE: return "gui_fade";
        case LP_PROGRESS: return "progress";
        default: return "?";
    }
}

// Tick/message accounting is bucketed by where the load is in the handshake:
//   0 before finalize (ignored)   1 finalize..server-running (handshake)
//   2 server-running..area-loaded (streaming; the throttle-paced part)
//   3 after the client's area-loaded ack (post; counted, not summarized)
#define LP_PHASES 4

struct LpEvent {
    LARGE_INTEGER qpc;
    unsigned char kind;
    unsigned char phase;
    int a, b, c;
    float f1, f2;
};
struct LpTick {  // one SENT tick (a tick the throttle let through)
    LARGE_INTEGER qpc;
    unsigned int durUs;
    unsigned int heldBefore;  // held ticks since the previous sent tick
    unsigned char phase;
};
struct LpMsg {  // one 2000-byte-capped object-update message build
    LARGE_INTEGER qpc;
    unsigned int durUs;
    unsigned int bytes;
    unsigned int getCalls;
    unsigned char phase;
};
struct LpTickCounts {
    unsigned int calls, sent, held, forced;
    long long sentUs;
};
struct LoadPhaseTracker {
    bool active;
    DWORD ownerThreadId;
    unsigned int id;
    int phase;
    unsigned int netSeen;  // bitmask of HandleNetEvents minors already logged
    unsigned int heldSincePrevSent;
    unsigned int guiFadeEvents;
    unsigned int eventCount;  // total, including drops past LP_EVENT_MAX
    unsigned int tickListCount;
    unsigned int msgListCount;
    LpTickCounts ticks[LP_PHASES];
    unsigned int msgs[LP_PHASES];
    long long msgBytes[LP_PHASES];
    long long msgUs[LP_PHASES];
    LpEvent events[LP_EVENT_MAX];
    LpTick tickList[LP_TICK_MAX];
    LpMsg msgList[LP_MSG_MAX];
};
static LoadPhaseTracker g_lp = {};
static thread_local int g_lpBuildDepth = 0;    // inside Server_BuildClientObjectUpdate
static thread_local int g_lpState12Depth = 0;  // inside Server_StartGame_State1to2
static thread_local unsigned int g_lpCurMsgBytes = 0;
static thread_local unsigned int g_lpCurMsgGets = 0;

static bool LpActive() {
    return g_lp.active && g_lp.ownerThreadId == GetCurrentThreadId();
}

static void LpArm(unsigned int id) {
    // A re-arm while a window is still open is the same load: the perceived-load
    // tracker ends and restarts on short state-activation transitions inside one
    // load (seen on the main-menu load: 10 windows), and resetting here wiped the
    // finalize/handshake/stream events.  EmitVisualLoad("rearmed") does not
    // emit or close the phase window; only the final emit does.
    if (g_lp.active) {
        g_lp.id = id;
        return;
    }
    g_lp = {};
    g_lp.active = true;
    g_lp.ownerThreadId = GetCurrentThreadId();
    g_lp.id = id;
}

static void LpPushAt(LARGE_INTEGER qpc, unsigned char kind, int a = 0, int b = 0,
                     int c = 0, float f1 = 0.0f, float f2 = 0.0f) {
    if (kind == LP_FINALIZE_ENTER && g_lp.phase < 1) {
        g_lp.phase = 1;
    } else if (kind == LP_STATE12_EXIT && g_lp.phase < 2) {
        g_lp.phase = 2;
    } else if (kind == LP_AREA_LOADED) {
        g_lp.phase = 3;
    }
    if (g_lp.eventCount < LP_EVENT_MAX) {
        LpEvent& e = g_lp.events[g_lp.eventCount];
        e.qpc = qpc;
        e.kind = kind;
        e.phase = (unsigned char)g_lp.phase;
        e.a = a;
        e.b = b;
        e.c = c;
        e.f1 = f1;
        e.f2 = f2;
    }
    ++g_lp.eventCount;
}

static void LpPush(unsigned char kind, int a = 0, int b = 0, int c = 0,
                   float f1 = 0.0f, float f2 = 0.0f) {
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    LpPushAt(now, kind, a, b, c, f1, f2);
}

static void EmitLoadPhases(unsigned int id, LARGE_INTEGER t0, long long vlFirstLoadingUs,
                           long long vlFirstBlackUs, long long vlFirstGameplayUs,
                           long long vlTotalUs) {
    if (!g_lp.active) {
        return;
    }
    const unsigned int n = g_lp.eventCount < LP_EVENT_MAX ? g_lp.eventCount : LP_EVENT_MAX;
    auto find = [&](unsigned char kind, long long minQpc) -> const LpEvent* {
        for (unsigned int i = 0; i < n; ++i) {
            if (g_lp.events[i].kind == kind && g_lp.events[i].qpc.QuadPart >= minQpc) {
                return &g_lp.events[i];
            }
        }
        return nullptr;
    };
    auto off = [&](const LpEvent* e) -> long long {
        return e ? QpcRelUs(t0, e->qpc) : -1;
    };
    auto span = [&](const LpEvent* from, const LpEvent* to) -> long long {
        return (from && to) ? QpcElapsedUs(from->qpc, to->qpc) : -1;
    };
    const LpEvent* finEnter = find(LP_FINALIZE_ENTER, 0);
    const LpEvent* finExit = find(LP_FINALIZE_EXIT, 0);
    const LpEvent* announce = find(LP_ANNOUNCE, 0);
    const LpEvent* st12Enter = find(LP_STATE12_ENTER, 0);
    const LpEvent* st12Exit = find(LP_STATE12_EXIT, 0);
    const LpEvent* areaLoaded = find(LP_AREA_LOADED, 0);
    // The OnEnter-queueing placement is the one after the client's ack; the
    // one inside State1to2 (if any) is the pre-stream placement.
    const LpEvent* place = find(LP_PLACE_ENTER, areaLoaded ? areaLoaded->qpc.QuadPart : 0);
    const long long placeMin = place ? place->qpc.QuadPart : 0;
    const LpEvent* fadeOut = find(LP_SCRIPT_FADE_OUT, placeMin);
    const LpEvent* fadeIn = find(LP_SCRIPT_FADE_IN, placeMin);
    const LpEvent* fadeUntil = find(LP_SCRIPT_FADE_UNTIL, placeMin);
    const LpEvent* guiFade = fadeIn ? find(LP_GUI_FADE, fadeIn->qpc.QuadPart) : nullptr;
    const LpEvent* net[4] = {};
    for (unsigned int i = 0; i < n; ++i) {
        if (g_lp.events[i].kind == LP_NET_EVENT && g_lp.events[i].a >= 1 &&
            g_lp.events[i].a <= 3 && net[g_lp.events[i].a] == nullptr) {
            net[g_lp.events[i].a] = &g_lp.events[i];
        }
    }
    const LpEvent* lastProgress = nullptr;
    for (unsigned int i = 0; i < n; ++i) {
        if (g_lp.events[i].kind == LP_PROGRESS) {
            lastProgress = &g_lp.events[i];
        }
    }

    // Phase durations.  Streaming starts when the server is running; if the
    // State1to2 exit was not seen, fall back to the finalize exit.
    const LpEvent* streamFrom = st12Exit ? st12Exit : finExit;
    const long long workUs = finExit ? QpcRelUs(t0, finExit->qpc) : -1;
    const long long finalizeUs = span(finEnter, finExit);
    const long long handshakeUs = span(finExit, st12Exit);
    const long long streamUs = span(streamFrom, areaLoaded);
    const LpTickCounts& hs = g_lp.ticks[1];
    const LpTickCounts& st = g_lp.ticks[2];
    // Time inside the stream window NOT spent in a send tick: the throttle's
    // dead time (approximate -- main-thread rendering also lives here).
    const long long throttleWaitUs = streamUs >= 0
        ? (streamUs > st.sentUs ? streamUs - st.sentUs : 0) : -1;
    long long gapSum = 0;
    unsigned int gapN = 0;
    const unsigned int tickN = g_lp.tickListCount < LP_TICK_MAX ? g_lp.tickListCount : LP_TICK_MAX;
    const LpTick* prevSent = nullptr;
    for (unsigned int i = 0; i < tickN; ++i) {
        const LpTick& tk = g_lp.tickList[i];
        if (tk.phase != 2) {
            continue;
        }
        if (prevSent) {
            gapSum += QpcElapsedUs(prevSent->qpc, tk.qpc);
            ++gapN;
        }
        prevSent = &tk;
    }
    const long long areaToOnEnterUs = span(areaLoaded, place);
    // Script hold: OnEnter queue -> SetGlobalFadeIn call (the script's own
    // DelayCommand) plus the fade's wait (progress) once the fade starts.
    const long long scriptDelayUs = span(fadeOut ? fadeOut : place, fadeIn);
    const float fadeHoldS = guiFade ? guiFade->f1 : 0.0f;
    const float fadeS = guiFade ? guiFade->f2 : 0.0f;
    long long scriptHoldUs = -1;
    if (place && fadeIn) {
        scriptHoldUs = QpcElapsedUs(place->qpc, fadeIn->qpc) +
                       (long long)(fadeHoldS * 1000000.0f);
    }
    const long long placeUs = off(place);
    const long long placeToGameplayUs =
        (place && vlFirstGameplayUs >= 0) ? vlFirstGameplayUs - placeUs : -1;
    // The visual timeline's "first gameplay frame" is the first frame bright
    // enough to leave the black class, i.e. the START of the fade-in, so the
    // fade's own duration is NOT part of place-to-gameplay.
    long long residualUs = -1;
    if (placeToGameplayUs >= 0 && scriptHoldUs >= 0) {
        residualUs = placeToGameplayUs - scriptHoldUs;
    }

    std::string line = "LoadPhases: run_id=" + std::to_string(g_profilerRunId) +
        " id=" + std::to_string(id);
    auto kv = [&](const char* k, long long v) {
        line += std::string(" ") + k + "=" + std::to_string(v);
    };
    char fbuf[32];
    auto kvf = [&](const char* k, float v) {
        sprintf_s(fbuf, sizeof(fbuf), "%.3f", v);
        line += std::string(" ") + k + "=" + fbuf;
    };
    kv("work_us", workUs);
    kv("finalize_us", finalizeUs);
    kv("handshake_us", handshakeUs);
    kv("hs_ticks", hs.calls);
    kv("hs_sent", hs.sent);
    kv("hs_held", hs.held);
    kv("stream_us", streamUs);
    kv("throttle_wait_us", throttleWaitUs);
    kv("stream_ticks", st.calls);
    kv("stream_sent", st.sent);
    kv("stream_held", st.held);
    kv("stream_forced", st.forced);
    kv("stream_send_work_us", st.sentUs);
    kv("stream_msgs", g_lp.msgs[2]);
    kv("stream_bytes", g_lp.msgBytes[2]);
    kv("stream_build_us", g_lp.msgUs[2]);
    kv("msg_gap_mean_us", gapN ? gapSum / gapN : -1);
    kv("hs_msgs", g_lp.msgs[1]);
    kv("post_ticks", g_lp.ticks[3].calls);
    kv("post_msgs", g_lp.msgs[3]);
    kv("area_to_onenter_us", areaToOnEnterUs);
    kv("place_after_area", areaLoaded && place ? 1 : 0);
    kv("place_in_state12", place ? place->b : -1);
    kv("script_delay_us", scriptDelayUs);
    kvf("fade_hold_s", fadeHoldS);
    kvf("fade_s", fadeS);
    kv("script_hold_us", scriptHoldUs);
    kv("place_to_gameplay_us", placeToGameplayUs);
    kv("residual_us", residualUs);
    kv("vl_first_loading_us", vlFirstLoadingUs);
    kv("vl_first_black_us", vlFirstBlackUs);
    kv("vl_first_gameplay_us", vlFirstGameplayUs);
    kv("vl_total_us", vlTotalUs);
    kv("events", g_lp.eventCount);
    Log(line);

    line = "LoadPhaseMarks: id=" + std::to_string(id);
    kv("finalize_enter_us", off(finEnter));
    kv("finalize_exit_us", off(finExit));
    kv("announce_us", off(announce));
    kv("net1_us", off(net[1]));
    kv("net2_us", off(net[2]));
    kv("net3_us", off(net[3]));
    kv("state12_enter_us", off(st12Enter));
    kv("state12_exit_us", off(st12Exit));
    kv("area_loaded_us", off(areaLoaded));
    kv("place_us", placeUs);
    kv("fade_out_us", off(fadeOut));
    kv("fade_in_us", off(fadeIn));
    kv("fade_until_us", off(fadeUntil));
    kv("last_progress_stage", lastProgress ? lastProgress->a : -1);
    kv("last_progress_total", lastProgress ? lastProgress->b : -1);
    Log(line);

    // Chronological raw events (capped) so every derived number is checkable.
    for (unsigned int i = 0; i < n && i < 96; ++i) {
        const LpEvent& e = g_lp.events[i];
        char buf[224];
        sprintf_s(buf, sizeof(buf),
            "LoadPhaseEvent: id=%u n=%u kind=%s at_us=%lld phase=%u a=%d b=%d c=%d f1=%.3f f2=%.3f",
            id, i, LpKindName(e.kind), QpcRelUs(t0, e.qpc), (unsigned)e.phase,
            e.a, e.b, e.c, e.f1, e.f2);
        Log(buf);
    }
    // One line per message build and per SENT tick; held ticks appear only as
    // the count between sent ticks.
    const unsigned int msgN = g_lp.msgListCount < LP_MSG_MAX ? g_lp.msgListCount : LP_MSG_MAX;
    for (unsigned int i = 0; i < msgN; ++i) {
        const LpMsg& m = g_lp.msgList[i];
        char buf[192];
        sprintf_s(buf, sizeof(buf),
            "LoadPhaseMsg: id=%u n=%u at_us=%lld phase=%u dur_us=%u bytes=%u get_calls=%u",
            id, i, QpcRelUs(t0, m.qpc), (unsigned)m.phase, m.durUs, m.bytes, m.getCalls);
        Log(buf);
    }
    for (unsigned int i = 0; i < tickN; ++i) {
        const LpTick& tk = g_lp.tickList[i];
        char buf[192];
        sprintf_s(buf, sizeof(buf),
            "LoadPhaseTick: id=%u n=%u at_us=%lld phase=%u dur_us=%u held_before=%u",
            id, i, QpcRelUs(t0, tk.qpc), (unsigned)tk.phase, tk.durUs, tk.heldBefore);
        Log(buf);
    }
    g_lp.active = false;
}

static void EmitVisualLoad(LARGE_INTEGER now, const char* endReason) {
    VisualLoadTracker t = g_visualLoad;
    g_visualLoad = {};
    if (!t.active) {
        return;
    }
    const unsigned int available = g_vlRingWrite < VL_RING_SIZE ? g_vlRingWrite : VL_RING_SIZE;
    const unsigned int end = g_vlRingWrite;  // exclusive
    const unsigned int oldest = end - available;

    // Walk back from arming until a frame that is neither loading-like nor
    // black, then keep going while it is within 10 s of arming to include the
    // last menu frames before the click; the click search below is what
    // actually decides where the report starts.
    unsigned int start = t.armRingIndex < oldest ? oldest : t.armRingIndex;
    while (start > oldest) {
        const VisualFrameSample& f = g_vlRing[(start - 1) % VL_RING_SIZE];
        if (QpcElapsedUs(f.qpc, t.armed) > 10000000LL) {
            break;
        }
        --start;
    }

    // First loading-like frame at or before arming (searching forward from
    // start so the earliest of the contiguous pre-activation run wins), falling
    // back to the first loading-like frame after arming.
    long long firstLoadingIdx = -1;
    for (unsigned int i = start; i < end; ++i) {
        if (IsLoadingClass(g_vlRing[i % VL_RING_SIZE].cls)) {
            firstLoadingIdx = i;
            break;
        }
    }
    // Click: latest click strictly before the first loading frame (or before
    // arming when no loading frame was ever classified), within 15 s.
    LARGE_INTEGER clickBefore = firstLoadingIdx >= 0
        ? g_vlRing[(unsigned int)firstLoadingIdx % VL_RING_SIZE].qpc : t.armed;
    bool clickSeen = false;
    char clickSource = '-';
    LARGE_INTEGER click = {};
    const unsigned int clicksAvailable =
        g_vlClickWrite < VL_CLICK_RING_SIZE ? g_vlClickWrite : VL_CLICK_RING_SIZE;
    for (unsigned int k = 0; k < clicksAvailable; ++k) {
        const VisualClickSample& c = g_vlClicks[(g_vlClickWrite - 1 - k) % VL_CLICK_RING_SIZE];
        if (c.qpc.QuadPart < clickBefore.QuadPart) {
            if (QpcElapsedUs(c.qpc, clickBefore) <= 15000000LL) {
                clickSeen = true;
                click = c.qpc;
                clickSource = c.source;
            }
            break;
        }
    }
    LARGE_INTEGER t0 = clickSeen ? click
        : (firstLoadingIdx >= 0 ? g_vlRing[(unsigned int)firstLoadingIdx % VL_RING_SIZE].qpc
                                : t.armed);
    const char* ref = clickSeen ? "click" : (firstLoadingIdx >= 0 ? "first_loading" : "activation");
    // Report from the click (or 1 s before the first loading frame) onward.
    unsigned int reportStart = start;
    for (unsigned int i = start; i < end; ++i) {
        const VisualFrameSample& f = g_vlRing[i % VL_RING_SIZE];
        if (f.qpc.QuadPart >= t0.QuadPart - (clickSeen ? 0 : 0)) {
            reportStart = i > start ? i - 1 : i;  // include the last pre-click frame
            break;
        }
    }

    // Build class runs and derive the anatomy.
    long long lastLoadingIdx = -1;
    long long firstBlackAfterLoading = -1;
    long long lastBlackAfterLoading = -1;
    long long firstGameplayIdx = -1;
    unsigned int loadingFrames = 0;
    unsigned int blackFrames = 0;
    for (unsigned int i = (unsigned int)(firstLoadingIdx >= 0 ? firstLoadingIdx : reportStart);
         i < end; ++i) {
        const VisualFrameSample& f = g_vlRing[i % VL_RING_SIZE];
        if (IsLoadingClass(f.cls)) {
            if (firstGameplayIdx < 0) {
                lastLoadingIdx = i;
                ++loadingFrames;
                firstBlackAfterLoading = -1;
                lastBlackAfterLoading = -1;
                blackFrames = 0;
            }
        } else if (f.cls == VL_FRAME_BLACK) {
            if (firstGameplayIdx < 0 && (lastLoadingIdx >= 0 || firstLoadingIdx < 0)) {
                if (firstBlackAfterLoading < 0) {
                    firstBlackAfterLoading = i;
                }
                lastBlackAfterLoading = i;
                ++blackFrames;
            }
        } else if (f.cls == VL_FRAME_OTHER) {
            if (firstGameplayIdx < 0 && (lastLoadingIdx >= 0 || firstLoadingIdx < 0) &&
                (unsigned int)i > t.armRingIndex) {
                firstGameplayIdx = i;
            }
        }
    }

    auto rel = [&](long long idx) -> long long {
        return idx < 0 ? -1 : QpcElapsedUs(t0, g_vlRing[(unsigned int)idx % VL_RING_SIZE].qpc);
    };
    long long firstLoadingUs = rel(firstLoadingIdx);
    long long lastLoadingUs = rel(lastLoadingIdx);
    long long firstBlackUs = rel(firstBlackAfterLoading);
    long long lastBlackUs = rel(lastBlackAfterLoading);
    long long firstGameplayUs = rel(firstGameplayIdx);
    long long loadingUs = (firstLoadingIdx >= 0 && lastLoadingIdx >= 0)
        ? lastLoadingUs - firstLoadingUs : -1;
    long long blackUs = (firstBlackAfterLoading >= 0 && lastBlackAfterLoading >= 0)
        ? (firstGameplayIdx >= 0 ? firstGameplayUs - firstBlackUs : lastBlackUs - firstBlackUs)
        : -1;
    long long totalUs = firstGameplayIdx >= 0 ? firstGameplayUs : QpcElapsedUs(t0, now);

    Log("VisualLoad: " + std::to_string(totalUs) +
        " us run_id=" + std::to_string(g_profilerRunId) +
        " id=" + std::to_string(t.id) +
        " ref=" + ref +
        " click_seen=" + std::to_string(clickSeen ? 1 : 0) +
        " click_source=" + std::string(1, clickSource) +
        " end_reason=" + std::string(endReason) +
        " first_loading_us=" + std::to_string(firstLoadingUs) +
        " loading_us=" + std::to_string(loadingUs) +
        " loading_frames=" + std::to_string(loadingFrames) +
        " first_black_us=" + std::to_string(firstBlackUs) +
        " black_us=" + std::to_string(blackUs) +
        " black_frames=" + std::to_string(blackFrames) +
        " first_gameplay_us=" + std::to_string(firstGameplayUs) +
        " activation_us=" + std::to_string(QpcElapsedUs(t0, t.armed)) +
        " perceived_end_us=" + std::to_string(
            t.perceivedEndSeen ? QpcElapsedUs(t0, t.perceivedEnd) : -1) +
        " fade_us=" + std::to_string(t.fadeSeen ? QpcElapsedUs(t0, t.fade) : -1) +
        " fade_mode=" + std::to_string(t.fadeSeen ? t.fadeMode : -1) +
        " fade_seconds=" + std::to_string(t.fadeSeen ? t.fadeSeconds : 0.0f) +
        " frames=" + std::to_string(end - reportStart));

    // Run-length by class so the classification can be checked against video.
    unsigned int runNo = 0;
    unsigned int i = reportStart;
    while (i < end) {
        const VisualFrameSample& first = g_vlRing[i % VL_RING_SIZE];
        unsigned int j = i;
        unsigned int staticFrames = 0;
        unsigned long long lumSum = 0;
        unsigned int prevHash = 0;
        bool prevHashValid = false;
        while (j < end && g_vlRing[j % VL_RING_SIZE].cls == first.cls) {
            const VisualFrameSample& f = g_vlRing[j % VL_RING_SIZE];
            if (prevHashValid && f.hash == prevHash) {
                ++staticFrames;
            }
            prevHash = f.hash;
            prevHashValid = true;
            lumSum += f.meanLum;
            ++j;
        }
        const VisualFrameSample& last = g_vlRing[(j - 1) % VL_RING_SIZE];
        LARGE_INTEGER runEnd = j < end ? g_vlRing[j % VL_RING_SIZE].qpc : last.qpc;
        if (runNo < 64) {
            Log("FrameRun: id=" + std::to_string(t.id) +
                " n=" + std::to_string(runNo) +
                " class=" + VisualFrameClassName(first.cls) +
                " start_us=" + std::to_string(QpcElapsedUs(t0, first.qpc)) +
                " dur_us=" + std::to_string(QpcElapsedUs(first.qpc, runEnd)) +
                " frames=" + std::to_string(j - i) +
                " static=" + std::to_string(staticFrames) +
                " mean_lum=" + std::to_string(lumSum / (j - i)) +
                " load_state=" + std::to_string((int)first.loadState));
        }
        ++runNo;
        i = j;
    }
#if ENABLE_LOAD_PHASES_LOG
    if (std::strcmp(endReason, "rearmed") != 0) {
        EmitLoadPhases(t.id, t0, firstLoadingUs, firstBlackUs, firstGameplayUs, totalUs);
    }
#endif
}

static void ArmVisualLoad(unsigned int id, LARGE_INTEGER now) {
    if (g_visualLoad.active) {
        EmitVisualLoad(now, "rearmed");
    }
    g_visualLoad = {};
    g_visualLoad.active = true;
    g_visualLoad.id = id;
    g_visualLoad.armed = now;
    g_visualLoad.armRingIndex = g_vlRingWrite;
#if ENABLE_LOAD_PHASES_LOG
    LpArm(id);
#endif
}

// Called from Hook_SwapBuffers BEFORE the original SwapBuffers so the sampled
// pixels are the frame about to be shown.
static void RecordVisualFrame(HDC hdc, LARGE_INTEGER now) {
    PollInputForClicks(now);
    VisualFrameSample s = {};
    s.qpc = now;
    unsigned char maxLum = 0;
    unsigned char meanLum = 0;
    unsigned int hash = 0;
    bool sampled = SampleBackBuffer(hdc, maxLum, meanLum, hash);
    EngineLoadStateSnapshot state = {};
    if (TryReadEngineLoadState(state)) {
        s.loadState = (unsigned char)(state.state & 0xff);
    }
    if (!sampled) {
        s.cls = VL_FRAME_UNSAMPLED;
        if (!g_vlGlUnavailableLogged) {
            g_vlGlUnavailableLogged = true;
            Log("VisualLoad: back-buffer sampling unavailable on this present path");
        }
    } else {
        s.hash = hash;
        s.maxLum = maxLum;
        s.meanLum = meanLum;
        if (g_vlLoadingScreenFrameDepth > 0) {
            s.cls = VL_FRAME_LOADING;
            g_vlLastLoadingHash = hash;
            g_vlLastLoadingHashValid = true;
        } else if (maxLum < 12) {
            s.cls = VL_FRAME_BLACK;
        } else if (g_vlLastLoadingHashValid && hash == g_vlLastLoadingHash) {
            s.cls = VL_FRAME_LOADING_STALE;
        } else {
            s.cls = VL_FRAME_OTHER;
        }
    }
    g_vlRing[g_vlRingWrite % VL_RING_SIZE] = s;
    ++g_vlRingWrite;

    if (g_visualLoad.active) {
        if (s.cls == VL_FRAME_OTHER) {
            ++g_visualLoad.consecutiveBrightOther;
        } else if (s.cls != VL_FRAME_UNSAMPLED) {
            g_visualLoad.consecutiveBrightOther = 0;
        }
        bool loadOver = g_visualLoad.perceivedEndSeen && !g_perceivedLoad.active;
        if (loadOver && g_visualLoad.consecutiveBrightOther >= 5) {
            EmitVisualLoad(now, "gameplay_frames");
        } else if (QpcElapsedUs(g_visualLoad.armed, now) > 60000000LL) {
            EmitVisualLoad(now, "timeout");
        }
    }
}

static void StartLoadPerceived(const char* startSource, LARGE_INTEGER now) {
    if (g_perceivedLoad.active) {
        return;
    }
    g_perceivedLoad = {};
    g_perceivedLoad.active = true;
    g_perceivedLoad.ownerThreadId = GetCurrentThreadId();
    g_perceivedLoad.id = ++g_nextPerceivedLoadId;
    g_perceivedLoad.startSource = startSource;
    g_perceivedLoad.start = now;
    g_clickToControl = {};
    g_clickToControl.armed = true;
    g_clickToControl.id = g_perceivedLoad.id;
    g_clickToControl.clickSource = startSource;
    g_clickToControl.click = now;
#if ENABLE_VISUAL_LOAD_TIMELINE
    ArmVisualLoad(g_perceivedLoad.id, now);
#endif
}

static std::string ToHexByte(int value) {
    if (value < 0) {
        return std::string("--");
    }
    char buffer[8];
    sprintf_s(buffer, sizeof(buffer), "%02X", value);
    return std::string(buffer);
}

static void FinishPerceivedLoad(LARGE_INTEGER end, const char* endReason) {
    PerceivedLoadProfile completed = g_perceivedLoad;
    g_perceivedLoad.active = false;
#if ENABLE_VISUAL_LOAD_TIMELINE
    // The engine-anchored window is over, but the visible load may not be:
    // the visual tracker stays armed until bright gameplay frames actually
    // appear on screen.
    if (g_visualLoad.active) {
        g_visualLoad.perceivedEndSeen = true;
        g_visualLoad.perceivedEnd = end;
    }
#endif
    if (!completed.transitionEnded && g_clickToControl.armed) {
        // Window expired before any load transition (cancelled load,
        // save-only action): drop the click-to-control tracker with it so no
        // stale window emits later.
        g_clickToControl = {};
    }
    long long wallUs = QpcElapsedUs(completed.start, end);
    long long preActivationUs = completed.transitionStarted
        ? QpcElapsedUs(completed.start, completed.transitionStart) : -1;
    long long transitionWallUs = completed.transitionStarted &&
            completed.transitionEnded
        ? QpcElapsedUs(completed.transitionStart, completed.transitionEnd) : -1;
    long long postDrainUs = completed.transitionEnded
        ? QpcElapsedUs(completed.transitionEnd, end) : -1;
    long long frozenPreUs = completed.transitionStarted &&
            completed.lastPresentBeforeTransitionSeen
        ? QpcElapsedUs(completed.lastPresentBeforeTransition, completed.transitionStart)
        : -1;
    long long lsSpanUs = g_lsFirstCallSinceLoad.QuadPart != 0
        ? QpcElapsedUs(g_lsFirstCallSinceLoad, end) : -1;
    long long gslcSpanUs = g_gslc.firstCall.QuadPart != 0
        ? QpcElapsedUs(g_gslc.firstCall, g_gslc.lastCall) : -1;
    Log("PerceivedLoadWallTime: " + std::to_string(wallUs) +
        " us run_id=" + std::to_string(g_profilerRunId) +
        " id=" + std::to_string(completed.id) +
        " start_source=" + completed.startSource +
        " end_reason=" + endReason +
        " pre_activation_us=" + std::to_string(preActivationUs) +
        " transition_wall_us=" + std::to_string(transitionWallUs) +
        " post_drain_us=" + std::to_string(postDrainUs) +
        " frozen_pre_us=" + std::to_string(frozenPreUs) +
        " post_gap1_us=" + std::to_string(completed.postGap1Us) +
        " post_gap2_us=" + std::to_string(completed.postGap2Us) +
        " post_gap3_us=" + std::to_string(completed.postGap3Us) +
        " loading_presents=" + std::to_string(completed.loadingPresents) +
        " post_drain_presents=" + std::to_string(completed.postDrainPresents) +
        " ls_calls=" + std::to_string(g_lsCallsSinceLoad) +
        " ls_busy_us=" + std::to_string(g_lsBusyUsSinceLoad) +
        " ls_span_us=" + std::to_string(lsSpanUs) +
        " gslc_calls=" + std::to_string(g_gslc.calls) +
        " gslc_busy_us=" + std::to_string(g_gslc.totalUs) +
        " gslc_span_us=" + std::to_string(gslcSpanUs) +
        " gslc_max_us=" + std::to_string(g_gslc.maxUs) +
        " gslc_s1_us=" + std::to_string(g_gslc.stageUs[1]) +
        " gslc_s4_us=" + std::to_string(g_gslc.stageUs[4]) +
        " gslc_s5_us=" + std::to_string(g_gslc.stageUs[5]) +
        " gslc_s1_n=" + std::to_string(g_gslc.stageCalls[1]) +
        " gslc_s4_n=" + std::to_string(g_gslc.stageCalls[4]) +
        " gslc_s5_n=" + std::to_string(g_gslc.stageCalls[5]) +
        " loadgame_calls=" + std::to_string(
            InterlockedCompareExchange64(&g_hookCalls_LoadGame, 0, 0)) +
        " save_request_calls=" + std::to_string(
            InterlockedCompareExchange64(&g_hookCalls_SaveLoadRequest, 0, 0)));
    g_lsCallsSinceLoad = 0;
    g_lsBusyUsSinceLoad = 0;
    g_lsFirstCallSinceLoad = {};
    g_gslc = {};

    // Top P-packet majors of this load window by busy time: names come from
    // the PPacketHandler switch (0x03 Module, 0x05 GameObjUpdate, 0x2D SaveLoad, ...).
    int top[3] = {-1, -1, -1};
    for (int pass = 0; pass < 3; ++pass) {
        long long best = -1;
        int bestIdx = -1;
        for (int i = 0; i < 256; ++i) {
            if (g_ppMajorUs[i] > best) {
                bool already = false;
                for (int j = 0; j < pass; ++j) {
                    already = already || top[j] == i;
                }
                if (!already) {
                    best = g_ppMajorUs[i];
                    bestIdx = i;
                }
            }
        }
        top[pass] = bestIdx;
    }
    Log("PacketProfile: run_id=" + std::to_string(g_profilerRunId) +
        " id=" + std::to_string(completed.id) +
        " prq_busy_us=" + std::to_string(g_prqBusyUsSinceLoad) +
        " prq_calls=" + std::to_string(g_prqCallsSinceLoad) +
        " pp_busy_us=" + std::to_string(g_ppBusyUsSinceLoad) +
        " pp_calls=" + std::to_string(g_ppCallsSinceLoad) +
        " sp_busy_us=" + std::to_string(g_spBusyUsSinceLoad) +
        " sp_calls=" + std::to_string(g_spCallsSinceLoad) +
        " top1=0x" + ToHexByte(top[0]) + "/" + std::to_string(top[0] >= 0 ? g_ppMajorUs[top[0]] : 0) +
        "us/" + std::to_string(top[0] >= 0 ? g_ppMajorCalls[top[0]] : 0) + "n" +
        " top2=0x" + ToHexByte(top[1]) + "/" + std::to_string(top[1] >= 0 ? g_ppMajorUs[top[1]] : 0) +
        "us/" + std::to_string(top[1] >= 0 ? g_ppMajorCalls[top[1]] : 0) + "n" +
        " top3=0x" + ToHexByte(top[2]) + "/" + std::to_string(top[2] >= 0 ? g_ppMajorUs[top[2]] : 0) +
        "us/" + std::to_string(top[2] >= 0 ? g_ppMajorCalls[top[2]] : 0) + "n" +
        " fade_busy_us=" + std::to_string(g_fadeBusyUsSinceLoad) +
        " fade_calls=" + std::to_string(g_fadeCallsSinceLoad) +
        " fade_clamps=" + std::to_string(g_fadeClampsSinceLoad) +
        " igc_busy_us=" + std::to_string(g_igcBusyUsSinceLoad) +
        " igc_calls=" + std::to_string(g_igcCallsSinceLoad) +
        " isc_busy_us=" + std::to_string(g_iscBusyUsSinceLoad) +
        " isc_calls=" + std::to_string(g_iscCallsSinceLoad) +
        " lfopen_busy_us=" + std::to_string(g_lfOpenBusyUsSinceLoad) +
        " lfread_busy_us=" + std::to_string(g_lfReadBusyUsSinceLoad) +
        " lfread_calls=" + std::to_string(g_lfReadCallsSinceLoad) +
        " lfread_bytes=" + std::to_string(g_lfReadBytesSinceLoad));
    g_prqCallsSinceLoad = 0;
    g_prqBusyUsSinceLoad = 0;
    g_ppCallsSinceLoad = 0;
    g_ppBusyUsSinceLoad = 0;
    g_spCallsSinceLoad = 0;
    g_spBusyUsSinceLoad = 0;
    for (int i = 0; i < 256; ++i) {
        g_ppMajorCalls[i] = 0;
        g_ppMajorUs[i] = 0;
    }
    g_fadeCallsSinceLoad = 0;
    g_fadeBusyUsSinceLoad = 0;
    g_fadeClampsSinceLoad = 0;
    g_igcCallsSinceLoad = 0;
    g_igcBusyUsSinceLoad = 0;
    g_iscCallsSinceLoad = 0;
    g_iscBusyUsSinceLoad = 0;
    g_lfOpenCallsSinceLoad = 0;
    g_lfOpenBusyUsSinceLoad = 0;
    g_lfReadCallsSinceLoad = 0;
    g_lfReadBusyUsSinceLoad = 0;
    g_lfReadBytesSinceLoad = 0;
}

// Emit and disarm the click-to-control tracker.  Phase names match the
// frame-captured anatomy: click->transition_start is the loading-screen
// pre-activation drain, transition_start->first_present is the measured
// transition window, first_present->control is the black window.
static void FinishClickToControl(
    LARGE_INTEGER end, const char* controlReason, int substate) {
    ClickToControlProfile completed = g_clickToControl;
    g_clickToControl = {};
    if (!completed.armed) {
        return;
    }
    long long wallUs = QpcElapsedUs(completed.click, end);
    long long clickToTransitionUs = completed.transitionStartSeen
        ? QpcElapsedUs(completed.click, completed.transitionStart) : -1;
    long long transitionToFirstPresentUs =
        completed.transitionStartSeen && completed.firstPresentSeen
            ? QpcElapsedUs(completed.transitionStart, completed.firstPresent)
            : -1;
    long long firstPresentToControlUs = completed.firstPresentSeen
        ? QpcElapsedUs(completed.firstPresent, end) : -1;
    Log("ClickToControl: " + std::to_string(wallUs) +
        " us run_id=" + std::to_string(g_profilerRunId) +
        " id=" + std::to_string(completed.id) +
        " click_source=" + completed.clickSource +
        " control_reason=" + std::string(controlReason) +
        " control_substate=" + std::to_string(substate) +
        " click_to_transition_us=" + std::to_string(clickToTransitionUs) +
        " transition_to_first_present_us=" +
        std::to_string(transitionToFirstPresentUs) +
        " first_present_to_control_us=" +
        std::to_string(firstPresentToControlUs));
}

typedef BOOL (WINAPI* SwapBuffersPtr_t)(HDC);
SwapBuffersPtr_t g_originalSwapBuffers = nullptr;

BOOL WINAPI Hook_SwapBuffers(HDC hdc) {
    InterlockedIncrement64(&g_hookCalls_SwapBuffers);
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
#if ENABLE_VISUAL_LOAD_TIMELINE
    // Sample the back buffer BEFORE the flip: these pixels are the frame the
    // player is about to see, which is what the hand-timed video measures.
    RecordVisualFrame(hdc, now);
#endif
    BOOL result = g_originalSwapBuffers(hdc);
    g_lastPresentQpc = now;
    if (g_perceivedLoad.active &&
        g_perceivedLoad.ownerThreadId == GetCurrentThreadId()) {
        if (!g_perceivedLoad.transitionEnded) {
            ++g_perceivedLoad.loadingPresents;
            // A cancelled load (or a save-only LoadGame call) never activates a
            // transition; expire the window instead of waiting forever.
            if (!g_perceivedLoad.transitionStarted &&
                QpcElapsedUs(g_perceivedLoad.start, now) > 20000000LL) {
                FinishPerceivedLoad(now, "expired_no_transition");
            } else if (QpcElapsedUs(g_perceivedLoad.start, now) > 60000000LL) {
                FinishPerceivedLoad(now, "expired_overall");
            }
        } else {
            ++g_perceivedLoad.postDrainPresents;
            if (!g_clickToControl.firstPresentSeen) {
                g_clickToControl.firstPresentSeen = true;
                g_clickToControl.firstPresent = now;
            }
            // Capture the first post-drain inter-frame gaps: a long gap after
            // the first gameplay frame means the engine stalls (fade, texture
            // uploads) while the player is still looking at a static screen.
            if (g_perceivedLoad.lastPostDrainPresent.QuadPart != 0) {
                long long gapUs =
                    QpcElapsedUs(g_perceivedLoad.lastPostDrainPresent, now);
                if (g_perceivedLoad.postGap1Us < 0) {
                    g_perceivedLoad.postGap1Us = gapUs;
                } else if (g_perceivedLoad.postGap2Us < 0) {
                    g_perceivedLoad.postGap2Us = gapUs;
                } else if (g_perceivedLoad.postGap3Us < 0) {
                    g_perceivedLoad.postGap3Us = gapUs;
                }
            }
            g_perceivedLoad.lastPostDrainPresent = now;
            EngineLoadStateSnapshot state = {};
            // First presented frame after the drain with the load state cleared
            // is the first frame the player would call gameplay.
            if (!TryReadEngineLoadState(state) || state.state == 0) {
                FinishPerceivedLoad(now, "first_gameplay_present");
            }
        }
    }
    return result;
}

typedef int (__thiscall* LoadGamePtr_t)(
    void* thisPtr, uint32_t param1, uint32_t param2, uint32_t param3, uint32_t param4);
LoadGamePtr_t g_originalLoadGame = nullptr;

int __fastcall Hook_LoadGame(
    void* thisPtr, void* edxDummy,
    uint32_t param1, uint32_t param2, uint32_t param3, uint32_t param4) {
    InterlockedIncrement64(&g_hookCalls_LoadGame);
    LARGE_INTEGER start = {};
    QueryPerformanceCounter(&start);
    // Save-load entry behind the Load button; this is the moment the player
    // starts waiting.  Actual save read/deserialization happens inside.
    StartLoadPerceived("loadgame_entry", start);
    return g_originalLoadGame(thisPtr, param1, param2, param3, param4);
}

// Server-side save/load request dispatcher (reached from the save/load packet
// handler).  This is the earliest reliable "player asked for a load" marker:
// the click reaches the server half as a command packet and is dispatched
// here before the load context arms.  The original action-byte filter (2 /
// 0x11) never matched on the SP save-load path — runs logged
// start_source=state_activation despite save_request firing — so the window
// now opens on ANY request and the action byte is shadow-logged to learn the
// real load/save action values.  A newer request re-anchors an existing
// not-yet-transitioning window (save click followed by load click).
typedef void (__thiscall* SaveLoadRequestPtr_t)(void* thisPtr, int param1, char action);
SaveLoadRequestPtr_t g_originalSaveLoadRequest = nullptr;

void __fastcall Hook_SaveLoadRequest(
    void* thisPtr, void* edxDummy, int param1, char action) {
    InterlockedIncrement64(&g_hookCalls_SaveLoadRequest);
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    Log("SaveLoadRequestAction: action=0x" + ToHexByte((unsigned char)action) +
        " param1=" + std::to_string(param1));
    if (g_perceivedLoad.active && !g_perceivedLoad.transitionStarted &&
        g_perceivedLoad.ownerThreadId == GetCurrentThreadId()) {
        // A fresher click supersedes an un-started window (e.g. the save-list
        // click that preceded the Load click).
        g_perceivedLoad.start = now;
        g_clickToControl.click = now;
        g_clickToControl.clickSource = "save_request_reanchor";
    } else {
        StartLoadPerceived("save_request", now);
    }
    g_originalSaveLoadRequest(thisPtr, param1, action);
}

// Save/load state-machine core: driven stage-by-stage through the packet
// pipeline (NetPacketMajorDispatcher -> FUN_0065fef0 -> here), with every
// stage gated on the packet queue being empty.  Stage 4 opens and
// deserializes the save through the resource system; stage 5 finishes world
// restore and enqueues the module load that starts the measured load state.
// This is the engine work inside the pre-activation phase.
typedef void (__thiscall* GameSaveLoadCorePtr_t)(
    void* thisPtr, int param1, char stage, uint32_t p3, uint32_t p4, uint32_t p5);
GameSaveLoadCorePtr_t g_originalGameSaveLoadCore = nullptr;

void __fastcall Hook_GameSaveLoadCore(
    void* thisPtr, void* edxDummy,
    int param1, char stage, uint32_t p3, uint32_t p4, uint32_t p5) {
    InterlockedIncrement64(&g_hookCalls_GameSaveLoadCore);
    LARGE_INTEGER callStart = {};
    QueryPerformanceCounter(&callStart);
    if (g_gslc.firstCall.QuadPart == 0) {
        g_gslc.firstCall = callStart;
    }
    g_originalGameSaveLoadCore(thisPtr, param1, stage, p3, p4, p5);
    LARGE_INTEGER callEnd = {};
    QueryPerformanceCounter(&callEnd);
    int64_t durationUs = QpcElapsedUs(callStart, callEnd);
    g_gslc.lastCall = callEnd;
    ++g_gslc.calls;
    g_gslc.totalUs += durationUs;
    if (durationUs > g_gslc.maxUs) {
        g_gslc.maxUs = durationUs;
    }
    unsigned int bucket = (unsigned char)stage;
    if (bucket > 5) {
        bucket = 6;
    }
    ++g_gslc.stageCalls[bucket];
    g_gslc.stageUs[bucket] += durationUs;
    // Slow stages are rare and are the interesting ones (save read,
    // deserialization); log them individually.
    if (durationUs > 50000) {
        Log("GameSaveLoadCoreSlow: stage=" + std::to_string((int)(unsigned char)stage) +
            " " + std::to_string(durationUs) + " us");
    }
}

#if ENABLE_LOAD_PHASES_LOG
// Load-phase hooks (log only).  Conventions were read from each function's
// prologue and RET in Ghidra (2026-09-24); a wrong arity corrupts the caller's
// stack, so the stack-arg counts below are the verified RET immediates:
//   0x0055a650 RET          0x00647af0 RET 4     0x005361d0 RET
//   0x00537590 RET 0x10     0x0063caa0 RET 8 (2 stack args, NOT 1)
//   0x0063cee0 RET 0x10     0x008798a0 RET       0x00535a10 RET 4
//   0x00812350 RET 4        0x00696b00/0x006969e0/0x00699db0 RET 8
//   0x005e47e0 RET 8 (out-params: data ptr, size)
// Every detour forwards all args and returns the original's EAX untouched.
struct LpDepthGuard {
    int& depth;
    explicit LpDepthGuard(int& d) : depth(d) { ++depth; }
    ~LpDepthGuard() { --depth; }
};

typedef uint32_t (__thiscall* LpThis0Ptr_t)(void* thisPtr);
typedef uint32_t (__thiscall* LpThis1Ptr_t)(void* thisPtr, uint32_t a1);
typedef uint32_t (__thiscall* LpThis2Ptr_t)(void* thisPtr, uint32_t a1, uint32_t a2);
typedef uint32_t (__thiscall* LpThis4Ptr_t)(void* thisPtr, uint32_t a1, uint32_t a2,
                                             uint32_t a3, uint32_t a4);

static LpThis0Ptr_t g_lpOrigFinalize = nullptr;
static LpThis1Ptr_t g_lpOrigAnnounce = nullptr;
static LpThis1Ptr_t g_lpOrigNetEvents = nullptr;
static LpThis0Ptr_t g_lpOrigState12 = nullptr;
static LpThis4Ptr_t g_lpOrigTick = nullptr;
static LpThis2Ptr_t g_lpOrigBuildUpdate = nullptr;
static LpThis2Ptr_t g_lpOrigGetWriteMessage = nullptr;
static LpThis4Ptr_t g_lpOrigProgress = nullptr;
static LpThis0Ptr_t g_lpOrigAreaLoaded = nullptr;
static LpThis1Ptr_t g_lpOrigPlace = nullptr;
static LpThis2Ptr_t g_lpOrigFadeOut = nullptr;
static LpThis2Ptr_t g_lpOrigFadeIn = nullptr;
static LpThis2Ptr_t g_lpOrigFadeUntil = nullptr;

uint32_t __fastcall Hook_LpFinalize(void* thisPtr, void* edx) {
    if (!LpActive()) {
        return g_lpOrigFinalize(thisPtr);
    }
    LpPush(LP_FINALIZE_ENTER);
    uint32_t r = g_lpOrigFinalize(thisPtr);
    LpPush(LP_FINALIZE_EXIT);
    return r;
}

uint32_t __fastcall Hook_LpAnnounce(void* thisPtr, void* edx, uint32_t arg) {
    if (LpActive()) {
        LpPush(LP_ANNOUNCE, (int)arg, g_lpState12Depth > 0 ? 1 : 0);
    }
    return g_lpOrigAnnounce(thisPtr, arg);
}

// The engine's Module.Run reply lands here; only the first hit per minor is
// kept so a chatty handler cannot flood the event buffer.
uint32_t __fastcall Hook_LpNetEvents(void* thisPtr, void* edx, uint32_t minorArg) {
    if (LpActive()) {
        unsigned int minor = minorArg & 0xff;
        if (minor < 32 && (g_lp.netSeen & (1u << minor)) == 0) {
            g_lp.netSeen |= 1u << minor;
            LpPush(LP_NET_EVENT, (int)minor);
        }
    }
    return g_lpOrigNetEvents(thisPtr, minorArg);
}

uint32_t __fastcall Hook_LpState12(void* thisPtr, void* edx) {
    if (!LpActive()) {
        return g_lpOrigState12(thisPtr);
    }
    LpPush(LP_STATE12_ENTER);
    uint32_t r;
    {
        LpDepthGuard guard(g_lpState12Depth);
        r = g_lpOrigState12(thisPtr);
    }
    LpPush(LP_STATE12_EXIT, (int)r);
    return r;
}

// Reads the throttle stamp (player+0x2c/+0x30) that the engine rewrites only
// when the tick passes the 200 ms gate (0x00537723); unchanged == held back
// (or an early-out for a player that is not yet updatable -- both mean "no
// message from this tick", which is what the throttle-wait number needs).
static bool LpReadThrottleStamp(void* player, unsigned long long& out) {
    if (player == nullptr) {
        return false;
    }
    __try {
        unsigned int lo = *(unsigned int*)((char*)player + 0x2c);
        unsigned int hi = *(unsigned int*)((char*)player + 0x30);
        out = ((unsigned long long)hi << 32) | lo;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// FORCE_AREA_STREAM_DURING_LOAD: player+0x24 is the player's area-load state.
// It is 1 while the area streams and Server_HandleAreaMsg (0x00660600) flips it
// to 2 on the client's area-loaded ack (P(4,3)), so forcing only while it is 1
// covers exactly the throttle-paced stream and leaves gameplay at 200 ms.  The
// force flag skips only the 200 ms time compare (0x00537689); the function's
// own "is this player updatable yet" checks (+0x7c, creature+0x350) still run
// first.  The engine passes force=1 itself from FUN_0089fbd0.
static uint32_t StreamForceFlag(uint32_t player, uint32_t force) {
#if FORCE_AREA_STREAM_DURING_LOAD
    if (force != 1 && player != 0 && *(unsigned char*)(player + 0x24) == 1) {
        return 1;
    }
#endif
    return force;
}

uint32_t __fastcall Hook_LpUpdateClient(
    void* thisPtr, void* edx, uint32_t player, uint32_t force, uint32_t timeLo,
    uint32_t timeHi) {
    force = StreamForceFlag(player, force);
    // Fast path for the ~whole session: no window, or the load has not reached
    // finalize yet.
    if (!g_lp.active || g_lp.phase == 0 || !LpActive()) {
        return g_lpOrigTick(thisPtr, player, force, timeLo, timeHi);
    }
    unsigned long long before = 0, after = 0;
    const bool haveBefore = LpReadThrottleStamp((void*)player, before);
    LARGE_INTEGER t0 = {}, t1 = {};
    QueryPerformanceCounter(&t0);
    uint32_t r = g_lpOrigTick(thisPtr, player, force, timeLo, timeHi);
    QueryPerformanceCounter(&t1);
    const bool haveAfter = LpReadThrottleStamp((void*)player, after);
    const bool sent = haveBefore && haveAfter && before != after;
    const int phase = g_lp.phase;
    const long long durUs = QpcElapsedUs(t0, t1);
    LpTickCounts& c = g_lp.ticks[phase];
    ++c.calls;
    c.forced += force == 1 ? 1 : 0;
    if (sent) {
        ++c.sent;
        c.sentUs += durUs;
        if (g_lp.tickListCount < LP_TICK_MAX) {
            LpTick& tk = g_lp.tickList[g_lp.tickListCount];
            tk.qpc = t0;
            tk.durUs = (unsigned int)durUs;
            tk.heldBefore = g_lp.heldSincePrevSent;
            tk.phase = (unsigned char)phase;
        }
        ++g_lp.tickListCount;
        g_lp.heldSincePrevSent = 0;
    } else {
        ++c.held;
        ++g_lp.heldSincePrevSent;
    }
    return r;
}

// One call == one message build (up to 2000 bytes, 14 stages).  Bytes come
// from the out-param of the buffer accessor the builder calls right before it
// sends (hooked below; only counted while a build is in flight).
uint32_t __fastcall Hook_LpBuildUpdate(void* thisPtr, void* edx, uint32_t a1, uint32_t a2) {
    if (!g_lp.active || g_lp.phase == 0 || !LpActive()) {
        return g_lpOrigBuildUpdate(thisPtr, a1, a2);
    }
    g_lpCurMsgBytes = 0;
    g_lpCurMsgGets = 0;
    LARGE_INTEGER t0 = {}, t1 = {};
    QueryPerformanceCounter(&t0);
    uint32_t r;
    {
        LpDepthGuard guard(g_lpBuildDepth);
        r = g_lpOrigBuildUpdate(thisPtr, a1, a2);
    }
    QueryPerformanceCounter(&t1);
    const int phase = g_lp.phase;
    const long long durUs = QpcElapsedUs(t0, t1);
    ++g_lp.msgs[phase];
    g_lp.msgBytes[phase] += g_lpCurMsgBytes;
    g_lp.msgUs[phase] += durUs;
    if (g_lp.msgListCount < LP_MSG_MAX) {
        LpMsg& m = g_lp.msgList[g_lp.msgListCount];
        m.qpc = t0;
        m.durUs = (unsigned int)durUs;
        m.bytes = g_lpCurMsgBytes;
        m.getCalls = g_lpCurMsgGets;
        m.phase = (unsigned char)phase;
    }
    ++g_lp.msgListCount;
    return r;
}

uint32_t __fastcall Hook_LpGetWriteMessage(
    void* thisPtr, void* edx, uint32_t dataOut, uint32_t sizeOut) {
    uint32_t r = g_lpOrigGetWriteMessage(thisPtr, dataOut, sizeOut);
    if (g_lpBuildDepth > 0 && sizeOut != 0) {
        g_lpCurMsgBytes += *(uint32_t*)sizeOut;
        ++g_lpCurMsgGets;
    }
    return r;
}

// 'W' load-bar message: stage/total is the engine's real load progress (x/14).
uint32_t __fastcall Hook_LpProgress(
    void* thisPtr, void* edx, uint32_t obj, uint32_t flag, uint32_t stage, uint32_t total) {
    if (LpActive() && g_lp.phase != 0) {
        LpPush(LP_PROGRESS, (int)stage, (int)total, (int)flag);
    }
    return g_lpOrigProgress(thisPtr, obj, flag, stage, total);
}

uint32_t __fastcall Hook_LpAreaLoaded(void* thisPtr, void* edx) {
    if (LpActive()) {
        LpPush(LP_AREA_LOADED);
    }
    return g_lpOrigAreaLoaded(thisPtr);
}

uint32_t __fastcall Hook_LpPlace(void* thisPtr, void* edx, uint32_t player) {
    if (!LpActive()) {
        return g_lpOrigPlace(thisPtr, player);
    }
    LpPush(LP_PLACE_ENTER, 0, g_lpState12Depth > 0 ? 1 : 0);
    uint32_t r = g_lpOrigPlace(thisPtr, player);
    LpPush(LP_PLACE_EXIT);
    return r;
}

// NWScript command handlers: (this, nCommand, nParams).  Stamped on ENTRY: the
// handler itself starts the GUI fade (CSWGuiFade_SetTransitionState), so a
// stamp taken after it returns lands after the fade event it caused and the
// summary's "first fade at or after fade-in" search would miss it.
#define LP_SCRIPT_FADE_HOOK(NAME, ORIG, KIND)                                        \
    uint32_t __fastcall NAME(void* thisPtr, void* edx, uint32_t cmd, uint32_t params) { \
        if (LpActive()) {                                                            \
            LpPush(KIND);                                                            \
        }                                                                            \
        return ORIG(thisPtr, cmd, params);                                           \
    }
LP_SCRIPT_FADE_HOOK(Hook_LpFadeOut, g_lpOrigFadeOut, LP_SCRIPT_FADE_OUT)
LP_SCRIPT_FADE_HOOK(Hook_LpFadeIn, g_lpOrigFadeIn, LP_SCRIPT_FADE_IN)
LP_SCRIPT_FADE_HOOK(Hook_LpFadeUntil, g_lpOrigFadeUntil, LP_SCRIPT_FADE_UNTIL)

// Both are defined with the other hook-install helpers further down.
static bool InstallCheckedHook(DWORD address, LPVOID detour, LPVOID* original, const char* name);
static bool MatchesExecutableBytes(DWORD address, const BYTE* expected, size_t length);

// Each phase hook is signature-checked on its own and skipped (with a log
// line) on mismatch, so a bad address costs one log row instead of the whole
// mod: the global IsSupportedSteamExecutable gate is all-or-nothing.
static void InstallLoadPhaseHook(
    DWORD address, const BYTE* sig, size_t sigLen, LPVOID detour, LPVOID* original,
    const char* name) {
    if (!MatchesExecutableBytes(address, sig, sigLen)) {
        Log(std::string("LoadPhases: signature mismatch, hook skipped: ") + name);
        return;
    }
    InstallCheckedHook(address, detour, original, name);
}

static void InstallLoadPhaseHooks() {
    // 55 8b ec + (6a ff 68 <seh>) | (83 ec <n>) | (51): the prologue pattern
    // each function was verified against.
    static const BYTE sigFinalize[] = {0x55,0x8b,0xec,0x6a,0xff,0x68,0x5b,0x7b,0x95,0x00};
    static const BYTE sigAnnounce[] = {0x55,0x8b,0xec,0x83,0xec,0x10};
    static const BYTE sigState12[] = {0x55,0x8b,0xec,0x6a,0xff,0x68,0xe8,0xec,0x94,0x00};
    static const BYTE sigTick[] = {0x55,0x8b,0xec,0x83,0xec,0x54};
    static const BYTE sigBuild[] = {0x55,0x8b,0xec,0x83,0xec,0x74};
    static const BYTE sigProgress[] = {0x55,0x8b,0xec,0x83,0xec,0x10};
    static const BYTE sigAreaLoaded[] = {0x55,0x8b,0xec,0x51};
    static const BYTE sigPlace[] = {0x55,0x8b,0xec,0x83,0xec,0x48};
    static const BYTE sigNetEvents[] = {0x55,0x8b,0xec,0x6a,0xff,0x68,0xf8,0xce,0x96,0x00};
    static const BYTE sigFadeOut[] = {0x55,0x8b,0xec,0x83,0xec,0x34};
    static const BYTE sigFadeIn[] = {0x55,0x8b,0xec,0x83,0xec,0x3c};
    static const BYTE sigFadeUntil[] = {0x55,0x8b,0xec,0x83,0xec,0x0c};
    static const BYTE sigGetWrite[] = {0x55,0x8b,0xec,0x83,0xec,0x0c};
#define LP_INSTALL(ADDR, SIG, DETOUR, ORIG, NAME) \
    InstallLoadPhaseHook(ADDR, SIG, sizeof(SIG), (LPVOID)&DETOUR, (LPVOID*)&ORIG, NAME)
    LP_INSTALL(0x0055a650, sigFinalize, Hook_LpFinalize, g_lpOrigFinalize, "LP_ModuleLoad_FinalizeAndQueueReady");
    LP_INSTALL(0x00647af0, sigAnnounce, Hook_LpAnnounce, g_lpOrigAnnounce, "LP_Server_SendStateToClient");
    LP_INSTALL(0x00812350, sigNetEvents, Hook_LpNetEvents, g_lpOrigNetEvents, "LP_HandleNetEvents");
    LP_INSTALL(0x005361d0, sigState12, Hook_LpState12, g_lpOrigState12, "LP_Server_StartGame_State1to2");
    LP_INSTALL(0x00537590, sigTick, Hook_LpUpdateClient, g_lpOrigTick, "LP_Server_UpdateClient_Throttle200ms");
    LP_INSTALL(0x0063caa0, sigBuild, Hook_LpBuildUpdate, g_lpOrigBuildUpdate, "LP_Server_BuildClientObjectUpdate");
    LP_INSTALL(0x005e47e0, sigGetWrite, Hook_LpGetWriteMessage, g_lpOrigGetWriteMessage, "LP_Message_GetWriteMessage");
    LP_INSTALL(0x0063cee0, sigProgress, Hook_LpProgress, g_lpOrigProgress, "LP_Server_WriteAreaLoadProgressW");
    LP_INSTALL(0x008798a0, sigAreaLoaded, Hook_LpAreaLoaded, g_lpOrigAreaLoaded, "LP_Send_P_AreaLoaded_4_3");
    LP_INSTALL(0x00535a10, sigPlace, Hook_LpPlace, g_lpOrigPlace, "LP_Server_PlacePlayerInArea");
    LP_INSTALL(0x00696b00, sigFadeOut, Hook_LpFadeOut, g_lpOrigFadeOut, "LP_SetGlobalFadeOut");
    LP_INSTALL(0x006969e0, sigFadeIn, Hook_LpFadeIn, g_lpOrigFadeIn, "LP_SetGlobalFadeIn");
    LP_INSTALL(0x00699db0, sigFadeUntil, Hook_LpFadeUntil, g_lpOrigFadeUntil, "LP_SetFadeUntilScript");
#undef LP_INSTALL
}
#endif  // ENABLE_LOAD_PHASES_LOG

// Save-list entry parser: the Load menu parses EVERY save file (GFF open,
// ~20 field reads, portrait loads) each time the list is populated.  All of
// that happens before load-state activation and is invisible to both the
// transition window and the frozen-preload stamp, so it gets its own
// aggregate timer.  GUI-thread only; batching avoids per-call log noise.
typedef void (__thiscall* PopulateSaveGameEntryPtr_t)(void* thisPtr, int* saveName);
PopulateSaveGameEntryPtr_t g_originalPopulateSaveGameEntry = nullptr;
struct SaveListParseBatch {
    uint32_t calls;
    int64_t totalUs;
    int64_t maxUs;
};
static SaveListParseBatch g_saveListParseBatch = {};

void __fastcall Hook_PopulateSaveGameEntry(
    void* thisPtr, void* edxDummy, int* saveName) {
    InterlockedIncrement64(&g_hookCalls_PopulateSave);
    auto start = std::chrono::high_resolution_clock::now();
    g_originalPopulateSaveGameEntry(thisPtr, saveName);
    auto end = std::chrono::high_resolution_clock::now();
    int64_t duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    g_saveListParseBatch.totalUs += duration;
    if (duration > g_saveListParseBatch.maxUs) {
        g_saveListParseBatch.maxUs = duration;
    }
    if (++g_saveListParseBatch.calls >= 16) {
        Log("SaveListParse: " + std::to_string(g_saveListParseBatch.totalUs) +
            " us count=" + std::to_string(g_saveListParseBatch.calls) +
            " max_single=" + std::to_string(g_saveListParseBatch.maxUs));
        g_saveListParseBatch = {};
    }
}

static void StartLoadTransition(
    const EngineLoadStateSnapshot* state, LARGE_INTEGER start,
    const char* startReason) {
    if (g_loadTransition.active) {
        return;
    }
    g_loadTransition = {};
    g_loadTransition.active = true;
    g_loadTransition.ownerThreadId = GetCurrentThreadId();
    g_loadTransition.id = ++g_nextLoadTransitionId;
    g_loadTransition.startReason = startReason;
    g_loadTransition.start = start;
    if (state != nullptr) {
        g_loadTransition.startState = *state;
        if (state->state != 0) {
            g_loadTransition.stateActivationSeen = true;
            g_loadTransition.stateActivated = start;
        }
    }
    g_finishTransitionAtOutermostReturn = false;
    if (g_perceivedLoad.active) {
        if (g_perceivedLoad.ownerThreadId == GetCurrentThreadId()) {
            g_perceivedLoad.transitionStarted = true;
            g_perceivedLoad.transitionStart = start;
            g_perceivedLoad.lastPresentBeforeTransitionSeen =
                g_lastPresentQpc.QuadPart != 0;
            g_perceivedLoad.lastPresentBeforeTransition = g_lastPresentQpc;
            g_clickToControl.transitionStartSeen = true;
            g_clickToControl.transitionStart = start;
        }
    } else {
        // Door/module transitions have no LoadGame entry; anchor the perceived
        // window at state activation instead.
        StartLoadPerceived("state_activation", start);
        g_perceivedLoad.transitionStarted = true;
        g_perceivedLoad.transitionStart = start;
        g_perceivedLoad.lastPresentBeforeTransitionSeen =
            g_lastPresentQpc.QuadPart != 0;
        g_perceivedLoad.lastPresentBeforeTransition = g_lastPresentQpc;
        g_clickToControl.transitionStartSeen = true;
        g_clickToControl.transitionStart = start;
    }
}

static void ObserveTransitionState(
    const EngineLoadStateSnapshot& state, LARGE_INTEGER now) {
    if (!g_loadTransition.active ||
        g_loadTransition.ownerThreadId != GetCurrentThreadId()) {
        return;
    }
    if (state.state != 0 && !g_loadTransition.stateActivationSeen) {
        g_loadTransition.stateActivationSeen = true;
        g_loadTransition.stateActivated = now;
    } else if (state.state == 0 && g_loadTransition.stateActivationSeen &&
               !g_loadTransition.coordinatorClearSeen) {
        g_loadTransition.coordinatorClearSeen = true;
        g_loadTransition.coordinatorCleared = now;
    }
}

static void MaybeStartTransitionFromCoordinator(
    int manager, LARGE_INTEGER now, const char* pendingReason,
    const char* stateFallbackReason) {
    if (g_loadTransition.active) {
        return;
    }
    EngineLoadStateSnapshot state = {};
    bool haveState = TryReadEngineLoadState(state);
    LoadCoordinatorSnapshot coordinator = {};
    bool haveCoordinator = TryReadLoadCoordinator(manager, coordinator);
    if (haveCoordinator && coordinator.preparationPending == 1) {
        StartLoadTransition(haveState ? &state : nullptr, now, pendingReason);
    } else if (haveState && state.state != 0) {
        StartLoadTransition(&state, now, stateFallbackReason);
    }
}

static void FinishLoadTransition(
    const EngineLoadStateSnapshot* state, LARGE_INTEGER end,
    const char* endReason) {
    if (!g_loadTransition.active ||
        g_loadTransition.ownerThreadId != GetCurrentThreadId()) {
        return;
    }
    if (state != nullptr) {
        ObserveTransitionState(*state, end);
    }
    LoadTransitionProfile completed = g_loadTransition;
    g_loadTransition.active = false;
    g_finishTransitionAtOutermostReturn = false;
    if (g_perceivedLoad.active &&
        g_perceivedLoad.ownerThreadId == GetCurrentThreadId()) {
        g_perceivedLoad.transitionEnded = true;
        g_perceivedLoad.transitionEnd = end;
    }

    long long wallUs = QpcElapsedUs(completed.start, end);
    long long stateActivatedUs = completed.stateActivationSeen
        ? QpcElapsedUs(completed.start, completed.stateActivated) : -1;
    long long coordinatorClearUs = completed.coordinatorClearSeen
        ? QpcElapsedUs(completed.start, completed.coordinatorCleared) : -1;
    long long finalDrainStartUs = completed.finalDrainSeen
        ? QpcElapsedUs(completed.start, completed.finalDrainStarted) : -1;
    long long visibleScreenUs = completed.presentedFrameSeen
        ? QpcElapsedUs(completed.firstPresentedFrame,
                       completed.lastPresentedFrameEnd) : -1;
    long long firstFrameDelayUs = completed.presentedFrameSeen
        ? QpcElapsedUs(completed.start, completed.firstPresentedFrame) : -1;
    long long postFrameUs = completed.presentedFrameSeen &&
            end.QuadPart >= completed.lastPresentedFrameEnd.QuadPart
        ? QpcElapsedUs(completed.lastPresentedFrameEnd, end) : -1;

    bool frameOrderValid = !completed.presentedFrameSeen ||
        (completed.firstPresentedFrame.QuadPart >= completed.start.QuadPart &&
         completed.lastPresentedFrameEnd.QuadPart >= completed.firstPresentedFrame.QuadPart &&
         completed.lastPresentedFrameEnd.QuadPart <= end.QuadPart);
    bool moduleOrderValid = !completed.moduleWindowSeen ||
        (completed.firstModuleStart.QuadPart >= completed.start.QuadPart &&
         completed.lastModuleEnd.QuadPart <= end.QuadPart);
    bool archiveOrderValid = !completed.archiveWindowSeen ||
        (completed.firstArchiveStart.QuadPart >= completed.start.QuadPart &&
         completed.lastArchiveEnd.QuadPart <= end.QuadPart);
    bool valid = completed.finalDrainSeen && completed.presentedFrameSeen &&
        frameOrderValid && moduleOrderValid && archiveOrderValid &&
        wallUs >= completed.moduleChunkMaxCallUs;

    EngineLoadStateSnapshot endState = {};
    if (state != nullptr) {
        endState = *state;
    }
    Log("LoadTransitionWallTime: " + std::to_string(wallUs) +
        " us run_id=" + std::to_string(g_profilerRunId) +
        " id=" + std::to_string(completed.id) +
        " valid=" + std::to_string(valid ? 1 : 0) +
        " start_reason=" + completed.startReason +
        " end_reason=" + endReason +
        " start_state=" + std::to_string(completed.startState.state) +
        " start_mode=" + std::to_string(completed.startState.mode) +
        " current_index=" + std::to_string(completed.startState.currentIndex) +
        " target_or_count=" + std::to_string(completed.startState.targetOrCount) +
        " end_state=" + std::to_string(endState.state) +
        " end_mode=" + std::to_string(endState.mode) +
        " state_activated_us=" + std::to_string(stateActivatedUs) +
        " coordinator_clear_us=" + std::to_string(coordinatorClearUs) +
        " final_drain_start_us=" + std::to_string(finalDrainStartUs) +
        " visible_screen_us=" + std::to_string(visibleScreenUs) +
        " first_frame_delay_us=" + std::to_string(firstFrameDelayUs) +
        " post_frame_us=" + std::to_string(postFrameUs) +
        " presented_frame_calls=" + std::to_string(completed.presentedFrameCalls) +
        " engine_us=" + std::to_string(completed.engineUs) +
        " engine_calls=" + std::to_string(completed.engineCalls) +
        " outer_loadingscreen_us=" + std::to_string(completed.outerLoadingScreenUs) +
        " outer_calls=" + std::to_string(completed.outerLoadingScreenCalls) +
        " module_chunk_us=" + std::to_string(completed.moduleChunkUs) +
        " module_calls=" + std::to_string(completed.moduleChunkCalls) +
        " module_max_call_us=" + std::to_string(completed.moduleChunkMaxCallUs) +
        " loading_frame_us=" + std::to_string(completed.loadingFrameUs) +
        " frame_calls=" + std::to_string(completed.loadingFrameCalls) +
        " archive_us=" + std::to_string(completed.archiveUs) +
        " archive_calls=" + std::to_string(completed.archiveCalls) +
        " worker_submit_wait_us=" + std::to_string(completed.workerSubmitWaitUs) +
        " worker_submit_calls=" + std::to_string(completed.workerSubmitCalls) +
        " queue_drain_us=" + std::to_string(completed.resourceQueueDrainUs) +
        " queue_drain_calls=" + std::to_string(completed.resourceQueueDrainCalls));
}

static void RecordTransitionDuration(
    long long* totalUs, unsigned int* calls, long long durationUs) {
    if (g_loadTransition.active &&
        g_loadTransition.ownerThreadId == GetCurrentThreadId()) {
        *totalUs += durationUs;
        ++*calls;
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

typedef uint32_t (__thiscall* EnginePtr_t)(void* thisPtr);
EnginePtr_t g_originalEngine = nullptr;

uint32_t __fastcall Hook_Engine(void* thisPtr, void* edxDummy) {
    InterlockedIncrement64(&g_hookCalls_Engine);
    LARGE_INTEGER callStart = {};
    QueryPerformanceCounter(&callStart);
    const bool activeAtCallStart = g_loadTransition.active;

    EngineLoadStateSnapshot before = {};
    bool haveBefore = TryReadEngineLoadState(before);
    if (haveBefore && g_loadTransition.active) {
        ObserveTransitionState(before, callStart);
    }

    uint32_t result = g_originalEngine(thisPtr);

    LARGE_INTEGER callEnd = {};
    QueryPerformanceCounter(&callEnd);
    EngineLoadStateSnapshot after = {};
    bool haveAfter = TryReadEngineLoadState(after);
    if (haveAfter && !g_loadTransition.active && after.state != 0) {
        // Last-resort coverage for direct/network loads that bypass the normal
        // coordinator preparation flag. Do not charge the completed Engine tick.
        StartLoadTransition(&after, callEnd, "engine_state_fallback");
    }
    if (activeAtCallStart && g_loadTransition.active &&
        g_loadTransition.ownerThreadId == GetCurrentThreadId()) {
        RecordTransitionDuration(
            &g_loadTransition.engineUs, &g_loadTransition.engineCalls,
            QpcElapsedUs(callStart, callEnd));
    }
    if (haveAfter && g_loadTransition.active) {
        // Coordinator clear is only a phase marker. Queued client packets,
        // ModuleChunkLoadCore and final object stabilization can follow it.
        ObserveTransitionState(after, callEnd);
    }

    // Click-to-control tracking: shadow-log the load-context substate and
    // the client busy flag from the click onward, and once the first
    // post-drain frame has been presented (the frame the video session
    // proved is black), treat the first tick where the client busy flag
    // (client+0x90, the gate that early-outs UpdatePlayerInputAndTargeting)
    // clears as the moment the player regains control.
    if (g_clickToControl.armed &&
        GetCurrentThreadId() == g_perceivedLoad.ownerThreadId) {
        int gateValue = haveAfter ? after.clientBusy : -1;
        if (gateValue > 0) {
            g_clickToControl.gateSeenBusy = true;
        }
        if (!g_clickToControl.lastSubstateValid ||
            after.substate != g_clickToControl.lastSubstate ||
            gateValue != g_clickToControl.lastGateValue) {
            g_clickToControl.lastSubstateValid = true;
            g_clickToControl.lastSubstate = after.substate;
            g_clickToControl.lastGateValue = gateValue;
            if (g_clickToControl.substateLogs < 32) {
                ++g_clickToControl.substateLogs;
                Log("LoadGate: substate=" + std::to_string(after.substate) +
                    " client_busy=" + std::to_string(gateValue) +
                    " at_us_after_click=" +
                    std::to_string(QpcElapsedUs(g_clickToControl.click, callEnd)));
            }
        }
        if (g_clickToControl.firstPresentSeen) {
            if (gateValue == 0) {
                FinishClickToControl(
                    callEnd,
                    g_clickToControl.gateSeenBusy ? "control_input_unblocked"
                                                  : "gate_never_busy_anchor_invalid",
                    after.substate);
            } else if (QpcElapsedUs(g_clickToControl.firstPresent, callEnd) >
                       120000000LL) {
                // Never unblocked: do not spin on this window forever; the
                // shadow lines above carry whatever the gate actually did.
                FinishClickToControl(callEnd, "expired_no_control", after.substate);
            }
        } else if (QpcElapsedUs(g_clickToControl.click, callEnd) >
                   30000000LL) {
            // No transition/present followed the click (e.g. a save action or
            // cancelled load): drop the window instead of holding a stale one.
            FinishClickToControl(callEnd, "expired_no_present", after.substate);
        }
    }
    return result;
}

// LoadingScreen
typedef int (__fastcall* loadingscreenPtr_t)(int param1);
loadingscreenPtr_t g_originalLoadingScreenPtr = nullptr;


int __fastcall Hook_loadingscreenPtr(int param1) {
    InterlockedIncrement64(&g_hookCalls_Loadingscreen);
    LARGE_INTEGER transitionStart = {};
    QueryPerformanceCounter(&transitionStart);
    const bool outermost = (g_loadingscreenDepth++ == 0);

    MaybeStartTransitionFromCoordinator(
        param1, transitionStart,
        outermost ? "coordinator_preparation" : "nested_coordinator_preparation",
        "coordinator_state_fallback");

    const bool activeAtEntry = g_loadTransition.active;
    EngineLoadStateSnapshot before = {};
    bool haveBefore = TryReadEngineLoadState(before);
    if (haveBefore && g_loadTransition.active) {
        ObserveTransitionState(before, transitionStart);
    }

    LoadCoordinatorSnapshot coordinator = {};
    bool finalDrainGate = g_loadTransition.active &&
        TryReadLoadCoordinator(param1, coordinator) &&
        coordinator.preparationPending != 1 &&
        haveBefore && before.state == 0 && coordinator.managerStatus == 2;
    if (finalDrainGate) {
        if (!g_loadTransition.finalDrainSeen) {
            g_loadTransition.finalDrainSeen = true;
            g_loadTransition.finalDrainStarted = transitionStart;
        }
        // This path runs FUN_0051D790, FUN_00537510 and the final object-list
        // clear. Complete only when the containing outermost call returns.
        g_finishTransitionAtOutermostReturn = true;
    }

    auto start = std::chrono::high_resolution_clock::now();

    int result = g_originalLoadingScreenPtr(param1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    --g_loadingscreenDepth;
    ++g_lsCallsSinceLoad;
    g_lsBusyUsSinceLoad += duration.count();
    if (g_lsFirstCallSinceLoad.QuadPart == 0) {
        g_lsFirstCallSinceLoad = transitionStart;
    }
    LARGE_INTEGER transitionEnd = {};
    QueryPerformanceCounter(&transitionEnd);
    if (outermost) {
        if (activeAtEntry && g_loadTransition.active) {
            RecordTransitionDuration(
                &g_loadTransition.outerLoadingScreenUs,
                &g_loadTransition.outerLoadingScreenCalls,
                QpcElapsedUs(transitionStart, transitionEnd));
        }
        EngineLoadStateSnapshot after = {};
        bool haveAfter = TryReadEngineLoadState(after);
        if (haveAfter && g_loadTransition.active) {
            ObserveTransitionState(after, transitionEnd);
        }
        if (g_finishTransitionAtOutermostReturn && g_loadTransition.active) {
            FinishLoadTransition(
                haveAfter ? &after : nullptr, transitionEnd,
                "final_drain_outer_return");
        }
    }
#if LOG_HIGH_FREQUENCY_CALLS
    Log("loadingscreen: " + std::to_string(duration.count()) + " μs");
#endif

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


void __fastcall Hook_ProcessResourceQueueTransition(int param1, void*, int param2) {
    auto start = std::chrono::high_resolution_clock::now();
    g_originalProcessResourceQueuePtr(param1, nullptr, param2);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    // Always-on accumulation: pre-activation load work is drained through
    // these calls before the load state ever activates.
    ++g_prqCallsSinceLoad;
    g_prqBusyUsSinceLoad += duration.count();
    if (param2 != 0) {
        RecordTransitionDuration(
            &g_loadTransition.resourceQueueDrainUs,
            &g_loadTransition.resourceQueueDrainCalls,
            duration.count());
    }
}

// Packet handlers: P = major-dispatched client packets (module, game objects,
// save/load...), S = server query packets.  Timing-only wrappers.
// Convention verified from call sites and epilogues (RET 0x8, this in ECX):
// both are __thiscall(this, packet, packetSize).
typedef uint32_t (__thiscall* PacketHandlerPtr_t)(
    void* thisPtr, char* packet, int packetSize);
PacketHandlerPtr_t g_originalPPacketHandlerTiming = nullptr;
PacketHandlerPtr_t g_originalSPacketHandlerTiming = nullptr;

uint32_t __fastcall Hook_PPacketHandlerTiming(
    void* thisPtr, void* edxDummy, char* packet, int packetSize) {
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t result = g_originalPPacketHandlerTiming(thisPtr, packet, packetSize);
    auto end = std::chrono::high_resolution_clock::now();
    int64_t duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    ++g_ppCallsSinceLoad;
    g_ppBusyUsSinceLoad += duration;
    if (packet != nullptr) {
        unsigned char major = (unsigned char)packet[1];
        ++g_ppMajorCalls[major];
        g_ppMajorUs[major] += duration;
    }
    return result;
}

uint32_t __fastcall Hook_SPacketHandlerTiming(
    void* thisPtr, void* edxDummy, char* packet, int packetSize) {
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t result = g_originalSPacketHandlerTiming(thisPtr, packet, packetSize);
    auto end = std::chrono::high_resolution_clock::now();
    ++g_spCallsSinceLoad;
    g_spBusyUsSinceLoad +=
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    return result;
}

typedef void (__fastcall* InitShadowCachePtr_t)(uint32_t param1);
InitShadowCachePtr_t g_originalInitShadowCachePtr = nullptr;

// InitGraphicsCache: per-tick texture/mesh pull scan during loading phases.
// __thiscall(this) verified from prologue/epilogue (ECX stored, plain RET).
typedef void (__thiscall* InitGraphicsCachePtr_t)(void* thisPtr);
InitGraphicsCachePtr_t g_originalInitGraphicsCache = nullptr;

void __fastcall Hook_InitGraphicsCache(void* thisPtr, void* edxDummy) {
    auto start = std::chrono::high_resolution_clock::now();
    g_originalInitGraphicsCache(thisPtr);
    auto end = std::chrono::high_resolution_clock::now();
    ++g_igcCallsSinceLoad;
    g_igcBusyUsSinceLoad +=
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

void __fastcall Hook_InitShadowCache(uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();

    g_originalInitShadowCachePtr(param1);

    auto end = std::chrono::high_resolution_clock::now();
    ++g_iscCallsSinceLoad;
    g_iscBusyUsSinceLoad +=
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
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
    LARGE_INTEGER transitionCallStart = {};
    QueryPerformanceCounter(&transitionCallStart);
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
    LARGE_INTEGER transitionCallEnd = {};
    QueryPerformanceCounter(&transitionCallEnd);
    if (outermostModuleChunkLoad) {
        if (g_loadTransition.active &&
            g_loadTransition.ownerThreadId == GetCurrentThreadId()) {
            if (!g_loadTransition.moduleWindowSeen) {
                g_loadTransition.moduleWindowSeen = true;
                g_loadTransition.firstModuleStart = transitionCallStart;
            }
            g_loadTransition.lastModuleEnd = transitionCallEnd;
            if (duration.count() > g_loadTransition.moduleChunkMaxCallUs) {
                g_loadTransition.moduleChunkMaxCallUs = duration.count();
            }
        }
        RecordTransitionDuration(
            &g_loadTransition.moduleChunkUs,
            &g_loadTransition.moduleChunkCalls,
            duration.count());
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
    LARGE_INTEGER transitionCallStart = {};
    QueryPerformanceCounter(&transitionCallStart);
    int coordinatorManager = 0;
    LoadCoordinatorSnapshot coordinator = {};
    if (TryReadGlobalLoadCoordinator(coordinatorManager, coordinator)) {
        MaybeStartTransitionFromCoordinator(
            coordinatorManager, transitionCallStart,
            "loading_frame_preparation", "loading_frame_state_fallback");
    }
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

    bool executeFrame = true;
#if SKIP_LOADING_SCREEN_UPDATE_FRAME_IN_MODULE_CHUNK_LOAD_CORE
    bool calledFromModuleChunkLoadCore = (g_moduleChunkLoadCoreDepth > 0);
    // Skipping LoadingScreenUpdateFrame when called from ModuleChunkLoadCore with param2 == 0, as this seems to be redundant.
    bool skipLoadingScreenUpdate = calledFromModuleChunkLoadCore && runLoadingScreenWork == 0;

    if (skipLoadingScreenUpdate) {
        Log("LoadingScreenUpdateFrame: skipped inside ModuleChunkLoadCore");
        executeFrame = false;
    }
#endif

    bool trackPresentedFrame = executeFrame && effectiveSuppressPresent == 0 &&
        g_loadTransition.active &&
        g_loadTransition.ownerThreadId == GetCurrentThreadId();
    if (trackPresentedFrame) {
        if (!g_loadTransition.presentedFrameSeen) {
            g_loadTransition.presentedFrameSeen = true;
            g_loadTransition.firstPresentedFrame = transitionCallStart;
        }
        ++g_loadTransition.presentedFrameCalls;
    }
    if (executeFrame) {
#if ENABLE_VISUAL_LOAD_TIMELINE
        // Any SwapBuffers the loading-screen renderer performs while we are
        // inside this call presents the loading screen itself; the visual
        // classifier uses this to tell "loading screen" from ordinary frames.
        ++g_vlLoadingScreenFrameDepth;
#endif
        g_originalLoadingScreenUpdateFrame(
            deltaTime, runLoadingScreenWork, effectiveSuppressPresent);
#if ENABLE_VISUAL_LOAD_TIMELINE
        --g_vlLoadingScreenFrameDepth;
#endif
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    LARGE_INTEGER transitionCallEnd = {};
    QueryPerformanceCounter(&transitionCallEnd);
    if (trackPresentedFrame && g_loadTransition.active) {
        g_loadTransition.lastPresentedFrameEnd = transitionCallEnd;
    }
    RecordTransitionDuration(
        &g_loadTransition.loadingFrameUs,
        &g_loadTransition.loadingFrameCalls,
        duration.count());
#if LOG_HIGH_FREQUENCY_CALLS
    Log("LoadingScreenUpdateFrame: " + std::to_string(duration.count()) + " μs");
#endif
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
    // duration arrives as the raw bits of a float in seconds (measured load
    // fade: 0x3F800000 = 1.0f, once per load transition).
    float durationSeconds = 0.0f;
    static_assert(sizeof(durationSeconds) == sizeof(duration),
        "fade duration bit-cast requires matching sizes");
    std::memcpy(&durationSeconds, &duration, sizeof(durationSeconds));
    // progress is the same float-bits encoding; on a scripted fade-in it is the
    // hold before the fade starts (1.0 s on 101PER).
    float progressSeconds = 0.0f;
    std::memcpy(&progressSeconds, &progress, sizeof(progressSeconds));
#if ENABLE_LOAD_PHASES_LOG
    if (LpActive() && g_lp.guiFadeEvents < LP_GUI_FADE_MAX) {
        ++g_lp.guiFadeEvents;
        LpPush(LP_GUI_FADE, mode, 0, (int)progress, progressSeconds, durationSeconds);
    }
#endif
    if (durationSeconds >= 0.25f) {
        Log("CSWGuiFadeLong: mode=" + std::to_string(mode) +
            " duration_raw=" + std::to_string(duration) +
            " seconds=" + std::to_string(durationSeconds) +
            " progress_raw=" + std::to_string(progress) +
            " progress_seconds=" + std::to_string(progressSeconds));
#if ENABLE_VISUAL_LOAD_TIMELINE
        // Stamp the raw (pre-clamp) fade into the visual timeline: its offset
        // from the visual phases shows whether a fade is gating the visible
        // loading screen or the black window.
        if (g_visualLoad.active) {
            LARGE_INTEGER fadeNow = {};
            QueryPerformanceCounter(&fadeNow);
            g_visualLoad.fadeSeen = true;
            g_visualLoad.fade = fadeNow;
            g_visualLoad.fadeMode = mode;
            g_visualLoad.fadeSeconds = durationSeconds;
        }
#endif
#if CLAMP_LONG_FADES
        // 0.001f rather than 0.0f: keeps any scale-by-duration math finite.
        durationSeconds = 0.001f;
        std::memcpy(&duration, &durationSeconds, sizeof(duration));
        ++g_fadeClampsSinceLoad;
        Log("FadeClamped: mode=" + std::to_string(mode) + " to 1ms");
#endif
    }
    auto start = std::chrono::high_resolution_clock::now();
    g_originalCSWGuiFade_SetTransitionState(thisPtr, edx, mode, progress, duration, targetColor);
    auto end = std::chrono::high_resolution_clock::now();
    ++g_fadeCallsSinceLoad;
    g_fadeBusyUsSinceLoad +=
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
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

typedef void (__thiscall* WorkerSubmitJobPtr_t)(
    void* thisPtr, uint32_t resourceName, uint32_t jobType, uint32_t jobFlags);
WorkerSubmitJobPtr_t g_originalWorkerSubmitJob = nullptr;

void __fastcall Hook_WorkerSubmitJob(
    void* thisPtr, void* edxDummy,
    uint32_t resourceName, uint32_t jobType, uint32_t jobFlags) {
    auto start = std::chrono::high_resolution_clock::now();
    g_originalWorkerSubmitJob(thisPtr, resourceName, jobType, jobFlags);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    RecordTransitionDuration(
        &g_loadTransition.workerSubmitWaitUs,
        &g_loadTransition.workerSubmitCalls,
        duration.count());
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
    LARGE_INTEGER transitionCallStart = {};
    QueryPerformanceCounter(&transitionCallStart);
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
    LARGE_INTEGER transitionCallEnd = {};
    QueryPerformanceCounter(&transitionCallEnd);
    if (g_loadTransition.active &&
        g_loadTransition.ownerThreadId == GetCurrentThreadId()) {
        if (!g_loadTransition.archiveWindowSeen) {
            g_loadTransition.archiveWindowSeen = true;
            g_loadTransition.firstArchiveStart = transitionCallStart;
        }
        g_loadTransition.lastArchiveEnd = transitionCallEnd;
    }
    RecordTransitionDuration(
        &g_loadTransition.archiveUs,
        &g_loadTransition.archiveCalls,
        duration.count());
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
    ++g_lfOpenCallsSinceLoad;
    g_lfOpenBusyUsSinceLoad +=
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    return result;
}

typedef int (__thiscall* LooseFileReadPtr_t)(int* thisPtr, void* buffer, size_t elementSize, size_t elementCount);
LooseFileReadPtr_t g_originalLooseFileRead = nullptr;

int __fastcall Hook_LooseFileRead(int* thisPtr, void* edxDummy, void* buffer, size_t elementSize, size_t elementCount) {
    auto start = std::chrono::high_resolution_clock::now();

    int result = g_originalLooseFileRead(thisPtr, buffer, elementSize, elementCount);

    auto end = std::chrono::high_resolution_clock::now();
    ++g_lfReadCallsSinceLoad;
    g_lfReadBusyUsSinceLoad +=
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    g_lfReadBytesSinceLoad += (long long)elementSize * (long long)elementCount;
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
    static const BYTE engineSignature[] = {
        0x55,0x8b,0xec,0x6a,0xff,0x68,0x5b,0x58,0x96,0x00
    };
    static const BYTE loadGameSignature[] = {
        0x55,0x8b,0xec,0x6a,0xff,0x68,0x8b,0x80,0x95,0x00
    };
    static const BYTE saveLoadRequestSignature[] = {
        0x55,0x8b,0xec,0x83,0xec,0x4c,0xa1,0x20,0x1f,0xa1
    };
    static const BYTE populateSaveSignature[] = {
        0x55,0x8b,0xec,0x6a,0xff,0x68,0x15,0x2a,0x97,0x00
    };
    static const BYTE gameSaveLoadCoreSignature[] = {
        0x55,0x8b,0xec,0x6a,0xff,0x68,0x59,0x86,0x95,0x00
    };
    static const BYTE pPacketHandlerSignature[] = {
        0x55,0x8b,0xec,0x6a,0xff,0x68,0xf0,0xcd,0x96,0x00
    };
    static const BYTE sPacketHandlerSignature[] = {
        0x55,0x8b,0xec,0x6a,0xff,0x68,0x4b,0x4d,0x97,0x00
    };
    static const BYTE initGraphicsCacheSignature[] = {
        0x55,0x8b,0xec,0x6a,0xff,0x68,0x2e,0xf1,0x94,0x00
    };
    static const BYTE initShadowCacheSignature[] = {
        0x55,0x8b,0xec,0x83,0xec,0x20,0x89,0x4d,0xe0,0xa1
    };
    static const BYTE fadeSetStateSignature[] = {
        0x55,0x8b,0xec,0x83,0xec,0x28,0x89,0x4d,0xf0,0x8b
    };
    static const BYTE looseFileOpenSignature[] = {
        0x55,0x8b,0xec,0x6a,0xff,0x68,0x6b,0x1e,0x96,0x00
    };
    static const BYTE looseFileReadSignature[] = {
        0x55,0x8b,0xec,0x83,0xec,0x0c,0x89,0x4d,0xf4,0x8b
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
    static const BYTE workerSubmitSignature[] = {
        0x55,0x8b,0xec,0x51,0x89,0x4d,0xfc
    };
    static const BYTE processResourceQueueSignature[] = {
        0x55,0x8b,0xec,0x83,0xec,0x0c,0x56
    };
    static const BYTE sourceListBeginSignature[] = {
        0x55,0x8b,0xec,0x83,0xec,0x0c
    };
    static const BYTE sourceListAccessSignature[] = {
        0x55,0x8b,0xec,0x51,0x89,0x4d,0xfc
    };
    struct SignatureCheck {
        const char* name;
        DWORD address;
        const BYTE* bytes;
        size_t size;
    };
    const SignatureCheck checks[] = {
        {"Engine", 0x00781be0, engineSignature, sizeof(engineSignature)},
        {"LoadGame", 0x006310d0, loadGameSignature, sizeof(loadGameSignature)},
        {"SaveLoadRequest", 0x0065f630, saveLoadRequestSignature, sizeof(saveLoadRequestSignature)},
        {"PopulateSaveGameEntry", 0x00855f30, populateSaveSignature, sizeof(populateSaveSignature)},
        {"GameSaveLoad_Core", 0x00638bd0, gameSaveLoadCoreSignature, sizeof(gameSaveLoadCoreSignature)},
        {"PPacketHandler", 0x00810cf0, pPacketHandlerSignature, sizeof(pPacketHandlerSignature)},
        {"SPacketHandler", 0x00884530, sPacketHandlerSignature, sizeof(sPacketHandlerSignature)},
        {"InitGraphicsCache", 0x0053a0c0, initGraphicsCacheSignature, sizeof(initGraphicsCacheSignature)},
        {"InitShadowCache", 0x0053a8b0, initShadowCacheSignature, sizeof(initShadowCacheSignature)},
        {"CSWGuiFade_SetTransitionState", 0x007bc8f0, fadeSetStateSignature, sizeof(fadeSetStateSignature)},
        {"LooseFileOpen", 0x0073da40, looseFileOpenSignature, sizeof(looseFileOpenSignature)},
        {"LooseFileRead", 0x0073dd20, looseFileReadSignature, sizeof(looseFileReadSignature)},
        {"ModuleChunkLoadCore", 0x007be4c0, moduleChunkSignature, sizeof(moduleChunkSignature)},
        {"LoadingScreenUpdateFrame", 0x00409ed0, loadingFrameSignature, sizeof(loadingFrameSignature)},
        {"LoadingScreen", 0x00533830, loadScreenSignature, sizeof(loadScreenSignature)},
        {"GUI_FindAndBindControlByTag", 0x00418df0, guiFindSignature, sizeof(guiFindSignature)},
        {"GFF_LookupFieldLabelByName", 0x007178e0, gffLookupSignature, sizeof(gffLookupSignature)},
        {"ResourceLoadFromArchive", 0x00713bf0, resourceLoadArchiveSignature, sizeof(resourceLoadArchiveSignature)},
        {"CExoEncapsulatedFile_ReadResourceSync", 0x00729370, encapsulatedReadSignature, sizeof(encapsulatedReadSignature)},
        {"Resource_AllocateLoadBuffer", 0x00712f30, allocateBufferSignature, sizeof(allocateBufferSignature)},
        {"Worker_SubmitJob", 0x00711600, workerSubmitSignature, sizeof(workerSubmitSignature)},
        {"ProcessResourceQueue", 0x00703f30, processResourceQueueSignature, sizeof(processResourceQueueSignature)},
        {"ArchiveSourceListBegin", 0x007a1720, sourceListBeginSignature, sizeof(sourceListBeginSignature)},
        {"ArchiveSourceListGet", 0x00561430, sourceListAccessSignature, sizeof(sourceListAccessSignature)},
        {"ArchiveSourceListNext", 0x0058c370, sourceListAccessSignature, sizeof(sourceListAccessSignature)},
    };
    for (const SignatureCheck& check : checks) {
        if (!MatchesExecutableBytes(check.address, check.bytes, check.size)) {
            // Name the failing check: an all-or-nothing gate that fails silently
            // turns the whole mod (including the intro skip) off with no clue.
            Log(std::string("Signature check failed: ") + check.name);
            return false;
        }
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
    if (g_profilerRunId == 0) {
        g_profilerRunId =
            ((unsigned long long)GetCurrentProcessId() << 32) |
            (unsigned long long)GetTickCount();
    }
    Log("ProfilerRunStart: run_id=" + std::to_string(g_profilerRunId) +
        " pid=" + std::to_string(GetCurrentProcessId()) +
        " scenario=" + kScenarioName +
        " performance_hook_set_only=" + std::to_string(PERFORMANCE_HOOK_SET_ONLY) +
        " preload_noop=" + std::to_string(SKIP_PRELOAD_INITIAL_ASSETS_WRAPPER) +
        " archive_cache=" + std::to_string(ENABLE_ARCHIVE_RESOURCE_CACHE) +
        " gui_cache=" + std::to_string(ENABLE_GUI_CONTROLS_LOOKUP_CACHE) +
        " present_throttle=" + std::to_string(THROTTLE_LOADING_SCREEN_PRESENTS) +
        " skip_debug_gui=" + std::to_string(SKIP_DEBUG_GUI_CONSTRUCTION) +
        " defer_ingame_tabs=" + std::to_string(DEFER_INGAME_TAB_CONSTRUCTION) +
        " clamp_long_fades=" + std::to_string(CLAMP_LONG_FADES) +
        " load_phases_log=" + std::to_string(ENABLE_LOAD_PHASES_LOG) +
        " stream_force=" + std::to_string(FORCE_AREA_STREAM_DURING_LOAD));
#if SKIP_PRELOAD_INITIAL_ASSETS_WRAPPER
    InstallCheckedHook(0x0073f050, (LPVOID)&Hook_PreloadInitialAssetsWrapper,
        (LPVOID*)&g_originalPreloadInitialAssetsWrapperPtr, "PreloadInitialAssetsWrapper");
#endif

    // Engine supplies phase observations only. Coordinator state clear is not a
    // completion boundary because queued client/module work continues afterward.
    InstallCheckedHook(0x00781be0, (LPVOID)&Hook_Engine,
        (LPVOID*)&g_originalEngine, "EngineTransitionObserver");

    // Save-load entry: anchors the true perceived window at the Load button.
    InstallCheckedHook(0x006310d0, (LPVOID)&Hook_LoadGame,
        (LPVOID*)&g_originalLoadGame, "LoadGame");

    // Server-side save/load request dispatcher: fallback anchor closer to the
    // button click on the real (packet-driven) save-load path.
    InstallCheckedHook(0x0065f630, (LPVOID)&Hook_SaveLoadRequest,
        (LPVOID*)&g_originalSaveLoadRequest, "SaveLoadRequest");

    // Save-list population cost (per menu open, before any load starts).
    InstallCheckedHook(0x00855f30, (LPVOID)&Hook_PopulateSaveGameEntry,
        (LPVOID*)&g_originalPopulateSaveGameEntry, "PopulateSaveGameEntry");

    // Load diagnostics (timing-only): fade pacing, per-tick cache scans,
    // loose-file I/O — targets of the near-zero load plan.
#define ENABLE_LOAD_DIAGNOSTIC_TIMING 1
#if ENABLE_LOAD_DIAGNOSTIC_TIMING
    InstallCheckedHook(0x0053a0c0, (LPVOID)&Hook_InitGraphicsCache,
        (LPVOID*)&g_originalInitGraphicsCache, "InitGraphicsCacheTiming");
    InstallCheckedHook(0x0053a8b0, (LPVOID)&Hook_InitShadowCache,
        (LPVOID*)&g_originalInitShadowCachePtr, "InitShadowCacheTiming");
    InstallCheckedHook(0x007bc8f0, (LPVOID)&Hook_CSWGuiFade_SetTransitionState,
        (LPVOID*)&g_originalCSWGuiFade_SetTransitionState, "CSWGuiFadeTiming");
    InstallCheckedHook(0x0073da40, (LPVOID)&Hook_LooseFileOpen,
        (LPVOID*)&g_originalLooseFileOpen, "LooseFileOpenTiming");
    InstallCheckedHook(0x0073dd20, (LPVOID)&Hook_LooseFileRead,
        (LPVOID*)&g_originalLooseFileRead, "LooseFileReadTiming");
#endif

    // Save/load state-machine core: per-stage timing of the pre-activation
    // save read/deserialization phase.
    InstallCheckedHook(0x00638bd0, (LPVOID)&Hook_GameSaveLoadCore,
        (LPVOID*)&g_originalGameSaveLoadCore, "GameSaveLoad_Core");

    // Packet handlers: attribute pre-activation packet-drain work per family.
    // DISABLED: crashing the game on load.  Convention must be verified from
    // the handler epilogues (RET vs RET imm) before re-enabling; a wrong
    // calling convention corrupts the stack on the first call.
#define ENABLE_PACKET_HANDLER_TIMING 0
#if ENABLE_PACKET_HANDLER_TIMING
    InstallCheckedHook(0x00810cf0, (LPVOID)&Hook_PPacketHandlerTiming,
        (LPVOID*)&g_originalPPacketHandlerTiming, "PPacketHandlerTiming");
    InstallCheckedHook(0x00884530, (LPVOID)&Hook_SPacketHandlerTiming,
        (LPVOID*)&g_originalSPacketHandlerTiming, "SPacketHandlerTiming");
#endif

    // Present tracker: closes the perceived window at the first gameplay frame.
    HMODULE gdi32Module = GetModuleHandleA("gdi32.dll");
    void* swapBuffersAddr = gdi32Module != nullptr
        ? (void*)GetProcAddress(gdi32Module, "SwapBuffers") : nullptr;
    if (swapBuffersAddr != nullptr) {
        InstallCheckedHook((DWORD)swapBuffersAddr, (LPVOID)&Hook_SwapBuffers,
            (LPVOID*)&g_originalSwapBuffers, "SwapBuffersPresentTracker");
    } else {
        Log("Failed to install SwapBuffers present tracker: gdi32 unavailable");
    }

    // Read-only coordinator timing and scope tracking.  This detour does not alter
    // the CClientExoApp object graph or any engine ownership state.
    bool moduleChunkHookReady = InstallCheckedHook(0x007be4c0, (LPVOID)&Hook_ModuleChunkLoadCore,
        (LPVOID*)&g_originalModuleChunkLoadCore, "ModuleChunkLoadCore");

    // Load-phase attribution (log only): engine events that split a reload
    // into work / handshake / throttle wait / scripted hold.  See load_phases.md.
#if ENABLE_LOAD_PHASES_LOG
    InstallLoadPhaseHooks();
#endif

    InstallCheckedHook(0x00409ed0, (LPVOID)&Hook_LoadingScreenUpdateFrame,
        (LPVOID*)&g_originalLoadingScreenUpdateFrame, "LoadingScreenUpdateFrame");
    InstallCheckedHook(0x00533830, (LPVOID)&Hook_loadingscreenPtr,
        (LPVOID*)&g_originalLoadingScreenPtr, "loadingscreen");
    InstallCheckedHook(0x00713bf0, (LPVOID)&Hook_ResourceLoadFromArchive,
        (LPVOID*)&g_originalResourceLoadFromArchive, "ResourceLoadFromArchive");
    InstallCheckedHook(0x00729370, (LPVOID)&Hook_CExoEncapsulatedFile_ReadResourceSync,
        (LPVOID*)&g_originalCExoEncapsulatedFile_ReadResourceSync,
        "CExoEncapsulatedFile_ReadResourceSync");
    InstallCheckedHook(0x00711600, (LPVOID)&Hook_WorkerSubmitJob,
        (LPVOID*)&g_originalWorkerSubmitJob, "Worker_SubmitJob");
    InstallCheckedHook(0x00703f30, (LPVOID)&Hook_ProcessResourceQueueTransition,
        (LPVOID*)&g_originalProcessResourceQueuePtr, "ProcessResourceQueueTransition");
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
            DumpProfilerHookCounts("detach");
#if ENABLE_VISUAL_LOAD_TIMELINE
            // A load still in its black window when the player quits would
            // otherwise never emit; flush whatever the ring captured.
            if (g_visualLoad.active) {
                LARGE_INTEGER detachNow = {};
                QueryPerformanceCounter(&detachNow);
                EmitVisualLoad(detachNow, "detach");
            }
#endif
            MH_Uninitialize();
            if (g_logFile.is_open()) g_logFile.close();
            break;
    }
    return TRUE;
}
