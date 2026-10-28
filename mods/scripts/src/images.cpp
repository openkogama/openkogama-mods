#include "images.h"
#include "runtime.h"
#include "textures.h"
#include "lua.h"
#include "lauxlib.h"
#include <windows.h>
#include <wincodec.h>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <map>
#include <vector>

namespace images {
    namespace {
        using Clock = std::chrono::steady_clock;

        constexpr UINT SideLimit = 4096;
        constexpr size_t PixelLimit = 32u << 20;
        constexpr int DecodesPerFrame = 4;
        constexpr std::chrono::seconds Unused(30);

        struct Entry {
            Image image;
            size_t pixels = 0;
            bool served = false;
            Clock::time_point drawn;
        };

        std::map<std::string, std::map<std::string, Entry>> loaded;
        IWICImagingFactory* factory = nullptr;
        Source source = nullptr;
        int decodes = 0;
        Clock::time_point swept;

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

        void Drop(const Entry& entry) {
            if (entry.image.texture) textures::Destroy(entry.image.texture);
        }

        bool Room(std::map<std::string, Entry>& files, size_t pixels) {
            for (;;) {
                size_t used = 0;
                auto oldest = files.end();
                for (auto it = files.begin(); it != files.end(); ++it) {
                    used += it->second.pixels;
                    if (it->second.served && it->second.pixels && (oldest == files.end() || it->second.drawn < oldest->second.drawn)) oldest = it;
                }
                if (used + pixels <= PixelLimit) return true;
                if (oldest == files.end()) return false;
                Drop(oldest->second);
                files.erase(oldest);
            }
        }
    }

    void SetSource(Source value) { source = value; }

    bool Served(const std::string& file) {
        if (file.size() != 64) return false;
        for (char c : file)
            if (!std::isdigit((unsigned char)c) && (c < 'a' || c > 'f')) return false;
        return true;
    }

    State Want(const std::string& id) {
        std::filesystem::path path;
        return source ? source(id, path) : State::Failed;
    }

    Image Get(lua_State* L, const char* file) {
        bool served = Served(file);
        if (!served && !Inside(file)) luaL_error(L, "image '%s' has to be a file in the script folder or a file id from the server", file);
        std::map<std::string, Entry>& files = loaded[runtime::Name(L)];
        auto known = files.find(file);
        if (known != files.end()) {
            known->second.drawn = Clock::now();
            return known->second.image;
        }
        std::filesystem::path path;
        if (served) {
            if (!source || source(file, path) != State::Ready || decodes >= DecodesPerFrame) return {};
            decodes++;
        }
        else path = runtime::Root(L) / std::filesystem::u8path(file);
        Entry entry;
        entry.served = served;
        entry.drawn = Clock::now();
        std::vector<unsigned char> pixels;
        UINT width = 0, height = 0;
        if (Decode(path, pixels, width, height) && Room(files, (size_t)width * height)) {
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
        for (const auto& entry : found->second) Drop(entry.second);
        loaded.erase(found);
    }

    void Sweep() {
        decodes = 0;
        Clock::time_point now = Clock::now();
        if (now - swept < std::chrono::seconds(1)) return;
        swept = now;
        for (auto& script : loaded)
            for (auto it = script.second.begin(); it != script.second.end();) {
                if (!it->second.served || now - it->second.drawn < Unused) {
                    ++it;
                    continue;
                }
                Drop(it->second);
                it = script.second.erase(it);
            }
    }
}
