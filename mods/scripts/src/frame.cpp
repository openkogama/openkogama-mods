#include "frame.h"
#include "textures.h"
#include <cstring>

namespace frame {
    namespace {
        std::vector<unsigned char> Pixels(ImTextureData* texture, int x, int y, int width, int height) {
            std::vector<unsigned char> pixels((size_t)width * height * 4);
            for (int row = 0; row < height; row++) {
                const unsigned char* source = static_cast<const unsigned char*>(texture->GetPixelsAt(x, y + row));
                unsigned char* target = pixels.data() + (size_t)row * width * 4;
                if (texture->Format == ImTextureFormat_RGBA32) {
                    std::memcpy(target, source, (size_t)width * 4);
                    continue;
                }
                for (int column = 0; column < width; column++) {
                    target[column * 4 + 0] = 255;
                    target[column * 4 + 1] = 255;
                    target[column * 4 + 2] = 255;
                    target[column * 4 + 3] = source[column];
                }
            }
            return pixels;
        }

        void Upload(ImTextureData* texture) {
            if (texture->Status == ImTextureStatus_WantCreate) {
                uint64_t id = textures::Create(texture->Width, texture->Height, Pixels(texture, 0, 0, texture->Width, texture->Height));
                texture->SetTexID((ImTextureID)id);
                texture->SetStatus(ImTextureStatus_OK);
            }
            else if (texture->Status == ImTextureStatus_WantUpdates) {
                for (const ImTextureRect& rect : texture->Updates)
                    textures::Update((uint64_t)texture->TexID, rect.x, rect.y, rect.w, rect.h, Pixels(texture, rect.x, rect.y, rect.w, rect.h));
                texture->SetStatus(ImTextureStatus_OK);
            }
            if (texture->Status == ImTextureStatus_WantDestroy && texture->UnusedFrames > 0) {
                textures::Destroy((uint64_t)texture->TexID);
                texture->SetTexID(ImTextureID_Invalid);
                texture->SetStatus(ImTextureStatus_Destroyed);
            }
        }
    }

    std::unique_ptr<Frame> Build(ImDrawData* data) {
        auto frame = std::make_unique<Frame>();
        if (!data) return frame;
        frame->size = data->DisplaySize;
        if (data->Textures)
            for (ImTextureData* texture : *data->Textures) Upload(texture);
        frame->vertices.reserve((size_t)data->TotalVtxCount);
        frame->indices.reserve((size_t)data->TotalIdxCount);
        for (ImDrawList* list : data->CmdLists) {
            unsigned first = (unsigned)frame->indices.size();
            int base = (int)frame->vertices.size();
            frame->vertices.insert(frame->vertices.end(), list->VtxBuffer.begin(), list->VtxBuffer.end());
            frame->indices.insert(frame->indices.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());
            for (const ImDrawCmd& command : list->CmdBuffer) {
                if (command.UserCallback || command.ElemCount == 0) continue;
                frame->commands.push_back({command.ClipRect, command.GetTexID(), first + command.IdxOffset, command.ElemCount, base + (int)command.VtxOffset});
            }
        }
        return frame;
    }
}
