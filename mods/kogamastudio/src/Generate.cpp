#include "Generate.h"
#include "pipe.h"
#include "MenuHelpers.h"

using namespace menu;

namespace Generate {

    static int targetId = -1;

	void Render()
	{

        if (ImGui::Button(TR(u8"Open Folder"))) OpenFolder(u8"Generate\\Models");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (pipe::generateProgress == 1.0f || pipe::generateProgress == 0.0f) {
            // showing available models
            char path[MAX_PATH];
            ExpandEnvironmentStringsA(u8"%LOCALAPPDATA%\\KogamaStudio\\Generate\\Models", path, MAX_PATH);
            DisplayDirTree(path);
        }
        else
        {
            ImGui::Text(TR(u8"Progress"));
            ImGui::SameLine();

            ImGui::ProgressBar(pipe::generateProgress, ImVec2(-100, 0));
            ImGui::SameLine();
            if (ImGui::Button(TR(u8"Cancel"))) {
                SendCommand(u8"generate_cancel");
                pipe::generateProgress = 0.0f;
            }
        }
	}
}
