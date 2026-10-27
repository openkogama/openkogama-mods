#include "Worlds.h"
#include "MenuHelpers.h"
#include "pipe.h"
#include <commdlg.h>
#include <shlobj.h>

using namespace menu;

namespace Worlds {

    static std::string GetObjectsDir()
    {
        char buf[MAX_PATH] = {};
        SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, buf);
        std::string dir = std::string(buf) + "\\KogamaStudio\\Generate\\Objects";
        SHCreateDirectoryExA(NULL, dir.c_str(), NULL);
        return dir;
    }

    static std::string SaveDialog()
    {
        char filename[MAX_PATH] = "world.ksobjects";
        std::string initDir = GetObjectsDir();
        OPENFILENAMEA ofn = {};
        ofn.lStructSize     = sizeof(ofn);
        ofn.lpstrFilter     = "KS Objects (*.ksobjects)\0*.ksobjects\0All Files\0*.*\0";
        ofn.lpstrFile       = filename;
        ofn.nMaxFile        = MAX_PATH;
        ofn.lpstrInitialDir = initDir.c_str();
        ofn.lpstrDefExt     = "ksobjects";
        ofn.Flags           = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
        return GetSaveFileNameA(&ofn) ? std::string(filename) : "";
    }

    static std::string OpenDialog()
    {
        char filename[MAX_PATH] = "";
        std::string initDir = GetObjectsDir();
        OPENFILENAMEA ofn = {};
        ofn.lStructSize     = sizeof(ofn);
        ofn.lpstrFilter     = "KS Objects (*.ksobjects)\0*.ksobjects\0All Files\0*.*\0";
        ofn.lpstrFile       = filename;
        ofn.nMaxFile        = MAX_PATH;
        ofn.lpstrInitialDir = initDir.c_str();
        ofn.Flags           = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        return GetOpenFileNameA(&ofn) ? std::string(filename) : "";
    }

    void Render()
    {
        ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);
        if (ImGui::Begin((std::string(TR(u8"Worlds")) + u8"###Worlds").c_str(), nullptr, ImGuiWindowFlags_NoCollapse))
        {
            if (!IsExtendedAvailable())
            {
                ImGui::TextDisabled(TR(u8"KogamaStudioExtended not loaded"));
            }
            else if (pipe::isWorldLoading)
            {
                float progress = pipe::worldLoadTotal > 0
                    ? (float)pipe::worldLoadPlaced / (float)pipe::worldLoadTotal
                    : 0.0f;

                char overlay[64];
                snprintf(overlay, sizeof(overlay), "%d / %d  (%.0f%%)",
                    pipe::worldLoadPlaced, pipe::worldLoadTotal, progress * 100.0f);
                ImGui::ProgressBar(progress, ImVec2(-1, 0), overlay);

                if (pipe::worldLoadEta >= 0.0f)
                {
                    int eta = (int)pipe::worldLoadEta;
                    char etaBuf[32];
                    if (eta >= 60)
                        snprintf(etaBuf, sizeof(etaBuf), "%dm %ds", eta / 60, eta % 60);
                    else
                        snprintf(etaBuf, sizeof(etaBuf), "%ds", eta);
                    ImGui::TextDisabled(TR(u8"Time left: %s"), etaBuf);
                }
            }
            else
            {
                if (ImGui::Button(TR(u8"Save .ksobjects")))
                {
                    std::string p = SaveDialog();
                    if (!p.empty())
                        SendCommandExtended((std::string(u8"world_save_to_file|") + p).c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button(TR(u8"Load .ksobjects")))
                {
                    std::string p = OpenDialog();
                    if (!p.empty())
                        SendCommandExtended((std::string(u8"world_load_from_file|") + p).c_str());
                }
            }
        }
        ImGui::End();
    }
}
