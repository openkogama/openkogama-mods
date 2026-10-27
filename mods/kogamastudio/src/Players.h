#pragma once
#include <string>
#include <vector>

namespace Players {
    struct PlayerEntry {
        std::string id;
        std::string name;
    };

    extern std::vector<PlayerEntry> playerList;
    void SetPlayers(const std::string& csv);
    void Render();
}
