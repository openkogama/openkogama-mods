#include "host.h"

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <string>
#endif

extern "C" {
OKMOD_EXPORT int okmod_flag(const char *name) { return okmod::host_api()->flag_get(name); }
OKMOD_EXPORT double okmod_paramf(const char *name, double fallback) { return okmod::host_api()->param_get_num(name, fallback); }
OKMOD_EXPORT void okmod_pump() { okmod::pump(); }

OKMOD_EXPORT int okmod_gate(const char *hook) { return okmod::run_gate(hook); }

OKMOD_EXPORT int okmod_event(const char *hook, int64_t *args, int argc, int64_t *result) {
    return okmod::run_event(hook, args, argc, result);
}

OKMOD_EXPORT int64_t okmod_address(void *handle) { return okmod::runtime_address(handle); }
}

#if defined(_WIN32)

static DWORD WINAPI control_thread(LPVOID) {
    const char *name = "\\\\.\\pipe\\okmod-control";
    for (;;) {
        HANDLE pipe = CreateNamedPipeA(name, PIPE_ACCESS_INBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES, 0, 8192, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) { Sleep(1000); continue; }
        if (ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) {
            std::string acc;
            char buf[1024];
            DWORD n = 0;
            while (ReadFile(pipe, buf, sizeof(buf), &n, nullptr) && n) acc.append(buf, n);
            size_t nl;
            while ((nl = acc.find('\n')) != std::string::npos) {
                okmod::control_command(acc.substr(0, nl).c_str());
                acc.erase(0, nl + 1);
            }
            if (!acc.empty()) okmod::control_command(acc.c_str());
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    return 0;
}

static void start(HMODULE self) {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(self, path, MAX_PATH);
    std::string dir(path, n);
    size_t slash = dir.find_last_of("\\/");
    std::string mods = (slash == std::string::npos ? std::string(".") : dir.substr(0, slash)) + "\\mods";
    okmod::runtime_attach();
    okmod::load_mods(mods.c_str());
    CreateThread(nullptr, 0, control_thread, nullptr, 0, nullptr);
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        start(self);
    }
    return TRUE;
}

#endif
