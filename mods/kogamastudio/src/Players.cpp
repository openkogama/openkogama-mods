#include "stdafx.h"
#include "Players.h"
#include "MenuHelpers.h"
#include "IconsFontAwesome6.h"
#include "GameInfo.h"
#include "ui.h"

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
        if (ui::BeginPanel(u8"Players", ICON_FA_USERS, TR(u8"Players"), nullptr))
        {
            if (playerList.empty()) ui::EmptyState(TR(u8"No players online"));
            float button = ImGui::GetFrameHeight();
            for (const auto& player : playerList)
            {
                ImGui::PushID(player.id.c_str());
                ImGui::AlignTextToFramePadding();
                bool you = player.name == GameInfo::localPlayer;
                ImGui::TextUnformatted((std::string(ICON_FA_USER u8"  ") + player.name + (you ? std::string(u8"  (") + TR(u8"you") + u8")" : std::string())).c_str());
                ImGui::SameLine(ImGui::GetContentRegionMax().x - button);
                if (ui::IconButton(u8"teleport", ICON_FA_LOCATION_DOT, TR(u8"Teleport"))) SendCommand((u8"teleport_player|" + player.id).c_str());
                ImGui::PopID();
            }
        }
        ui::EndPanel();
    }
}
