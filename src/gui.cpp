#include "gui.h"
#include "config.h"
#include "memory.h"
#include "recoil.h"
#include "anticheat.h"
#include <imgui.h>
#include <thread>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>
#include <set>
#include <shobjidl.h>
#include <shlobj.h>

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

void Theme() {
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

static void SectionTitle(const char* t) {
    ImGui::Spacing();
    ImGui::TextColored(kAccent, "%s", t);
    ImGui::Dummy(ImVec2(0, 1));
}
static void Gap(float h = 4) { ImGui::Dummy(ImVec2(0, h)); }
static void Rule() { Gap(2); ImGui::Separator(); Gap(3); }

void SyncAddrBufs() {
    snprintf(g_app.pitchBuf, sizeof(g_app.pitchBuf), "%llX", (unsigned long long)g_cfg.pitchAddr);
    snprintf(g_app.yawBuf, sizeof(g_app.yawBuf), "%llX", (unsigned long long)g_cfg.yawAddr);
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
    ImGui::TextColored(kDim, "pitch");
    ImGui::SameLine(56);
    if (ImGui::InputText("##pitch", g_app.pitchBuf, sizeof(g_app.pitchBuf), ImGuiInputTextFlags_CharsHexadecimal))
        g_cfg.pitchAddr = (uintptr_t)_strtoui64(g_app.pitchBuf, nullptr, 16);
    ImGui::TextColored(kDim, "yaw");
    ImGui::SameLine(56);
    if (ImGui::InputText("##yaw", g_app.yawBuf, sizeof(g_app.yawBuf), ImGuiInputTextFlags_CharsHexadecimal))
        g_cfg.yawAddr = (uintptr_t)_strtoui64(g_app.yawBuf, nullptr, 16);

    float y, p;
    bool live = ReadF(g_cfg.yawAddr, y) && ReadF(g_cfg.pitchAddr, p);
    Gap(2);
    if (live) ImGui::TextColored(kGreen, "yaw %.3f   pitch %.3f", y, p);
    else      ImGui::TextColored(kDim, "no readable data");
    Gap(4);

    if (g_app.autoScanning) {
        ImGui::TextColored(kYellow, "Auto-scanning... Do not move mouse!");
    } else {
        if (ImGui::Button("AUTO-SCAN ADDRESSES", ImVec2(-1, 30))) {
            if (!g_app.proc) SetStatus("Attach to game first!");
            else { std::thread t(AutoScanThread); t.detach(); }
        }
    }
    Rule();

    SectionTitle("BEHAVIOR");
    ImGui::Checkbox("Require right mouse (ADS)", &g_cfg.requireAds);
    ImGui::Checkbox("Add noise", &g_cfg.humanize);
    if (g_cfg.humanize) ImGui::SliderFloat("##jit", &g_cfg.jitter, 0.0f, 3.0f, "jitter %.2f px");
    ImGui::SliderFloat("##maxstep", &g_cfg.maxStep, 5.0f, 90.0f, "reject jumps > %.0f deg");
}

static void PagePattern() {
    size_t n;
    { std::lock_guard<std::mutex> lk(g_app.patternMx); n = g_app.pattern.size(); }
    ImGui::TextColored(kDim, "%zu steps loaded", n);
    Gap(6);

    if (g_app.recording.load()) {
        ImGui::ProgressBar(g_app.recProgress.load(), ImVec2(-1, 6), "");
        Gap(4);
        if (ImGui::Button("Cancel", ImVec2(-1, 28))) g_app.recording = false;
    } else {
        bool can = g_cfg.pitchAddr && g_cfg.yawAddr && g_app.proc.load();
        if (ImGui::Button("Record (3s delay)", ImVec2(-1, 30)) && can) {
            if (g_app.recThread.joinable()) g_app.recThread.join();
            g_app.recProgress = 0; g_app.recording = true;
            g_app.recThread = std::thread(RecordThread);
        }
        if (!can) ImGui::TextColored(kDim, "attach + set addresses first");
    }
    Gap(6);

    SectionTitle("TIMING");
    ImGui::SliderFloat("##delay", &g_cfg.shotDelayMs, 20.0f, 500.0f, "interval %.0f ms");
    ImGui::SliderInt("##shots", &g_cfg.shotCount, 5, 100, "shots %d");
    ImGui::Checkbox("Loop pattern", &g_cfg.patternLoop);
    Rule();

    if (ImGui::Button("Load pattern.json", ImVec2(-1, 26))) {
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::filesystem::path p = std::filesystem::path(buf).parent_path() / "pattern.json";
        LoadPattern(p.wstring());
        SetStatus("pattern loaded");
    }
    if (ImGui::Button("Save pattern.json", ImVec2(-1, 26)) && n > 0) {
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::filesystem::path p = std::filesystem::path(buf).parent_path() / "pattern.json";
        SavePattern(p.wstring());
        SetStatus("pattern saved");
    }
}

static void PageSettings() {
    SectionTitle("INPUT API");
    const char* api[] = { "SendInput", "mouse_event" };
    ImGui::Combo("##api", &g_cfg.legacyApi, api, 2);
    ImGui::TextColored(kDim, "worker: %d Hz", g_app.workerHz.load());
    Rule();

    SectionTitle("STEALTH");
    bool ac = g_cfg.antiCapture;
    if (ImGui::Checkbox("Hide this menu from capture", &ac)) {
        g_cfg.antiCapture = ac;
        ApplyAntiCapture();
    }
    bool hg = g_cfg.hideGame;
    if (ImGui::Checkbox("Hide GAME from capture (black screen)", &hg)) {
        g_cfg.hideGame = hg;
        ToggleGameCapture(hg);
    }
    Rule();

    SectionTitle("SAFETY");
    ImGui::Checkbox("Pause when game not focused", &g_cfg.pauseOnF3);
    bool tm = g_cfg.topmost;
    if (ImGui::Checkbox("Always on top", &tm)) {
        g_cfg.topmost = tm;
        SetWindowPos(g_app.hwnd, tm ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    ImGui::Checkbox("Delete traces on exit", &g_cfg.deleteOnExit);
    Rule();

    SectionTitle("HOTKEYS");
    ImGui::TextColored(kDim, "HOME   show / hide");
    ImGui::TextColored(kDim, "F8     cycle mode");
    ImGui::TextColored(kDim, "DEL    stop + close");
    Rule();

    if (ImGui::Button("Save config", ImVec2(-1, 28))) { SaveConfig(); SetStatus("config saved"); }
    if (ImGui::Button("Stop and close", ImVec2(-1, 28))) g_app.running = false;
}

void DrawGUI() {
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
    static ImVec2 dsm = { 0, 0 };
    static POINT dsw = { 0, 0 };
    static bool dragging = false;
    if (ImGui::IsItemActive()) {
        if (!dragging) {
            dragging = true;
            dsm = io.MousePos;
            RECT r; GetWindowRect(g_app.hwnd, &r);
            dsw = { r.left, r.top };
        }
        float dx = io.MousePos.x - dsm.x;
        float dy = io.MousePos.y - dsm.y;
        SetWindowPos(g_app.hwnd, nullptr, dsw.x + (int)dx, dsw.y + (int)dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    } else dragging = false;

    float ty = (headerH - ImGui::GetFontSize()) * 0.5f;
    dl->AddText(ImVec2(14, ty), ImGui::ColorConvertFloat4ToU32(kAccent), "vortex");
    dl->AddText(ImVec2(14 + ImGui::CalcTextSize("vortex").x + 10, ty), ImGui::ColorConvertFloat4ToU32(kDim), "rustmine tool");

    const char* mn[] = { "off", "compensate", "pattern" };
    ImVec4 mc = (g_cfg.mode == Mode::Off) ? kDim : kGreen;
    const char* mt = mn[(int)g_cfg.mode];
    float mw = ImGui::CalcTextSize(mt).x;
    dl->AddText(ImVec2(disp.x - 76.0f - mw - 12.0f, ty), ImGui::ColorConvertFloat4ToU32(mc), mt);

    ImGui::SetCursorPos(ImVec2(disp.x - 68.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.06f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1,1,1,0.10f));
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    if (ImGui::Button("_", ImVec2(26, 24))) ShowWindow(g_app.hwnd, SW_MINIMIZE);
    ImGui::SameLine(0, 4);
    ImGui::PopStyleColor(4);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(kRed.x, kRed.y, kRed.z, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kRed);
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    if (ImGui::Button("x", ImVec2(26, 24))) g_app.running = false;
    ImGui::PopStyleColor(4);

    float bodyH = disp.y - headerH - footerH - pad * 2.0f;
    if (bodyH < 60) bodyH = 60;

    ImGui::SetCursorPos(ImVec2(pad, headerH + pad));
    ImGui::BeginChild("rail", ImVec2(railW, bodyH), false);
    if (ImGui::Button("Main", ImVec2(-1, 30))) g_app.page = 0;
    if (ImGui::Button("Pattern", ImVec2(-1, 30))) g_app.page = 1;
    if (ImGui::Button("Settings", ImVec2(-1, 30))) g_app.page = 2;
    ImGui::EndChild();

    ImGui::SameLine(0, pad);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel2);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
    ImGui::BeginChild("body", ImVec2(0, bodyH), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImGui::PushItemWidth(-1);
    switch (g_app.page) {
    case 0: PageMain(); break;
    case 1: PagePattern(); break;
    default: PageSettings(); break;
    }
    ImGui::PopItemWidth();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    float fy = disp.y - footerH;
    dl->AddLine(ImVec2(0, fy), ImVec2(disp.x, fy), ImGui::ColorConvertFloat4ToU32(kLine));
    ImGui::SetCursorPos(ImVec2(pad, fy + (footerH - ImGui::GetFontSize()) * 0.5f));

    DWORD pid = g_app.pid.load();
    if (pid) ImGui::TextColored(kDim, "pid %lu", (unsigned long)pid);
    else {
        ImGui::TextColored(kDim, "no process");
        ImGui::SameLine(0, 6);
        if (ImGui::SmallButton("attach")) {
            RefreshCandidates();
            SetStatus((!g_app.candidates.empty() && Attach(g_app.candidates[0])) ? "attached" : "java not found");
        }
    }
    ImGui::SameLine(0, 12);
    ImGui::TextColored(kDim, "%d Hz", g_app.workerHz.load());

    if (!g_app.status.empty()) {
        ImGui::SameLine(0, 12);
        ImGui::TextColored(kAccentH, "%s", g_app.status.c_str());
    }

    dl->AddRect(ImVec2(0, 0), ImVec2(disp.x - 1, disp.y - 1), ImGui::ColorConvertFloat4ToU32(kLine));
    ImGui::End();
}