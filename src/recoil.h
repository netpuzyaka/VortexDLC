#pragma once
#include "app.h"
#include <string>

void Worker();
void RecordThread();
bool LoadPattern(const std::wstring& path);
void SavePattern(const std::wstring& path);

// NoRecoil через патчинг памяти JVM
bool ApplyNoRecoil();
void RevertNoRecoil();
bool IsNoRecoilApplied();

// RapidFire
void ApplyRapidFire(float multiplier);
void RevertRapidFire();

// FOV Changer
void ApplyFOV(float fov);
void RevertFOV();