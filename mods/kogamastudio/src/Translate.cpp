#include "Translate.h"
#include "MenuHelpers.h"
#include "pipe.h"

using namespace menu;

namespace Translate {

	static bool TranslateOwnMessagesEnabled = false;
	static char TranslateOwnMessagesLanguage[64] = u8"en";
	static bool TranslateTextCubesEnabled = false;
    static char TranslateCubesLanguage[64] = u8"en";

	void Render()
	{
        if (!ImGui::Begin((std::string(TR(u8"Translate")) + u8"###Translate").c_str(), nullptr, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::End();
            return;
        }

        // translate own messages
		if (ImGui::Checkbox(TR(u8"Translate Own Messages"), &TranslateOwnMessagesEnabled)) {
			if (TranslateOwnMessagesEnabled) SendCommand(u8"translate_own_messages_enabled|true");
			else SendCommand(u8"translate_own_messages_enabled|false");
		}

		if (TranslateOwnMessagesEnabled) {
			ImGui::Indent();
			ImGui::PushItemWidth(100);
            ImGui::PushID(u8"own");
			ImGui::InputText(TR(u8"Language"), TranslateOwnMessagesLanguage, sizeof(TranslateOwnMessagesLanguage));
            ImGui::PopID();
			ImGui::PopItemWidth();

			if (!typing) typing = ImGui::IsItemActive();

			if (ImGui::IsItemEdited()) {
				SendCommand((std::string(u8"translate_own_messages_language|") + TranslateOwnMessagesLanguage).c_str());
			}
			ImGui::Unindent();
		}

        // translate text cubes
		if (ImGui::Checkbox(TR(u8"Translate Text Cubes"), &TranslateTextCubesEnabled)) {
			if (TranslateTextCubesEnabled) SendCommand(u8"translate_text_cubes_enabled|true");
			else SendCommand(u8"translate_text_cubes_enabled|false");
		}

		if (TranslateTextCubesEnabled) {
			ImGui::Indent();
			ImGui::PushItemWidth(100);
            ImGui::PushID(u8"cubes");
			ImGui::InputText(TR(u8"Language"), TranslateCubesLanguage, sizeof(TranslateCubesLanguage));
            ImGui::PopID();
			ImGui::PopItemWidth();

			if (!typing) typing = ImGui::IsItemActive();

			if (ImGui::IsItemEdited()) {
				SendCommand((std::string(u8"translate_text_cubes_language|") + TranslateCubesLanguage).c_str());
			}

			ImGui::SameLine();

			if (ImGui::Button(TR(u8"Translate"))) SendCommand(u8"translate_text_cubes_translate");

			ImGui::Unindent();
		}

        ImGui::End();
	}

}
