#pragma once
#include "stdafx.h"
#include <iostream>
namespace Appearance {
    void Render(bool embedded = false);
    void LoadSettings();
    void SaveSettings();
    void ApplyTheme();
    std::string GetConfigPath();

    extern bool appearanceOpen;
    extern int selectedTheme;
    extern float rounding;
    extern bool showColorEditor;
    extern std::string fontPath;
    extern char fontPathBuf[256];
    extern float fontSize;

    // Large font for headers (set during init, may be null)
    extern ImFont* headerFont;

    // License
    bool IsExtendedActive();

    // Cubes Preview settings
    extern float previewColor[4];
    extern int   previewMode;       // 0 = Outline, 1 = Surface Grid, 2 = Full Grid
    extern float previewOpacity;

    // Danger Zone
    extern bool allowDangerousDelete;
}