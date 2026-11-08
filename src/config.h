#pragma once
#include <windows.h>
#include <string>
#include <cstdint>

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
    std::wstring modDir;
    std::wstring launcherDir;
    int          bundleFiles = 12;
    bool         antiCapture = true;
    bool         hideGame = false;
    bool         pauseOnF3 = true;
    bool         deleteOnExit = false;
    bool         topmost = false;
    bool         blockSnippingTool = true;
    bool         antiAnalysis = true;
    int          keyToggle = VK_F8;
    int          keyPanic = VK_DELETE;
    std::wstring agentPath;
    std::wstring interceptorDll;
    bool         autoAttach = false;
    bool         injectIntoLauncher = false;
    std::wstring launcherExe;
    std::wstring className = L"";
    std::wstring winTitle = L"";
};

void LoadConfig();
void SaveConfig();
void DeleteTraces();