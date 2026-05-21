#include <Windows.h>
#include <fstream>
#include <string>
#include <chrono>
#include "minhook/include/MinHook.h"
#include <Windows.h>

#define LOGGING_ENABLED 1
#define LOG_LOADSCREEN_ONLY 0
#define SKIP_PRELOAD_INITIAL_ASSETS_WRAPPER 1
#define SKIP_LOADING_SCREEN_UPDATE_FRAME_IN_MODULE_CHUNK_LOAD_CORE 0
#define HOOK_GUI_DEEP_GFF_TIMING 0
#define HOOK_APPSTATE_GET_GUI_CONTEXT_TIMING 0
#define HOOK_APPSTATE_GET_LOAD_PROGRESS_BYTE_TIMING 0
#define HOOK_RUNTIME_FLOAT_TO_INT_ST0_TIMING 0
#define HOOK_APPSTATE_SET_LOAD_BAR_VALUE_TIMING 0
#define HOOK_CSWGUIFADE_SET_TRANSITION_STATE_TIMING 0
#define HOOK_LOADING_SCREEN_FADE_UPDATE_FRAME_TIMING 0

// DirectInput8 proxy
typedef HRESULT(WINAPI *DICREATE)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DICREATE realCreate = nullptr;

// Simple logging
std::ofstream g_logFile;

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
        if (g_logFile.is_open() && ShouldLogMessage(msg)) {
            SYSTEMTIME st;
            GetSystemTime(&st);
            g_logFile << st.wHour << ":" << st.wMinute << ":" << st.wSecond 
                      << " - " << msg << std::endl;
            g_logFile.flush();
        }
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
            // Not currently logging packets
            // packetcounter++;
            // Log("Number of packets: " + std::to_string(packetcounter));
            if(readPos > 0xFFFF){
                readPos = 0;
            }
            
            uint32_t packetLen = *(uint32_t*)(base + readPos);
            readPos += 4;

            uint32_t payloadStart = readPos;
            char* packet = base + payloadStart;
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
            
            readPos = payloadStart + packetLen + (4 - (packetLen % 4));
            
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

// Skips splash screens
void __fastcall Hook_PreloadInitialAssetsWrapper(uint32_t param1){
#if SKIP_PRELOAD_INITIAL_ASSETS_WRAPPER
    return;
#else
    g_originalPreloadInitialAssetsWrapperPtr(param1);
#endif
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

    uint32_t result=g_originalPpacketHandler(param1,param2);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("PpacketHandler: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__cdecl* SpacketHandlerPtr_t)(char *param1,int param2);
SpacketHandlerPtr_t g_originalSpacketHandler= nullptr;

uint32_t __cdecl Hook_SpacketHandler(char *param1,int param2){
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result=g_originalSpacketHandler(param1,param2);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("SpacketHandler: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__cdecl* ModuleHandlerPtr_t)(byte param1);
ModuleHandlerPtr_t g_originalModuleHandler=nullptr;

uint32_t __cdecl Hook_ModuleHandler(byte param1){
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result=g_originalModuleHandler(param1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ModuleHandler: " + std::to_string(duration.count()) + " μs");
    
    return result;
}

typedef uint32_t (__cdecl* GameObjUpdatePtr_t)(byte param1);
GameObjUpdatePtr_t g_originalGameObjUpdate=nullptr;

uint32_t __cdecl Hook_GameObjUpdate(byte param1){
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result=g_originalGameObjUpdate(param1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("GameObjUpdate: " + std::to_string(duration.count()) + " μs");
    return result;
}

thread_local int g_moduleChunkLoadCoreDepth = 0;

// Game's own _free (0x0091c6b5), matched to the _malloc inside AllocateMemoryOrThrow.
// Used to release pre-allocated blocks that we skip constructing, avoiding heap leaks.
// Must use the game's CRT free — calling our DLL's free on game-malloc'd memory corrupts the heap.
typedef void (*GameFree_t)(void*);
static const GameFree_t GameFree = (GameFree_t)(0x0091c6b5);

typedef uint32_t (__fastcall* ModuleChunkLoadCorePtr_t)(int param1);
ModuleChunkLoadCorePtr_t g_originalModuleChunkLoadCore = nullptr;

uint32_t __fastcall Hook_ModuleChunkLoadCore(int param1, void* edx) {
    auto start = std::chrono::high_resolution_clock::now();

    g_moduleChunkLoadCoreDepth++;
    uint32_t result = g_originalModuleChunkLoadCore(param1);
    g_moduleChunkLoadCoreDepth--;

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ModuleChunkLoadCore: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef void (__thiscall* Texture_ApplyTXIAndBuildControllerPtr_t)(int* thisPtr, uint32_t textureName);
Texture_ApplyTXIAndBuildControllerPtr_t g_originalTexture_ApplyTXIAndBuildController = nullptr;

void __fastcall Hook_Texture_ApplyTXIAndBuildController(int* thisPtr, void* edxDummy, uint32_t textureName) {
    auto start = std::chrono::high_resolution_clock::now();

    g_originalTexture_ApplyTXIAndBuildController(thisPtr, textureName);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("Texture_ApplyTXIAndBuildController: " + std::to_string(duration.count()) + " Î¼s");
}

typedef void (__thiscall* Texture_ApplyTXIBlendingModePtr_t)(int* thisPtr, uint32_t textureName, int materialState);
Texture_ApplyTXIBlendingModePtr_t g_originalTexture_ApplyTXIBlendingMode = nullptr;

void __fastcall Hook_Texture_ApplyTXIBlendingMode(int* thisPtr, void* edxDummy, uint32_t textureName, int materialState) {
    auto start = std::chrono::high_resolution_clock::now();

    g_originalTexture_ApplyTXIBlendingMode(thisPtr, textureName, materialState);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("Texture_ApplyTXIBlendingMode: " + std::to_string(duration.count()) + " Î¼s");
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
    auto start = std::chrono::high_resolution_clock::now();

    // no oping this
    // void* result = g_originalCSWGuiLoadModuleDebugMenu_Ctor(thisPtr,edx,param1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiLoadModuleDebugMenu_Ctor: " + std::to_string(duration.count()) + " μs");
    return 0;
}

typedef uint32_t* (__fastcall* CSWGuiPowersFeatsSkillsDebugMenu_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiPowersFeatsSkillsDebugMenu_CtorPtr_t g_originalCSWGuiPowersFeatsSkillsDebugMenu_Ctor = nullptr;

uint32_t* __fastcall Hook_CSWGuiPowersFeatsSkillsDebugMenu_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();

    // no oping this
    // uint32_t* result = g_originalCSWGuiPowersFeatsSkillsDebugMenu_Ctor(thisPtr,edx,param1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiPowersFeatsSkillsDebugMenu_Ctor: " + std::to_string(duration.count()) + " μs");
    return 0;
}

typedef void* (__fastcall* CSWGuiDialogCinematic_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiDialogCinematic_CtorPtr_t g_originalCSWGuiDialogCinematic_Ctor = nullptr;

void* __fastcall Hook_CSWGuiDialogCinematic_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();

    void* result = g_originalCSWGuiDialogCinematic_Ctor(thisPtr,edx,param1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiDialogCinematic_Ctor: " + std::to_string(duration.count()) + " μs");
    return 0;
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
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t* result = g_originalCSWGuiCreateDebugItemSubMenu_Ctor(thisPtr,edx,param1);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiCreateDebugItemSubMenu_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
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

typedef uint32_t* (__fastcall* CSWGuiMessageBoxVariant_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiMessageBoxVariant_CtorPtr_t g_originalCSWGuiMessageBoxVariant_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiMessageBoxVariant_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiMessageBoxVariant_Ctor(thisPtr,edx,param1);
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
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiInGameEquip_Ctor(thisPtr,edx,param1);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiInGameEquip_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiInGameInventory_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiInGameInventory_CtorPtr_t g_originalCSWGuiInGameInventory_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiInGameInventory_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiInGameInventory_Ctor(thisPtr,edx,param1);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiInGameInventory_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiInGameCharacter_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiInGameCharacter_CtorPtr_t g_originalCSWGuiInGameCharacter_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiInGameCharacter_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiInGameCharacter_Ctor(thisPtr,edx,param1);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiInGameCharacter_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
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
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiInGameMap_Ctor(thisPtr,edx,param1);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiInGameMap_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiInGameAbilities_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiInGameAbilities_CtorPtr_t g_originalCSWGuiInGameAbilities_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiInGameAbilities_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiInGameAbilities_Ctor(thisPtr,edx,param1);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiInGameAbilities_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiInGameJournal_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiInGameJournal_CtorPtr_t g_originalCSWGuiInGameJournal_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiInGameJournal_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiInGameJournal_Ctor(thisPtr,edx,param1);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiInGameJournal_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiInGameOptions_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiInGameOptions_CtorPtr_t g_originalCSWGuiInGameOptions_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiInGameOptions_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiInGameOptions_Ctor(thisPtr,edx,param1);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiInGameOptions_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t* (__fastcall* CSWGuiPartySelection_CtorPtr_t)(void* thisPtr, void* edx, uint32_t param1);
CSWGuiPartySelection_CtorPtr_t g_originalCSWGuiPartySelection_Ctor = nullptr;
uint32_t* __fastcall Hook_CSWGuiPartySelection_Ctor(void* thisPtr, void* edx, uint32_t param1){
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t* result = g_originalCSWGuiPartySelection_Ctor(thisPtr,edx,param1);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiPartySelection_Ctor: " + std::to_string(duration.count()) + " μs");
    return result;
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

typedef void (__cdecl* LoadingScreenUpdateFramePtr_t)(uint32_t param1,int param2,int param3);
LoadingScreenUpdateFramePtr_t g_originalLoadingScreenUpdateFrame=nullptr;

void __cdecl Hook_LoadingScreenUpdateFrame(uint32_t param1,int param2,int param3){
    auto start = std::chrono::high_resolution_clock::now();

#if SKIP_LOADING_SCREEN_UPDATE_FRAME_IN_MODULE_CHUNK_LOAD_CORE
    bool calledFromModuleChunkLoadCore = (g_moduleChunkLoadCoreDepth > 0);
    // Skipping LoadingScreenUpdateFrame when called from ModuleChunkLoadCore with param2 == 0, as this seems to be redundant.
    bool skipLoadingScreenUpdate = calledFromModuleChunkLoadCore && param2 == 0;

    if (skipLoadingScreenUpdate) {
        Log("LoadingScreenUpdateFrame: skipped inside ModuleChunkLoadCore");
    }
    else{
        g_originalLoadingScreenUpdateFrame(param1, param2, param3);
    }
#else
    g_originalLoadingScreenUpdateFrame(param1, param2, param3);
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
    Log("AppState_GetGuiContext: " + std::to_string(duration.count()) + " Î¼s");
    return result;
}

typedef unsigned char (__fastcall* AppState_GetLoadProgressBytePtr_t)(void* thisPtr, void* edx, int index);
AppState_GetLoadProgressBytePtr_t g_originalAppState_GetLoadProgressByte = nullptr;

unsigned char __fastcall Hook_AppState_GetLoadProgressByte(void* thisPtr, void* edx, int index){
    auto start = std::chrono::high_resolution_clock::now();
    unsigned char result = g_originalAppState_GetLoadProgressByte(thisPtr, edx, index);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("AppState_GetLoadProgressByte: " + std::to_string(duration.count()) + " Î¼s");
    return result;
}

typedef int (__cdecl* Runtime_FloatToInt_ST0Ptr_t)();
Runtime_FloatToInt_ST0Ptr_t g_originalRuntime_FloatToInt_ST0 = nullptr;

int __cdecl Hook_Runtime_FloatToInt_ST0(){
    auto start = std::chrono::high_resolution_clock::now();
    int result = g_originalRuntime_FloatToInt_ST0();
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("Runtime_FloatToInt_ST0: " + std::to_string(duration.count()) + " Î¼s");
    return result;
}

typedef void (__fastcall* AppState_SetLoadBarValuePtr_t)(void* thisPtr, void* edx, int value, int updateFlag);
AppState_SetLoadBarValuePtr_t g_originalAppState_SetLoadBarValue = nullptr;

void __fastcall Hook_AppState_SetLoadBarValue(void* thisPtr, void* edx, int value, int updateFlag){
    auto start = std::chrono::high_resolution_clock::now();
    g_originalAppState_SetLoadBarValue(thisPtr, edx, value, updateFlag);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("AppState_SetLoadBarValue: " + std::to_string(duration.count()) + " Î¼s");
}

typedef void (__fastcall* CSWGuiFade_SetTransitionStatePtr_t)(void* thisPtr, void* edx, int mode, uint32_t progress, uint32_t duration, uint32_t* targetColor);
CSWGuiFade_SetTransitionStatePtr_t g_originalCSWGuiFade_SetTransitionState = nullptr;

void __fastcall Hook_CSWGuiFade_SetTransitionState(void* thisPtr, void* edx, int mode, uint32_t progress, uint32_t duration, uint32_t* targetColor){
    auto start = std::chrono::high_resolution_clock::now();
    g_originalCSWGuiFade_SetTransitionState(thisPtr, edx, mode, progress, duration, targetColor);
    auto end = std::chrono::high_resolution_clock::now();
    auto durationTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CSWGuiFade_SetTransitionState: " + std::to_string(durationTime.count()) + " Î¼s");
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
    // attempting to just have it do nothing
    // ignoring this works fine but it only saves like 4ms per load
    // uint32_t* result =g_originalDebugMenuConstructor(param1);
    
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

typedef void (__fastcall* GUI_FindAndBindControlByTagPtr_t)(int thisPtr, void* edxDummy, int param1, int gffPtr, int controlsListPtr, void* requestedTag);
GUI_FindAndBindControlByTagPtr_t g_originalGUI_FindAndBindControlByTag = nullptr;

void __fastcall Hook_GUI_FindAndBindControlByTag(int thisPtr, void* edxDummy, int param1, int gffPtr, int controlsListPtr, void* requestedTag) {
    auto start = std::chrono::high_resolution_clock::now();

    g_originalGUI_FindAndBindControlByTag(thisPtr, edxDummy, param1, gffPtr, controlsListPtr, requestedTag);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("GUI_FindAndBindControlByTag: " + std::to_string(duration.count()) + " μs");
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

typedef void (__fastcall* GUI_InitWidgetFromGFFPtr_t)(int* thisPtr, void* edxDummy, int param2, int param3);
GUI_InitWidgetFromGFFPtr_t g_originalGUI_InitWidgetFromGFF = nullptr;

void __fastcall Hook_GUI_InitWidgetFromGFF(int* thisPtr, void* edxDummy, int param2, int param3){
    auto start = std::chrono::high_resolution_clock::now();

    g_originalGUI_InitWidgetFromGFF(thisPtr, edxDummy, param2, param3);

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

int __fastcall Hook_GFF_LookupFieldLabelByName(int gffPtr, void* edxDummy, int structPtr, const char* fieldName) {
#if HOOK_GUI_DEEP_GFF_TIMING
    auto start = std::chrono::high_resolution_clock::now();
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

typedef int (__thiscall* ResourceLoadFromArchivePtr_t)(int thisPtr, int* resourceEntry, int asyncFlag);
ResourceLoadFromArchivePtr_t g_originalResourceLoadFromArchive = nullptr;

int __fastcall Hook_ResourceLoadFromArchive(int thisPtr, void* edxDummy, int* resourceEntry, int asyncFlag) {
    auto start = std::chrono::high_resolution_clock::now();

    int result = g_originalResourceLoadFromArchive(thisPtr, resourceEntry, asyncFlag);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ResourceLoadFromArchive: " + std::to_string(duration.count()) + " μs");
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

typedef uint32_t (__thiscall* CExoResFile_ReadResourceSyncPtr_t)(int thisPtr, uint32_t resourceId, int buffer, uint32_t size);
CExoResFile_ReadResourceSyncPtr_t g_originalCExoResFile_ReadResourceSync = nullptr;

uint32_t __fastcall Hook_CExoResFile_ReadResourceSync(int thisPtr, void* edxDummy, uint32_t resourceId, int buffer, uint32_t size) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoResFile_ReadResourceSync(thisPtr, resourceId, buffer, size);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResFile_ReadResourceSync: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__thiscall* CExoResFile_ReadResourceAsyncPtr_t)(int thisPtr, uint32_t resourceId, int buffer, uint32_t size);
CExoResFile_ReadResourceAsyncPtr_t g_originalCExoResFile_ReadResourceAsync = nullptr;

uint32_t __fastcall Hook_CExoResFile_ReadResourceAsync(int thisPtr, void* edxDummy, uint32_t resourceId, int buffer, uint32_t size) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoResFile_ReadResourceAsync(thisPtr, resourceId, buffer, size);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResFile_ReadResourceAsync: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__thiscall* CExoResFile_OpenSyncHandlePtr_t)(int thisPtr, uint32_t resourceType);
CExoResFile_OpenSyncHandlePtr_t g_originalCExoResFile_OpenSyncHandle = nullptr;

uint32_t __fastcall Hook_CExoResFile_OpenSyncHandle(int thisPtr, void* edxDummy, uint32_t resourceType) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoResFile_OpenSyncHandle(thisPtr, resourceType);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResFile_OpenSyncHandle: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__thiscall* CExoResFile_OpenAsyncHandlePtr_t)(int thisPtr, uint32_t resourceType);
CExoResFile_OpenAsyncHandlePtr_t g_originalCExoResFile_OpenAsyncHandle = nullptr;

uint32_t __fastcall Hook_CExoResFile_OpenAsyncHandle(int thisPtr, void* edxDummy, uint32_t resourceType) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoResFile_OpenAsyncHandle(thisPtr, resourceType);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResFile_OpenAsyncHandle: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef void (__thiscall* ArchiveReaderShared_AddRefSyncOpenPtr_t)(int* thisPtr);
ArchiveReaderShared_AddRefSyncOpenPtr_t g_originalArchiveReaderShared_AddRefSyncOpen = nullptr;

void __fastcall Hook_ArchiveReaderShared_AddRefSyncOpen(int* thisPtr, void* edxDummy) {
    auto start = std::chrono::high_resolution_clock::now();

    g_originalArchiveReaderShared_AddRefSyncOpen(thisPtr);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("ArchiveReaderShared_AddRefSyncOpen: " + std::to_string(duration.count())+ " μs");
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

    g_originalCExoEncapsulatedFile_ReleaseSyncClose(thisPtr);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoEncapsulatedFile_ReleaseSyncClose: " + std::to_string(duration.count()) + " μs");
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

typedef uint32_t (__thiscall* CExoEncapsulatedFile_ReadResourceSyncPtr_t)(int thisPtr, uint32_t resourceId, int buffer, uint32_t size);
CExoEncapsulatedFile_ReadResourceSyncPtr_t g_originalCExoEncapsulatedFile_ReadResourceSync = nullptr;

uint32_t __fastcall Hook_CExoEncapsulatedFile_ReadResourceSync(int thisPtr, void* edxDummy, uint32_t resourceId, int buffer, uint32_t size) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoEncapsulatedFile_ReadResourceSync(thisPtr, resourceId, buffer, size);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoEncapsulatedFile_ReadResourceSync: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__thiscall* CExoEncapsulatedFile_ReadResourceAsyncPtr_t)(int thisPtr, uint32_t resourceId, int buffer, uint32_t size);
CExoEncapsulatedFile_ReadResourceAsyncPtr_t g_originalCExoEncapsulatedFile_ReadResourceAsync = nullptr;

uint32_t __fastcall Hook_CExoEncapsulatedFile_ReadResourceAsync(int thisPtr, void* edxDummy, uint32_t resourceId, int buffer, uint32_t size) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoEncapsulatedFile_ReadResourceAsync(thisPtr, resourceId, buffer, size);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoEncapsulatedFile_ReadResourceAsync: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__thiscall* CExoEncapsulatedFile_OpenSyncHandlePtr_t)(int thisPtr, uint32_t resourceType);
CExoEncapsulatedFile_OpenSyncHandlePtr_t g_originalCExoEncapsulatedFile_OpenSyncHandle = nullptr;

uint32_t __fastcall Hook_CExoEncapsulatedFile_OpenSyncHandle(int thisPtr, void* edxDummy, uint32_t resourceType) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoEncapsulatedFile_OpenSyncHandle(thisPtr, resourceType);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoEncapsulatedFile_OpenSyncHandle: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__thiscall* CExoEncapsulatedFile_OpenAsyncHandlePtr_t)(int thisPtr, uint32_t resourceType);
CExoEncapsulatedFile_OpenAsyncHandlePtr_t g_originalCExoEncapsulatedFile_OpenAsyncHandle = nullptr;

uint32_t __fastcall Hook_CExoEncapsulatedFile_OpenAsyncHandle(int thisPtr, void* edxDummy, uint32_t resourceType) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoEncapsulatedFile_OpenAsyncHandle(thisPtr, resourceType);

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

typedef uint32_t (__thiscall* CExoResourceImageFile_ReadResourceSyncPtr_t)(int thisPtr, uint32_t resourceId, int buffer, uint32_t size);
CExoResourceImageFile_ReadResourceSyncPtr_t g_originalCExoResourceImageFile_ReadResourceSync = nullptr;

uint32_t __fastcall Hook_CExoResourceImageFile_ReadResourceSync(int thisPtr, void* edxDummy, uint32_t resourceId, int buffer, uint32_t size) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoResourceImageFile_ReadResourceSync(thisPtr, resourceId, buffer, size);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResourceImageFile_ReadResourceSync: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__thiscall* CExoResourceImageFile_ReadResourceAsyncPtr_t)(int thisPtr, uint32_t resourceId, int buffer, uint32_t size);
CExoResourceImageFile_ReadResourceAsyncPtr_t g_originalCExoResourceImageFile_ReadResourceAsync = nullptr;

uint32_t __fastcall Hook_CExoResourceImageFile_ReadResourceAsync(int thisPtr, void* edxDummy, uint32_t resourceId, int buffer, uint32_t size) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoResourceImageFile_ReadResourceAsync(thisPtr, resourceId, buffer, size);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResourceImageFile_ReadResourceAsync: " + std::to_string(duration.count()) + " μs");
    return result;
}

typedef uint32_t (__thiscall* CExoResourceImageFile_LoadImagePtr_t)(int thisPtr, uint32_t resourceType);
CExoResourceImageFile_LoadImagePtr_t g_originalCExoResourceImageFile_LoadImage = nullptr;

uint32_t __fastcall Hook_CExoResourceImageFile_LoadImage(int thisPtr, void* edxDummy, uint32_t resourceType) {
    auto start = std::chrono::high_resolution_clock::now();

    uint32_t result = g_originalCExoResourceImageFile_LoadImage(thisPtr, resourceType);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    Log("CExoResourceImageFile_LoadImage: " + std::to_string(duration.count()) + + " μs");
    return result;
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
    
    void* targetAddr_CSWGuiTutorialBox_Ctor = (void*)(0x8bf1c0); //Just putting in actual address
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

    void* targetAddr_CSWGuiStatusSummary_Ctor = (void*)(0x75cec0);
    if (MH_CreateHook(targetAddr_CSWGuiStatusSummary_Ctor, &Hook_CSWGuiStatusSummary_Ctor,
        (LPVOID*)&g_originalCSWGuiStatusSummary_Ctor) == MH_OK) {
            if (MH_EnableHook(targetAddr_CSWGuiStatusSummary_Ctor) == MH_OK) {
                Log("CSWGuiStatusSummary_Ctor hook installed successfully");
            } else { Log("Failed to enable hook"); }
        } else { Log("Failed to create hook"); }

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
