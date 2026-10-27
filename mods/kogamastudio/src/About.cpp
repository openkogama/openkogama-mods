#include "About.h"
#include "MenuHelpers.h"
#include "version.h"

namespace About {
	void Render() {
        if (!ImGui::Begin((std::string(TR(u8"About")) + u8"###About").c_str(), nullptr, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::End();
            return;
        }

        ImGui::Text(u8"KogamaStudio v" KS_VERSION);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Text(u8"%s Becko, MauryDev, mindiro", TR(u8"Special Thanks to:"));
        ImGui::Spacing();

        ImGui::End();
	}
}
