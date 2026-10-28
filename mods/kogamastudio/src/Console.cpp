#include "stdafx.h"
#include "Console.h"
#include <string>
#include "IconsFontAwesome6.h"
#include "ui.h"
#include <algorithm>
#include <cctype>

namespace AppLog {

    struct LogEntry {
        LogType type;
        std::string msg;
        char time[16];
    };

    static const int MAX_ENTRIES = 512;
    static std::vector<LogEntry> entries;
    static CRITICAL_SECTION cs;
    static bool csInit = false;
    static bool autoScroll = true;
    static bool scrollToBottom = false;
    static char filterBuf[128] = u8"";

    static void EnsureInit() {
        if (!csInit) {
            InitializeCriticalSection(&cs);
            csInit = true;
        }
    }

    void AddLog(LogType type, const char* msg) {
        EnsureInit();
        LogEntry e;
        e.type = type;
        e.msg = msg;
        SYSTEMTIME st;
        GetLocalTime(&st);
        snprintf(e.time, sizeof(e.time), u8"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
        EnterCriticalSection(&cs);
        if ((int)entries.size() >= MAX_ENTRIES)
            entries.erase(entries.begin());
        entries.push_back(std::move(e));
        if (autoScroll)
            scrollToBottom = true;
        LeaveCriticalSection(&cs);
    }

    void Render() {
        EnsureInit();
        if (ui::BeginPanel(u8"Console", ICON_FA_TERMINAL, TR(u8"Console"), nullptr)) {
            float clear = ImGui::CalcTextSize(ICON_FA_TRASH).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
            float scroll = ImGui::GetFrameHeight() + ImGui::CalcTextSize(TR(u8"Auto Scroll")).x + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetNextItemWidth(-(clear + scroll));
            ImGui::InputTextWithHint(u8"##filter", TR(u8"Filter"), filterBuf, sizeof(filterBuf));
            ImGui::SameLine();
            ImGui::Checkbox(TR(u8"Auto Scroll"), &autoScroll);
            ImGui::SameLine();
            if (ui::IconButton(u8"clear", ICON_FA_TRASH, TR(u8"Clear"))) {
                EnterCriticalSection(&cs);
                entries.clear();
                LeaveCriticalSection(&cs);
            }

            ImGui::BeginChild(u8"##log", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
            EnterCriticalSection(&cs);
            std::string query = filterBuf;
            std::transform(query.begin(), query.end(), query.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
            std::vector<int> shown;
            for (int i = 0; i < (int)entries.size(); i++) {
                if (!query.empty()) {
                    std::string text = entries[i].msg;
                    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
                    if (text.find(query) == std::string::npos) continue;
                }
                shown.push_back(i);
            }
            if (shown.empty()) ui::EmptyState(TR(u8"Nothing logged yet"));
            ImGuiListClipper clipper;
            clipper.Begin((int)shown.size());
            while (clipper.Step())
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                    const LogEntry& e = entries[shown[row]];
                    ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                    switch (e.type) {
                        case LogType::Incoming: color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f); break;
                        case LogType::Outgoing: color = ImVec4(1.0f, 0.85f, 0.35f, 1.0f); break;
                        case LogType::Warning: color = ImVec4(1.0f, 0.6f, 0.1f, 1.0f); break;
                        case LogType::Error: color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); break;
                    }
                    ImGui::TextDisabled(u8"%s", e.time);
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                    ImGui::TextUnformatted(e.msg.c_str());
                    ImGui::PopStyleColor();
                }
            if (scrollToBottom) {
                ImGui::SetScrollHereY(1.0f);
                scrollToBottom = false;
            }
            LeaveCriticalSection(&cs);
            ImGui::EndChild();
        }
        ui::EndPanel();
    }
}
