#include "app.h"
#include <cmath>

AppContext g_app;
Config g_cfg;
float g_accX = 0;
float g_accY = 0;

void SetStatus(const std::string& s) {
    g_app.status = s;
    g_app.statusTTL = 4.0f;
}

void MoveMouse(float dx, float dy) {
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

void AppShutdown() {
    g_app.running = false;
    if (g_app.workerThread.joinable()) g_app.workerThread.join();
    if (g_app.recThread.joinable()) g_app.recThread.join();
    if (g_app.scanThread.joinable()) g_app.scanThread.join();
}