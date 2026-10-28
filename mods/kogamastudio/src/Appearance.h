#pragma once
#include "stdafx.h"
#include <string>

namespace Appearance {
    void Render();
    void LoadSettings();
    void SaveSettings();
    bool IsExtendedActive();

    extern std::string fontPath;
    extern float fontSize;
    extern bool allowDangerousDelete;
}
