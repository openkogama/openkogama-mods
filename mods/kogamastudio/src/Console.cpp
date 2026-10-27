#include "stdafx.h"
#include "Console.h"
#include <string>

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
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
        if (!ImGui::Begin((std::string(TR(u8"Console")) + u8"###Console").c_str(), nullptr, flags)) {
            ImGui::End();
            return;
        }

        if (ImGui::Button(TR(u8"Clear"))) {
            EnterCriticalSection(&cs);
            entries.clear();
            LeaveCriticalSection(&cs);
        }
        ImGui::SameLine();
        ImGui::Checkbox(TR(u8"Auto Scroll"), &autoScroll);
        ImGui::SameLine();
        ImGui::Text(TR(u8"Filter"));
        ImGui::SameLine();
        ImGui::PushItemWidth(200.0f);
        ImGui::InputText(u8"##ConsoleFilter", filterBuf, sizeof(filterBuf));
        ImGui::PopItemWidth();
        ImGui::Separator();

        ImGui::BeginChild(u8"##ConsoleLog", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);

        std::vector<LogEntry> snapshot;
        EnterCriticalSection(&cs);
        snapshot = entries;
        LeaveCriticalSection(&cs);

        for (const auto& e : snapshot) {
            if (filterBuf[0] != u8'\0' && e.msg.find(filterBuf) == std::string::npos)
                continue;

            ImVec4 color;
            switch (e.type) {
                case LogType::Incoming: color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);  break;
                case LogType::Outgoing: color = ImVec4(1.0f, 0.85f, 0.35f, 1.0f); break;
                case LogType::Warning:  color = ImVec4(1.0f, 0.6f, 0.1f, 1.0f);  break;
                case LogType::Error:    color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  break;
                default:                color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);  break;
            }

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            std::string line = std::string(e.time) + u8"  " + e.msg;
            ImGui::TextUnformatted(line.c_str());
            ImGui::PopStyleColor();
        }

        if (scrollToBottom) {
            ImGui::SetScrollHereY(1.0f);
            scrollToBottom = false;
        }

        ImGui::EndChild();
        ImGui::End();
    }
}
