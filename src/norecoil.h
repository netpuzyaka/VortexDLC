#pragma once
#include "app.h"
#include <string>
#include <vector>

struct NoRecoilPatch {
    std::string className;
    std::string methodName;
    std::string descriptor;
    uintptr_t address = 0;
    std::vector<uint8_t> originalBytes;
    std::vector<uint8_t> patchBytes;
    bool applied = false;
    std::string description;
};

class NoRecoilManager {
public:
    static std::vector<NoRecoilPatch> FindPatches();
    static bool ApplyPatch(NoRecoilPatch& patch);
    static bool RevertPatch(NoRecoilPatch& patch);
    static void ApplyAll();
    static void RevertAll();
    static bool IsApplied();

private:
    static std::vector<NoRecoilPatch> g_patches;
    static bool g_applied;

    static uintptr_t FindMethodAddress(const std::string& className, const std::string& methodName);
    static std::vector<uint8_t> ReadBytes(uintptr_t addr, size_t len);
    static bool WriteBytes(uintptr_t addr, const std::vector<uint8_t>& bytes);
};