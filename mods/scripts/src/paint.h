#pragma once
#include "frame.h"
#include "textures.h"
#include <windows.h>
#include <memory>
#include <vector>

namespace render {
    struct Work {
        std::unique_ptr<frame::Frame> frame;
        std::vector<textures::Op> ops;
    };

    Work Take();
    HWND GameWindow();
    bool Quitting();
}

namespace paint9 {
    bool Hook();
    void Unhook();
}
