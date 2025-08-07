#include <Windows.h>
#include <fstream>
#include <string>
#include <chrono>
#include "minhook/include/MinHook.h"
#include <unordered_map>
#include <mutex>
#include <unordered_set>

// DirectInput8 proxy
typedef HRESULT(WINAPI *DICREATE)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DICREATE realCreate = nullptr;

// Simple logging
std::ofstream g_logFile;
void Log(const std::string& msg) {
    if (g_logFile.is_open()) {
        SYSTEMTIME st;
        GetSystemTime(&st);
        g_logFile << st.wHour << ":" << st.wMinute << ":" << st.wSecond 
                  << " - " << msg << std::endl;
        g_logFile.flush();
    }
}



typedef void (__fastcall* LoadAndInitializePtr_t)(void* thisPtr, int param1, uint32_t param2, int param3);
LoadAndInitializePtr_t g_originalLoadAndInitializePtr = nullptr;

void __fastcall Hook_LoadAndInitializePtr(void* thisPtr, int param1, uint32_t param2, int param3) {
    // Timing seems to mess up this function
    // auto start = std::chrono::high_resolution_clock::now();
    g_originalLoadAndInitializePtr(thisPtr, param1, param2, param3);
    // auto end = std::chrono::high_resolution_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    // Log("LoadAndInitialize: " + std::to_string(duration.count()) + " μs");
    
}

// LoadingScreen
typedef int (__fastcall* loadingscreenPtr_t)(int param1);
loadingscreenPtr_t g_originalLoadingScreenPtr = nullptr;


int __fastcall Hook_loadingscreenPtr(int param1) {
    auto start = std::chrono::high_resolution_clock::now();
    

    int result = g_originalLoadingScreenPtr(param1);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Log with more detail
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

typedef void (__fastcall* ProcessResourceQueuePtr_t)(int param1, void*,int param2);
ProcessResourceQueuePtr_t g_originalProcessResourceQueuePtr = nullptr;


static std::unordered_set<uint64_t> g_seenHashes;

inline uint64_t Hash64(const void* p, uint32_t len) {
    uint64_t h = 0xcbf29ce484222325ULL;
    const uint8_t* c = static_cast<const uint8_t*>(p);
    for (uint32_t i = 0; i < len; ++i)
        h = (h ^ c[i]) * 0x100000001b3ULL;
    return h ^ len;
}

// void __fastcall Hook_ProcessResourceQueue(int param1, void*,int param2){
//     auto start = std::chrono::high_resolution_clock::now();
//     g_originalProcessResourceQueuePtr(param1,nullptr,param2);

//     auto end = std::chrono::high_resolution_clock::now();
//     auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
//     Log("ProcessResourceQueue: " + std::to_string(duration.count()) + " μs");
// }

void __fastcall Hook_ProcessResourceQueue(int param1, void*, int param2){
    auto start = std::chrono::high_resolution_clock::now();

    if(param2 != 0){
        uint32_t* readPosPtr  = (uint32_t*)(param1 + 0x20000);
        uint32_t* writePosPtr = (uint32_t*)(param1 + 0x20004);
        char* base = (char*)param1;
        uint32_t readPos  = *readPosPtr;
        uint32_t writePos = *writePosPtr;

        while(readPos != writePos){
            if(readPos > 0xFFFF){
                readPos = 0;
            }
            
            uint32_t packetLen = *(uint32_t*)(base + readPos);
            readPos += 4;

            char* packet = base + readPos;
            if (*(uint16_t*)packet == 0x4E42){  // "BN"
                g_originalHandleBNPacketPtr(param1, 0, 0, packet, packetLen);
            }
            else{
                void* object = *(void**)(param1 + 0x20008);
                void** vtable = *(void***)object;
                typedef void (__fastcall* VTableFunc)(void* thisPtr, void* edx, int p1, char* p2, uint32_t p3, int p4);
                VTableFunc func = (VTableFunc)vtable[1];
                func(object, 0, 0, packet, packetLen, 0);
            }
            
            readPos += packetLen;
            readPos += (4 - (packetLen & 3)) & 3;  // Alignment
            
            writePos = *writePosPtr;
        }
        *readPosPtr = readPos;
    }

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

void __fastcall Hook_PreloadInitialAssetsWrapper(uint32_t param1){
    g_originalPreloadInitialAssetsWrapperPtr(param1);
}


void InstallHook() {
    
    if (MH_Initialize() != MH_OK) {
        Log("MinHook init failed");
        return;
    }
    
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


    void* targetAddr_PreloadInitialAssetsWrapper = (void*)(0x73f050); //Just putting in actual address
    if (MH_CreateHook(targetAddr_PreloadInitialAssetsWrapper, &Hook_PreloadInitialAssetsWrapper, 
        (LPVOID*)&g_originalPreloadInitialAssetsWrapperPtr) == MH_OK) {
            if (MH_EnableHook(targetAddr_PreloadInitialAssetsWrapper) == MH_OK) {
                Log("PreloadInitialAssetsWrapper hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }
        
        


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
                Sleep(1000);//oringal 3000
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