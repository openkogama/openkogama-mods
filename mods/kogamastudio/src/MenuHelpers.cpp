#include "stdafx.h"
#include "MenuHelpers.h"
#include "pipe.h"
#include "bridge.h"
#include "Console.h"
#include <cstdlib>

namespace menu {

    bool typing = false;

    void SendCommand(const char* cmd)
    {
        bridge::ToGame(cmd);
        DebugLog("[menu] Command sent: %s\n", cmd);
        AppLog::AddLog(AppLog::LogType::Outgoing, cmd);
    }

    void SendCommandExtended(const char* cmd)
    {
        (void)cmd;
    }

    bool IsExtendedAvailable()
    {
        return false;
    }

    void OpenFolder(std::string subpath)
    {
        char* buffer = nullptr;
        size_t len;
        _dupenv_s(&buffer, &len, "APPDATA");
        std::string path = std::string(buffer) + "\\..\\Local\\KogamaStudio\\" + subpath;
        free(buffer);

        std::string cmd = "explorer \"" + path + "\"";
        system(cmd.c_str());
    }

    void DisplayDirTree(const std::string& path, const std::string& rel) {
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((path + "\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;

        do {
            if (fd.cFileName[0] == '.') continue;
            std::string name = fd.cFileName;
            std::string full = path + "\\" + name;
            std::string relPath = rel.empty() ? name : rel + "/" + name;

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (ImGui::TreeNode(name.c_str()))
                    DisplayDirTree(full, relPath), ImGui::TreePop();
            }
            else {
                ImGui::Text(name.c_str());
                ImGui::SameLine();
                if (ImGui::Button(("Generate##" + name).c_str()))
                    SendCommand(("generate_model|" + relPath).c_str());
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    std::string FloatToStr(float value) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%g", value);
        return buf;
    }

    void FloatInput(const char* label, float& value, const char* commandPrefix, bool& typing, float width, const char* format) {
        ImGui::PushItemWidth(width);
        ImGui::InputFloat(label, &value, 0.0f, 0.0f, format);
        ImGui::PopItemWidth();

        if (!typing) typing = ImGui::IsItemActive();

        if (ImGui::IsItemDeactivatedAfterEdit()) {
            SendCommand((std::string(commandPrefix) + "|" + FloatToStr(value)).c_str());
        }
    }

    void DragFloatInput(const char* label, float& value, const char* commandPrefix, bool& typing, float speed, float width, const char* format) {
        ImGui::PushItemWidth(width);
        ImGui::DragFloat(label, &value, speed, 0.0f, 0.0f, format);
        ImGui::PopItemWidth();

        if (!typing) typing = ImGui::IsItemActive();

        if (ImGui::IsItemEdited()) {
            SendCommand((std::string(commandPrefix) + "|" + FloatToStr(value)).c_str());
        }
    }

    void IntInput(const char* label, int& value, const char* commandPrefix, bool& typing, float width) {
        ImGui::PushItemWidth(width);
        ImGui::InputInt(label, &value);
        ImGui::PopItemWidth();
        if (!typing) typing = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            SendCommand((std::string(commandPrefix) + "|" + std::to_string(value)).c_str());
        }
    }

}