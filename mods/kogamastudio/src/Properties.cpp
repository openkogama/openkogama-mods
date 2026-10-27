#include "Properties.h"
#include "MenuHelpers.h"
#include "pipe.h"
#include "Inventory.h"
#include "History.h"
#include "Explorer.h"
#include "Appearance.h"

using namespace menu;

namespace Properties {

    std::string targetObjectId = u8"-1";
    float positionX = 0.0f;
    float positionY = 0.0f;
    float positionZ = 0.0f;
    float rotationX = 0.0f;
    float rotationY = 0.0f;
    float rotationZ = 0.0f;
    bool isModel = false;
    bool isPrevievPaste = false;
    bool clearBeforePaste = false;
    int itemId = -1;
    int groupId = -1;
    int prototypeId = -1;
    std::string objectType = "";

    static std::string sPrevObjectId = "";
    static std::string sPrevItemId = "";

    static float snapPosX = 0, snapPosY = 0, snapPosZ = 0;
    static float snapRotX = 0, snapRotY = 0, snapRotZ = 0;

	void Render() {
        ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(490, 20), ImGuiCond_FirstUseEver);
        ImGuiWindowFlags newFlags = ImGuiWindowFlags_NoCollapse;
        if (ImGui::Begin((std::string(TR(u8"Properties")) + u8"###Properties").c_str(), nullptr, newFlags))
        {
            const auto& selIds = Explorer::GetSelectedIds();
            bool hasMulti = selIds.size() > 1;
            bool hasObject = !hasMulti && targetObjectId != u8"-1";
            bool hasItem = !hasMulti && !hasObject && Inventory::HasSelected();

            std::string currentItemId = hasItem ? Inventory::GetSelected().id : "";
            bool selectionChanged = (targetObjectId != sPrevObjectId) || (currentItemId != sPrevItemId);
            if (selectionChanged)
            {
                sPrevObjectId = targetObjectId;
                sPrevItemId = currentItemId;
                ImGui::SetScrollHereY(0.0f);
            }

            if (!hasObject && !hasItem && !hasMulti)
            {
                ImGui::TextDisabled(TR(u8"Nothing selected"));
            }

            if (hasMulti)
            {
                if (ImGui::CollapsingHeader(TR(u8"Selection"), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::TextDisabled("%s: %zu", TR(u8"Objects"), selIds.size());
                    ImGui::Separator();
                    if (ImGui::Button(TR(u8"Group")))
                    {
                        std::string ids;
                        for (const auto& id : selIds)
                        {
                            if (!ids.empty()) ids += ",";
                            ids += id;
                        }
                        SendCommand((std::string(u8"explorer_group_selection|") + ids).c_str());
                    }
                    ImGui::Separator();
                    if (ImGui::BeginTable(u8"##sel_list", 2,
                        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp |
                        ImGuiTableFlags_ScrollY, ImVec2(0, 200)))
                    {
                        ImGui::TableSetupColumn(TR(u8"Name"), ImGuiTableColumnFlags_WidthStretch, 0.6f);
                        ImGui::TableSetupColumn(TR(u8"ID"),   ImGuiTableColumnFlags_WidthStretch, 0.4f);
                        ImGui::TableHeadersRow();
                        for (const auto& id : selIds)
                        {
                            const Explorer::ObjectEntry* e = Explorer::FindById(id);
                            std::string name = e ? (e->name.empty() ? e->type : e->name) : id;
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name.c_str());
                            ImGui::TableSetColumnIndex(1); ImGui::TextDisabled("%s", id.c_str());
                        }
                        ImGui::EndTable();
                    }
                }
            }

            if (hasObject)
            {
                if (selectionChanged) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                if (ImGui::CollapsingHeader(TR(u8"Object")))
                {
                    if (ImGui::BeginTable(u8"##obj_info", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableSetupColumn(u8"##k", ImGuiTableColumnFlags_WidthStretch, 0.45f);
                        ImGui::TableSetupColumn(u8"##v", ImGuiTableColumnFlags_WidthStretch, 0.55f);
                        auto Row = [](const char* key, const std::string& val) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("%s", key);
                            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(val.c_str());
                        };
                        Row(TR(u8"Object ID"),   targetObjectId);
                        Row(TR(u8"Type"),     objectType.empty() ? u8"-" : objectType);
                        Row(TR(u8"Item ID"),  itemId  >= 0 ? std::to_string(itemId)  : u8"-");
                        Row(TR(u8"Group ID"), groupId >= 0 ? std::to_string(groupId) : u8"-");
                        ImGui::EndTable();
                    }

                    bool protectedType = !Appearance::allowDangerousDelete &&
                        (objectType == "CubeModelPrototypeTerrain" ||
                         objectType == "GameOptionsDataObject" ||
                         objectType == "GamePassProgressionDataObject");
                    if (protectedType) ImGui::BeginDisabled();
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.5f, 0.05f, 0.05f, 1.0f));
                    if (ImGui::Button(TR(u8"Remove"))) SendCommand(u8"properties_remove");
                    ImGui::PopStyleColor(3);
                    if (protectedType) ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::Button(TR(u8"Teleport to"))) SendCommand((u8"recovery_teleport_to_object|" + targetObjectId).c_str());
                    ImGui::SameLine();
                    if (ImGui::Button(TR(u8"Add to Inventory"))) SendCommand((u8"inventory_add_object|" + targetObjectId).c_str());
                }

                if (selectionChanged) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                if (ImGui::CollapsingHeader(TR(u8"Transform")))
                {
                    auto DragPos = [&](const char* label, float& val) {
                        ImGui::PushItemWidth(-1);
                        ImGui::DragFloat(label, &val, 0.1f, 0.0f, 0.0f, "%.3f");
                        ImGui::PopItemWidth();
                        if (!typing) typing = ImGui::IsItemActive();
                        if (ImGui::IsItemActivated()) { snapPosX = positionX; snapPosY = positionY; snapPosZ = positionZ; }
                        if (ImGui::IsItemEdited()) {
                            char cmd[128]; snprintf(cmd, sizeof(cmd), "properties_position|%.4f|%.4f|%.4f", positionX, positionY, positionZ);
                            SendCommand(cmd);
                        }
                        return ImGui::IsItemDeactivatedAfterEdit();
                    };
                    auto DragRot = [&](const char* label, float& val) {
                        ImGui::PushItemWidth(-1);
                        ImGui::DragFloat(label, &val, 1.0f, 0.0f, 0.0f, "%.3f");
                        ImGui::PopItemWidth();
                        if (!typing) typing = ImGui::IsItemActive();
                        if (ImGui::IsItemActivated()) { snapRotX = rotationX; snapRotY = rotationY; snapRotZ = rotationZ; }
                        if (ImGui::IsItemEdited()) {
                            char cmd[128]; snprintf(cmd, sizeof(cmd), "properties_rotation|%.4f|%.4f|%.4f", rotationX, rotationY, rotationZ);
                            SendCommand(cmd);
                        }
                        return ImGui::IsItemDeactivatedAfterEdit();
                    };

                    ImGui::Text(TR(u8"Position"));
                    bool posXDone = DragPos(u8"X##pos_x", positionX);
                    bool posYDone = DragPos(u8"Y##pos_y", positionY);
                    bool posZDone = DragPos(u8"Z##pos_z", positionZ);

                    ImGui::Text(TR(u8"Rotation"));
                    bool rotXDone = DragRot(u8"X##rot_x", rotationX);
                    bool rotYDone = DragRot(u8"Y##rot_y", rotationY);
                    bool rotZDone = DragRot(u8"Z##rot_z", rotationZ);

                    if (posXDone || posYDone || posZDone)
                        History::AddTransform(targetObjectId,
                            snapPosX, snapPosY, snapPosZ,
                            positionX, positionY, positionZ);

                    if (rotXDone || rotYDone || rotZDone)
                    {
                        char cmd[256];
                        snprintf(cmd, sizeof(cmd),
                            "properties_history_rotation|%s|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f",
                            targetObjectId.c_str(),
                            snapRotX, snapRotY, snapRotZ,
                            rotationX, rotationY, rotationZ);
                        SendCommand(cmd);
                    }
                }

                if (isModel)
                {
                    if (selectionChanged) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                    if (ImGui::CollapsingHeader(TR(u8"Model")))
                    {
                        if (prototypeId >= 0)
                        {
                            ImGui::TextDisabled(u8"%s: %d", TR(u8"Prototype ID"), prototypeId);
                            ImGui::Spacing();
                        }

                        bool building = pipe::isPasteBuilding;

                        if (ImGui::Button(TR(u8"Copy Cubes")))
                            SendCommand(u8"clipboard_copy_model");

                        if (building) ImGui::BeginDisabled();
                        if (ImGui::Button(TR(u8"Paste Cubes")))
                            SendCommand(u8"clipboard_paste_model");
                        if (building) ImGui::EndDisabled();

                        if (ImGui::Checkbox(TR(u8"Clear Model Before Paste"), &clearBeforePaste))
                            SendCommand(clearBeforePaste ? u8"clipboard_clear_before_paste|true" : u8"clipboard_clear_before_paste|false");

                        if (ImGui::Checkbox(TR(u8"Preview Paste Cubes"), &isPrevievPaste))
                        {
                            if (isPrevievPaste) SendCommand(u8"clipboard_preview_paste_model|true");
                            else SendCommand(u8"clipboard_preview_paste_model|false");
                        }

                        if (building)
                        {
                            ImGui::Spacing();
                            ImGui::Separator();

                            float progress = pipe::pasteTotalCubes > 0
                                ? (float)pipe::pastePlacedCubes / (float)pipe::pasteTotalCubes
                                : 0.0f;

                            char overlay[64];
                            snprintf(overlay, sizeof(overlay), "%d / %d  (%.0f%%)",
                                pipe::pastePlacedCubes, pipe::pasteTotalCubes, progress * 100.0f);
                            ImGui::ProgressBar(progress, ImVec2(-1, 0), overlay);

                            if (pipe::pasteEtaSeconds >= 0.0f)
                            {
                                int eta = (int)pipe::pasteEtaSeconds;
                                char etaBuf[32];
                                if (eta >= 60)
                                    snprintf(etaBuf, sizeof(etaBuf), "%dm %ds", eta / 60, eta % 60);
                                else
                                    snprintf(etaBuf, sizeof(etaBuf), "%ds", eta);
                                ImGui::TextDisabled(TR(u8"Time left: %s"), etaBuf);
                                ImGui::SameLine();
                            }

                            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.5f, 0.05f, 0.05f, 1.0f));
                            if (ImGui::Button(TR(u8"Cancel")))
                                SendCommand(u8"clipboard_paste_cancel");
                            ImGui::PopStyleColor(3);
                        }
                    }
                }
            }

            if (hasItem)
            {
                if (selectionChanged) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                if (ImGui::CollapsingHeader(TR(u8"Item")))
                {
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

                        Row(TR(u8"Item ID"),     item.id);
                        Row(TR(u8"Type ID"),     item.typeId);
                        Row(TR(u8"Category"),    item.categoryName);
                        Row(TR(u8"Category ID"), item.categoryId);
                        Row(TR(u8"Slot"),        std::to_string(item.slotPosition));
                        Row(TR(u8"Resellable"),  item.resellable ? TR(u8"Yes") : TR(u8"No"));
                        Row(TR(u8"Price"),       std::to_string(item.priceGold));
                        Row(TR(u8"Purchased"),   item.purchased ? TR(u8"Yes") : TR(u8"No"));
                        Row(TR(u8"Author ID"),   item.authorProfileId);

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
            }
        }
        ImGui::End();
	}
}
