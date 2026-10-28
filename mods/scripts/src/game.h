#pragma once
#include <string>

struct OkMod;

namespace game {
    void Attach(const OkMod* host);
    bool PlayerPosition(float* value);
    std::string PlayerName();
    bool CameraPosition(float* value);
}
