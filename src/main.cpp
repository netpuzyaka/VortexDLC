// ============================================================================
//  vortex — overlay tool for Rustmine (stealth build)
//  build: cl /std:c++17 /O2 /EHsc /MD /DUNICODE /D_UNICODE main.cpp
//            imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp
//            imgui_impl_win32.cpp imgui_impl_dx11.cpp /link /SUBSYSTEM:WINDOWS
// ============================================================================
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <tlhelp32.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <psapi.h>
#include <mmsystem.h>
#include <intrin.h>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <random>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "json.hpp"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdiplus.lib")

#ifndef MOUSEEVENTF_MOVE_NOCOALESCE
#define MOUSEEVENTF_MOVE_NOCOALESCE 0x2000
#endif
#ifndef WDA_NONE
#define WDA_NONE 0x0
#endif
#ifndef WDA_MONITOR
#define WDA_MONITOR 0x1
#endif
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x11
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;
using clk = std::chrono::steady_clock;

// ============================================================================
//  global state
// ============================================================================
static HWND              g_hwnd = nullptr;
static bool              g_visible = true;
static std::atomic<bool> g_run{ true };

// ============================================================================
//  string helpers
// ============================================================================
static std::string U8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, 0, 0);
    std::string s(n, '\0');
    if (n) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, 0, 0);
    return s;
}
static std::wstring W16(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::string Lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    return s;
}
static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static bool Has(const std::string& h, const char* n) { return h.find(n) != std::string::npos; }

// ============================================================================
//  PEB structures (manual definitions for WIN32_LEAN_AND_MEAN)
// ============================================================================
typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING, * PUNICODE_STRING;

typedef struct _PEB_LDR_DATA {
    ULONG Length;
    BOOLEAN Initialized;
    PVOID SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
} PEB_LDR_DATA, * PPEB_LDR_DATA;

typedef struct _LDR_DATA_TABLE_ENTRY {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
    ULONG Flags;
    USHORT LoadCount;
    USHORT TlsIndex;
    LIST_ENTRY HashLinks;
    PVOID SectionPointer;
    ULONG CheckSum;
    ULONG TimeDateStamp;
    PVOID LoadedImports;
    PVOID EntryPointActivationContext;
    PVOID PatchInformation;
} LDR_DATA_TABLE_ENTRY, * PLDR_DATA_TABLE_ENTRY;

typedef struct _PEB {
    BYTE Reserved1[2];
    BYTE BeingDebugged;
    BYTE Reserved2[1];
    PVOID Reserved3[2];
    PPEB_LDR_DATA Ldr;
    PVOID ProcessParameters;
    PVOID Reserved4[3];
    PVOID AtlThunkSListPtr;
    PVOID Reserved5;
    ULONG Reserved6;
    PVOID Reserved7;
    ULONG Reserved8;
    ULONG AtlThunkSListPtr32;
    PVOID Reserved9[45];
    BYTE Reserved10[96];
    PVOID PostProcessInitRoutine;
    BYTE Reserved11[128];
    PVOID Reserved12[1];
    ULONG SessionId;
} PEB, * PPEB;

// ============================================================================
//  PEB hiding + PE header wipe
// ============================================================================
static void HideFromPEB() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)&HideFromPEB, &self);
    if (!self) return;

    PPEB peb = (PPEB)__readgsqword(0x60);
    if (!peb || !peb->Ldr) return;
    PPEB_LDR_DATA ldr = peb->Ldr;

    LIST_ENTRY* head = &ldr->InLoadOrderModuleList;
    LIST_ENTRY* cur = head->Flink;
    while (cur != head) {
        PLDR_DATA_TABLE_ENTRY entry = CONTAINING_RECORD(cur, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (entry->DllBase == self) {
            // Удаляем из всех трёх списков
            entry->InLoadOrderLinks.Blink->Flink = entry->InLoadOrderLinks.Flink;
            entry->InLoadOrderLinks.Flink->Blink = entry->InLoadOrderLinks.Blink;
            entry->InMemoryOrderLinks.Blink->Flink = entry->InMemoryOrderLinks.Flink;
            entry->InMemoryOrderLinks.Flink->Blink = entry->InMemoryOrderLinks.Blink;
            entry->InInitializationOrderLinks.Blink->Flink = entry->InInitializationOrderLinks.Flink;
            entry->InInitializationOrderLinks.Flink->Blink = entry->InInitializationOrderLinks.Blink;
            // Затираем имена
            memset(&entry->FullDllName, 0, sizeof(UNICODE_STRING));
            memset(&entry->BaseDllName, 0, sizeof(UNICODE_STRING));
            entry->SizeOfImage = 0;
            entry->EntryPoint = nullptr;
            break;
        }
        cur = cur->Flink;
    }

    // Стираем PE-заголовок (обход manualMap detection)
    DWORD oldProtect;
    if (VirtualProtect(self, 0x1000, PAGE_READWRITE, &oldProtect)) {
        memset(self, 0, 0x1000);
        VirtualProtect(self, 0x1000, oldProtect, &oldProtect);
    }
}

// ============================================================================
//  config
// ============================================================================
enum class Mode { Off = 0, Compensate = 1, Pattern = 2 };
struct Config {
    Mode         mode = Mode::Off;
    float        sensitivity = 8.0f;
    float        patternScale = 8.0f;
    uintptr_t    pitchAddr = 0;
    uintptr_t    yawAddr = 0;
    bool         requireAds = false;
    bool         humanize = false;
    float        jitter = 0.6f;
    float        maxStep = 25.0f;
    int          legacyApi = 0;
    float        shotDelayMs = 133.0f;
    int          shotCount = 30;
    bool         patternLoop = false;
    bool         antiCapture = true;
    bool         pauseOnF3 = true;
    bool         deleteOnExit = false;
    int          keyToggle = VK_F8;
    int          keyPanic = VK_DELETE;
    std::wstring agentPath;
    std::wstring interceptorDll;
    bool         noRecoilEnabled = false;
    bool         espEnabled = false;
    float        espMaxDist = 200.0f;
};
static Config g_cfg;

static fs::path ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
}
static fs::path ConfigPath() { return ExeDir() / "vortex.json"; }

static void SaveConfig() {
    json j;
    j["mode"] = (int)g_cfg.mode;
    j["sens"] = g_cfg.sensitivity;
    j["pscale"] = g_cfg.patternScale;
    j["pitch"] = (uint64_t)g_cfg.pitchAddr;
    j["yaw"] = (uint64_t)g_cfg.yawAddr;
    j["ads"] = g_cfg.requireAds;
    j["humanize"] = g_cfg.humanize;
    j["jitter"] = g_cfg.jitter;
    j["maxstep"] = g_cfg.maxStep;
    j["legacy"] = g_cfg.legacyApi;
    j["delay"] = g_cfg.shotDelayMs;
    j["shots"] = g_cfg.shotCount;
    j["loop"] = g_cfg.patternLoop;
    j["anticap"] = g_cfg.antiCapture;
    j["f3"] = g_cfg.pauseOnF3;
    j["del"] = g_cfg.deleteOnExit;
    j["agent"] = U8(g_cfg.agentPath);
    j["interceptor"] = U8(g_cfg.interceptorDll);
    j["norecoil"] = g_cfg.noRecoilEnabled;
    j["esp"] = g_cfg.espEnabled;
    j["espdist"] = g_cfg.espMaxDist;
    std::ofstream f(ConfigPath());
    if (f) f << j.dump(2);
}
static void LoadConfig() {
    std::ifstream f(ConfigPath());
    if (!f) return;
    try {
        json j; f >> j;
        int m = j.value("mode", 0);
        g_cfg.mode = (Mode)((m >= 0 && m <= 2) ? m : 0);
        g_cfg.sensitivity = j.value("sens", 8.0f);
        g_cfg.patternScale = j.value("pscale", 8.0f);
        g_cfg.pitchAddr = (uintptr_t)j.value("pitch", (uint64_t)0);
        g_cfg.yawAddr = (uintptr_t)j.value("yaw", (uint64_t)0);
        g_cfg.requireAds = j.value("ads", false);
        g_cfg.humanize = j.value("humanize", false);
        g_cfg.jitter = j.value("jitter", 0.6f);
        g_cfg.maxStep = j.value("maxstep", 25.0f);
        g_cfg.legacyApi = j.value("legacy", 0);
        g_cfg.shotDelayMs = j.value("delay", 133.0f);
        g_cfg.shotCount = j.value("shots", 30);
        g_cfg.patternLoop = j.value("loop", false);
        g_cfg.antiCapture = j.value("anticap", true);
        g_cfg.pauseOnF3 = j.value("f3", true);
        g_cfg.deleteOnExit = j.value("del", false);
        g_cfg.agentPath = W16(j.value("agent", std::string()));
        g_cfg.interceptorDll = W16(j.value("interceptor", std::string()));
        g_cfg.noRecoilEnabled = j.value("norecoil", false);
        g_cfg.espEnabled = j.value("esp", false);
        g_cfg.espMaxDist = j.value("espdist", 200.0f);
    } catch (...) {}
}

// ============================================================================
//  target process
// ============================================================================
static std::atomic<HANDLE> g_proc{ nullptr };
static std::atomic<DWORD>  g_pid{ 0 };
static std::vector<DWORD>  g_candidates;
static std::mutex          g_attachMx;

static void RefreshCandidates() {
    g_candidates.clear();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (!_wcsicmp(pe.szExeFile, L"javaw.exe") || !_wcsicmp(pe.szExeFile, L"java.exe"))
                g_candidates.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}
static void Detach() {
    HANDLE old = g_proc.exchange(nullptr);
    if (old) CloseHandle(old);
    g_pid = 0;
}
static bool Attach(DWORD pid) {
    std::lock_guard<std::mutex> lk(g_attachMx);
    Detach();
    if (!pid) return false;
    HANDLE h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    g_proc = h; g_pid = pid;
    return true;
}
static inline bool ReadF(uintptr_t addr, float& out) {
    HANDLE h = g_proc.load(std::memory_order_acquire);
    if (!h || !addr) return false;
    SIZE_T got = 0;
    if (!ReadProcessMemory(h, (LPCVOID)addr, &out, sizeof(float), &got) || got != sizeof(float))
        return false;
    return std::isfinite(out);
}

// ============================================================================
//  mouse
// ============================================================================
static float g_accX = 0, g_accY = 0;
static void MoveMouse(float dx, float dy) {
    g_accX += dx; g_accY += dy;
    int ix = (int)std::lround(g_accX);
    int iy = (int)std::lround(g_accY);
    if (!ix && !iy) return;
    g_accX -= (float)ix;
    g_accY -= (float)iy;
    if (g_cfg.legacyApi) { mouse_event(MOUSEEVENTF_MOVE, ix, iy, 0, 0); return; }
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = ix; in.mi.dy = iy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_MOVE_NOCOALESCE;
    SendInput(1, &in, sizeof(in));
}
static inline float AngleDelta(float a, float b) {
    float d = a - b;
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

// ============================================================================
//  pattern
// ============================================================================
struct Step { float dx = 0, dy = 0, dt = 0; };
static std::vector<Step>  g_pattern;
static std::mutex         g_patternMx;
static std::atomic<bool>  g_recording{ false };
static std::atomic<float> g_recProgress{ 0 };

static void SavePattern(const fs::path& p) {
    std::lock_guard<std::mutex> lk(g_patternMx);
    json j = json::array();
    for (auto& s : g_pattern) j.push_back({ {"dx", s.dx}, {"dy", s.dy}, {"dt", s.dt} });
    std::ofstream f(p);
    if (f) f << j.dump(2);
}
static bool LoadPattern(const fs::path& p) {
    std::ifstream f(p);
    if (!f) return false;
    try {
        json j; f >> j;
        std::vector<Step> v;
        for (auto& it : j)
            v.push_back({ it.value("dx", 0.f), it.value("dy", 0.f), it.value("dt", g_cfg.shotDelayMs) });
        if (v.empty()) return false;
        std::lock_guard<std::mutex> lk(g_patternMx);
        g_pattern = std::move(v);
        return true;
    } catch (...) { return false; }
}

// ============================================================================
//  game window
// ============================================================================
static HWND GameHwnd() {
    HWND h = FindWindowW(L"GLFW30", nullptr);
    if (!h) h = FindWindowW(L"LWJGL", nullptr);
    if (!h) h = FindWindowW(nullptr, L"Minecraft");
    return h;
}
static bool GameActive() {
    HWND h = GameHwnd();
    if (!h) return false;
    if (IsIconic(h)) return false;
    return GetForegroundWindow() == h;
}

// ============================================================================
//  worker
// ============================================================================
static std::atomic<int> g_workerHz{ 0 };
static std::thread      g_worker, g_recThread;

static void RecordThread() {
    for (int i = 0; i < 30 && g_run && g_recording; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::vector<Step> rec;
    float lastY = 0, lastP = 0;
    bool ok = ReadF(g_cfg.yawAddr, lastY) && ReadF(g_cfg.pitchAddr, lastP);
    if (ok && g_recording && g_run) {
        INPUT d{}; d.type = INPUT_MOUSE; d.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        SendInput(1, &d, sizeof(d));
        auto prev = clk::now();
        for (int i = 0; i < g_cfg.shotCount && g_run && g_recording; ++i) {
            std::this_thread::sleep_for(std::chrono::microseconds((int)(g_cfg.shotDelayMs * 1000)));
            auto now = clk::now();
            float dt = std::chrono::duration<float, std::milli>(now - prev).count();
            prev = now;
            float y, p;
            if (ReadF(g_cfg.yawAddr, y) && ReadF(g_cfg.pitchAddr, p)) {
                float dy = AngleDelta(y, lastY), dp = p - lastP;
                if (std::fabs(dy) < g_cfg.maxStep && std::fabs(dp) < g_cfg.maxStep)
                    rec.push_back({ dy, dp, dt });
                lastY = y; lastP = p;
            }
            g_recProgress = (float)(i + 1) / (float)std::max(1, g_cfg.shotCount);
        }
        INPUT u{}; u.type = INPUT_MOUSE; u.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &u, sizeof(u));
        if (!rec.empty()) {
            { std::lock_guard<std::mutex> lk(g_patternMx); g_pattern = rec; }
            SavePattern(ExeDir() / "pattern.json");
        }
    }
    g_recording = false;
}

static void Worker() {
    std::mt19937 rng{ std::random_device{}() };
    std::normal_distribution<float> noise(0.0f, 1.0f);
    float lastY = 0, lastP = 0;
    bool haveLast = false, prevFire = false;
    int idx = 0;
    float nextShotAt = 0;
    auto t0 = clk::now();
    auto next = clk::now();
    auto hzT = clk::now();
    int ticks = 0;
    while (g_run) {
        next += std::chrono::microseconds(1000);
        std::this_thread::sleep_until(next);
        if (clk::now() - next > std::chrono::milliseconds(4)) next = clk::now();
        if (++ticks >= 500) {
            auto now = clk::now();
            float sec = std::chrono::duration<float>(now - hzT).count();
            g_workerHz = (int)(ticks / (sec > 0.0001f ? sec : 1.0f));
            ticks = 0; hzT = now;
        }
        if (g_cfg.mode == Mode::Off || g_recording) { haveLast = false; prevFire = false; continue; }
        if (g_cfg.pauseOnF3 && !GameActive()) { haveLast = false; prevFire = false; continue; }
        bool fire = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (g_cfg.requireAds) fire = fire && (GetAsyncKeyState(VK_RBUTTON) & 0x8000);
        if (g_cfg.mode == Mode::Compensate) {
            float y, p;
            if (!ReadF(g_cfg.yawAddr, y) || !ReadF(g_cfg.pitchAddr, p)) { haveLast = false; continue; }
            if (fire && haveLast) {
                float dy = AngleDelta(y, lastY), dp = p - lastP;
                if (std::fabs(dy) < g_cfg.maxStep && std::fabs(dp) < g_cfg.maxStep) {
                    float mx = -dy * g_cfg.sensitivity;
                    float my = -dp * g_cfg.sensitivity;
                    if (g_cfg.humanize) { mx += noise(rng) * g_cfg.jitter; my += noise(rng) * g_cfg.jitter; }
                    MoveMouse(mx, my);
                }
            }
            lastY = y; lastP = p; haveLast = true;
        }
        else if (g_cfg.mode == Mode::Pattern) {
            if (!fire) { idx = 0; prevFire = false; continue; }
            if (!prevFire) { t0 = clk::now(); idx = 0; nextShotAt = 0; g_accX = g_accY = 0; prevFire = true; }
            std::vector<Step> pat;
            { std::lock_guard<std::mutex> lk(g_patternMx); pat = g_pattern; }
            if (pat.empty()) continue;
            float elapsed = std::chrono::duration<float, std::milli>(clk::now() - t0).count();
            while (idx < (int)pat.size() && elapsed >= nextShotAt) {
                float mx = -pat[idx].dx * g_cfg.patternScale;
                float my = -pat[idx].dy * g_cfg.patternScale;
                if (g_cfg.humanize) { mx += noise(rng) * g_cfg.jitter; my += noise(rng) * g_cfg.jitter; }
                MoveMouse(mx, my);
                nextShotAt += (pat[idx].dt > 0 ? pat[idx].dt : g_cfg.shotDelayMs);
                ++idx;
            }
            if (idx >= (int)pat.size() && g_cfg.patternLoop) { idx = 0; nextShotAt = 0; t0 = clk::now(); }
        }
    }
}

// ============================================================================
//  anti-capture
// ============================================================================
typedef BOOL(WINAPI* pfnSetWDA)(HWND, DWORD);
static pfnSetWDA g_pSetWDA = nullptr;
static DWORD g_osBuild = 0;
static bool g_acSupported = false;
static bool g_acApplied = false;
static DWORD g_acMode = WDA_NONE;

static void InitAntiCaptureAPI() {
    HMODULE hU = GetModuleHandleW(L"user32.dll");
    if (hU) g_pSetWDA = (pfnSetWDA)GetProcAddress(hU, "SetWindowDisplayAffinity");
    typedef LONG(WINAPI* RtlGetVersionT)(PRTL_OSVERSIONINFOW);
    HMODULE hN = GetModuleHandleW(L"ntdll.dll");
    if (hN) {
        auto pRtl = (RtlGetVersionT)GetProcAddress(hN, "RtlGetVersion");
        if (pRtl) {
            RTL_OSVERSIONINFOW vi{}; vi.dwOSVersionInfoSize = sizeof(vi);
            if (pRtl(&vi) == 0) g_osBuild = vi.dwBuildNumber;
        }
    }
    g_acSupported = (g_pSetWDA != nullptr) && (g_osBuild >= 19041);
}
static bool ApplyAntiCapture() {
    InitAntiCaptureAPI();
    g_acApplied = false; g_acMode = WDA_NONE;
    if (!g_hwnd || !g_pSetWDA) return false;
    if (!g_cfg.antiCapture) { g_pSetWDA(g_hwnd, WDA_NONE); return false; }
    DWORD want = g_acSupported ? WDA_EXCLUDEFROMCAPTURE : WDA_MONITOR;
    if (!g_pSetWDA(g_hwnd, want)) {
        if (want == WDA_EXCLUDEFROMCAPTURE && g_pSetWDA(g_hwnd, WDA_MONITOR)) want = WDA_MONITOR;
        else return false;
    }
    g_acApplied = true; g_acMode = want;
    return true;
}
static void RemoveAntiCapture() {
    if (g_hwnd && g_pSetWDA) g_pSetWDA(g_hwnd, WDA_NONE);
    g_acApplied = false; g_acMode = WDA_NONE;
}

// ============================================================================
//  d3d + window
// ============================================================================
static ID3D11Device* g_dev = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;
static IDXGISwapChain* g_sc = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;

static void ReleaseRTV() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }
static void MakeRTV() {
    ID3D11Texture2D* bb = nullptr;
    if (SUCCEEDED(g_sc->GetBuffer(0, IID_PPV_ARGS(&bb))) && bb) {
        g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv);
        bb->Release();
    }
}
static bool InitD3D(HWND h) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = h;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL fla[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        fla, 2, D3D11_SDK_VERSION, &sd, &g_sc, &g_dev, &fl, &g_ctx);
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            fla, 2, D3D11_SDK_VERSION, &sd, &g_sc, &g_dev, &fl, &g_ctx);
        if (FAILED(hr)) return false;
    }
    MakeRTV();
    return g_rtv != nullptr;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return TRUE;
    switch (m) {
    case WM_SIZE:
        if (g_dev && g_sc && w != SIZE_MINIMIZED) {
            ReleaseRTV();
            g_sc->ResizeBuffers(0, (UINT)LOWORD(l), (UINT)HIWORD(l), DXGI_FORMAT_UNKNOWN, 0);
            MakeRTV();
        }
        return 0;
    case WM_CLOSE: g_run = false; return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h, m, w, l);
}

// ============================================================================
//  theme
// ============================================================================
static ImVec4 C(float r, float g, float b, float a = 1) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a); }
static const ImVec4 kBg = C(12, 13, 15);
static const ImVec4 kPanel = C(18, 20, 23);
static const ImVec4 kPanel2 = C(21, 23, 27);
static const ImVec4 kLine = C(32, 35, 40);
static const ImVec4 kText = C(224, 226, 230);
static const ImVec4 kDim = C(120, 125, 135);
static const ImVec4 kAccent = C(139, 124, 246);
static const ImVec4 kAccentH = C(163, 150, 255);
static const ImVec4 kGreen = C(74, 222, 128);
static const ImVec4 kRed = C(239, 68, 68);
static const ImVec4 kYellow = C(251, 191, 36);
static const ImVec4 kCyan = C(34, 211, 238);

static void Theme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0; s.ChildRounding = 6; s.FrameRounding = 5;
    s.WindowPadding = ImVec2(0, 0); s.FramePadding = ImVec2(9, 5);
    s.Colors[ImGuiCol_Text] = kText;
    s.Colors[ImGuiCol_WindowBg] = kBg;
    s.Colors[ImGuiCol_FrameBg] = C(26, 28, 32);
    s.Colors[ImGuiCol_Button] = C(28, 30, 35);
    s.Colors[ImGuiCol_ButtonHovered] = C(38, 41, 47);
    s.Colors[ImGuiCol_Header] = C(30, 32, 38);
    s.Colors[ImGuiCol_CheckMark] = kAccent;
    s.Colors[ImGuiCol_SliderGrab] = kAccent;
}

// ============================================================================
//  GUI helpers
// ============================================================================
static void SectionTitle(const char* t) { ImGui::Spacing(); ImGui::TextColored(kAccent, "%s", t); ImGui::Dummy(ImVec2(0, 1)); }
static void Gap(float h = 4) { ImGui::Dummy(ImVec2(0, h)); }
static void Rule() { Gap(2); ImGui::Separator(); Gap(3); }

// ============================================================================
//  pages
// ============================================================================
static int g_page = 0;
static char g_pitchBuf[32] = "";
static char g_yawBuf[32] = "";
static std::string g_status;
static float g_statusTTL = 0;
static void SetStatus(const std::string& s) { g_status = s; g_statusTTL = 4.0f; }

static void SyncAddrBufs() {
    snprintf(g_pitchBuf, sizeof(g_pitchBuf), "%llX", (unsigned long long)g_cfg.pitchAddr);
    snprintf(g_yawBuf, sizeof(g_yawBuf), "%llX", (unsigned long long)g_cfg.yawAddr);
}

static void PageMain() {
    SectionTitle("MODE");
    const char* modes[] = { "Off", "Compensate", "Pattern" };
    int m = (int)g_cfg.mode;
    if (ImGui::Combo("##mode", &m, modes, 3)) g_cfg.mode = (Mode)m;
    SectionTitle("STRENGTH");
    float* s = (g_cfg.mode == Mode::Pattern) ? &g_cfg.patternScale : &g_cfg.sensitivity;
    ImGui::SliderFloat("##strength", s, 0.5f, 30.0f, "%.2f");
    Rule();
    SectionTitle("MEMORY ADDRESSES");
    ImGui::TextColored(kDim, "pitch"); ImGui::SameLine(56);
    if (ImGui::InputText("##pitch", g_pitchBuf, sizeof(g_pitchBuf), ImGuiInputTextFlags_CharsHexadecimal))
        g_cfg.pitchAddr = (uintptr_t)_strtoui64(g_pitchBuf, nullptr, 16);
    ImGui::TextColored(kDim, "yaw"); ImGui::SameLine(56);
    if (ImGui::InputText("##yaw", g_yawBuf, sizeof(g_yawBuf), ImGuiInputTextFlags_CharsHexadecimal))
        g_cfg.yawAddr = (uintptr_t)_strtoui64(g_yawBuf, nullptr, 16);
    float y, p;
    bool live = ReadF(g_cfg.yawAddr, y) && ReadF(g_cfg.pitchAddr, p);
    Gap(2);
    if (live) ImGui::TextColored(kGreen, "yaw %.3f   pitch %.3f", y, p);
    else      ImGui::TextColored(kDim, "no readable data");
    Rule();
    SectionTitle("BEHAVIOR");
    ImGui::Checkbox("Require right mouse (ADS)", &g_cfg.requireAds);
    ImGui::Checkbox("Add noise", &g_cfg.humanize);
    if (g_cfg.humanize) ImGui::SliderFloat("##jit", &g_cfg.jitter, 0.0f, 3.0f, "jitter %.2f px");
    ImGui::SliderFloat("##maxstep", &g_cfg.maxStep, 5.0f, 90.0f, "reject jumps > %.0f deg");
}

static void PagePattern() {
    size_t n; { std::lock_guard<std::mutex> lk(g_patternMx); n = g_pattern.size(); }
    ImGui::TextColored(kDim, "%zu steps loaded", n);
    Gap(6);
    if (g_recording.load()) {
        ImGui::ProgressBar(g_recProgress.load(), ImVec2(-1, 6), "");
        Gap(4);
        if (ImGui::Button("Cancel", ImVec2(-1, 28))) g_recording = false;
    } else {
        bool can = g_cfg.pitchAddr && g_cfg.yawAddr && g_proc.load();
        if (ImGui::Button("Record (3s delay)", ImVec2(-1, 30)) && can) {
            if (g_recThread.joinable()) g_recThread.join();
            g_recProgress = 0; g_recording = true;
            g_recThread = std::thread(RecordThread);
        }
        if (!can) ImGui::TextColored(kDim, "attach + set addresses first");
    }
    Gap(6);
    SectionTitle("TIMING");
    ImGui::SliderFloat("##delay", &g_cfg.shotDelayMs, 20.0f, 500.0f, "interval %.0f ms");
    ImGui::SliderInt("##shots", &g_cfg.shotCount, 5, 100, "shots %d");
    ImGui::Checkbox("Loop pattern", &g_cfg.patternLoop);
    Rule();
    if (ImGui::Button("Load pattern.json", ImVec2(-1, 26)))
        SetStatus(LoadPattern(ExeDir() / "pattern.json") ? "pattern loaded" : "load failed");
    if (ImGui::Button("Save pattern.json", ImVec2(-1, 26)) && n > 0) {
        SavePattern(ExeDir() / "pattern.json");
        SetStatus("pattern saved");
    }
}

static void PageCheats() {
    SectionTitle("NORECOIL (Java Agent)");
    ImGui::TextColored(kDim, "Patches HitscanGun.recoil() and ViewRecoil.addPunch() via bytecode");
    ImGui::Checkbox("Enable NoRecoil", &g_cfg.noRecoilEnabled);
    if (g_cfg.noRecoilEnabled) {
        ImGui::TextColored(kGreen, "Active: recoil methods patched to return immediately");
    } else {
        ImGui::TextColored(kDim, "Inactive");
    }
    Rule();
    SectionTitle("ESP");
    ImGui::Checkbox("Enable ESP", &g_cfg.espEnabled);
    if (g_cfg.espEnabled) {
        ImGui::SliderFloat("Max Distance", &g_cfg.espMaxDist, 50.0f, 500.0f, "%.0f m");
        ImGui::TextColored(kYellow, "Warning: server doesn't send players outside view distance");
    }
    Rule();
    SectionTitle("AGENT STATUS");
    ImGui::TextColored(kDim, "BypassAgent.jar must be attached via Intercept tab");
}

static void PageSettings() {
    SectionTitle("INPUT API");
    const char* api[] = { "SendInput", "mouse_event" };
    ImGui::Combo("##api", &g_cfg.legacyApi, api, 2);
    ImGui::TextColored(kDim, "worker: %d Hz", g_workerHz.load());
    Rule();
    SectionTitle("STEALTH");
    bool ac = g_cfg.antiCapture;
    if (ImGui::Checkbox("Hide GUI from capture", &ac)) {
        g_cfg.antiCapture = ac;
        ApplyAntiCapture();
    }
    Rule();
    SectionTitle("SAFETY");
    ImGui::Checkbox("Pause when game not focused", &g_cfg.pauseOnF3);
    ImGui::Checkbox("Delete traces on exit", &g_cfg.deleteOnExit);
    Rule();
    SectionTitle("HOTKEYS");
    ImGui::TextColored(kDim, "HOME   show / hide");
    ImGui::TextColored(kDim, "F8     cycle mode");
    ImGui::TextColored(kRed, "DEL    stop + close");
    Rule();
    if (ImGui::Button("Save config", ImVec2(-1, 28))) { SaveConfig(); SetStatus("config saved"); }
}

// ============================================================================
//  DrawGUI
// ============================================================================
static void DrawGUI() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 disp = io.DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(disp, ImGuiCond_Always);
    ImGui::Begin("##root", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float headerH = 36.0f, footerH = 26.0f, railW = 132.0f, pad = 10.0f;
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(disp.x, headerH), ImGui::ColorConvertFloat4ToU32(kPanel));
    dl->AddLine(ImVec2(0, headerH), ImVec2(disp.x, headerH), ImGui::ColorConvertFloat4ToU32(kLine));
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::InvisibleButton("##drag", ImVec2(disp.x - 76.0f, headerH));
    static ImVec2 dragStartMouse = { 0, 0 };
    static POINT dragStartWin = { 0, 0 };
    static bool dragging = false;
    if (ImGui::IsItemActive()) {
        if (!dragging) {
            dragging = true;
            dragStartMouse = io.MousePos;
            RECT r; GetWindowRect(g_hwnd, &r);
            dragStartWin = { r.left, r.top };
        }
        SetWindowPos(g_hwnd, nullptr,
            dragStartWin.x + (int)(io.MousePos.x - dragStartMouse.x),
            dragStartWin.y + (int)(io.MousePos.y - dragStartMouse.y),
            0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    } else dragging = false;
    float ty = (headerH - ImGui::GetFontSize()) * 0.5f;
    dl->AddText(ImVec2(14, ty), ImGui::ColorConvertFloat4ToU32(kAccent), "vortex");
    dl->AddText(ImVec2(14 + ImGui::CalcTextSize("vortex").x + 10, ty),
        ImGui::ColorConvertFloat4ToU32(C(78, 82, 90)), "stealth");
    const char* mn[] = { "off", "compensate", "pattern" };
    ImVec4 mc = (g_cfg.mode == Mode::Off) ? kDim : kGreen;
    float mw = ImGui::CalcTextSize(mn[(int)g_cfg.mode]).x;
    dl->AddText(ImVec2(disp.x - 76.0f - mw - 12.0f, ty),
        ImGui::ColorConvertFloat4ToU32(mc), mn[(int)g_cfg.mode]);
    ImGui::SetCursorPos(ImVec2(disp.x - 68.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.06f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.10f));
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    if (ImGui::Button("_", ImVec2(26, 24))) ShowWindow(g_hwnd, SW_MINIMIZE);
    ImGui::SameLine(0, 4);
    ImGui::PopStyleColor(4);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(kRed.x, kRed.y, kRed.z, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kRed);
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    if (ImGui::Button("x", ImVec2(26, 24))) g_run = false;
    ImGui::PopStyleColor(4);
    float bodyH = disp.y - headerH - footerH - pad * 2.0f;
    if (bodyH < 60) bodyH = 60;
    ImGui::SetCursorPos(ImVec2(pad, headerH + pad));
    ImGui::BeginChild("rail", ImVec2(railW, bodyH), false);
    if (ImGui::Button("Main", ImVec2(-1, 30))) g_page = 0;
    if (ImGui::Button("Pattern", ImVec2(-1, 30))) g_page = 1;
    if (ImGui::Button("Cheats", ImVec2(-1, 30))) g_page = 2;
    if (ImGui::Button("Settings", ImVec2(-1, 30))) g_page = 3;
    ImGui::EndChild();
    ImGui::SameLine(0, pad);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel2);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
    ImGui::BeginChild("body", ImVec2(0, bodyH), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImGui::PushItemWidth(-1);
    switch (g_page) {
    case 0: PageMain(); break;
    case 1: PagePattern(); break;
    case 2: PageCheats(); break;
    default: PageSettings(); break;
    }
    ImGui::PopItemWidth();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    float fy = disp.y - footerH;
    dl->AddLine(ImVec2(0, fy), ImVec2(disp.x, fy), ImGui::ColorConvertFloat4ToU32(kLine));
    ImGui::SetCursorPos(ImVec2(pad, fy + (footerH - ImGui::GetFontSize()) * 0.5f));
    bool active = (g_cfg.mode != Mode::Off);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float r = 3.5f, y = p.y + ImGui::GetTextLineHeight() * 0.5f;
    dl->AddCircleFilled(ImVec2(p.x + r, y), r + 2.2f,
        ImGui::ColorConvertFloat4ToU32(ImVec4((active ? kGreen : C(80, 84, 92)).x,
            (active ? kGreen : C(80, 84, 92)).y, (active ? kGreen : C(80, 84, 92)).z, 0.16f)));
    dl->AddCircleFilled(ImVec2(p.x + r, y), r,
        ImGui::ColorConvertFloat4ToU32(active ? kGreen : C(80, 84, 92)));
    ImGui::Dummy(ImVec2(r * 2 + 4, 1));
    ImGui::SameLine(0, 6);
    ImGui::TextColored(kDim, "%s", active ? "engine active" : "engine idle");
    ImGui::SameLine(0, 12);
    ImGui::TextColored(kDim, "%d Hz", g_workerHz.load());
    if (!g_status.empty()) {
        ImGui::SameLine(0, 12);
        ImGui::TextColored(kAccentH, "%s", g_status.c_str());
    }
    dl->AddRect(ImVec2(0, 0), ImVec2(disp.x - 1, disp.y - 1), ImGui::ColorConvertFloat4ToU32(kLine));
    ImGui::End();
}

// ============================================================================
//  GUI thread
// ============================================================================
static void GuiThread() {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MSCTFIME UI";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
    const int ww = 600, wh = 540;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    
    // НЕТ WS_EX_TOPMOST! Иначе детект overlay
    g_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"Default IME",
        WS_POPUP | WS_MINIMIZEBOX,
        (sw - ww) / 2, (sh - wh) / 2, ww, wh,
        nullptr, nullptr, wc.hInstance, nullptr);
    
    if (!g_hwnd) return;
    if (!InitD3D(g_hwnd)) { DestroyWindow(g_hwnd); return; }
    
    ShowWindow(g_hwnd, SW_HIDE);
    UpdateWindow(g_hwnd);
    ApplyAntiCapture();
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImFontConfig fc; fc.OversampleH = 2; fc.OversampleV = 1;
    ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 15.0f,
                                                &fc, io.Fonts->GetGlyphRangesCyrillic());
    if (!font) io.Fonts->AddFontDefault();
    Theme();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);
    
    auto lastFrame = clk::now();
    MSG msg{};
    while (g_run) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) g_run = false;
        }
        if (!g_run) break;
        if (!g_visible || IsIconic(g_hwnd)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            lastFrame = clk::now();
            continue;
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        auto now = clk::now();
        float dt = std::chrono::duration<float>(now - lastFrame).count();
        lastFrame = now;
        if (g_statusTTL > 0) {
            g_statusTTL -= dt;
            if (g_statusTTL <= 0) { g_status.clear(); g_statusTTL = 0; }
        }
        DrawGUI();
        ImGui::Render();
        const float clear[4] = { kBg.x, kBg.y, kBg.z, 1.0f };
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        HRESULT hr = g_sc->Present(1, 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) break;
    }
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    ReleaseRTV();
    if (g_ctx) { g_ctx->ClearState(); g_ctx->Flush(); g_ctx->Release(); g_ctx = nullptr; }
    if (g_sc) { g_sc->Release(); g_sc = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
    if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = nullptr; }
    UnregisterClassW(L"MSCTFIME UI", wc.hInstance);
}

// ============================================================================
//  Hotkeys
// ============================================================================
static bool KeyEdge(int vk) {
    static std::map<int, bool> st;
    bool now = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool was = st[vk];
    st[vk] = now;
    return now && !was;
}

static void HotkeyThread() {
    while (g_run) {
        if (KeyEdge(VK_HOME)) {
            g_visible = !g_visible;
            ShowWindow(g_hwnd, g_visible ? SW_SHOW : SW_HIDE);
            if (g_visible) ApplyAntiCapture();
        }
        if (KeyEdge(g_cfg.keyToggle)) g_cfg.mode = (Mode)(((int)g_cfg.mode + 1) % 3);
        if (KeyEdge(g_cfg.keyPanic)) g_run = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

// ============================================================================
//  Init thread
// ============================================================================
static void InitThread() {
    HideFromPEB();
    LoadConfig();
    SyncAddrBufs();
    InitAntiCaptureAPI();
    LoadPattern(ExeDir() / "pattern.json");
    g_worker = std::thread(Worker);
    std::thread(GuiThread).detach();
    std::thread(HotkeyThread).detach();
}

// ============================================================================
//  DLL entry point
// ============================================================================
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);
        timeBeginPeriod(1);
        std::thread(InitThread).detach();
    }
    else if (reason == DLL_PROCESS_DETACH) {
        g_run = false;
        if (g_worker.joinable()) g_worker.join();
        if (g_recThread.joinable()) g_recThread.join();
        SaveConfig();
        RemoveAntiCapture();
        timeEndPeriod(1);
    }
    return TRUE;
}