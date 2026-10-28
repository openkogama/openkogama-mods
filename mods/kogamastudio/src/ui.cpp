#include "stdafx.h"
#include "ui.h"
#include <algorithm>
#include <string>

namespace ui {
    namespace {
        constexpr float FooterButton = 8.0f;
    }

    bool BeginPanel(const char* id, const char* icon, const char* title, bool* open, ImGuiWindowFlags flags) {
        std::string name = std::string(icon) + u8"  " + title + u8"###" + id;
        return ImGui::Begin(name.c_str(), open, flags | ImGuiWindowFlags_NoCollapse);
    }

    void EndPanel() { ImGui::End(); }

    bool EmptyState(const char* text, const char* action) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float wrap = (std::max)(avail.x - ImGui::GetStyle().WindowPadding.x * 2.0f, 1.0f);
        ImVec2 size = ImGui::CalcTextSize(text, nullptr, false, wrap);
        float below = action ? ImGui::GetStyle().ItemSpacing.y * 2.0f + ImGui::GetFrameHeight() : 0.0f;
        ImVec2 at = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(at.x + (std::max)(0.0f, (avail.x - size.x) * 0.5f), at.y + (std::max)(0.0f, (avail.y - size.y - below) * 0.4f)));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + size.x);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        if (!action) return false;
        ImGui::Spacing();
        float width = ImGui::CalcTextSize(action).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetCursorPosX(at.x + (std::max)(0.0f, (avail.x - width) * 0.5f));
        return Button(action, Tone::Primary);
    }

    void Hint(const char* text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped(u8"%s", text);
        ImGui::PopStyleColor();
    }

    bool Section(const char* label, bool open) {
        return ImGui::CollapsingHeader(label, open ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    }

    bool BeginProperties(const char* id) {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_None)) return false;
        ImGui::TableSetupColumn(u8"##label", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn(u8"##value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    void Property(const char* label) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    void EndProperties() { ImGui::EndTable(); }

    bool Button(const char* label, Tone tone, float width) {
        int pushed = 0;
        if (tone == Tone::Danger) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.05f, 0.05f, 1.0f));
            pushed = 3;
        }
        else if (tone == Tone::Primary) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
            pushed = 1;
        }
        bool pressed = ImGui::Button(label, ImVec2(width, 0.0f));
        ImGui::PopStyleColor(pushed);
        return pressed;
    }

    bool IconButton(const char* id, const char* icon, const char* tooltip, bool active) {
        ImGui::PushID(id);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        bool pressed = ImGui::Button(icon);
        if (active) ImGui::PopStyleColor();
        if (tooltip && *tooltip) ImGui::SetItemTooltip(u8"%s", tooltip);
        ImGui::PopID();
        return pressed;
    }

    bool Toggle(const char* label, bool* value) { return ImGui::Checkbox(label, value); }

    void OpenModal(const char* id) { ImGui::OpenPopup((std::string(u8"###") + id).c_str()); }

    bool BeginModal(const char* id, const char* title, float width) {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, ImGui::GetMainViewport()->WorkSize.y * 0.9f));
        std::string name = std::string(title) + u8"###" + id;
        return ImGui::BeginPopupModal(name.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    }

    int ModalFooter(const char* primary, const char* secondary, bool primaryEnabled) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        float button = ImGui::GetFontSize() * FooterButton;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float total = button + (secondary ? button + spacing : 0.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, ImGui::GetContentRegionAvail().x - total));
        int result = 0;
        if (secondary) {
            if (ImGui::Button(secondary, ImVec2(button, 0.0f))) result = 2;
            ImGui::SameLine();
        }
        ImGui::BeginDisabled(!primaryEnabled);
        if (ImGui::Button(primary, ImVec2(button, 0.0f))) result = 1;
        ImGui::EndDisabled();
        if (!result && ImGui::IsKeyPressed(ImGuiKey_Escape)) result = secondary ? 2 : 1;
        if (!result && primaryEnabled && !ImGui::IsAnyItemActive() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) result = 1;
        if (result) ImGui::CloseCurrentPopup();
        return result;
    }

    void EndModal() { ImGui::EndPopup(); }
}
