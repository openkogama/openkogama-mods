#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING
#include "stdafx.h"
#include "Welcome.h"
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

        // Ensure current language matches selection (guard against external resets)
        if (strcmp(Locale::GetCurrentLanguage(), s_selectedLang.c_str()) != 0) {
            DebugLog("[Welcome] Language mismatch: current=%s, selected=%s, reloading\n",
                     Locale::GetCurrentLanguage(), s_selectedLang.c_str());
            Locale::SetLanguage(s_selectedLang.c_str());
        }

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoSavedSettings;

        std::string title = std::string(TR(u8"Welcome")) + u8"###Welcome";
        if (!ImGui::Begin(title.c_str(), nullptr, flags)) {
            ImGui::End();
            return;
        }

        // Title - large header font
        if (Appearance::headerFont) {
            ImGui::PushFont(Appearance::headerFont);
            ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), u8"KogamaStudio");
            ImGui::PopFont();
        } else {
            ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), u8"KogamaStudio");
        }
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (Appearance::headerFont ? 14.0f : 0.0f));
        ImGui::TextDisabled(u8"v" KS_VERSION);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Welcome text
        ImGui::TextWrapped("%s", TR(u8"Welcome to KogamaStudio! This version introduces many new features and changes, which may also mean new bugs. If you encounter any issues, please report them."));
        ImGui::Spacing();
        ImGui::TextWrapped("%s", TR(u8"Press F2 to open/close the menu at any time."));
        ImGui::Spacing();

        // Discord link
        ImGui::TextDisabled("%s", TR(u8"Join our Discord community:"));
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.34f, 0.40f, 0.87f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.44f, 0.50f, 0.95f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.24f, 0.30f, 0.77f, 1.0f));
        if (ImGui::SmallButton(u8"Discord")) {
            ShellExecuteA(NULL, "open", "https://discord.gg/u6tKuP3k4M", NULL, NULL, SW_SHOWNORMAL);
        }
        ImGui::PopStyleColor(3);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Language selection
        ImGui::Text("%s", TR(u8"Select language:"));
        ImGui::Spacing();

        int langCount = Locale::GetLanguageCount();
        float contentWidth = ImGui::GetContentRegionAvail().x;
        float colWidth = contentWidth / 2.0f;

        ImGui::BeginChild(u8"##LangList", ImVec2(0, 200), true);
        ImGui::Columns(2, u8"##LangCols", false);
        ImGui::SetColumnWidth(0, colWidth);
        ImGui::SetColumnWidth(1, colWidth);

        for (int i = 0; i < langCount; i++) {
            if (i == langCount / 2 + langCount % 2)
                ImGui::NextColumn();

            const char* code = Locale::GetLanguageName(i);
            const char* display = Locale::GetDisplayName(code);
            bool selected = (s_selectedLang == code);

            if (ImGui::RadioButton(display, selected)) {
                s_selectedLang = code;
                Locale::SetLanguage(code);
                DebugLog("[Welcome] User selected language: %s (current: %s)\n", code, Locale::GetCurrentLanguage());
            }
        }

        ImGui::Columns(1);
        ImGui::EndChild();

        ImGui::Spacing();

        // Continue button
        float buttonWidth = 200.0f;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonWidth) * 0.5f);
        if (ImGui::Button(TR(u8"Continue"), ImVec2(buttonWidth, 35))) {
            MarkComplete();
        }

        ImGui::Spacing();
        ImGui::End();
    }
}
