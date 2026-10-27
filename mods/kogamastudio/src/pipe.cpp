#include <stdafx.h>
#include <windows.h>
#include <thread>
#include <string>
#include "pipe.h"
#include "Properties.h"
#include "Clipboard.h"
#include "bridge.h"
#include "Console.h"
#include "Players.h"
#include "Explorer.h"
#include "GameInfo.h"
#include "Inventory.h"
#include "History.h"
#include "Camera.h"
#include "Appearance.h"


namespace pipe {

    bool cursorVisible = false;
    bool gameInitialized = false;
    bool openMenu = false;
    float generateProgress = 0.0f;
    bool isFullscreen = false;

    bool  isPasteBuilding  = false;
    int   pastePlacedCubes = 0;
    int   pasteTotalCubes  = 0;
    float pasteEtaSeconds  = -1.0f;

    bool  isWorldLoading  = false;
    int   worldLoadPlaced = 0;
    int   worldLoadTotal  = 0;
    float worldLoadEta    = -1.0f;


    void ProcessCommand(const std::string& cmd)
    {
        size_t pos = cmd.find("|");
        std::string command = cmd.substr(0, pos);
        std::string param = (pos != std::string::npos) ? cmd.substr(pos + 1) : "";

        DebugLog("[pipe] Command: %s, Param: %s\n", command.c_str(), param.c_str());

        ImGuiIO& io = ImGui::GetIO();

        if (command == "cursor")
        {
            if (param == "true")
            {
                cursorVisible = true;
            }
            else if (param == "false")
            {
                cursorVisible = false;
            }
        }
        else if (command == "fullscreen")
        {
            isFullscreen = param == "true";
        }
        else if (command == "game_initialized")
        {
            menu::isOpen = true;
            gameInitialized = true;

            // Send preview settings to C#
            char cmd[256];
            snprintf(cmd, sizeof(cmd),
                "preview_settings|%.3f|%.3f|%.3f|%.3f|%d|%.3f",
                Appearance::previewColor[0], Appearance::previewColor[1],
                Appearance::previewColor[2], Appearance::previewColor[3],
                Appearance::previewMode, Appearance::previewOpacity);
            menu::SendCommand(cmd);
        }
        else if (command == "key_down")
        {
            if (param == "F2")
            {
                openMenu = !openMenu;
            }
        }
        else if (command == "generate_progress")
        {
            float progress = std::stof(param);
            generateProgress = progress;
        }
        else if (command == "properties_update")
        {
            // format: id|itemId|groupId|posX|posY|posZ|rotX|rotY|rotZ|isModel|protoId|type
            auto tok = [](const std::string& s, int idx) -> std::string {
                size_t start = 0;
                for (int i = 0; i < idx; ++i) {
                    start = s.find('|', start);
                    if (start == std::string::npos) return "";
                    ++start;
                }
                size_t end = s.find('|', start);
                return s.substr(start, end == std::string::npos ? end : end - start);
            };
            Properties::targetObjectId = tok(param, 0);
            Properties::itemId         = std::stoi(tok(param, 1));
            Properties::groupId        = std::stoi(tok(param, 2));
            Properties::positionX      = std::stof(tok(param, 3));
            Properties::positionY      = std::stof(tok(param, 4));
            Properties::positionZ      = std::stof(tok(param, 5));
            Properties::rotationX      = std::stof(tok(param, 6));
            Properties::rotationY      = std::stof(tok(param, 7));
            Properties::rotationZ      = std::stof(tok(param, 8));
            Properties::isModel        = tok(param, 9) == "1";
            Properties::prototypeId    = std::stoi(tok(param, 10));
            Properties::objectType     = tok(param, 11);
            Inventory::ClearSelection();
        }
        else if (command == "clipboard_count")
        {
            Clipboard::cubeCount = param.empty() ? 0 : ::atoi(param.c_str());
        }
        else if (command == "clipboard_paste_progress")
        {
            size_t sep1 = param.find('|');
            size_t sep2 = sep1 != std::string::npos ? param.find('|', sep1 + 1) : std::string::npos;
            if (sep1 != std::string::npos)
            {
                pastePlacedCubes = ::atoi(param.substr(0, sep1).c_str());
                pasteTotalCubes  = ::atoi(param.substr(sep1 + 1, sep2 - sep1 - 1).c_str());
            }
            if (sep2 != std::string::npos)
                pasteEtaSeconds = ::atof(param.substr(sep2 + 1).c_str());
            isPasteBuilding = true;
        }
        else if (command == "clipboard_paste_done")
        {
            isPasteBuilding  = false;
            pastePlacedCubes = 0;
            pasteTotalCubes  = 0;
            pasteEtaSeconds  = -1.0f;
        }
        else if (command == "clipboard_preview_paste_model")
        {
            if (param == "true")
            {
                Properties::isPrevievPaste = true;
            }
            else if (param == "false")
            {
                Properties::isPrevievPaste = false;
            }
        }
        else if (command == "console_log")
        {
            AppLog::AddLog(AppLog::LogType::Incoming, param.c_str());
        }
        else if (command == "console_warning")
        {
            AppLog::AddLog(AppLog::LogType::Warning, param.c_str());
        }
        else if (command == "console_error")
        {
            AppLog::AddLog(AppLog::LogType::Error, param.c_str());
        }
        else if (command == "players_list")
        {
            Players::SetPlayers(param);
        }
        else if (command == "explorer_ready")
        {
            size_t sep = param.find('|');
            if (sep != std::string::npos)
            {
                std::string path       = param.substr(0, sep);
                int         rootGroup  = std::stoi(param.substr(sep + 1));
                Explorer::SetRootGroupId(rootGroup);
                Explorer::LoadFromFile(path);
            }
            else
            {
                Explorer::LoadFromFile(param);
            }
        }
        else if (command == "explorer_set_selected")
        {
            Explorer::SetSelected(param);
        }
        else if (command == "explorer_deselect_all")
        {
            Explorer::DeselectAll();
        }
        else if (command == "camera_pos")
        {
            size_t s1 = param.find('|');
            size_t s2 = s1 != std::string::npos ? param.find('|', s1 + 1) : std::string::npos;
            if (s2 != std::string::npos)
            {
                float cx = std::stof(param.substr(0, s1));
                float cy = std::stof(param.substr(s1 + 1, s2 - s1 - 1));
                float cz = std::stof(param.substr(s2 + 1));
                Explorer::SetCameraPos(cx, cy, cz);
            }
        }
        else if (command == "gameinfo_update")
        {
            GameInfo::SetData(param);
        }
        else if (command == "history_add")
        {
            History::Add(param);
        }
        else if (command == "history_transform")
        {
            // Format: id|oldX|oldY|oldZ|newX|newY|newZ
            auto nextF = [](std::string& s) -> std::string {
                size_t p = s.find('|');
                if (p == std::string::npos) { std::string v = s; s.clear(); return v; }
                std::string v = s.substr(0, p); s = s.substr(p + 1); return v;
            };
            std::string rest = param;
            std::string id = nextF(rest);
            float oldX = rest.empty() ? 0.f : std::stof(nextF(rest));
            float oldY = rest.empty() ? 0.f : std::stof(nextF(rest));
            float oldZ = rest.empty() ? 0.f : std::stof(nextF(rest));
            float newX = rest.empty() ? 0.f : std::stof(nextF(rest));
            float newY = rest.empty() ? 0.f : std::stof(nextF(rest));
            float newZ = rest.empty() ? 0.f : std::stof(rest);
            History::AddTransform(id, oldX, oldY, oldZ, newX, newY, newZ);
        }
        else if (command == "history_rotation")
        {
            // Format: id|oldQx|oldQy|oldQz|oldQw|newQx|newQy|newQz|newQw
            auto nextF = [](std::string& s) -> std::string {
                size_t p = s.find('|');
                if (p == std::string::npos) { std::string v = s; s.clear(); return v; }
                std::string v = s.substr(0, p); s = s.substr(p + 1); return v;
            };
            std::string rest = param;
            std::string id = nextF(rest);
            float oldQx = rest.empty() ? 0.f : std::stof(nextF(rest));
            float oldQy = rest.empty() ? 0.f : std::stof(nextF(rest));
            float oldQz = rest.empty() ? 0.f : std::stof(nextF(rest));
            float oldQw = rest.empty() ? 1.f : std::stof(nextF(rest));
            float newQx = rest.empty() ? 0.f : std::stof(nextF(rest));
            float newQy = rest.empty() ? 0.f : std::stof(nextF(rest));
            float newQz = rest.empty() ? 0.f : std::stof(nextF(rest));
            float newQw = rest.empty() ? 1.f : std::stof(rest);
            History::AddRotation(id, oldQx, oldQy, oldQz, oldQw, newQx, newQy, newQz, newQw);
        }
        else if (command == "history_link_added" || command == "history_link_removed"
              || command == "history_object_link_added" || command == "history_object_link_removed")
        {
            auto nextI = [](std::string& s) -> int {
                size_t p = s.find('|');
                std::string v = (p == std::string::npos) ? s : s.substr(0, p);
                s = (p == std::string::npos) ? "" : s.substr(p + 1);
                return v.empty() ? -1 : std::stoi(v);
            };
            std::string rest = param;
            int a = nextI(rest), b = nextI(rest);
            if (command == "history_link_added")
                History::AddLinkAdded(a, b);
            else if (command == "history_link_removed")
                History::AddLinkRemoved(a, b);
            else if (command == "history_object_link_added")
                History::AddObjectLinkAdded(a, b);
            else if (command == "history_object_link_removed")
                History::AddObjectLinkRemoved(a, b);
        }
        else if (command == "inventory_clear")
        {
            Inventory::Clear();
        }
        else if (command == "inventory_item")
        {
            // Format: id|name|typeId|categoryId|categoryName|resellable|priceGold|purchased|authorProfileId|slotPosition|description
            auto trim = [](std::string s) -> std::string { while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back(); return s; };
            auto nextField = [&](std::string& s) -> std::string {
                size_t p = s.find('|');
                if (p == std::string::npos) { std::string v = trim(s); s.clear(); return v; }
                std::string v = trim(s.substr(0, p));
                s = s.substr(p + 1);
                return v;
            };

            Inventory::InventoryItem item;
            std::string rest = param;
            item.id             = nextField(rest);
            item.name           = nextField(rest);
            item.typeId         = nextField(rest);
            item.categoryId     = nextField(rest);
            item.categoryName   = nextField(rest);
            item.resellable     = (nextField(rest) == "True");
            { std::string f = nextField(rest); item.priceGold    = f.empty() ? 0 : ::atoi(f.c_str()); }
            item.purchased      = (nextField(rest) == "True");
            item.authorProfileId = nextField(rest);
            { std::string f = nextField(rest); item.slotPosition = f.empty() ? 0 : ::atoi(f.c_str()); }
            item.description    = rest;

            Inventory::AddItem(item);
        }
        else if (command == "camera_mode_unavailable")
        {
            CameraPanel::ResetModeSelection();
        }
        else if (command == "world_load_progress")
        {
            size_t s1 = param.find('|');
            size_t s2 = s1 != std::string::npos ? param.find('|', s1 + 1) : std::string::npos;
            worldLoadPlaced = ::atoi(param.substr(0, s1).c_str());
            worldLoadTotal  = s1 != std::string::npos ? ::atoi(param.substr(s1 + 1, s2 - s1 - 1).c_str()) : 0;
            worldLoadEta    = s2 != std::string::npos ? ::atof(param.substr(s2 + 1).c_str()) : -1.0f;
            isWorldLoading  = true;
        }
        else if (command == "world_load_done")
        {
            isWorldLoading  = false;
            worldLoadPlaced = 0;
            worldLoadTotal  = 0;
            worldLoadEta    = -1.0f;
        }
    }

    void ProcessPending()
    {
        std::string message;
        while (bridge::NextForMenu(message))
            ProcessCommand(message);
    }
}