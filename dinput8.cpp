#include <Windows.h>
#include <fstream>
#include <string>
#include <chrono>
#include "minhook/include/MinHook.h"
#include <unordered_map>
#include <mutex>
#include <unordered_set>
#include <atomic>

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


typedef int (__fastcall* LoadAndInitializePtr_t)(void* thisPtr, int param1, uint32_t param2, int param3);
LoadAndInitializePtr_t g_originalLoadAndInitializePtr = nullptr;

int __fastcall Hook_LoadAndInitializePtr(void* thisPtr, int param1, uint32_t param2, int param3) {
    // Was crashing due to function actually returning an int value
    auto start = std::chrono::high_resolution_clock::now();
    int result=g_originalLoadAndInitializePtr(thisPtr, param1, param2, param3);
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


// void __fastcall Hook_ProcessResourceQueue(int param1, void*,int param2){
//     auto start = std::chrono::high_resolution_clock::now();
//     g_originalProcessResourceQueuePtr(param1,nullptr,param2);

//     auto end = std::chrono::high_resolution_clock::now();
//     auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
//     Log("ProcessResourceQueue: " + std::to_string(duration.count()) + " μs");
// }
void __fastcall Hook_ProcessResourceQueue(int param1, void*, int param2){
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t packetcounter=0;
    if(param2 != 0){
        uint32_t* readPosPtr  = (uint32_t*)(param1 + 0x20000);
        uint32_t* writePosPtr = (uint32_t*)(param1 + 0x20004);
        char* base = (char*)param1;
        uint32_t readPos  = *readPosPtr;
        uint32_t writePos = *writePosPtr;
        while(readPos != writePos){
            packetcounter++;
            Log("Number of packets: " + std::to_string(packetcounter));
            if(readPos > 0xFFFF){
                readPos = 0;
            }
            
            uint32_t packetLen = *(uint32_t*)(base + readPos);
            readPos += 4;

            char* packet = base + readPos;
            if (*(uint16_t*)packet == 0x4E42){  // "BN"
                Hook_HandleBNPacket(param1, 0, 0, packet, packetLen);
            }
            else{
                auto startelse = std::chrono::high_resolution_clock::now();
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

typedef uint32_t (__cdecl* ModuleHandlerPtr_t)(byte param1);
ModuleHandlerPtr_t g_originalModuleHandler=nullptr;

uint32_t __cdecl Hook_ModuleHandler(byte param1){
    auto start = std::chrono::high_resolution_clock::now();

    g_originalModuleHandler(param1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ModuleHandler: " + std::to_string(duration.count()) + " μs");
}

typedef uint32_t (__cdecl* GameObjUpdatePtr_t)(byte param1);
GameObjUpdatePtr_t g_originalGameObjUpdate=nullptr;

uint32_t __cdecl Hook_GameObjUpdate(byte param1){
    auto start = std::chrono::high_resolution_clock::now();

    g_originalGameObjUpdate(param1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("GameObjUpdate: " + std::to_string(duration.count()) + " μs");
}


typedef uint32_t (__cdecl* ModuleChunkLoadCorePtr_t)(int param1);
ModuleChunkLoadCorePtr_t g_originalModuleChunkLoadCore=nullptr;

uint32_t __cdecl Hook_ModuleChunkLoadCore(int param1){
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result=g_originalModuleChunkLoadCore(param1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ModuleChunkLoadCore: " + std::to_string(duration.count()) + " μs");
    return result;
}


typedef void (__cdecl* LoadingScreenUpdateFramePtr_t)(uint32_t param1,int param2,int param3);
LoadingScreenUpdateFramePtr_t g_originalLoadingScreenUpdateFrame=nullptr;

void __cdecl Hook_LoadingScreenUpdateFrame(uint32_t param1,int param2,int param3){
    auto start = std::chrono::high_resolution_clock::now();

    g_originalLoadingScreenUpdateFrame(param1,param2,param3);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("LoadingScreenUpdateFrame: " + std::to_string(duration.count()) + " μs");
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

typedef int (__cdecl* LevelLoaderAndInitializerPtr_t)(char* filename,char* param_2,int param_3,uint32_t param_4);
LevelLoaderAndInitializerPtr_t g_originalLevelLoaderAndInitializer=nullptr;

int __cdecl Hook_LevelLoaderAndInitializer(char* filename,char* param_2,int param_3,uint32_t param_4){
    auto start = std::chrono::high_resolution_clock::now();
    
    Log("Loading from file: "+std::string(filename));
    int result =g_originalLevelLoaderAndInitializer(filename,param_2,param_3,param_4);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("LevelLoaderAndInitializer: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef FILE* (__cdecl* _fopenptr_t)(char* filename, char* mode);
_fopenptr_t g_originalfopen=nullptr;

FILE* __cdecl Hook_fopen(char* filename, char* mode){
    auto start = std::chrono::high_resolution_clock::now();
    
    Log("fopening file: "+std::string(filename));

    if (filename == nullptr || *filename == '\0') {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        Log("fopen: " + std::to_string(duration.count()) + " μs");
        return nullptr; 
    }

    FILE* result=g_originalfopen(filename,mode);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("fopen: " + std::to_string(duration.count()) + " μs");
    return result;
}


typedef uint32_t* (__cdecl* DebugMenuContructorPtr_t)(uint32_t param1);
DebugMenuContructorPtr_t g_originalDebugMenuConstructor=nullptr;
uint32_t* __cdecl Hook_DebugMenuConstructor(uint32_t param1){
        auto start = std::chrono::high_resolution_clock::now();
    
    uint32_t* result =g_originalDebugMenuConstructor(param1);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("DebugMenuConstructor: " + std::to_string(duration.count()) + " μs");
    return 0;
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

typedef void (__cdecl* ModuleDirectoryScannerPtr_t)(int param_1,uint32_t param_2,uint32_t param_3,int param_4,int param_5);
ModuleDirectoryScannerPtr_t g_originalModuleDirectoryScanner = nullptr;

void __cdecl Hook_ModuleDirectoryScanner(int param_1,uint32_t param_2,uint32_t param_3,int param_4,int param_5){
    auto start = std::chrono::high_resolution_clock::now();

    g_originalModuleDirectoryScanner(param_1,param_2,param_3,param_4,param_5);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ModuleDirectoryScanner: " + std::to_string(duration.count()) + " μs");
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

    void* targetAddr_ModuleChunkLoadCore = (void*)(0x7BE4C0); //Just putting in actual address
    if (MH_CreateHook(targetAddr_ModuleChunkLoadCore, &Hook_ModuleChunkLoadCore, 
        (LPVOID*)&g_originalModuleChunkLoadCore) == MH_OK) {
            if (MH_EnableHook(targetAddr_ModuleChunkLoadCore) == MH_OK) {
                Log("ModuleChunkLoadCore hook installed successfully");
            } else {
                Log("Failed to enable hook");
            }
        } else {
            Log("Failed to create hook");
        }

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