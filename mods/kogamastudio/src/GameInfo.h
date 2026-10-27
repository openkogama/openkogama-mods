#pragma once
#include <string>

namespace GameInfo {
    extern float fps;
    extern int ping;
    extern int worldObjects;
    extern int logicObjects;
    extern int links;
    extern int objectLinks;
    extern int prototypes;
    extern int uniquePrototypes;
    extern std::string gameVersion;

    extern int playerCount;
    extern std::string localPlayer;
    extern std::string serverIP;
    extern int profileID;
    extern int planetID;
    extern std::string gameMode;
    extern std::string region;
    extern std::string language;

    void SetData(const std::string& data);
    void Render();
}
