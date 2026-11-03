#include "files.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace files {
    namespace {
        constexpr std::uintmax_t FileLimit = 16u << 20;
        constexpr std::uintmax_t FolderLimit = 512u << 20;
        constexpr size_t NameLimit = 120;
        const char* const Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        struct Kind {
            const char* name;
            std::vector<std::string> extensions;
            bool readable;
            bool writable;
        };

        const std::vector<Kind>& Kinds() {
            static const std::vector<Kind> kinds = {
                {"Models", {".ktm"}, true, true},
                {"Crosshairs", {".png", ".jpg", ".jpeg"}, false, false},
                {"Screenshots", {".png"}, false, false},
            };
            return kinds;
        }

        const Kind* Find(const std::string& kind) {
            for (const Kind& known : Kinds())
                if (kind == known.name) return &known;
            return nullptr;
        }

        fs::path Root() {
            const wchar_t* appdata = _wgetenv(L"APPDATA");
            return fs::path(appdata ? appdata : L".") / L"KogamaTools";
        }

        fs::path Path(const std::string& kind) { return Root() / fs::u8path(kind); }

        std::string Lower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return text;
        }

        std::uintmax_t Used(const fs::path& folder) {
            std::uintmax_t total = 0;
            std::error_code error;
            for (const auto& entry : fs::directory_iterator(folder, error))
                if (entry.is_regular_file(error)) total += entry.file_size(error);
            return total;
        }
    }

    bool Known(const std::string& kind) { return Find(kind) != nullptr; }

    bool Writable(const std::string& kind) {
        const Kind* known = Find(kind);
        return known && known->writable;
    }

    bool Readable(const std::string& kind) {
        const Kind* known = Find(kind);
        return known && known->readable;
    }

    bool ValidName(const std::string& kind, const std::string& name) {
        const Kind* known = Find(kind);
        if (!known || name.empty() || name.size() > NameLimit || name[0] == '.' || name[0] == ' ') return false;
        for (unsigned char c : name)
            if (!std::isalnum(c) && c != ' ' && c != '-' && c != '_' && c != '.' && c != '(' && c != ')') return false;
        if (name.find("..") != std::string::npos) return false;
        std::string extension = Lower(fs::u8path(name).extension().u8string());
        return std::find(known->extensions.begin(), known->extensions.end(), extension) != known->extensions.end();
    }

    std::string Folder(const std::string& kind) { return Known(kind) ? Path(kind).u8string() : std::string(); }

    std::vector<std::string> List(const std::string& kind) {
        std::vector<std::string> names;
        if (!Known(kind)) return names;
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(Path(kind), error)) {
            if (!entry.is_regular_file(error)) continue;
            std::string name = entry.path().filename().u8string();
            if (ValidName(kind, name)) names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    bool Read(const std::string& kind, const std::string& name, std::string& data) {
        if (!ValidName(kind, name)) return false;
        fs::path file = Path(kind) / fs::u8path(name);
        std::error_code error;
        std::uintmax_t size = fs::file_size(file, error);
        if (error || size > FileLimit) return false;
        std::ifstream input(file, std::ios::binary);
        if (!input) return false;
        data.assign((size_t)size, '\0');
        input.read(data.data(), (std::streamsize)size);
        return (std::uintmax_t)input.gcount() == size;
    }

    bool Write(const std::string& kind, const std::string& name, const std::string& data, std::string& path) {
        if (!ValidName(kind, name) || data.size() > FileLimit) return false;
        fs::path folder = Path(kind);
        std::error_code error;
        fs::create_directories(folder, error);
        fs::path file = folder / fs::u8path(name);
        std::uintmax_t replaced = fs::exists(file, error) ? fs::file_size(file, error) : 0;
        if (Used(folder) - std::min(replaced, Used(folder)) + data.size() > FolderLimit) return false;
        std::ofstream output(file, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output.write(data.data(), (std::streamsize)data.size());
        if (!output) return false;
        path = file.u8string();
        return true;
    }

    std::string Encode(const std::string& data) {
        std::string text;
        text.reserve((data.size() + 2) / 3 * 4);
        size_t i = 0;
        for (; i + 2 < data.size(); i += 3) {
            unsigned value = ((unsigned char)data[i] << 16) | ((unsigned char)data[i + 1] << 8) | (unsigned char)data[i + 2];
            text += Alphabet[(value >> 18) & 63];
            text += Alphabet[(value >> 12) & 63];
            text += Alphabet[(value >> 6) & 63];
            text += Alphabet[value & 63];
        }
        if (i < data.size()) {
            unsigned value = (unsigned char)data[i] << 16;
            if (i + 1 < data.size()) value |= (unsigned char)data[i + 1] << 8;
            text += Alphabet[(value >> 18) & 63];
            text += Alphabet[(value >> 12) & 63];
            text += i + 1 < data.size() ? Alphabet[(value >> 6) & 63] : '=';
            text += '=';
        }
        return text;
    }

    bool Decode(const std::string& text, std::string& data) {
        data.clear();
        data.reserve(text.size() / 4 * 3);
        unsigned value = 0;
        int bits = 0;
        size_t padding = 0;
        for (char c : text) {
            if (c == '=') {
                padding++;
                continue;
            }
            if (padding) return false;
            const char* at = std::strchr(Alphabet, c);
            if (!at || !c) return false;
            value = (value << 6) | (unsigned)(at - Alphabet);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                data += (char)((value >> bits) & 0xFF);
            }
        }
        return padding <= 2;
    }
}
