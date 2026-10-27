#pragma once
#include <windows.h>
#include <shlobj.h>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <share.h>
#include <dxgi.h>
#include <d3d11.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include "namespaces.h"
#include "UILocale.h"

inline FILE* GetLogFile() {
    static FILE* logFile = nullptr;
    static bool attempted = false;
    if (!attempted) {
        attempted = true;
        char localAppData[MAX_PATH] = {};
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
            char baseDir[MAX_PATH];
            snprintf(baseDir, MAX_PATH, "%s\\KogamaStudio", localAppData);
            CreateDirectoryA(baseDir, NULL);
            char logDir[MAX_PATH];
            snprintf(logDir, MAX_PATH, "%s\\Logs", baseDir);
            CreateDirectoryA(logDir, NULL);
            char logPath[MAX_PATH];
            snprintf(logPath, MAX_PATH, "%s\\okmod-menu.log", logDir);
            logFile = _fsopen(logPath, "w", _SH_DENYNO);
        }
    }
    return logFile;
}

inline void DebugLog(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    FILE* f = GetLogFile();
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%02d:%02d:%02d.%03d] %s", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
        fflush(f);
    }
    if (globals::enableDebugLog) OutputDebugStringA(buf);
}
