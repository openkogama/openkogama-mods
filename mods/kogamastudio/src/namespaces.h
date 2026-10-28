#pragma once
#include <windows.h>
#include <d3d11.h>
#include "imgui/imgui.h"

namespace globals {
    extern HMODULE mainModule;
    extern HWND mainWindow;
    extern bool enableDebugLog;
    void SetDebugLogging(bool enable);
}

namespace hooks_dx11 {
    extern ID3D11Device* gDevice;
    extern ID3D11DeviceContext* gContext;
    ID3D11ShaderResourceView* GetGameFrameSRV();
    void GetGameFrameSize(UINT& width, UINT& height);
}

namespace menu {
    extern bool isOpen;
    extern bool viewportHovered;
    extern bool blockMouseInput;
    extern ImVec2 viewportImageMin;
    extern ImVec2 viewportImageMax;
    extern float menuBarHeight;
    extern bool captionDrag;
    extern bool welcomeOpen;
    extern void Init();
    void ShowPanel(const char* window);
    void SendCommand(const char* cmd);
}
