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


typedef uint32_t (__stdcall* ResourcePacketDispatcherPtr_t)(uint32_t param1,char *param2,uint32_t param3,int param4);
ResourcePacketDispatcherPtr_t g_originalResourcePacketPtr=nullptr;

uint32_t __stdcall Hook_ResourcePacketDispatcher(uint32_t param1,char *param2,uint32_t param3,int param4){
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result=g_originalResourcePacketPtr(param1,param2,param3,param4);

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


void __fastcall Hook_ProcessResourceQueue(int param1, void*,int param2){
    auto start = std::chrono::high_resolution_clock::now();
    g_originalProcessResourceQueuePtr(param1,nullptr,param2);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ProcessResourceQueue: " + std::to_string(duration.count()) + " μs");
}

// void __fastcall Hook_ProcessResourceQueue(int param1, void*, int param2){
//     auto start = std::chrono::high_resolution_clock::now();

//     if(param2 != 0){
//         uint32_t* readPosPtr  = (uint32_t*)(param1 + 0x20000);
//         uint32_t* writePosPtr = (uint32_t*)(param1 + 0x20004);
//         char* base = (char*)param1;
//         uint32_t readPos  = *readPosPtr;
//         uint32_t writePos = *writePosPtr;

//         while(readPos != writePos){
//             if(readPos > 0xFFFF){
//                 readPos = 0;
//             }
            
//             uint32_t packetLen = *(uint32_t*)(base + readPos);
//             readPos += 4;

//             char* packet = base + readPos;
//             if (*(uint16_t*)packet == 0x4E42){  // "BN"
//                 Hook_HandleBNPacket(param1, 0, 0, packet, packetLen);
//             }
//             else{
//                 auto startelse = std::chrono::high_resolution_clock::now();
//                 void* object = *(void**)(param1 + 0x20008);
//                 void** vtable = *(void***)object;
//                 typedef void (__fastcall* VTableFunc)(void* thisPtr, void* edx, int p1, char* p2, uint32_t p3, int p4);
//                 VTableFunc func = (VTableFunc)vtable[1];
//                 func(object, 0, 0, packet, packetLen, 0);
//                 auto endelse = std::chrono::high_resolution_clock::now();
//                 auto durationelse = std::chrono::duration_cast<std::chrono::microseconds>(endelse - startelse);
//                 Log("Elsefunction: " + std::to_string(durationelse.count()) + " μs");
//                 Log("Object address: " + std::to_string((uintptr_t)object));
//                 Log("Vtable address: " + std::to_string((uintptr_t)vtable));
//                 Log("vtable[0] address: " + std::to_string((uintptr_t)vtable[0]));
//                 Log("vtable[1] address: " + std::to_string((uintptr_t)vtable[1]));
//                 Log("vtable[2] address: " + std::to_string((uintptr_t)vtable[2]));

//             }
            
//             readPos += packetLen;
//             readPos += (4 - (packetLen & 3)) & 3;  // Alignment
            
//             writePos = *writePosPtr;
//         }
//         *readPosPtr = readPos;
//     }

//     auto end = std::chrono::high_resolution_clock::now();
//     auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
//     Log("ProcessResourceQueue: " + std::to_string(duration.count()) + " μs");
// }


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
    return;
}

typedef void (__fastcall* FlushTracerPtr_t)(int *param1);
FlushTracerPtr_t g_originalFlushTracerPtr=nullptr;

void __fastcall Hook_FlushTracer(int *param1){
    return;
    // auto start = std::chrono::high_resolution_clock::now();

    // g_originalFlushTracerPtr(param1);

    // auto end = std::chrono::high_resolution_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    // Log("FlushTracer: " + std::to_string(duration.count()) + " μs");
}



typedef uint32_t (__cdecl* PpacketHandlerPtr_t)(char *param1,int param2);
PpacketHandlerPtr_t g_originalPpacketHandler= nullptr;

uint32_t __cdecl Hook_PpacketHandler(char *param1,int param2){
    auto start = std::chrono::high_resolution_clock::now();

    g_originalPpacketHandler(param1,param2);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("PpacketHandler: " + std::to_string(duration.count()) + " μs");
}

typedef uint32_t (__cdecl* SpacketHandlerPtr_t)(char *param1,int param2);
SpacketHandlerPtr_t g_originalSpacketHandler= nullptr;

uint32_t __cdecl Hook_SpacketHandler(char *param1,int param2){
    auto start = std::chrono::high_resolution_clock::now();

    g_originalSpacketHandler(param1,param2);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("SpacketHandler: " + std::to_string(duration.count()) + " μs");
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


    void* targetAddr_FlushTracer = (void*)(0x733780); //Just putting in actual address
    if (MH_CreateHook(targetAddr_FlushTracer, &Hook_FlushTracer, 
        (LPVOID*)&g_originalFlushTracerPtr) == MH_OK) {
            if (MH_EnableHook(targetAddr_FlushTracer) == MH_OK) {
                Log("FlushTracer hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

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
                Sleep(10);//oringal 3000
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