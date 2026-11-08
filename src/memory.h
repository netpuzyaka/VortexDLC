#pragma once
#include "app.h"
#include <filesystem>

void RefreshCandidates();
bool Attach(DWORD pid);
void Detach();
bool ReadF(uintptr_t addr, float& out);
HWND GameHwnd();
bool GameActive();
void AutoScanThread();
std::filesystem::path ExeDir();