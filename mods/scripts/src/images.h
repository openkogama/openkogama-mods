#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

struct lua_State;

namespace images {
    enum class State { Ready, Loading, Failed };
    using Source = State (*)(const std::string& id, std::filesystem::path& path);

    struct Image {
        uint64_t texture = 0;
        int width = 0;
        int height = 0;
    };

    void SetSource(Source source);
    bool Served(const std::string& file);
    State Want(const std::string& id);
    Image Get(lua_State* L, const char* file);
    void Release(const std::string& script);
    void Sweep();
}
