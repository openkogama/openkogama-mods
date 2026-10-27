#include "Properties.h"
#include "MenuHelpers.h"
#include "pipe.h"
#include <commdlg.h>

using namespace menu;

namespace Clipboard {

    static std::string GetModelsDir()
    {
        char buf[MAX_PATH] = "";
        GetEnvironmentVariableA("LOCALAPPDATA", buf, MAX_PATH);
        std::string dir = std::string(buf) + "\\KogamaStudio\\models";
        CreateDirectoryA(dir.c_str(), NULL);
        return dir;
    }

    static std::string OpenDialog()
    {
        char filename[MAX_PATH] = "";
        std::string initDir = GetModelsDir();
        OPENFILENAMEA ofn = {};
        ofn.lStructSize  = sizeof(ofn);
        ofn.lpstrFilter  = "KS Cubes (*.kscubes)\0*.kscubes\0All Files\0*.*\0";
        ofn.lpstrFile    = filename;
        ofn.nMaxFile     = MAX_PATH;
        ofn.lpstrInitialDir = initDir.c_str();
        ofn.Flags        = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        return GetOpenFileNameA(&ofn) ? std::string(filename) : "";
    }

    static std::string SaveDialog()
    {
        char filename[MAX_PATH] = "model.kscubes";
        std::string initDir = GetModelsDir();
        OPENFILENAMEA ofn = {};
        ofn.lStructSize  = sizeof(ofn);
        ofn.lpstrFilter  = "KS Cubes (*.kscubes)\0*.kscubes\0All Files\0*.*\0";
        ofn.lpstrFile    = filename;
        ofn.nMaxFile     = MAX_PATH;
        ofn.lpstrInitialDir = initDir.c_str();
        ofn.lpstrDefExt  = "kscubes";
        ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
        return GetSaveFileNameA(&ofn) ? std::string(filename) : "";
    }

    int cubeCount = 0;
    int offsetX = 0.0f;
    int offsetY = 0.0f;
    int offsetZ = 0.0f;
    int rotationX = 0;
    int rotationY = 0;
    int rotationZ = 0;
    bool mirrorX = 0;
    bool mirrorY = 0;
    bool mirrorZ = 0;
    int clipMinX = 0, clipMaxX = 0;
    int clipMinY = 0, clipMaxY = 0;
    int clipMinZ = 0, clipMaxZ = 0;

    void Render() {
        ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(800, 20), ImGuiCond_FirstUseEver);
        ImGuiWindowFlags newFlags = ImGuiWindowFlags_NoCollapse;
        if (ImGui::Begin((std::string(TR(u8"Clipboard")) + u8"###Clipboard").c_str(), nullptr, newFlags))
        {
            ImGui::Text("%s: %d", TR(u8"Cubes"), cubeCount);

            ImGui::Separator();
            if (ImGui::Button(TR(u8"Save .kscubes"))) { std::string p = SaveDialog(); if (!p.empty()) SendCommand((std::string(u8"clipboard_save_to_file|") + p).c_str()); }
            ImGui::SameLine();
            if (ImGui::Button(TR(u8"Load .kscubes"))) { std::string p = OpenDialog(); if (!p.empty()) SendCommand((std::string(u8"clipboard_load_from_file|") + p).c_str()); }

            ImGui::Separator();

            if (ImGui::CollapsingHeader(TR(u8"Transform"))) {
                // OFFSET
                ImGui::Text(TR(u8"Offset"));
                menu::IntInput(u8"X##offset_x", offsetX, u8"clipboard_offset_x", typing);
                menu::IntInput(u8"Y##offset_y", offsetY, u8"clipboard_offset_y", typing);
                menu::IntInput(u8"Z##offset_z", offsetZ, u8"clipboard_offset_z", typing);

                // ROTATION
                const char* rotationOptions[] = { u8"0\u00B0", u8"90\u00B0", u8"180\u00B0", u8"270\u00B0"};
                ImGui::Text(TR(u8"Rotation"));
                ImGui::Combo(u8"X##rot_x", &rotationX, rotationOptions, 4);
                if (ImGui::IsItemEdited()) {
                    SendCommand((u8"clipboard_rotation_x|" + std::to_string(rotationX * 90)).c_str());
                }
                ImGui::Combo(u8"Y##rot_y", &rotationY, rotationOptions, 4);
                if (ImGui::IsItemEdited()) {
                    SendCommand((u8"clipboard_rotation_y|" + std::to_string(rotationY * 90)).c_str());
                }
                ImGui::Combo(u8"Z##rot_z", &rotationZ, rotationOptions, 4);
                if (ImGui::IsItemEdited()) {
                    SendCommand((u8"clipboard_rotation_z|" + std::to_string(rotationZ * 90)).c_str());
                }

                // MIRROR
                ImGui::Text(TR(u8"Mirror"));
                ImGui::Checkbox(u8"X##mirror_x", &mirrorX);
                if (ImGui::IsItemEdited()) {
                    SendCommand((u8"clipboard_mirror_x|" + std::string(mirrorX ? u8"true" : u8"false")).c_str());
                }
                ImGui::Checkbox(u8"Y##mirror_y", &mirrorY);
                if (ImGui::IsItemEdited()) {
                    SendCommand((u8"clipboard_mirror_y|" + std::string(mirrorY ? u8"true" : u8"false")).c_str());
                }
                ImGui::Checkbox(u8"Z##mirror_z", &mirrorZ);
                if (ImGui::IsItemEdited()) {
                    SendCommand((u8"clipboard_mirror_z|" + std::string(mirrorZ ? u8"true" : u8"false")).c_str());
                }
            }
        }
        ImGui::End();
    }
}
