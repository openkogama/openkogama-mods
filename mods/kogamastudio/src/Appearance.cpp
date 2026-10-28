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
#include <winhttp.h>
#include <thread>
#pragma comment(lib, "winhttp.lib")

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

    // License key - stored separately from theme config
    static char licenseKey[128] = "";
    static bool licenseValid = false;
    static bool licenseChecked = false;
    static bool licenseValidating = false;

    static void ValidateLicenseAsync(std::string key) {
        licenseValidating = true;
        std::thread([key]() {
            bool valid = false;
            HINTERNET hSession = WinHttpOpen(L"KogamaStudio/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
            if (hSession) {
                HINTERNET hConnect = WinHttpConnect(hSession, L"qiuchbrbhlkwklmurufr.supabase.co", INTERNET_DEFAULT_HTTPS_PORT, 0);
                if (hConnect) {
                    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST",
                        L"/functions/v1/validate-license",
                        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
                    if (hRequest) {
                        std::string body = "{\"key\":\"" + key + "\"}";
                        LPCWSTR headers = L"Content-Type: application/json\r\nAuthorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InFpdWNoYnJiaGxrd2tsbXVydWZyIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzQyMDg1MzYsImV4cCI6MjA4OTc4NDUzNn0.HAVw8AnS_L5zJP2dc0A0ByL8TmfRhUIkOfzWebzwBoQ";

                        if (WinHttpSendRequest(hRequest, headers, -1,
                            (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0))
                        {
                            if (WinHttpReceiveResponse(hRequest, NULL)) {
                                char buf[1024] = {};
                                DWORD bytesRead = 0;
                                WinHttpReadData(hRequest, buf, sizeof(buf) - 1, &bytesRead);
                                buf[bytesRead] = '\0';
                                try {
                                    auto resp = nlohmann::json::parse(buf);
                                    valid = resp.value("valid", false);
                                } catch (...) {}
                            }
                        }
                        WinHttpCloseHandle(hRequest);
                    }
                    WinHttpCloseHandle(hConnect);
                }
                WinHttpCloseHandle(hSession);
            }
            licenseValid = valid;
            licenseChecked = true;
            licenseValidating = false;
        }).detach();
    }

    static std::string GetLicensePath() {
        char* buffer = nullptr;
        size_t len;
        _dupenv_s(&buffer, &len, u8"APPDATA");
        std::string path = std::string(buffer) + u8"\\..\\Local\\KogamaStudio\\Config\\.license";
        free(buffer);
        return path;
    }

    static void SaveLicense() {
        std::string path = GetLicensePath();
        fs::create_directories(fs::path(path).parent_path());
        std::ofstream file(path, std::ios::binary);
        // Simple XOR obfuscation so key isn't plaintext on disk
        std::string key(licenseKey);
        for (auto& c : key) c ^= 0x5A;
        file.write(key.data(), key.size());
    }

    static void LoadLicense() {
        std::string path = GetLicensePath();
        if (!fs::exists(path)) return;
        try {
            std::ifstream file(path, std::ios::binary);
            std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            for (auto& c : data) c ^= 0x5A;
            strncpy(licenseKey, data.c_str(), sizeof(licenseKey) - 1);
            licenseKey[sizeof(licenseKey) - 1] = '\0';
            if (strlen(licenseKey) > 0) {
                ValidateLicenseAsync(licenseKey);
            }
        } catch (...) {}
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
        LoadLicense();
    }

    bool IsExtendedActive() { return licenseValid && licenseChecked; }

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

        void LicensePage() {
            ui::Hint(TR(u8"Enter your license key to unlock Extended features."));
            ImGui::Spacing();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint(u8"##licenseKey", u8"KSE-XXXX-XXXX-XXXX-...", licenseKey, sizeof(licenseKey));
            if (!menu::typing) menu::typing = ImGui::IsItemActive();
            ImGui::Spacing();
            ImGui::BeginDisabled(licenseValidating);
            if (ui::Button(TR(u8"Activate"), ui::Tone::Primary)) {
                if (strlen(licenseKey) > 0) {
                    SaveLicense();
                    ValidateLicenseAsync(licenseKey);
                }
                else {
                    licenseChecked = true;
                    licenseValid = false;
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (licenseValidating) ImGui::TextDisabled(u8"%s", TR(u8"Validating..."));
            else if (licenseChecked && licenseValid) ImGui::TextUnformatted(TR(u8"License active"));
            else if (licenseChecked && strlen(licenseKey) > 0) ImGui::TextUnformatted(TR(u8"Invalid key"));
        }
    }

    void Render() {
        struct Page { const char* icon; const char* title; void (*render)(); };
        const Page pages[] = {
            {ICON_FA_PAINTBRUSH, u8"Appearance", ThemePage},
            {ICON_FA_GLOBE, u8"Language", LanguagePage},
            {ICON_FA_HAMMER, u8"Editing", EditingPage},
            {ICON_FA_STAR, u8"Extended", LicensePage},
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
