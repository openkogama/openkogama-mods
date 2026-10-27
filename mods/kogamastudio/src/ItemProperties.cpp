#include "stdafx.h"
#include "ItemProperties.h"
#include "Inventory.h"
#include "MenuHelpers.h"
#include "UILocale.h"

using namespace menu;

namespace ItemProperties {

    void Render()
    {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
        if (ImGui::Begin((std::string(TR(u8"Item Properties")) + u8"###ItemProperties").c_str(), nullptr, flags))
        {
            if (!Inventory::HasSelected())
            {
                ImGui::TextDisabled(TR(u8"Select an item from Inventory"));
                ImGui::End();
                return;
            }

            const Inventory::InventoryItem& item = Inventory::GetSelected();

            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : ImGui::GetIO().Fonts->Fonts[0]);
            std::string displayName = item.name.empty() ? (u8"[ID " + item.id + u8"]") : item.name;
            ImGui::TextUnformatted(displayName.c_str());
            ImGui::PopFont();

            ImGui::Separator();

            if (ImGui::BeginTable(u8"##item_props", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn(u8"##key",   ImGuiTableColumnFlags_WidthStretch, 0.4f);
                ImGui::TableSetupColumn(u8"##value", ImGuiTableColumnFlags_WidthStretch, 0.6f);

                auto Row = [](const char* key, const std::string& value) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("%s", key);
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(value.c_str());
                };

                Row(TR(u8"Item ID"),    item.id);
                Row(TR(u8"Type ID"),    item.typeId);
                Row(TR(u8"Category"),   item.categoryName);
                Row(TR(u8"Category ID"),item.categoryId);
                Row(TR(u8"Slot"),       std::to_string(item.slotPosition));
                Row(TR(u8"Resellable"), item.resellable ? TR(u8"Yes") : TR(u8"No"));
                Row(TR(u8"Price"),      std::to_string(item.priceGold));
                Row(TR(u8"Purchased"),  item.purchased ? TR(u8"Yes") : TR(u8"No"));
                Row(TR(u8"Author ID"),  item.authorProfileId);

                ImGui::EndTable();
            }

            if (!item.description.empty())
            {
                ImGui::Spacing();
                ImGui::TextDisabled(TR(u8"Description"));
                ImGui::Separator();
                ImGui::TextWrapped("%s", item.description.c_str());
            }

            ImGui::Spacing();
            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.5f, 0.05f, 0.05f, 1.0f));
            if (ImGui::Button(TR(u8"Remove from Inventory"), ImVec2(-1, 0)))
                SendCommand((u8"inventory_remove|" + item.id).c_str());
            ImGui::PopStyleColor(3);
        }
        ImGui::End();
    }

}
