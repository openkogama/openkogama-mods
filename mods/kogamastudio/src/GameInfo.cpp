#include "stdafx.h"
#include "GameInfo.h"
#include "MenuHelpers.h"

using namespace menu;

namespace GameInfo {

    float fps = 0.0f;
    int ping = 0;
    int worldObjects = 0;
    int logicObjects = 0;
    int links = 0;
    int objectLinks = 0;
    int prototypes = 0;
    int uniquePrototypes = 0;
    std::string gameVersion = "";

    int playerCount = 0;
    std::string localPlayer = "";
    std::string serverIP = "";
    int profileID = 0;
    int planetID = 0;
    std::string gameMode = "";
    std::string region = "";
    std::string language = "";

    void SetData(const std::string& data)
    {
        std::string s = data;
        while (!s.empty())
        {
            size_t comma = s.find(',');
            std::string entry = (comma != std::string::npos) ? s.substr(0, comma) : s;
            s = (comma != std::string::npos) ? s.substr(comma + 1) : "";

            size_t eq = entry.find('=');
            if (eq == std::string::npos) continue;
            std::string key = entry.substr(0, eq);
            std::string val = entry.substr(eq + 1);

            if      (key == "fps")              fps = std::stof(val);
            else if (key == "ping")             ping = std::stoi(val);
            else if (key == "worldobjects")     worldObjects = std::stoi(val);
            else if (key == "logicobjects")     logicObjects = std::stoi(val);
            else if (key == "links")            links = std::stoi(val);
            else if (key == "objectlinks")      objectLinks = std::stoi(val);
            else if (key == "prototypes")       prototypes = std::stoi(val);
            else if (key == "uniqueprototypes") uniquePrototypes = std::stoi(val);
            else if (key == "version")          gameVersion = val;
            else if (key == "players")          playerCount = std::stoi(val);
            else if (key == "localplayer")      localPlayer = val;
            else if (key == "serverip")         serverIP = val;
            else if (key == "profileid")        profileID = std::stoi(val);
            else if (key == "planetid")         planetID = std::stoi(val);
            else if (key == "gamemode")         gameMode = val;
            else if (key == "region")           region = val;
            else if (key == "language")         language = val;
        }
    }

    void Render()
    {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
        if (ImGui::Begin((std::string(TR(u8"Game Info")) + u8"###GameInfo").c_str(), nullptr, flags))
        {
            ImGui::Text("%s: %.1f", TR(u8"FPS"), fps);
            ImGui::SameLine(0, 20);
            ImGui::Text("%s: %d ms", TR(u8"Ping"), ping);
            ImGui::Separator();

            if (ImGui::CollapsingHeader(TR(u8"Session"), ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Text("%s: %s", TR(u8"Player"),         localPlayer.c_str());
                ImGui::Text("%s: %d", TR(u8"Profile ID"),     profileID);
                ImGui::Text("%s: %d", TR(u8"Planet ID"),      planetID);
                ImGui::Text("%s: %s", TR(u8"Game Mode"),      gameMode.c_str());
                ImGui::Text("%s: %s", TR(u8"Server IP"),      serverIP.c_str());
                ImGui::Text("%s: %s", TR(u8"Region"),         region.c_str());
                ImGui::Text("%s: %s", TR(u8"Language"),       language.c_str());
                ImGui::Text("%s: %d", TR(u8"Players Online"), playerCount);
            }

            if (ImGui::CollapsingHeader(TR(u8"World"), ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Text("%s: %d", TR(u8"World Objects"),     worldObjects);
                ImGui::Text("%s: %d", TR(u8"Logic Objects"),    logicObjects);
                ImGui::Text("%s: %d", TR(u8"Links"),            links);
                ImGui::Text("%s: %d", TR(u8"Object Links"),     objectLinks);
                ImGui::Text("%s: %d", TR(u8"Prototypes"),       prototypes);
                ImGui::Text("%s: %d", TR(u8"Unique Prototypes"), uniquePrototypes);
            }

            ImGui::Separator();
            ImGui::Text("%s: %s", TR(u8"Game Version"), gameVersion.c_str());
        }
        ImGui::End();
    }
}
