#include <Windows.h>
#include <fstream>
#include <string>
#include <chrono>
#include "minhook/include/MinHook.h"

// DirectInput8 proxy
typedef HRESULT(WINAPI *DICREATE)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DICREATE realCreate = nullptr;

// GetResourceDataPtr hook
typedef int (__fastcall* loadingscreenPtr_t)(int param1);
loadingscreenPtr_t g_originalGetResourceDataPtr = nullptr;

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

int __fastcall Hook_loadingscreenPtr(int param1) {
    auto start = std::chrono::high_resolution_clock::now();
    
    int result = g_originalGetResourceDataPtr(param1);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Log with more detail
    Log("loadingscreen: " + std::to_string(duration.count()) + " μs, result=0x" + 
        std::to_string(result) + ", param1=" + std::to_string(param1));
    
    return result;
}




void InstallHook() {
    Log("Installing GetResourceDataPtr hook...");
    
    if (MH_Initialize() != MH_OK) {
        Log("MinHook init failed");
        return;
    }
    
    HMODULE hModule = GetModuleHandle(nullptr);
    DWORD baseAddr = (DWORD)hModule;
    void* targetAddr = (void*)(0x533830); //Just putting in actual address
    
    if (MH_CreateHook(targetAddr, &Hook_loadingscreenPtr, 
                     (LPVOID*)&g_originalGetResourceDataPtr) == MH_OK) {
        if (MH_EnableHook(targetAddr) == MH_OK) {
            Log("GetResourceDataPtr hook installed successfully");
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
        
        Log("DirectInput8Create proxy loaded");
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