#pragma once
#include <cstdint>
#include <string>

struct lua_State;

namespace images {
    struct Image {
        uint64_t texture = 0;
        int width = 0;
        int height = 0;
    };

    Image Get(lua_State* L, const char* file);
    void Release(const std::string& script);
}
