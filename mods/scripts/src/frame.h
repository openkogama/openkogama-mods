#pragma once
#include "imgui.h"
#include <memory>
#include <vector>

namespace frame {
    struct Command {
        ImVec4 clip;
        ImTextureID texture;
        unsigned first;
        unsigned count;
        int base;
    };

    struct Frame {
        ImVec2 size{0, 0};
        std::vector<ImDrawVert> vertices;
        std::vector<ImDrawIdx> indices;
        std::vector<Command> commands;
    };

    std::unique_ptr<Frame> Build(ImDrawData* data);
}
