// dinput8.dll proxy for the Steam build of KOTOR 2 (swkotor2.exe).  Forwards
// DirectInput8Create to the system dinput8.dll and installs the load-time
// enhancements below with MinHook.  Engine notes behind every address:
// https://github.com/tayloroxelgren/kotor2-engine-internals
#include <Windows.h>
#include <fstream>
#include <string>
#include <cstdint>
#include <cstring>
#include <mutex>
#include "minhook/include/MinHook.h"

// Build modes (see build.bat):
//   default   LOGGING_ENABLED=1: hook installs are logged to kotor2_log.txt.
//   -release  LOGGING_ENABLED=0: same hooks, writes nothing.
//   -nohooks  NO_HOOKS: plain pass-through proxy.  MinHook is never
//             initialised and no hooks are installed; use it for an
//             unmodified-engine baseline.
#ifdef NO_HOOKS
#define LOGGING_ENABLED 0
#endif
#ifndef LOGGING_ENABLED
#define LOGGING_ENABLED 1
#endif

// Enhancement toggles.  Set any to 0 to leave that part of the engine alone.

// Skip intros: the startup splash/movie preload (PreloadInitialAssetsWrapper
// 0x0073f050) becomes a no-op.
#ifndef SKIP_PRELOAD_INITIAL_ASSETS_WRAPPER
#define SKIP_PRELOAD_INITIAL_ASSETS_WRAPPER 1
#endif
// Forced area streaming.  After a load the server streams the client's object
// data in ~2 KB messages, at most one per 200 ms
// (Server_UpdateClient_Throttle200ms 0x00537590), and the loading screen stays
// up until the last one lands.  Passing the engine's own force flag while the
// player is still streaming sends one per tick.
#ifndef FORCE_AREA_STREAM_DURING_LOAD
#define FORCE_AREA_STREAM_DURING_LOAD 1
#endif
// GPU mipmap generation.  Texture_UploadToGL (0x00433cd0) lets the driver
// build mipmaps only when GL_CanUseHardwareMipmapGen (0x00484a60, its only
// caller) returns 1; otherwise gluBuild2DMipmaps builds every level on the CPU.
// The check fails on every modern driver because GL_DetectExtensions sets a
// disqualifying bit (0x100000) for GL_ARB_fragment_program.  1 = return 1 when
// GL_SGIS_generate_mipmap is present.
#ifndef FORCE_HW_MIPMAP_GEN
#define FORCE_HW_MIPMAP_GEN 1
#endif

// DirectInput8 proxy
typedef HRESULT(WINAPI *DICREATE)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DICREATE realCreate = nullptr;

// Logging
std::ofstream g_logFile;
std::mutex g_logMutex;

void Log(const std::string& msg) {
#if LOGGING_ENABLED
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile.is_open()) {
        SYSTEMTIME st;
        GetSystemTime(&st);
        g_logFile << st.wHour << ":" << st.wMinute << ":" << st.wSecond
                  << " - " << msg << std::endl;
    }
#else
    (void)msg;
#endif
}

// ---------------------------------------------------------------------------
// Hook helpers
// ---------------------------------------------------------------------------
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

// Installs one detour, but only if the target's prologue matches the Steam
// build, so a different executable never receives a detour.
static bool InstallCheckedHook(DWORD address, const BYTE* signature, size_t signatureSize,
                               LPVOID detour, LPVOID* original, const char* name) {
    if (!MatchesExecutableBytes(address, signature, signatureSize)) {
        Log(std::string("Signature mismatch, hook skipped: ") + name);
        return false;
    }
    LPVOID target = (LPVOID)address;
    if (MH_CreateHook(target, detour, original) != MH_OK) {
        Log(std::string("Failed to create hook: ") + name);
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log(std::string("Failed to enable hook: ") + name);
        return false;
    }
    Log(std::string("Installed hook: ") + name);
    return true;
}

// ---------------------------------------------------------------------------
// Skip intros
// ---------------------------------------------------------------------------
static const BYTE kSigPreload[] = {
    0x55,0x8b,0xec,0x51,0x89,0x4d,0xfc,0x8b,0x45,0xfc,0x8b,0x48,0x04
};
typedef void (__fastcall* PreloadInitialAssetsWrapperPtr_t)(uint32_t param1);
static PreloadInitialAssetsWrapperPtr_t g_origPreloadInitialAssetsWrapper = nullptr;

void __fastcall Hook_PreloadInitialAssetsWrapper(uint32_t) {
}

// ---------------------------------------------------------------------------
// Forced area streaming
// ---------------------------------------------------------------------------
// player+0x24 is the player's area-load state.  It is 1 while the area streams
// and Server_HandleAreaMsg (0x00660600) flips it to 2 on the client's
// area-loaded ack (P(4,3)), so forcing only while it is 1 covers exactly the
// throttle-paced stream and leaves gameplay at 200 ms.  The force flag skips
// only the 200 ms time compare (0x00537689); the function's own "is this
// player updatable yet" checks (+0x7c, creature+0x350) still run first.  The
// engine passes force=1 itself from FUN_0089fbd0.
static const BYTE kSigUpdateClient[] = {0x55,0x8b,0xec,0x83,0xec,0x54};
typedef uint32_t (__thiscall* UpdateClientPtr_t)(void* thisPtr, uint32_t player,
    uint32_t force, uint32_t timeLo, uint32_t timeHi);
static UpdateClientPtr_t g_origUpdateClient = nullptr;

// thiscall, RET 0x10 (player, force, timeLo, timeHi).
uint32_t __fastcall Hook_UpdateClientForce(
    void* thisPtr, void* edx, uint32_t player, uint32_t force, uint32_t timeLo,
    uint32_t timeHi) {
    if (force != 1 && player != 0 && *(unsigned char*)(player + 0x24) == 1) {
        force = 1;
    }
    return g_origUpdateClient(thisPtr, player, force, timeLo, timeHi);
}

// ---------------------------------------------------------------------------
// GPU mipmap generation
// ---------------------------------------------------------------------------
// GL_CanUseHardwareMipmapGen 0x00484a60: cdecl, no args, plain RET; result
// cached in 0x009f6134.  A 0 becomes 1 when the GL_SGIS_generate_mipmap bit
// (mask at 0x009f60b8) is set in the detected-extension word (0x00a32df8).
static const BYTE kSigMipCheck[] = {
    0x55,0x8b,0xec,0x83,0xec,0x0c,0x83,0x3d,0x34,0x61,0x9f,0x00
};
typedef int (__cdecl* MipCheckPtr_t)();
static MipCheckPtr_t g_origMipCheck = nullptr;

int __cdecl Hook_MipCheckForce() {
    int r = g_origMipCheck();
    if (r == 0) {
        const DWORD haveExt = *(const DWORD*)0x00a32df8; //Extensions available according to driver
        const DWORD sgisBit = *(const DWORD*)0x009f60b8; //  flag bit for GL_SGIS_generate_mipmap
        if (sgisBit != 0 && (haveExt & sgisBit) == sgisBit) {
            r = 1;
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------
void InstallHooks() {
    if ((DWORD)GetModuleHandle(nullptr) != 0x00400000) {
        Log("Hooks not installed: unexpected swkotor2.exe image base");
        return;
    }
    if (MH_Initialize() != MH_OK) {
        Log("MinHook init failed");
        return;
    }
    Log("Enhancements: skip_intros=" + std::to_string(SKIP_PRELOAD_INITIAL_ASSETS_WRAPPER) +
        " stream_force=" + std::to_string(FORCE_AREA_STREAM_DURING_LOAD) +
        " hw_mipmaps=" + std::to_string(FORCE_HW_MIPMAP_GEN));

#if SKIP_PRELOAD_INITIAL_ASSETS_WRAPPER
    InstallCheckedHook(0x0073f050, kSigPreload, sizeof(kSigPreload),
        (LPVOID)&Hook_PreloadInitialAssetsWrapper,
        (LPVOID*)&g_origPreloadInitialAssetsWrapper, "PreloadInitialAssetsWrapper");
#endif
#if FORCE_AREA_STREAM_DURING_LOAD
    InstallCheckedHook(0x00537590, kSigUpdateClient, sizeof(kSigUpdateClient),
        (LPVOID)&Hook_UpdateClientForce,
        (LPVOID*)&g_origUpdateClient, "Server_UpdateClient_Throttle200ms");
#endif
#if FORCE_HW_MIPMAP_GEN
    InstallCheckedHook(0x00484a60, kSigMipCheck, sizeof(kSigMipCheck),
        (LPVOID)&Hook_MipCheckForce,
        (LPVOID*)&g_origMipCheck, "GL_CanUseHardwareMipmapGen");
#endif
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
#if LOGGING_ENABLED
            g_logFile.open("kotor2_log.txt", std::ios::app);
#endif
#ifndef NO_HOOKS
            // Install from a thread so DllMain returns before MinHook runs.
            CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                Sleep(10);
                InstallHooks();
                return 0;
            }, nullptr, 0, nullptr);
#endif
            break;

        case DLL_PROCESS_DETACH:
#ifndef NO_HOOKS
            MH_Uninitialize();
#endif
            if (g_logFile.is_open()) g_logFile.close();
            break;
    }
    return TRUE;
}
