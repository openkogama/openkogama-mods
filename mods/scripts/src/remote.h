#pragma once
#include "images.h"
#include <filesystem>
#include <string>
#include <vector>

namespace remote {
    struct Message {
        std::string name;
        std::string data;
    };

    std::filesystem::path Start(const std::filesystem::path& cache);
    void Refresh();
    void Stop();
    bool Send(const std::string& name, const std::string& data);
    std::vector<Message> Take();
    images::State Want(const std::string& id, std::filesystem::path& path);
}
