#include "stdafx.h"

namespace globals {
    HMODULE mainModule = nullptr;
    HWND mainWindow = nullptr;
    bool enableDebugLog = false;

    void SetDebugLogging(bool enable) {
        enableDebugLog = enable;
    }
}
