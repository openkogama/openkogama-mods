#include "stdafx.h"
#include "Players.h"
#include "MenuHelpers.h"
#include "Translate.h"

using namespace menu;

namespace Players {

    std::vector<PlayerEntry> playerList;

    // Format: "id1:Name1,id2:Name2,..."
    void SetPlayers(const std::string& csv)
    {
        playerList.clear();
        if (csv.empty()) return;

        std::string s = csv;
        size_t pos;
        while ((pos = s.find(',')) != std::string::npos) {
            std::string entry = s.substr(0, pos);
            s = s.substr(pos + 1);

            size_t sep = entry.find(':');
            if (sep == std::string::npos) continue;

            PlayerEntry p;
            p.id   = entry.substr(0, sep);
            p.name = entry.substr(sep + 1);
            if (!p.id.empty()) playerList.push_back(p);
        }
        if (!s.empty()) {
            size_t sep = s.find(':');
            if (sep != std::string::npos) {
                PlayerEntry p;
                p.id   = s.substr(0, sep);
                p.name = s.substr(sep + 1);
                if (!p.id.empty()) playerList.push_back(p);
            }
        }
    }

    void Render()
    {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
        if (ImGui::Begin((std::string(TR(u8"Players")) + u8"###Players").c_str(), nullptr, flags))
        {
            if (playerList.empty()) {
                ImGui::TextDisabled(TR(u8"No players online"));
            }
            else {
                for (const auto& p : playerList) {
                    ImGui::Text("%s", p.name.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton((std::string(TR(u8"Teleport")) + "##tp_" + p.id).c_str())) {
                        SendCommand(("teleport_player|" + p.id).c_str());
                    }
                }
            }
        }   
        ImGui::End();
    }

}
