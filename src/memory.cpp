#include "memory.h"
#include <tlhelp32.h>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <thread>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

std::filesystem::path ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
}

void RefreshCandidates() {
    g_app.candidates.clear();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (!_wcsicmp(pe.szExeFile, L"javaw.exe") || !_wcsicmp(pe.szExeFile, L"java.exe"))
                g_app.candidates.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

void Detach() {
    HANDLE old = g_app.proc.exchange(nullptr);
    if (old) CloseHandle(old);
    g_app.pid = 0;
}

bool Attach(DWORD pid) {
    std::lock_guard<std::mutex> lk(g_app.attachMx);
    Detach();
    if (!pid) return false;
    HANDLE h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    g_app.proc = h;
    g_app.pid = pid;
    return true;
}

bool ReadF(uintptr_t addr, float& out) {
    HANDLE h = g_app.proc.load(std::memory_order_acquire);
    if (!h || !addr) return false;
    SIZE_T got = 0;
    if (!ReadProcessMemory(h, (LPCVOID)addr, &out, sizeof(float), &got) || got != sizeof(float))
        return false;
    return std::isfinite(out);
}

HWND GameHwnd() {
    HWND h = FindWindowW(L"GLFW30", nullptr);
    if (!h) h = FindWindowW(L"LWJGL", nullptr);
    if (!h) h = FindWindowW(nullptr, L"Minecraft");
    return h;
}

bool GameActive() {
    HWND h = GameHwnd();
    if (!h) return false;
    if (IsIconic(h)) return false;
    return GetForegroundWindow() == h;
}

struct FloatNode { uintptr_t addr; float initialVal; };

void AutoScanThread() {
    HANDLE h = g_app.proc.load();
    if (!h) return;
    g_app.autoScanning = true;
    SetStatus("Auto-scan: look straight. 3s...");
    std::this_thread::sleep_for(std::chrono::seconds(3));
    std::vector<FloatNode> allFloats;
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t address = 0;
    while (VirtualQueryEx(h, (LPCVOID)address, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT && (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE)) {
            std::vector<BYTE> buffer(mbi.RegionSize);
            SIZE_T bytesRead;
            if (ReadProcessMemory(h, mbi.BaseAddress, buffer.data(), mbi.RegionSize, &bytesRead)) {
                for (size_t i = 0; i + 4 <= bytesRead; i += 4) {
                    float val = *(float*)(buffer.data() + i);
                    if (val >= -360.0f && val <= 360.0f && std::abs(val) > 0.001f && !std::isnan(val) && !std::isinf(val))
                        allFloats.push_back({ (uintptr_t)mbi.BaseAddress + i, val });
                }
            }
        }
        address += mbi.RegionSize;
    }
    std::unordered_map<uintptr_t, float> orig;
    for (auto& f : allFloats) orig[f.addr] = f.initialVal;
    SetStatus("Scanning YAW...");
    MoveMouse(100, 0); Sleep(200);
    std::vector<uintptr_t> yawCand;
    for (auto& f : allFloats) {
        float val;
        if (ReadF(f.addr, val)) {
            float d = val - f.initialVal;
            if (std::abs(d) > 0.5f && std::abs(d) < 90.0f) yawCand.push_back(f.addr);
        }
    }
    MoveMouse(-100, 0); Sleep(200);
    std::vector<uintptr_t> finalYaw;
    for (auto addr : yawCand) {
        float val;
        if (ReadF(addr, val) && std::abs(val - orig[addr]) < 2.0f) finalYaw.push_back(addr);
    }
    SetStatus("Scanning PITCH...");
    MoveMouse(0, -100); Sleep(200);
    std::vector<uintptr_t> pitchCand;
    for (auto& f : allFloats) {
        float val;
        if (ReadF(f.addr, val)) {
            float d = val - f.initialVal;
            if (std::abs(d) > 0.5f && std::abs(d) < 90.0f) pitchCand.push_back(f.addr);
        }
    }
    MoveMouse(0, 100); Sleep(200);
    std::vector<uintptr_t> finalPitch;
    for (auto addr : pitchCand) {
        float val;
        if (ReadF(addr, val) && std::abs(val - orig[addr]) < 2.0f) finalPitch.push_back(addr);
    }
    if (!finalYaw.empty()) g_cfg.yawAddr = finalYaw[0];
    if (!finalPitch.empty()) g_cfg.pitchAddr = finalPitch[0];
    SaveConfig();
    g_app.autoScanning = false;
    SetStatus("Auto-scan complete.");
    Beep(1500, 200);
}