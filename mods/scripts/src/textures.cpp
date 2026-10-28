#include "textures.h"
#include <utility>

namespace textures {
    namespace {
        uint64_t next = 1;
        std::vector<Op> queued;
    }

    uint64_t Create(int width, int height, std::vector<unsigned char> pixels) {
        Op op;
        op.kind = Op::Create;
        op.id = next++;
        op.width = width;
        op.height = height;
        op.pixels = std::move(pixels);
        queued.push_back(std::move(op));
        return queued.back().id;
    }

    void Update(uint64_t id, int x, int y, int width, int height, std::vector<unsigned char> pixels) {
        Op op;
        op.kind = Op::Update;
        op.id = id;
        op.x = x;
        op.y = y;
        op.width = width;
        op.height = height;
        op.pixels = std::move(pixels);
        queued.push_back(std::move(op));
    }

    void Destroy(uint64_t id) {
        Op op;
        op.kind = Op::Destroy;
        op.id = id;
        queued.push_back(std::move(op));
    }

    std::vector<Op> Take() {
        std::vector<Op> taken;
        taken.swap(queued);
        return taken;
    }
}
