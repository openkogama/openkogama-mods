#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING
#include "stdafx.h"
#include "UILocale.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include "json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Locale {

    static std::unordered_map<std::string, std::string> s_translations;
    static std::string s_currentLang = "en";
    static std::vector<std::string> s_langList;

    // Language display names: code -> "NativeName (EnglishName)"
    struct LangInfo { const char* code; const char* native; const char* english; };
    static const LangInfo s_knownLangs[] = {
        { "en", "English",    "English" },
        { "pl", u8"Polski",   "Polish" },
        { "es", u8"Español",  "Spanish" },
        { "fr", u8"Français", "French" },
        { "de", "Deutsch",    "German" },
        { "pt", u8"Português","Portuguese" },
        { "it", "Italiano",   "Italian" },
        { "ru", u8"Русский",  "Russian" },
        { "uk", u8"Українська","Ukrainian" },
        { "ja", u8"日本語",    "Japanese" },
        { "ko", u8"한국어",    "Korean" },
        { "zh", u8"中文",      "Chinese" },
        { "tr", u8"Türkçe",   "Turkish" },
        { "nl", "Nederlands", "Dutch" },
        { "sv", "Svenska",    "Swedish" },
        { "cs", u8"Čeština",  "Czech" },
        { "da", "Dansk",      "Danish" },
        { "fi", "Suomi",      "Finnish" },
        { "hu", "Magyar",     "Hungarian" },
        { "no", "Norsk",      "Norwegian" },
        { "ro", u8"Română",   "Romanian" },
        { "sk", u8"Slovenčina","Slovak" },
        { "bg", u8"Български","Bulgarian" },
        { "hr", "Hrvatski",   "Croatian" },
        { "el", u8"Ελληνικά", "Greek" },
        { "th", u8"ไทย",      "Thai" },
        { "vi", u8"Tiếng Việt","Vietnamese" },
        { "id", "Bahasa Indonesia", "Indonesian" },
        { "ar", u8"العربية",  "Arabic" },
        { "hi", u8"हिन्दी",    "Hindi" },
    };
    static const int s_knownLangCount = sizeof(s_knownLangs) / sizeof(s_knownLangs[0]);
    static std::unordered_map<std::string, std::string> s_displayNames;

    static void BuildDisplayNames() {
        if (!s_displayNames.empty()) return;
        for (int i = 0; i < s_knownLangCount; i++) {
            std::string display = std::string(s_knownLangs[i].native);
            if (strcmp(s_knownLangs[i].native, s_knownLangs[i].english) != 0)
                display += std::string(" (") + s_knownLangs[i].english + ")";
            s_displayNames[s_knownLangs[i].code] = display;
        }
    }

    // Windows language ID to our lang code
    static const char* WinLangToCode(LANGID langId) {
        WORD primary = PRIMARYLANGID(langId);
        switch (primary) {
            case LANG_POLISH:      return "pl";
            case LANG_SPANISH:     return "es";
            case LANG_FRENCH:      return "fr";
            case LANG_GERMAN:      return "de";
            case LANG_PORTUGUESE:  return "pt";
            case LANG_ITALIAN:     return "it";
            case LANG_RUSSIAN:     return "ru";
            case LANG_UKRAINIAN:   return "uk";
            case LANG_JAPANESE:    return "ja";
            case LANG_KOREAN:      return "ko";
            case LANG_CHINESE:     return "zh";
            case LANG_TURKISH:     return "tr";
            case LANG_DUTCH:       return "nl";
            case LANG_SWEDISH:     return "sv";
            case LANG_CZECH:       return "cs";
            case LANG_DANISH:      return "da";
            case LANG_FINNISH:     return "fi";
            case LANG_HUNGARIAN:   return "hu";
            case LANG_NORWEGIAN:   return "no";
            case LANG_ROMANIAN:    return "ro";
            case LANG_SLOVAK:      return "sk";
            case LANG_BULGARIAN:   return "bg";
            case LANG_CROATIAN:    return "hr";
            case LANG_GREEK:       return "el";
            case LANG_THAI:        return "th";
            case LANG_VIETNAMESE:  return "vi";
            case LANG_INDONESIAN:  return "id";
            case LANG_ARABIC:      return "ar";
            case LANG_HINDI:       return "hi";
            default:               return "en";
        }
    }

    static std::string GetLangDir() {
        char buf[MAX_PATH];
        ExpandEnvironmentStringsA("%LOCALAPPDATA%\\KogamaStudio\\Lang", buf, MAX_PATH);
        return buf;
    }

    void SetLanguage(const char* langCode) {
        if (!langCode) langCode = "en";
        s_currentLang = langCode;
        s_translations.clear();
        DebugLog("[Locale] SetLanguage(\"%s\")\n", langCode);
        if (s_currentLang.empty() || s_currentLang == "en")
            return;

        std::string path = GetLangDir() + "\\" + s_currentLang + ".json";
        DebugLog("[Locale] Looking for: %s\n", path.c_str());
        if (fs::exists(path)) {
            try {
                std::ifstream f(path);
                json j = json::parse(f);
                for (auto it = j.begin(); it != j.end(); ++it) {
                    if (it.value().is_string())
                        s_translations[it.key()] = it.value().get<std::string>();
                }
                DebugLog("[Locale] Loaded %zu translations\n", s_translations.size());
            } catch (const std::exception& e) {
                DebugLog("[Locale] JSON parse error: %s\n", e.what());
            }
        } else {
            DebugLog("[Locale] File not found: %s\n", path.c_str());
        }
    }

    const char* T(const char* key) {
        if (!key) return "";
        auto it = s_translations.find(key);
        if (it != s_translations.end())
            return it->second.c_str();
        return key;
    }

    const char* GetCurrentLanguage() {
        return s_currentLang.c_str();
    }

    void RefreshLanguageList() {
        s_langList.clear();
        s_langList.push_back("en");
        std::string dir = GetLangDir();
        if (!fs::exists(dir)) return;
        try {
            for (auto& entry : fs::directory_iterator(dir)) {
                if (entry.path().extension() == ".json")
                    s_langList.push_back(entry.path().stem().string());
            }
        } catch (...) {}
    }

    int GetLanguageCount() {
        if (s_langList.empty()) RefreshLanguageList();
        return (int)s_langList.size();
    }

    const char* GetLanguageName(int index) {
        if (s_langList.empty()) RefreshLanguageList();
        if (index < 0 || index >= (int)s_langList.size()) return "";
        return s_langList[index].c_str();
    }

    const char* GetDisplayName(const char* langCode) {
        BuildDisplayNames();
        auto it = s_displayNames.find(langCode ? langCode : "en");
        if (it != s_displayNames.end())
            return it->second.c_str();
        return langCode; // fallback: just return the code
    }

    const char* DetectSystemLanguage() {
        static std::string detected;
        if (!detected.empty()) return detected.c_str();
        LANGID langId = GetUserDefaultUILanguage();
        detected = WinLangToCode(langId);
        // Check if we have a translation file for this language
        std::string path = GetLangDir() + "\\" + detected + ".json";
        if (detected != "en" && !fs::exists(path))
            detected = "en";
        return detected.c_str();
    }
}
