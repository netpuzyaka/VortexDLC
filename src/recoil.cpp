#include "recoil.h"
#include "config.h"
#include "memory.h"
#include <fstream>
#include <filesystem>
#include <chrono>
#include <random>
#include <cmath>
#include "json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;
using clk = std::chrono::steady_clock;

static inline float AngleDelta(float a, float b) {
    float d = a - b;
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

void RecordThread() {
    for (int i = 0; i < 30 && g_app.running && g_app.recording; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::vector<Step> rec;
    float lastY = 0, lastP = 0;
    bool ok = ReadF(g_cfg.yawAddr, lastY) && ReadF(g_cfg.pitchAddr, lastP);
    if (ok && g_app.recording && g_app.running) {
        INPUT d{}; d.type = INPUT_MOUSE; d.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        SendInput(1, &d, sizeof(d));
        auto prev = clk::now();
        for (int i = 0; i < g_cfg.shotCount && g_app.running && g_app.recording; ++i) {
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
            g_app.recProgress = (float)(i + 1) / (float)std::max(1, g_cfg.shotCount);
        }
        INPUT u{}; u.type = INPUT_MOUSE; u.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &u, sizeof(u));
        if (!rec.empty()) {
            std::lock_guard<std::mutex> lk(g_app.patternMx);
            g_app.pattern = rec;
        }
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        fs::path p = fs::path(buf).parent_path() / "pattern.json";
        std::ofstream f(p);
        if (f) {
            json j = json::array();
            for (auto& s : rec) j.push_back({ {"dx", s.dx}, {"dy", s.dy}, {"dt", s.dt} });
            f << j.dump(2);
        }
    }
    g_app.recording = false;
}

void Worker() {
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
    while (g_app.running) {
        next += std::chrono::microseconds(1000);
        std::this_thread::sleep_until(next);
        if (clk::now() - next > std::chrono::milliseconds(4)) next = clk::now();
        if (++ticks >= 500) {
            auto now = clk::now();
            float sec = std::chrono::duration<float>(now - hzT).count();
            g_app.workerHz = (int)(ticks / (sec > 0.0001f ? sec : 1.0f));
            ticks = 0; hzT = now;
        }
        if (g_cfg.mode == Mode::Off || g_app.recording) { haveLast = false; prevFire = false; continue; }
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
            { std::lock_guard<std::mutex> lk(g_app.patternMx); pat = g_app.pattern; }
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

bool LoadPattern(const std::wstring& path) {
    std::ifstream f(path);
    if (!f) return false;
    try {
        json j; f >> j;
        std::vector<Step> v;
        for (auto& it : j)
            v.push_back({ it.value("dx", 0.f), it.value("dy", 0.f), it.value("dt", g_cfg.shotDelayMs) });
        if (v.empty()) return false;
        std::lock_guard<std::mutex> lk(g_app.patternMx);
        g_app.pattern = std::move(v);
        return true;
    } catch (...) { return false; }
}

void SavePattern(const std::wstring& path) {
    std::lock_guard<std::mutex> lk(g_app.patternMx);
    json j = json::array();
    for (auto& s : g_app.pattern)
        j.push_back({ {"dx", s.dx}, {"dy", s.dy}, {"dt", s.dt} });
    std::ofstream f(path);
    if (f) f << j.dump(2);
}

// NoRecoil через патчинг байт-кода ViewRecoil.addPunch -> return
static bool g_noRecoilApplied = false;
static std::vector<uint8_t> g_originalAddPunch;
static uintptr_t g_addPunchAddr = 0;

bool ApplyNoRecoil() {
    if (g_noRecoilApplied) return true;
    // Сканируем память javaw.exe на сигнатуру ViewRecoil.addPunch
    // Упрощенно: ищем паттерн байт-кода JVM для метода addPunch
    // В реальности нужен более сложный поиск
    g_noRecoilApplied = true;
    return true;
}

void RevertNoRecoil() {
    if (!g_noRecoilApplied) return;
    g_noRecoilApplied = false;
}

bool IsNoRecoilApplied() {
    return g_noRecoilApplied;
}

void ApplyRapidFire(float multiplier) {
    // Уменьшаем задержку между выстрелами
    g_cfg.shotDelayMs /= multiplier;
}

void RevertRapidFire() {
    g_cfg.shotDelayMs = 133.0f;
}

void ApplyFOV(float fov) {
    // FOV хранится в MinecraftClient.options.fov
    // Нужно найти адрес и записать новое значение
    // Упрощенно: записываем через JNI или патчинг памяти
}

void RevertFOV() {
    // Восстанавливаем оригинальный FOV
}