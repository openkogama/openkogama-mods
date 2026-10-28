#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING
#include "stdafx.h"
#include "Welcome.h"
#include "ui.h"
#include "MenuHelpers.h"
#include "Appearance.h"
#include "version.h"
#include <fstream>
#include <filesystem>
#include "json.hpp"
#include <shellapi.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Welcome {

    static bool s_showWelcome = false;
    static bool s_initialized = false;
    static std::string s_selectedLang;

    static std::string GetConfigDir() {
        char buf[MAX_PATH];
        ExpandEnvironmentStringsA("%LOCALAPPDATA%\\KogamaStudio\\Config", buf, MAX_PATH);
        return buf;
    }

    static std::string GetWelcomeFlagPath() {
        return GetConfigDir() + "\\welcome_done.txt";
    }

    void Init() {
        if (s_initialized) return;
        s_initialized = true;

        std::string flagPath = GetWelcomeFlagPath();
        if (fs::exists(flagPath)) {
            s_showWelcome = false;
            return;
        }

        // First run - detect system language and show welcome
        const char* sysLang = Locale::DetectSystemLanguage();
        s_selectedLang = sysLang;
        Locale::SetLanguage(s_selectedLang.c_str());
        DebugLog("[Welcome] First run detected, system lang: %s\n", sysLang);
        s_showWelcome = true;
    }

    bool IsOpen() {
        return s_showWelcome;
    }

    static void MarkComplete() {
        s_showWelcome = false;
        // Save flag
        std::string dir = GetConfigDir();
        fs::create_directories(dir);
        std::ofstream f(GetWelcomeFlagPath());
        f << KS_VERSION;
        f.close();
        // Save chosen language to theme config
        Appearance::SaveSettings();
    }

    void Render() {
        if (!s_showWelcome) return;
        if (strcmp(Locale::GetCurrentLanguage(), s_selectedLang.c_str()) != 0) Locale::SetLanguage(s_selectedLang.c_str());

        if (!ImGui::IsPopupOpen(u8"###Welcome")) ui::OpenModal(u8"Welcome");
        if (!ui::BeginModal(u8"Welcome", (std::string(TR(u8"Welcome")) + u8"  \u00B7  KogamaStudio v" KS_VERSION).c_str(), 520.0f)) return;

        ImGui::TextWrapped(u8"%s", TR(u8"Welcome to KogamaStudio! This version introduces many new features and changes, which may also mean new bugs. If you encounter any issues, please report them."));
        ImGui::Spacing();
        ImGui::TextWrapped(u8"%s", TR(u8"Press F2 to open/close the menu at any time."));
        ImGui::Spacing();
        if (ui::Button(u8"Discord")) ShellExecuteA(NULL, "open", "https://discord.gg/u6tKuP3k4M", NULL, NULL, SW_SHOWNORMAL);
        ImGui::SameLine();
        ui::Hint(TR(u8"Join our Discord community:"));

        ImGui::Spacing();
        ImGui::SeparatorText(TR(u8"Select language:"));
        if (ImGui::BeginTable(u8"##languages", 2, ImGuiTableFlags_ScrollY, ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 8.0f))) {
            for (int i = 0; i < Locale::GetLanguageCount(); i++) {
                ImGui::TableNextColumn();
                const char* code = Locale::GetLanguageName(i);
                if (ImGui::Selectable(Locale::GetDisplayName(code), s_selectedLang == code)) {
                    s_selectedLang = code;
                    Locale::SetLanguage(code);
                }
            }
            ImGui::EndTable();
        }

        if (ui::ModalFooter(TR(u8"Continue")) == 1) MarkComplete();
        ui::EndModal();
    }
}
