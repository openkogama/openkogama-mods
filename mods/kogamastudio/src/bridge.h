#pragma once
#include <string>

namespace bridge {
    void ToGame(const std::string& command);
    bool NextForGame(std::string& command);
    void ToMenu(const std::string& message);
    bool NextForMenu(std::string& message);
}
