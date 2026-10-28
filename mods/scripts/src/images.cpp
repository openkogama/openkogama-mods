#include "images.h"
#include "runtime.h"
#include "textures.h"
#include "lua.h"
#include "lauxlib.h"
#include <windows.h>
#include <wincodec.h>
#include <filesystem>
#include <map>
#include <vector>

namespace images {
    namespace {
        constexpr UINT SideLimit = 4096;
        constexpr size_t PixelLimit = 32u << 20;

        struct Entry {
            Image image;
            size_t pixels = 0;
        };

        std::map<std::string, std::map<std::string, Entry>> loaded;
        IWICImagingFactory* factory = nullptr;

        bool Inside(const std::string& file) {
            if (file.empty() || file.size() > 200 || file.find(':') != std::string::npos) return false;
            if (file[0] == '/' || file[0] == '\\') return false;
            size_t start = 0;
            while (start <= file.size()) {
                size_t end = file.find_first_of("/\\", start);
                if (end == std::string::npos) end = file.size();
                std::string part = file.substr(start, end - start);
                if (part.empty() || part == "." || part == "..") return false;
                start = end + 1;
            }
            return true;
        }

        template <typename T> void Free(T*& object) {
            if (object) object->Release();
            object = nullptr;
        }

        bool Decode(const std::filesystem::path& path, std::vector<unsigned char>& pixels, UINT& width, UINT& height) {
            if (!factory) {
                CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return false;
            }
            IWICBitmapDecoder* decoder = nullptr;
            IWICBitmapFrameDecode* frame = nullptr;
            IWICFormatConverter* converter = nullptr;
            bool decoded = SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder))
                && SUCCEEDED(decoder->GetFrame(0, &frame))
                && SUCCEEDED(frame->GetSize(&width, &height))
                && width > 0 && height > 0 && width <= SideLimit && height <= SideLimit
                && SUCCEEDED(factory->CreateFormatConverter(&converter))
                && SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));
            if (decoded) {
                pixels.resize((size_t)width * height * 4);
                decoded = SUCCEEDED(converter->CopyPixels(nullptr, width * 4, (UINT)pixels.size(), pixels.data()));
            }
            Free(converter);
            Free(frame);
            Free(decoder);
            return decoded;
        }
    }

    Image Get(lua_State* L, const char* file) {
        if (!Inside(file)) luaL_error(L, "image '%s' has to be a file in the script folder", file);
        std::map<std::string, Entry>& files = loaded[runtime::Name(L)];
        auto known = files.find(file);
        if (known != files.end()) return known->second.image;
        size_t used = 0;
        for (const auto& entry : files) used += entry.second.pixels;
        Entry entry;
        std::vector<unsigned char> pixels;
        UINT width = 0, height = 0;
        if (Decode(runtime::Root(L) / std::filesystem::u8path(file), pixels, width, height) && used + (size_t)width * height <= PixelLimit) {
            entry.image.texture = textures::Create((int)width, (int)height, std::move(pixels));
            entry.image.width = (int)width;
            entry.image.height = (int)height;
            entry.pixels = (size_t)width * height;
        }
        else runtime::Log("[" + runtime::Name(L) + "] could not load the image " + file);
        files[file] = entry;
        return entry.image;
    }

    void Release(const std::string& script) {
        auto found = loaded.find(script);
        if (found == loaded.end()) return;
        for (const auto& entry : found->second)
            if (entry.second.image.texture) textures::Destroy(entry.second.image.texture);
        loaded.erase(found);
    }
}
