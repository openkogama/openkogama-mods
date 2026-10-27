#include "ResourcePacks.h"
#include "ResourcePackLoader.h"
#include "MenuHelpers.h"
#include "pipe.h"

using namespace menu;

namespace ResourcePacks {

    static std::vector<Pack> cachedPacks;
    static bool packsLoaded = false;

	void Render()
	{
        if (!ImGui::Begin((std::string(TR(u8"Resource Packs")) + u8"###ResourcePacks").c_str(), nullptr, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::End();
            return;
        }

        if (!packsLoaded) {
            cachedPacks = LoadPacks();
            packsLoaded = true;
        }

        if (ImGui::Button(TR(u8"Refresh Packs"))) cachedPacks = LoadPacks();
        ImGui::SameLine();
        if (ImGui::Button(TR(u8"Open Folder"))) OpenFolder("ResourcePacks");
        ImGui::SameLine();
        if (ImGui::Button(TR(u8"Reset"))) SendCommand("resourcepacks_reset");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        auto DrawPack = [](const char* name, const char* author, const char* description, const char* cmd, ImTextureID icon)
            {
                ImGui::BeginChild(name, ImVec2(300, 100), true);
                ImGui::Columns(2, nullptr, false);
                ImGui::SetColumnWidth(0, 200);

                ImGui::Text(name);
                ImGui::TextDisabled(author);
                ImGui::TextWrapped(description);

                ImGui::Spacing();
                ImGui::Spacing();
                if (ImGui::Button(TR(u8"Load"), ImVec2(-1, 0))) SendCommand(cmd);

                ImGui::NextColumn();
                ImGui::Image(icon, ImVec2(80, 80));

                ImGui::Columns(1);
                ImGui::EndChild();
            };

        for (auto& p : cachedPacks)
        {
            std::string cmd = std::string("resourcepacks_load|") + p.folder;
            DrawPack(p.name.c_str(), p.author.c_str(), p.description.c_str(), cmd.c_str(), p.iconTexture);
        }

        ImGui::End();
	}

}