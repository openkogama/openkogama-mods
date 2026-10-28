#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING
#include "Appearance.h"
#include "UILocale.h"
#include "IconsFontAwesome6.h"
#include "ui.h"
#include "Explorer.h"
#include "MenuHelpers.h"
#include <filesystem>
#include <fstream>
#include "json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Appearance {
    std::string fontPath = u8"";
    float fontSize = 13.0f;
    bool allowDangerousDelete = false;

    namespace {
        int selectedTheme = 0;
        float rounding = 0.0f;
        char fontPathBuf[256] = u8"";
        int settingsTab = 0;

        std::string ConfigPath() {
            char buf[MAX_PATH];
            ExpandEnvironmentStringsA("%LOCALAPPDATA%\\KogamaStudio\\Config\\theme.json", buf, MAX_PATH);
            return buf;
        }

        void ApplyTheme() {
            if (selectedTheme == 0) ImGui::StyleColorsDark();
            else if (selectedTheme == 1) ImGui::StyleColorsLight();
            else if (selectedTheme == 2) ImGui::StyleColorsClassic();
            ImGui::GetStyle().FrameRounding = rounding;
        }
    }

    void SaveSettings() {
        json config;
        config[u8"theme"] = selectedTheme;
        config[u8"rounding"] = rounding;
        config[u8"fontPath"] = fontPath;
        config[u8"fontSize"] = fontSize;
        config[u8"language"] = Locale::GetCurrentLanguage();
        config[u8"advGroupingWarnSkip"] = Explorer::advGroupingWarnSkip;
        config[u8"allowDangerousDelete"] = allowDangerousDelete;

        ImGuiStyle& style = ImGui::GetStyle();
        json colors;
        for (int i = 0; i < ImGuiCol_COUNT; i++) {
            ImVec4 col = style.Colors[i];
            colors[std::to_string(i)] = { col.x, col.y, col.z, col.w };
        }
        config[u8"colors"] = colors;

        std::string path = ConfigPath();
        fs::create_directories(fs::path(path).parent_path());

        std::ofstream file(path);
        file << config.dump(4);
        file.close();
    }

    void LoadSettings() {
        std::string path = ConfigPath();
        if (fs::exists(path)) {
            try {
                json config = json::parse(std::ifstream(path));
                selectedTheme = config.value(u8"theme", 0);
                rounding = config.value(u8"rounding", 0.0f);
                fontPath = config.value(u8"fontPath", u8"");
                fontSize = config.value(u8"fontSize", 13.0f);
                strcpy_s(fontPathBuf, sizeof(fontPathBuf), fontPath.c_str());

                std::string lang = config.value(u8"language", u8"en");
                Locale::SetLanguage(lang.c_str());

                Explorer::advGroupingWarnSkip = config.value(u8"advGroupingWarnSkip", false);
                allowDangerousDelete = config.value(u8"allowDangerousDelete", false);

                if (config.contains(u8"colors")) {
                    ImGuiStyle& style = ImGui::GetStyle();
                    json colors = config[u8"colors"];
                    for (auto it = colors.begin(); it != colors.end(); ++it) {
                        int idx = atoi(it.key().c_str());
                        if (idx >= 0 && idx < ImGuiCol_COUNT) {
                            style.Colors[idx] = ImVec4(
                                it.value()[0], it.value()[1], it.value()[2], it.value()[3]
                            );
                        }
                    }
                }

                ApplyTheme();
            }
            catch (...) {}
        }
    }

    namespace {
        void ThemePage() {
            if (ui::Section(TR(u8"Theme")) && ui::BeginProperties(u8"##theme")) {
                ui::Property(TR(u8"Theme"));
                const char* themes[] = {TR(u8"Dark"), TR(u8"Light"), TR(u8"Classic"), TR(u8"Custom")};
                if (ImGui::Combo(u8"##theme", &selectedTheme, themes, 4)) {
                    if (selectedTheme < 3) ApplyTheme();
                    SaveSettings();
                }
                ui::Property(TR(u8"Rounding"));
                if (ImGui::SliderFloat(u8"##rounding", &rounding, 0.0f, 10.0f)) {
                    ImGui::GetStyle().FrameRounding = rounding;
                    SaveSettings();
                }
                ui::EndProperties();
            }
            if (ui::Section(TR(u8"Font")) && ui::BeginProperties(u8"##font")) {
                ui::Property(TR(u8"Font Path"));
                if (ImGui::InputTextWithHint(u8"##path", u8"C:\\Windows\\Fonts\\consola.ttf", fontPathBuf, sizeof(fontPathBuf))) {
                    fontPath = fontPathBuf;
                    SaveSettings();
                }
                if (!menu::typing) menu::typing = ImGui::IsItemActive();
                ui::Property(TR(u8"Font Size"));
                if (ImGui::SliderFloat(u8"##size", &fontSize, 8.0f, 24.0f)) SaveSettings();
                ui::EndProperties();
                ui::Hint(TR(u8"Font changes apply the next time the game starts."));
            }
            if (selectedTheme == 3 && ui::Section(TR(u8"Colors"), false)) {
                ImGuiStyle& style = ImGui::GetStyle();
                for (int i = 0; i < ImGuiCol_COUNT; i++)
                    if (ImGui::ColorEdit4(ImGui::GetStyleColorName(i), (float*)&style.Colors[i], ImGuiColorEditFlags_NoInputs)) SaveSettings();
            }
            ImGui::Spacing();
            if (ui::Button(TR(u8"Reset"))) {
                selectedTheme = 0;
                rounding = 0.0f;
                fontPath = u8"";
                fontSize = 13.0f;
                memset(fontPathBuf, 0, sizeof(fontPathBuf));
                ApplyTheme();
                SaveSettings();
            }
        }

        void LanguagePage() {
            if (ui::Button(TR(u8"Refresh"))) Locale::RefreshLanguageList();
            ImGui::Spacing();
            for (int i = 0; i < Locale::GetLanguageCount(); i++) {
                const char* code = Locale::GetLanguageName(i);
                bool selected = strcmp(code, Locale::GetCurrentLanguage()) == 0;
                if (ImGui::Selectable(Locale::GetDisplayName(code), selected)) {
                    Locale::SetLanguage(code);
                    SaveSettings();
                }
            }
        }

        void EditingPage() {
            if (!ui::BeginProperties(u8"##editing")) return;
            ui::Property(TR(u8"Advanced Grouping"));
            bool wantEnable = Explorer::advancedGrouping;
            if (ImGui::Checkbox(u8"##grouping", &wantEnable)) {
                if (wantEnable && !Explorer::advancedGrouping) {
                    if (Explorer::advGroupingWarnSkip) Explorer::advancedGrouping = true;
                    else Explorer::advGroupingWarnPending = true;
                }
                else Explorer::advancedGrouping = wantEnable;
            }
            ImGui::SetItemTooltip(u8"%s", TR(u8"Allow dragging objects into any other object,\nnot just groups."));
            ui::Property(TR(u8"Allow Dangerous Deletion"));
            if (ImGui::Checkbox(u8"##deletion", &allowDangerousDelete)) SaveSettings();
            ImGui::SetItemTooltip(u8"%s", TR(u8"Allow deleting terrain, game options and progression objects."));
            ui::EndProperties();
        }
    }

    void Render() {
        struct Page { const char* icon; const char* title; void (*render)(); };
        const Page pages[] = {
            {ICON_FA_PAINTBRUSH, u8"Appearance", ThemePage},
            {ICON_FA_GLOBE, u8"Language", LanguagePage},
            {ICON_FA_HAMMER, u8"Editing", EditingPage},
        };
        int count = (int)(sizeof(pages) / sizeof(pages[0]));
        if (settingsTab >= count) settingsTab = 0;
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::BeginChild(u8"##pages", ImVec2(ImGui::GetFontSize() * 10.0f, avail.y), ImGuiChildFlags_Borders);
        for (int i = 0; i < count; i++)
            if (ImGui::Selectable((std::string(pages[i].icon) + u8"  " + TR(pages[i].title)).c_str(), settingsTab == i)) settingsTab = i;
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild(u8"##page", ImVec2(0.0f, avail.y), ImGuiChildFlags_Borders);
        pages[settingsTab].render();
        ImGui::EndChild();
    }
}
