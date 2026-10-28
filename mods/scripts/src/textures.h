#pragma once
#include <cstdint>
#include <vector>

namespace textures {
    struct Op {
        enum Kind { Create, Update, Destroy } kind = Create;
        uint64_t id = 0;
        int x = 0, y = 0, width = 0, height = 0;
        std::vector<unsigned char> pixels;
    };

    uint64_t Create(int width, int height, std::vector<unsigned char> pixels);
    void Update(uint64_t id, int x, int y, int width, int height, std::vector<unsigned char> pixels);
    void Destroy(uint64_t id);
    std::vector<Op> Take();
}
