#include "anticheat.h"
#include "memory.h"

#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif
#ifndef WDA_MONITOR
#define WDA_MONITOR 0x00000001
#endif
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

typedef BOOL(WINAPI* pfnSetWindowDisplayAffinity)(HWND, DWORD);
typedef BOOL(WINAPI* pfnGetWindowDisplayAffinity)(HWND, DWORD*);
static pfnSetWindowDisplayAffinity g_pSetAffinity = nullptr;
static pfnGetWindowDisplayAffinity g_pGetAffinity = nullptr;

void InitAntiCaptureAPI() {
    HMODULE hU = GetModuleHandleW(L"user32.dll");
    if (hU) {
        g_pSetAffinity = (pfnSetWindowDisplayAffinity)GetProcAddress(hU, "SetWindowDisplayAffinity");
        g_pGetAffinity = (pfnGetWindowDisplayAffinity)GetProcAddress(hU, "GetWindowDisplayAffinity");
    }
    
    typedef LONG(WINAPI* RtlGetVersionT)(PRTL_OSVERSIONINFOW);
    HMODULE hN = GetModuleHandleW(L"ntdll.dll");
    if (hN) {
        auto pRtl = (RtlGetVersionT)GetProcAddress(hN, "RtlGetVersion");
        if (pRtl) {
            RTL_OSVERSIONINFOW vi{}; vi.dwOSVersionInfoSize = sizeof(vi);
            if (pRtl(&vi) == 0) g_app.osBuild = vi.dwBuildNumber;
        }
    }
    g_app.acSupported = (g_pSetAffinity != nullptr) && (g_app.osBuild >= 19041);
}

bool ApplyAntiCapture() {
    InitAntiCaptureAPI();
    g_app.acApplied = false;
    g_app.acMode = WDA_NONE;
    if (!g_app.hwnd || !g_pSetAffinity) return false;
    if (!g_cfg.antiCapture) { g_pSetAffinity(g_app.hwnd, WDA_NONE); return false; }
    
    DWORD want = g_app.acSupported ? WDA_EXCLUDEFROMCAPTURE : WDA_MONITOR;
    if (!g_pSetAffinity(g_app.hwnd, want)) {
        if (want == WDA_EXCLUDEFROMCAPTURE && g_pSetAffinity(g_app.hwnd, WDA_MONITOR)) want = WDA_MONITOR;
        else return false;
    }
    
    DWORD cur = 0;
    if (g_pGetAffinity && g_pGetAffinity(g_app.hwnd, &cur)) {
        g_app.acApplied = (cur == want);
        g_app.acMode = cur;
    } else {
        g_app.acApplied = true;
        g_app.acMode = want;
    }
    return g_app.acApplied;
}

void RemoveAntiCapture() {
    if (g_app.hwnd && g_pSetAffinity) g_pSetAffinity(g_app.hwnd, WDA_NONE);
    g_app.acApplied = false;
    g_app.acMode = WDA_NONE;
}

void ToggleGameCapture(bool hide) {
    HWND hGame = GameHwnd();
    if (hGame && g_pSetAffinity) {
        DWORD mode = hide ? (g_app.acSupported ? WDA_EXCLUDEFROMCAPTURE : WDA_MONITOR) : WDA_NONE;
        g_pSetAffinity(hGame, mode);
    }
}