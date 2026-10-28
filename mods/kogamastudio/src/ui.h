#pragma once
#include "imgui/imgui.h"

namespace ui {
    enum class Tone { Normal, Primary, Danger };

    bool BeginPanel(const char* id, const char* icon, const char* title, bool* open, ImGuiWindowFlags flags = 0);
    void EndPanel();

    bool EmptyState(const char* text, const char* action = nullptr);
    void Hint(const char* text);

    bool Section(const char* label, bool open = true);

    bool BeginProperties(const char* id);
    void Property(const char* label);
    void EndProperties();

    bool Button(const char* label, Tone tone = Tone::Normal, float width = 0.0f);
    bool IconButton(const char* id, const char* icon, const char* tooltip, bool active = false);
    bool Toggle(const char* label, bool* value);

    void OpenModal(const char* id);
    bool BeginModal(const char* id, const char* title, float width = 420.0f);
    int ModalFooter(const char* primary, const char* secondary = nullptr, bool primaryEnabled = true);
    void EndModal();
}
