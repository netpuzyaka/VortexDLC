#pragma once
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include "config.h"

struct Step { float dx = 0, dy = 0, dt = 0; };

struct Hit { std::string file; int line = 0; int score = 0; std::string tag, code; };
struct NumTable { std::string file, name; int line = 0; std::vector<float> v; };
struct KV { std::string file, key, val; };
struct InterceptPoint {
    std::string file; int line = 0; std::string type, direction, description, code; int priority = 0;
};
struct Report {
    std::vector<Hit> hits;
    std::vector<NumTable> tables;
    std::vector<KV> config;
    std::vector<InterceptPoint> intercepts;
    std::vector<std::wstring> topFiles;
    int files = 0, lines = 0, dropped = 0, dupes = 0;
    double ms = 0;
};

struct AppContext {
    HWND hwnd = nullptr;
    bool visible = true;
    std::atomic<bool> running{ true };
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain* sc = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    std::atomic<HANDLE> proc{ nullptr };
    std::atomic<DWORD> pid{ 0 };
    std::vector<DWORD> candidates;
    std::mutex attachMx;
    std::vector<Step> pattern;
    std::mutex patternMx;
    std::atomic<bool> recording{ false };
    std::atomic<float> recProgress{ 0 };
    std::atomic<bool> autoScanning{ false };
    std::atomic<float> scanProgress{ 0 };
    std::thread workerThread;
    std::thread recThread;
    std::thread scanThread;
    std::atomic<int> workerHz{ 0 };
    bool acSupported = false;
    DWORD osBuild = 0;
    int page = 0;
    char pitchBuf[32] = "";
    char yawBuf[32] = "";
    std::string status;
    float statusTTL = 0;
    
    std::shared_ptr<Report> report;
    std::mutex reportMx;
    std::atomic<bool> scanning{ false };
    std::atomic<float> scanProg{ 0 };
    
    bool noRecoilEnabled = false;
    int noRecoilPatchesApplied = 0;
    
    std::atomic<int> injectStatus{ 0 };
    std::wstring injectMsg;
    
    bool debugDetected = false;
    bool vmDetected = false;
    bool acApplied = false;
    DWORD acMode = 0;
};

extern AppContext g_app;
extern Config g_cfg;
extern float g_accX;
extern float g_accY;

void SetStatus(const std::string& s);
void MoveMouse(float dx, float dy);
void AppShutdown();