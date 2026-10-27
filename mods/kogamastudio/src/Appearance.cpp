#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING
#include "Appearance.h"
#include "Explorer.h"
#include "MenuHelpers.h"
#include "pipe.h"
#include <filesystem>
#include <fstream>
#include "json.hpp"
#include <winhttp.h>
#include <thread>
#pragma comment(lib, "winhttp.lib")

using namespace menu;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Appearance {
    static int selectedTheme = 0;
    static float rounding = 0.0f;
    std::string fontPath = u8"";
    float fontSize = 13.0f;
    char fontPathBuf[256] = u8"";
    ImFont* headerFont = nullptr;

    float previewColor[4] = { 1.0f, 1.0f, 0.0f, 1.0f };
    int   previewMode = 0;       // 0 = Outline, 1 = Surface Grid, 2 = Full Grid
    float previewOpacity = 1.0f;

    bool allowDangerousDelete = false;

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

    // Settings tab index
    static int settingsTab = 0;

    std::string GetConfigPath() {
        char buf[MAX_PATH];
        ExpandEnvironmentStringsA("%LOCALAPPDATA%\\KogamaStudio\\Config\\theme.json", buf, MAX_PATH);
        return buf;
    }

    void SaveSettings() {
        json config;
        config[u8"theme"] = selectedTheme;
        config[u8"rounding"] = rounding;
        config[u8"fontPath"] = fontPath;
        config[u8"fontSize"] = fontSize;
        config[u8"language"] = Locale::GetCurrentLanguage();
        config[u8"previewColor"] = { previewColor[0], previewColor[1], previewColor[2], previewColor[3] };
        config[u8"previewMode"] = previewMode;
        config[u8"previewOpacity"] = previewOpacity;
        config[u8"advGroupingWarnSkip"] = Explorer::advGroupingWarnSkip;
        config[u8"allowDangerousDelete"] = allowDangerousDelete;

        ImGuiStyle& style = ImGui::GetStyle();
        json colors;
        for (int i = 0; i < ImGuiCol_COUNT; i++) {
            ImVec4 col = style.Colors[i];
            colors[std::to_string(i)] = { col.x, col.y, col.z, col.w };
        }
        config[u8"colors"] = colors;

        std::string path = GetConfigPath();
        fs::create_directories(fs::path(path).parent_path());

        std::ofstream file(path);
        file << config.dump(4);
        file.close();
    }

    void LoadSettings() {
        std::string path = GetConfigPath();
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

                previewMode = config.value(u8"previewMode", 0);
                previewOpacity = config.value(u8"previewOpacity", 1.0f);
                if (config.contains(u8"previewColor")) {
                    auto& pc = config[u8"previewColor"];
                    previewColor[0] = pc[0]; previewColor[1] = pc[1];
                    previewColor[2] = pc[2]; previewColor[3] = pc[3];
                }

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

    void ApplyTheme() {
        ImGuiStyle& style = ImGui::GetStyle();
        ImGuiIO& io = ImGui::GetIO();

        if (selectedTheme == 0) ImGui::StyleColorsDark();
        else if (selectedTheme == 1) ImGui::StyleColorsLight();
        else if (selectedTheme == 2) ImGui::StyleColorsClassic();

        style.FrameRounding = rounding;
    }

    void Render(bool embedded) {
        // Tabs: 0=Theme, 1=Language, 2=Cubes Preview, 3=Danger Zone, 4=License Key
        const char* tabNames[] = {
            TR(u8"Theme"),
            TR(u8"Language"),
            TR(u8"Cubes Preview"),
            TR(u8"Danger"),
            TR(u8"License Key"),
        };
        const int tabCount = 5;

        float sideW = 140.0f;
        ImVec2 avail = ImGui::GetContentRegionAvail();

        // Left sidebar
        ImGui::BeginChild(u8"##settingsSidebar", ImVec2(sideW, avail.y), true);
        for (int i = 0; i < tabCount; i++) {
            bool selected = (settingsTab == i);
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            if (ImGui::Button(tabNames[i], ImVec2(-1, 0)))
                settingsTab = i;
            if (selected)
                ImGui::PopStyleColor();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Right content
        ImGui::BeginChild(u8"##settingsContent", ImVec2(0, avail.y), true);

        if (settingsTab == 0) {
            // Theme
            ImGui::Text(TR(u8"Theme:"));
            if (ImGui::RadioButton(TR(u8"Dark"), &selectedTheme, 0)) { ApplyTheme(); SaveSettings(); }
            if (ImGui::RadioButton(TR(u8"Light"), &selectedTheme, 1)) { ApplyTheme(); SaveSettings(); }
            if (ImGui::RadioButton(TR(u8"Classic"), &selectedTheme, 2)) { ApplyTheme(); SaveSettings(); }
            if (ImGui::RadioButton(TR(u8"Custom"), &selectedTheme, 3)) { SaveSettings(); }
            ImGui::Separator();

            ImGuiStyle& style = ImGui::GetStyle();

            if (ImGui::InputText(TR(u8"Font Path"), fontPathBuf, sizeof(fontPathBuf))) {
                fontPath = fontPathBuf;
                SaveSettings();
            }
            if (ImGui::SliderFloat(TR(u8"Font Size"), &fontSize, 8.0f, 24.0f))
                SaveSettings();
            if (ImGui::SliderFloat(TR(u8"Rounding"), &rounding, 0.0f, 10.0f)) {
                style.FrameRounding = rounding;
                SaveSettings();
            }

            if (selectedTheme == 3) {
                ImGui::Separator();
                ImGui::BeginChild(u8"ColorScroll", ImVec2(0, 0), true);
                for (int i = 0; i < ImGuiCol_COUNT; i++) {
                    const char* colorName = ImGui::GetStyleColorName(i);
                    if (ImGui::ColorEdit4(colorName, (float*)&style.Colors[i], ImGuiColorEditFlags_NoInputs))
                        SaveSettings();
                }
                ImGui::EndChild();
            }

            ImGui::Spacing();
            if (ImGui::Button(TR(u8"Reset"), ImVec2(-1, 0))) {
                selectedTheme = 0;
                rounding = 0.0f;
                fontPath = u8"";
                fontSize = 13.0f;
                memset(fontPathBuf, 0, sizeof(fontPathBuf));
                ApplyTheme();
                SaveSettings();
            }
        }
        else if (settingsTab == 1) {
            // Language
            ImGui::Text(TR(u8"Language:"));
            ImGui::Spacing();

            if (ImGui::Button(TR(u8"Refresh")))
                Locale::RefreshLanguageList();
            ImGui::Spacing();

            int langCount = Locale::GetLanguageCount();
            for (int i = 0; i < langCount; i++) {
                const char* code = Locale::GetLanguageName(i);
                const char* display = Locale::GetDisplayName(code);
                bool sel = (strcmp(code, Locale::GetCurrentLanguage()) == 0);
                if (ImGui::RadioButton(display, sel)) {
                    Locale::SetLanguage(code);
                    SaveSettings();
                }
            }
        }
        else if (settingsTab == 2) {
            // Cubes Preview
            ImGui::Text(TR(u8"Cubes Preview"));
            ImGui::Spacing();

            bool changed = false;
            if (ImGui::ColorEdit4(TR(u8"Preview Color"), previewColor, ImGuiColorEditFlags_NoInputs))
                changed = true;
            if (ImGui::SliderFloat(TR(u8"Line Opacity"), &previewOpacity, 0.0f, 1.0f))
                changed = true;
            const char* modes[] = { "Outline", "Surface Grid", "Full Grid" };
            if (ImGui::Combo(TR(u8"Wireframe Mode"), &previewMode, modes, 3))
                changed = true;

            if (changed) {
                SaveSettings();
                char cmd[256];
                snprintf(cmd, sizeof(cmd),
                    "preview_settings|%.3f|%.3f|%.3f|%.3f|%d|%.3f",
                    previewColor[0], previewColor[1], previewColor[2], previewColor[3],
                    previewMode, previewOpacity);
                SendCommand(cmd);
            }
        }
        else if (settingsTab == 3) {
            // Danger Zone
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.85f, 0.1f, 1.f));
            ImGui::Text(u8"%s", TR(u8"Danger"));
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::Spacing();

            bool wantEnable = Explorer::advancedGrouping;
            if (ImGui::Checkbox(TR(u8"Advanced Grouping"), &wantEnable))
            {
                if (wantEnable && !Explorer::advancedGrouping)
                {
                    if (Explorer::advGroupingWarnSkip)
                        Explorer::advancedGrouping = true;
                    else
                        Explorer::advGroupingWarnPending = true;
                }
                else
                {
                    Explorer::advancedGrouping = wantEnable;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(TR(u8"Allow dragging objects into any other object,\nnot just groups."));

            ImGui::Spacing();

            if (ImGui::Checkbox(TR(u8"Allow Dangerous Deletion"), &allowDangerousDelete))
                SaveSettings();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(TR(u8"Allow deleting terrain, game options and progression objects."));
        }
        else if (settingsTab == 4) {
            // License Key
            ImGui::Text(TR(u8"KogamaStudio Extended"));
            ImGui::Spacing();
            ImGui::TextWrapped(TR(u8"Enter your license key to unlock Extended features."));
            ImGui::Spacing();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint(u8"##licenseKey", u8"KSE-XXXX-XXXX-XXXX-...", licenseKey, sizeof(licenseKey));
            ImGui::Spacing();
            if (licenseValidating) ImGui::BeginDisabled();
            if (ImGui::Button(TR(u8"Activate"), ImVec2(-1, 0))) {
                if (strlen(licenseKey) > 0) {
                    SaveLicense();
                    ValidateLicenseAsync(licenseKey);
                } else {
                    licenseChecked = true;
                    licenseValid = false;
                }
            }
            if (licenseValidating) ImGui::EndDisabled();
            ImGui::Spacing();
            if (licenseValidating) {
                ImGui::TextUnformatted(TR(u8"Validating..."));
            } else if (licenseChecked) {
                if (licenseValid) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
                    ImGui::TextUnformatted(TR(u8"License active"));
                    ImGui::PopStyleColor();
                } else if (strlen(licenseKey) > 0) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
                    ImGui::TextUnformatted(TR(u8"Invalid key"));
                    ImGui::PopStyleColor();
                }
            }
        }

        ImGui::EndChild();
    }
}
