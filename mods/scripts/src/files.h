#pragma once
#include <string>
#include <vector>

namespace files {
    bool Known(const std::string& kind);
    bool Writable(const std::string& kind);
    bool Readable(const std::string& kind);
    bool ValidName(const std::string& kind, const std::string& name);
    std::string Folder(const std::string& kind);
    std::vector<std::string> List(const std::string& kind);
    bool Read(const std::string& kind, const std::string& name, std::string& data);
    bool Write(const std::string& kind, const std::string& name, const std::string& data, std::string& path);

    std::string Encode(const std::string& data);
    bool Decode(const std::string& text, std::string& data);
}
