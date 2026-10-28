#include "Properties.h"
#include "MenuHelpers.h"
#include "Inventory.h"
#include "History.h"
#include "Explorer.h"
#include "IconsFontAwesome6.h"
#include "ui.h"

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
    int itemId = -1;
    int groupId = -1;
    int prototypeId = -1;
    std::string objectType = "";

    namespace {
        float movedFrom[3] = {};
        float turnedFrom[3] = {};

        std::string OrDash(int value) { return value >= 0 ? std::to_string(value) : u8"-"; }

        void Value(const char* label, const std::string& value) {
            ui::Property(label);
            ImGui::TextUnformatted(value.c_str());
        }

        void Title(const char* icon, const std::string& name, const std::string& detail) {
            ImGui::TextUnformatted((std::string(icon) + u8"  " + name).c_str());
            if (!detail.empty()) ui::Hint(detail.c_str());
            ImGui::Spacing();
        }

        void Selection(const std::set<std::string>& ids) {
            Title(ICON_FA_OBJECT_GROUP, std::to_string(ids.size()) + u8" " + TR(u8"objects"), TR(u8"Selected in the hierarchy"));
            if (ui::Button(TR(u8"Group"))) {
                std::string joined;
                for (const std::string& id : ids) {
                    if (!joined.empty()) joined += u8",";
                    joined += id;
                }
                SendCommand((u8"explorer_group_selection|" + joined).c_str());
            }
            ImGui::SameLine();
            bool locked = false;
            for (const std::string& id : ids)
                if (const Explorer::ObjectEntry* entry = Explorer::FindById(id)) locked = locked || Explorer::Protected(entry->type);
            ImGui::BeginDisabled(locked);
            if (ui::Button(TR(u8"Remove"), ui::Tone::Danger)) {
                std::set<std::string> removed = ids;
                for (const std::string& id : removed) SendCommand((u8"explorer_remove|" + id).c_str());
                Explorer::DeselectAll();
                ImGui::EndDisabled();
                return;
            }
            ImGui::EndDisabled();
            ImGui::Spacing();
            if (!ImGui::BeginTable(u8"##selection", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY)) return;
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(TR(u8"Name"), ImGuiTableColumnFlags_WidthStretch, 0.65f);
            ImGui::TableSetupColumn(TR(u8"ID"), ImGuiTableColumnFlags_WidthStretch, 0.35f);
            ImGui::TableHeadersRow();
            for (const std::string& id : ids) {
                const Explorer::ObjectEntry* entry = Explorer::FindById(id);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(entry ? (entry->name.empty() ? entry->type : entry->name).c_str() : id.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled(u8"%s", id.c_str());
            }
            ImGui::EndTable();
        }

        void Transform() {
            if (!ui::Section(TR(u8"Transform")) || !ui::BeginProperties(u8"##transform")) return;
            ui::Property(TR(u8"Position"));
            float position[3] = {positionX, positionY, positionZ};
            if (ImGui::DragFloat3(u8"##position", position, 0.1f, 0.0f, 0.0f, "%.3f")) {
                positionX = position[0]; positionY = position[1]; positionZ = position[2];
                char command[128];
                snprintf(command, sizeof(command), "properties_position|%.4f|%.4f|%.4f", positionX, positionY, positionZ);
                SendCommand(command);
            }
            if (ImGui::IsItemActivated()) { movedFrom[0] = positionX; movedFrom[1] = positionY; movedFrom[2] = positionZ; }
            if (!typing) typing = ImGui::IsItemActive();
            if (ImGui::IsItemDeactivatedAfterEdit())
                History::AddTransform(targetObjectId, movedFrom[0], movedFrom[1], movedFrom[2], positionX, positionY, positionZ);

            ui::Property(TR(u8"Rotation"));
            float rotation[3] = {rotationX, rotationY, rotationZ};
            if (ImGui::DragFloat3(u8"##rotation", rotation, 1.0f, 0.0f, 0.0f, "%.3f")) {
                rotationX = rotation[0]; rotationY = rotation[1]; rotationZ = rotation[2];
                char command[128];
                snprintf(command, sizeof(command), "properties_rotation|%.4f|%.4f|%.4f", rotationX, rotationY, rotationZ);
                SendCommand(command);
            }
            if (ImGui::IsItemActivated()) { turnedFrom[0] = rotationX; turnedFrom[1] = rotationY; turnedFrom[2] = rotationZ; }
            if (!typing) typing = ImGui::IsItemActive();
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                char command[256];
                snprintf(command, sizeof(command), "properties_history_rotation|%s|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f", targetObjectId.c_str(),
                    turnedFrom[0], turnedFrom[1], turnedFrom[2], rotationX, rotationY, rotationZ);
                SendCommand(command);
            }
            ui::EndProperties();
        }

        void Details() {
            if (!ui::Section(TR(u8"Object"))) return;
            if (ui::BeginProperties(u8"##object")) {
                Value(TR(u8"Type"), objectType.empty() ? u8"-" : objectType);
                Value(TR(u8"Object ID"), targetObjectId);
                Value(TR(u8"Item ID"), OrDash(itemId));
                Value(TR(u8"Group ID"), OrDash(groupId));
                if (isModel) Value(TR(u8"Prototype ID"), OrDash(prototypeId));
                ui::EndProperties();
            }
            ImGui::Spacing();
            if (ui::Button(TR(u8"Teleport to"))) SendCommand((u8"recovery_teleport_to_object|" + targetObjectId).c_str());
            ImGui::SameLine();
            if (ui::Button(TR(u8"Add to Assets"))) SendCommand((u8"inventory_add_object|" + targetObjectId).c_str());
            ImGui::SameLine();
            ImGui::BeginDisabled(Explorer::Protected(objectType));
            if (ui::Button(TR(u8"Remove"), ui::Tone::Danger)) SendCommand(u8"properties_remove");
            ImGui::EndDisabled();
        }

        void Asset(const Inventory::InventoryItem& item) {
            Title(ICON_FA_CUBES, item.name.empty() ? u8"ID " + item.id : item.name, item.categoryName);
            if (ui::Section(TR(u8"Asset")) && ui::BeginProperties(u8"##asset")) {
                Value(TR(u8"Item ID"), item.id);
                Value(TR(u8"Type ID"), item.typeId);
                Value(TR(u8"Category"), item.categoryName);
                Value(TR(u8"Category ID"), item.categoryId);
                Value(TR(u8"Slot"), std::to_string(item.slotPosition));
                Value(TR(u8"Price"), std::to_string(item.priceGold));
                Value(TR(u8"Resellable"), item.resellable ? TR(u8"Yes") : TR(u8"No"));
                Value(TR(u8"Purchased"), item.purchased ? TR(u8"Yes") : TR(u8"No"));
                Value(TR(u8"Author ID"), item.authorProfileId);
                ui::EndProperties();
            }
            if (!item.description.empty() && ui::Section(TR(u8"Description"))) ImGui::TextWrapped(u8"%s", item.description.c_str());
            ImGui::Spacing();
            if (ui::Button(TR(u8"Remove from Assets"), ui::Tone::Danger)) SendCommand((u8"inventory_remove|" + item.id).c_str());
        }
    }

    void Render() {
        if (ui::BeginPanel(u8"Properties", ICON_FA_SLIDERS, TR(u8"Inspector"), nullptr)) {
            const std::set<std::string>& ids = Explorer::GetSelectedIds();
            if (ids.size() > 1) Selection(ids);
            else if (targetObjectId != u8"-1") {
                const Explorer::ObjectEntry* entry = Explorer::FindById(targetObjectId);
                std::string name = entry ? (entry->name.empty() ? entry->type : entry->name) : objectType;
                Title(isModel ? ICON_FA_CUBE : ICON_FA_CIRCLE_INFO, name.empty() ? targetObjectId : name, objectType + u8"  ·  ID " + targetObjectId);
                Transform();
                Details();
            }
            else if (Inventory::HasSelected()) Asset(Inventory::GetSelected());
            else ui::EmptyState(TR(u8"Select an object in the world, the hierarchy or the assets"));
        }
        ui::EndPanel();
    }
}
