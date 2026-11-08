// ============================================================================
//  injector.exe — injects vortex.dll + noob_agent.jar into javaw.exe
//  build: cl /std:c++17 /O2 /EHsc /MD /DUNICODE /D_UNICODE injector.cpp
//         /link /SUBSYSTEM:WINDOWS user32.lib advapi32.lib shell32.lib ole32.lib
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>          // <-- ЭТО БЫЛО ПРОПУЩЕНО — главная причина ошибок
#include <shobjidl.h>
#include <shellapi.h>
#include <string>
#include <filesystem>
#include <cstdio>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace fs = std::filesystem;

static std::wstring PickFile(const wchar_t* filter, const wchar_t* desc) {
    IFileOpenDialog* d = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&d)))) return {};
    COMDLG_FILTERSPEC f[] = { { desc, filter } };
    d->SetFileTypes(1, f);
    std::wstring out;
    if (SUCCEEDED(d->Show(nullptr))) {
        IShellItem* it = nullptr;
        if (SUCCEEDED(d->GetResult(&it))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { out = p; CoTaskMemFree(p); }
            it->Release();
        }
    }
    d->Release();
    return out;
}

static DWORD FindJavaProcess() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (!_wcsicmp(pe.szExeFile, L"javaw.exe") || !_wcsicmp(pe.szExeFile, L"java.exe")) {
                pid = pe.th32ProcessID; break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static bool EnableDebugPrivilege() {
    HANDLE hTok;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hTok)) return false;
    TOKEN_PRIVILEGES tp{}; tp.PrivilegeCount = 1;
    LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &tp.Privileges[0].Luid);
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    BOOL ok = AdjustTokenPrivileges(hTok, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    CloseHandle(hTok);
    return ok != FALSE;
}

static bool InjectDll(DWORD pid, const std::wstring& dllPath) {
    HANDLE hProc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!hProc) return false;
    size_t sz = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(hProc, nullptr, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) { CloseHandle(hProc); return false; }
    if (!WriteProcessMemory(hProc, remote, dllPath.c_str(), sz, nullptr)) {
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE); CloseHandle(hProc); return false;
    }
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    auto pLoadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(k32, "LoadLibraryW");
    if (!pLoadLib) { VirtualFreeEx(hProc, remote, 0, MEM_RELEASE); CloseHandle(hProc); return false; }
    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, pLoadLib, remote, 0, nullptr);
    if (!hThread) { VirtualFreeEx(hProc, remote, 0, MEM_RELEASE); CloseHandle(hProc); return false; }
    WaitForSingleObject(hThread, 10000);
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);
    VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
    CloseHandle(hProc);
    return exitCode != 0;
}

static bool AttachJavaAgent(DWORD pid, const std::wstring& agentJar) {
    std::wstring jattach = fs::current_path() / "jattach.exe";
    if (fs::exists(jattach)) {
        std::wstring cmd = L"\"" + jattach + L"\" " + std::to_wstring(pid) + L" load instrument false \"" + agentJar + L"\"";
        STARTUPINFOW si{ sizeof(si) }; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 10000);
            DWORD exitCode = 1;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            return exitCode == 0;
        }
    }
    return false;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    
    std::wstring dllPath = PickFile(L"*.dll", L"DLL files");
    if (dllPath.empty()) { CoUninitialize(); return 0; }
    
    std::wstring agentPath = PickFile(L"*.jar", L"Java Agent");
    
    DWORD pid = FindJavaProcess();
    if (pid == 0) {
        MessageBoxW(nullptr, L"javaw.exe not found.\nLaunch the game first.", L"Injector", MB_ICONERROR);
        CoUninitialize(); return 1;
    }
    
    EnableDebugPrivilege();
    
    bool dllOk = InjectDll(pid, dllPath);
    
    bool agentOk = false;
    if (!agentPath.empty()) {
        agentOk = AttachJavaAgent(pid, agentPath);
    }
    
    wchar_t msg[512];
    swprintf_s(msg, L"DLL inject: %s\nAgent attach: %s\n\nPID: %lu",
        dllOk ? L"OK" : L"FAILED",
        agentPath.empty() ? L"skipped" : (agentOk ? L"OK" : L"FAILED"),
        (unsigned long)pid);
    MessageBoxW(nullptr, msg, L"Injector", dllOk ? MB_ICONINFORMATION : MB_ICONERROR);
    
    CoUninitialize();
    return dllOk ? 0 : 1;
}