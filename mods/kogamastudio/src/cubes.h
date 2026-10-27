#pragma once
#include "okmod.h"
#include <string>

namespace cubes {
    void Attach(const OkMod* host);
    bool Handle(const std::string& command, const std::string& value);
    void Tick();
}
