#include "config.h"
#include "app.h"
#include <fstream>
#include <filesystem>
#include "json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

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

static fs::path ConfigPath() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path() / "vortex.json";
}

void LoadConfig() {
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
        g_cfg.modDir = W16(j.value("mod", std::string()));
        g_cfg.launcherDir = W16(j.value("launcher", std::string()));
        g_cfg.bundleFiles = j.value("bundle", 12);
        g_cfg.antiCapture = j.value("anticap", true);
        g_cfg.hideGame = j.value("hidegame", false);
        g_cfg.pauseOnF3 = j.value("f3", true);
        g_cfg.deleteOnExit = j.value("del", false);
        g_cfg.topmost = j.value("topmost", false);
        g_cfg.blockSnippingTool = j.value("snip", true);
        g_cfg.antiAnalysis = j.value("antianalysis", true);
        g_cfg.agentPath = W16(j.value("agent", std::string()));
        g_cfg.interceptorDll = W16(j.value("interceptor", std::string()));
        g_cfg.autoAttach = j.value("autoattach", false);
        g_cfg.injectIntoLauncher = j.value("injectlauncher", false);
        g_cfg.launcherExe = W16(j.value("launcherexe", std::string()));
        g_cfg.className = W16(j.value("cls", std::string()));
        g_cfg.winTitle = W16(j.value("title", std::string()));
    } catch (...) {}
}

void SaveConfig() {
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
    j["mod"] = U8(g_cfg.modDir);
    j["launcher"] = U8(g_cfg.launcherDir);
    j["bundle"] = g_cfg.bundleFiles;
    j["anticap"] = g_cfg.antiCapture;
    j["hidegame"] = g_cfg.hideGame;
    j["f3"] = g_cfg.pauseOnF3;
    j["del"] = g_cfg.deleteOnExit;
    j["topmost"] = g_cfg.topmost;
    j["snip"] = g_cfg.blockSnippingTool;
    j["antianalysis"] = g_cfg.antiAnalysis;
    j["agent"] = U8(g_cfg.agentPath);
    j["interceptor"] = U8(g_cfg.interceptorDll);
    j["autoattach"] = g_cfg.autoAttach;
    j["injectlauncher"] = g_cfg.injectIntoLauncher;
    j["launcherexe"] = U8(g_cfg.launcherExe);
    j["cls"] = U8(g_cfg.className);
    j["title"] = U8(g_cfg.winTitle);
    std::ofstream f(ConfigPath());
    if (f) f << j.dump(2);
}

void DeleteTraces() {
    std::error_code ec;
    fs::remove(ConfigPath(), ec);
    fs::remove(ConfigPath().parent_path() / "pattern.json", ec);
    fs::remove_all(ConfigPath().parent_path() / "ai_bundle", ec);
}