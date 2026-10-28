#pragma once
#include <windows.h>

namespace overlay {
    void Start(HWND game);
    bool Running();
    bool Failed();
    void Release();
    void Minimize();
    void ToggleMaximize();
    void Close();
    bool Maximized();
    const char* Title();
}
