#include "stdafx.h"
#include "imgui/imgui_internal.h"
#include "Explorer.h"
#include "Blueprints.h"
#include "Appearance.h"
#include "MenuHelpers.h"
#include "UILocale.h"
#include "Inventory.h"
#include "IconsFontAwesome6.h"
#include "ui.h"
#include "json.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;
using namespace menu;

namespace Explorer {
    bool advancedGrouping = false;
    bool advGroupingWarnSkip = false;
    bool advGroupingWarnPending = false;

    namespace {
        enum class Category { Group, CubeModel, Terrain, SpawnPoint, Pickup, Blueprint, Logic, Light, Sound, Particle, Vehicle, Water, Avatar, Special, Unknown };
        enum class SortMode { Custom, Name, Distance };
        enum class Drop { Before, After, Into };

        struct Row {
            int index;
            int depth;
            bool parent;
        };

        std::vector<ObjectEntry> objects;
        std::unordered_map<std::string, int> indexById;
        std::unordered_map<std::string, std::vector<int>> children;
        std::vector<char> shownInTree;
        std::vector<char> hasChildren;
        std::unordered_set<std::string> expanded;
        std::vector<Row> rows;
        bool rowsDirty = true;
        bool builtWithAdvanced = false;

        int rootGroupId = -1;
        char filter[128] = "";
        SortMode sortMode = SortMode::Custom;
        bool sortDescending = false;
        float camX = 0.f, camY = 0.f, camZ = 0.f;

        std::set<std::string> selectedIds;
        int anchorRow = -1;
        bool revealSelected = false;

        std::string renameId;
        char renameBuffer[256] = "";
        bool renameFocused = false;

        bool createRequested = false;
        int createParent = -1;
        float createPosition[3] = {0.f, 10.f, 0.f};
        float createRotation[3] = {0.f, 0.f, 0.f};
        float createScale[3] = {1.f, 1.f, 1.f};
        bool createLocalOwner = false;

        const std::unordered_map<std::string, Category>& Categories() {
            static const std::unordered_map<std::string, Category> table = [] {
                std::unordered_map<std::string, Category> map;
                auto add = [&](Category category, std::initializer_list<const char*> types) {
                    for (const char* type : types) map.emplace(type, category);
                };
                add(Category::Group, {"Group"});
                add(Category::CubeModel, {"CubeModel", "RoundCube"});
                add(Category::Terrain, {"CubeModelPrototypeTerrain", "CubeModelTerrainFineGrained"});
                add(Category::SpawnPoint, {"SpawnPoint", "SpawnPointRed", "SpawnPointGreen", "SpawnPointBlue", "SpawnPointYellow"});
                add(Category::Pickup, {"PickupItemHealthPack", "PickupItemCenterGun", "PickupItemImpulseGun", "PickupItemBazookaGun", "PickupItemRailGun",
                    "PickupItemSpawner", "PickupCubeGun", "PickupMeleeWeapon", "PickupCostume", "PickupCustomGun", "CollectibleItem",
                    "CollectTheItemCollectable", "CollectTheItemCollectableInstance", "GameCoin", "GameCoinChest", "GamePoint", "GamePointChest"});
                add(Category::Blueprint, {"Blueprint", "BlueprintActivator", "BlueprintFire", "BlueprintSmoke", "BlueprintExplosion", "BlueprintDoor",
                    "BlueprintMeleeWeapon", "BlueprintCostume", "BlueprintCustomGun"});
                add(Category::Logic, {"Action", "Mover", "Path", "PathNode", "TriggerBox", "TriggerCube", "ToggleBox", "PulseBox", "RandomBox", "Negate",
                    "And", "Battery", "TimeTrigger", "PressurePlate", "ModelToggle", "ModelTransparency", "CountingCube", "GravityCube", "ShootableButton",
                    "UseLever", "WindTurbine", "TextMsg", "Teleporter", "Goal", "Flag", "CheckPoint", "KillLimit", "OculusKillLimit", "TeamEditor",
                    "CollectTheItem", "CollectTheItemDropOff", "TimeAttackFlag", "Door", "Explosives", "TestLogicCube", "SentryGun", "Skybox"});
                add(Category::Light, {"PointLight", "LightPreset", "Theme", "CameraSettings"});
                add(Category::Sound, {"SoundEmitter", "GlobalSoundEmitter"});
                add(Category::Particle, {"ParticleEmitter", "Fire", "Smoke"});
                add(Category::Vehicle, {"HoverCraft", "MonoPlane", "JetPack", "HamsterWheel", "Ghost", "AdvancedGhost", "ShrinkGun",
                    "WorldObjectSpawnerVehicle", "WorldObjectSpawnerVehicleEnergy", "VehicleEnergy"});
                add(Category::Water, {"WaterPlane", "WaterPlanePreset"});
                add(Category::Avatar, {"PlayModeAvatar", "BuildModeAvatar", "AvatarSpawnRoleCreator"});
                add(Category::Special, {"GameOptionsDataObject", "GamePassProgressionDataObject"});
                return map;
            }();
            return table;
        }

        Category CategoryOf(const std::string& type) {
            auto found = Categories().find(type);
            return found == Categories().end() ? Category::Unknown : found->second;
        }

        bool IsGroupType(const ObjectEntry& entry) { return entry.type.find("Group") != std::string::npos; }

        bool IsCircuit(const ObjectEntry& entry) { return CategoryOf(entry.type) == Category::Group && Blueprints::IsLogicCircuit(entry.id); }

        const char* IconOf(const ObjectEntry& entry) {
            if (IsCircuit(entry)) return ICON_FA_NETWORK_WIRED;
            switch (CategoryOf(entry.type)) {
            case Category::Group: return ICON_FA_LAYER_GROUP;
            case Category::CubeModel: return ICON_FA_CUBE;
            case Category::Terrain: return ICON_FA_MOUNTAIN;
            case Category::SpawnPoint: return ICON_FA_LOCATION_DOT;
            case Category::Pickup: return ICON_FA_STAR;
            case Category::Blueprint: return ICON_FA_WRENCH;
            case Category::Logic: return ICON_FA_GEAR;
            case Category::Light: return ICON_FA_LIGHTBULB;
            case Category::Sound: return ICON_FA_VOLUME_HIGH;
            case Category::Particle: return ICON_FA_FIRE;
            case Category::Vehicle: return ICON_FA_GAMEPAD;
            case Category::Water: return ICON_FA_WATER;
            case Category::Avatar: return ICON_FA_USER;
            case Category::Special: return ICON_FA_GEARS;
            default: return ICON_FA_CIRCLE;
            }
        }

        ImVec4 ColorOf(const ObjectEntry& entry) {
            if (IsCircuit(entry)) return ImVec4(0.30f, 0.90f, 0.80f, 1.f);
            switch (CategoryOf(entry.type)) {
            case Category::Group: return ImGui::GetStyleColorVec4(ImGuiCol_Text);
            case Category::CubeModel: return ImVec4(0.60f, 0.60f, 0.60f, 1.f);
            case Category::Terrain: return ImVec4(0.55f, 0.75f, 0.45f, 1.f);
            case Category::SpawnPoint: return ImVec4(0.30f, 0.85f, 0.50f, 1.f);
            case Category::Pickup: return ImVec4(1.00f, 0.85f, 0.20f, 1.f);
            case Category::Blueprint: return ImVec4(0.40f, 0.85f, 1.00f, 1.f);
            case Category::Logic: return ImVec4(1.00f, 0.65f, 0.25f, 1.f);
            case Category::Light: return ImVec4(1.00f, 0.95f, 0.60f, 1.f);
            case Category::Sound: return ImVec4(0.70f, 0.50f, 1.00f, 1.f);
            case Category::Particle: return ImVec4(1.00f, 0.40f, 0.20f, 1.f);
            case Category::Vehicle: return ImVec4(0.40f, 0.75f, 1.00f, 1.f);
            case Category::Water: return ImVec4(0.20f, 0.70f, 1.00f, 1.f);
            case Category::Avatar: return ImVec4(0.90f, 0.60f, 0.90f, 1.f);
            case Category::Special: return ImVec4(0.80f, 0.80f, 0.80f, 1.f);
            default: return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            }
        }

        const std::string& LabelOf(const ObjectEntry& entry) { return entry.name.empty() ? entry.type : entry.name; }

        std::string Lower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return text;
        }

        std::string Number(float value) {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%.6g", value);
            for (char* at = buffer; *at; at++) if (*at == ',') *at = '.';
            return buffer;
        }

        float DistanceSq(const ObjectEntry& entry) {
            float dx = entry.x - camX, dy = entry.y - camY, dz = entry.z - camZ;
            return dx * dx + dy * dy + dz * dz;
        }

        void Rebuild() {
            indexById.clear();
            children.clear();
            for (int i = 0; i < (int)objects.size(); i++) {
                indexById[objects[i].id] = i;
                children[std::to_string(objects[i].groupId)].push_back(i);
            }
            for (auto& entry : children)
                std::stable_sort(entry.second.begin(), entry.second.end(), [](int a, int b) { return objects[a].order < objects[b].order; });
            shownInTree.assign(objects.size(), 0);
            for (int i = 0; i < (int)objects.size(); i++) {
                const ObjectEntry& entry = objects[i];
                if (advancedGrouping || entry.groupId == rootGroupId) {
                    shownInTree[i] = 1;
                    continue;
                }
                auto parent = indexById.find(std::to_string(entry.groupId));
                shownInTree[i] = parent != indexById.end() && IsGroupType(objects[parent->second]);
            }
            hasChildren.assign(objects.size(), 0);
            for (int i = 0; i < (int)objects.size(); i++) {
                auto list = children.find(objects[i].id);
                if (list == children.end()) continue;
                for (int child : list->second)
                    if (shownInTree[child]) {
                        hasChildren[i] = 1;
                        break;
                    }
            }
            builtWithAdvanced = advancedGrouping;
            rowsDirty = true;
        }

        void AddTree(const std::string& parent, int depth) {
            auto list = children.find(parent);
            if (list == children.end()) return;
            for (int index : list->second) {
                if (!shownInTree[index]) continue;
                rows.push_back({index, depth, hasChildren[index] != 0});
                if (hasChildren[index] && expanded.count(objects[index].id)) AddTree(objects[index].id, depth + 1);
            }
        }

        void BuildRows() {
            if (builtWithAdvanced != advancedGrouping) Rebuild();
            if (!rowsDirty) return;
            rowsDirty = false;
            rows.clear();
            std::string query = Lower(filter);
            bool flat = !query.empty() || sortMode != SortMode::Custom || sortDescending || rootGroupId < 0;
            if (!flat) {
                AddTree(std::to_string(rootGroupId), 0);
                return;
            }
            for (int i = 0; i < (int)objects.size(); i++) {
                if (!query.empty() && Lower(LabelOf(objects[i])).find(query) == std::string::npos && objects[i].id.find(query) == std::string::npos) continue;
                rows.push_back({i, 0, false});
            }
            if (sortMode == SortMode::Name)
                std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return Lower(LabelOf(objects[a.index])) < Lower(LabelOf(objects[b.index])); });
            else if (sortMode == SortMode::Distance)
                std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return DistanceSq(objects[a.index]) < DistanceSq(objects[b.index]); });
            if (sortDescending) std::reverse(rows.begin(), rows.end());
        }

        void Reveal(const std::string& id) {
            auto found = indexById.find(id);
            for (int depth = 0; found != indexById.end() && depth < 64; depth++) {
                int parent = objects[found->second].groupId;
                if (parent == rootGroupId) break;
                expanded.insert(std::to_string(parent));
                found = indexById.find(std::to_string(parent));
            }
            rowsDirty = true;
        }

        void Click(int row) {
            const ImGuiIO& io = ImGui::GetIO();
            const std::string& id = objects[rows[row].index].id;
            if (io.KeyShift && anchorRow >= 0 && anchorRow < (int)rows.size()) {
                if (!io.KeyCtrl) selectedIds.clear();
                for (int i = (std::min)(anchorRow, row); i <= (std::max)(anchorRow, row); i++) selectedIds.insert(objects[rows[i].index].id);
            }
            else if (io.KeyCtrl) {
                if (!selectedIds.erase(id)) selectedIds.insert(id);
                anchorRow = row;
            }
            else {
                selectedIds = {id};
                anchorRow = row;
            }
            if (selectedIds.size() == 1) SendCommand((u8"explorer_select|" + *selectedIds.begin()).c_str());
        }

        int TargetGroup() {
            if (selectedIds.size() != 1) return rootGroupId;
            const ObjectEntry* selected = FindById(*selectedIds.begin());
            if (!selected) return rootGroupId;
            return IsGroupType(*selected) ? std::stoi(selected->id) : selected->groupId;
        }

        void OpenCreate(int parent) {
            createParent = parent;
            createPosition[0] = 0.f; createPosition[1] = 10.f; createPosition[2] = 0.f;
            createRotation[0] = createRotation[1] = createRotation[2] = 0.f;
            createScale[0] = createScale[1] = createScale[2] = 1.f;
            createLocalOwner = false;
            createRequested = true;
        }

        void Move(int source, int target, bool after) {
            if (source < 0 || source >= (int)objects.size() || source == target) return;
            int insertAt = after ? target + 1 : target;
            if (source < insertAt) insertAt--;
            if (insertAt == source) return;
            ObjectEntry moved = objects[source];
            objects.erase(objects.begin() + source);
            objects.insert(objects.begin() + insertAt, moved);
            for (int i = 0; i < (int)objects.size(); i++) objects[i].order = i;
            Rebuild();
            std::string order;
            for (const ObjectEntry& entry : objects) {
                if (!order.empty()) order += u8",";
                order += entry.id;
            }
            SendCommand((u8"explorer_set_order|" + order).c_str());
        }

        Drop DropZone(bool into) {
            ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
            float y = ImGui::GetMousePos().y, height = max.y - min.y;
            if (into) {
                if (y < min.y + height * 0.3f) return Drop::Before;
                if (y > max.y - height * 0.3f) return Drop::After;
                return Drop::Into;
            }
            return y < (min.y + max.y) * 0.5f ? Drop::Before : Drop::After;
        }

        void DropTarget(const ObjectEntry& entry, int index) {
            if (!ImGui::BeginDragDropTarget()) return;
            bool into = (advancedGrouping || IsGroupType(entry)) && !IsCircuit(entry);
            Drop zone = DropZone(into);
            if (zone != Drop::Into) {
                ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
                float y = zone == Drop::Before ? min.y : max.y;
                ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0f);
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(u8"EXPLORER_ITEM", ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
                    Move(*(const int*)payload->Data, index, zone == Drop::After);
            }
            else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(u8"EXPLORER_ITEMS"))
                SendCommand((u8"explorer_move_to_group|" + entry.id + u8"|" + (const char*)payload->Data).c_str());
            else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(u8"EXPLORER_ITEM"))
                SendCommand((u8"explorer_move_to_group|" + entry.id + u8"|" + objects[*(const int*)payload->Data].id).c_str());
            if (into)
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(u8"INVENTORY_ITEM"))
                    SendCommand((std::string(u8"world_place_item_in_group|") + (const char*)payload->Data + u8"|" + entry.id).c_str());
            ImGui::EndDragDropTarget();
        }

        void DragSource(const ObjectEntry& entry, int index) {
            if (!ImGui::BeginDragDropSource()) return;
            if (selectedIds.size() > 1 && selectedIds.count(entry.id)) {
                std::string payload;
                for (const std::string& id : selectedIds) {
                    if (!payload.empty()) payload += u8",";
                    payload += id;
                }
                ImGui::SetDragDropPayload(u8"EXPLORER_ITEMS", payload.c_str(), payload.size() + 1);
                ImGui::Text(u8"%zu %s", selectedIds.size(), TR(u8"objects"));
            }
            else {
                ImGui::SetDragDropPayload(u8"EXPLORER_ITEM", &index, sizeof(int));
                ImGui::Text(u8"%s  %s", IconOf(entry), LabelOf(entry).c_str());
            }
            ImGui::EndDragDropSource();
        }

        void ContextMenu(const ObjectEntry& entry) {
            if (!ImGui::BeginPopupContextItem(u8"##row")) return;
            bool group = IsGroupType(entry);
            bool circuit = IsCircuit(entry);
            if (ImGui::MenuItem(TR(u8"Rename"))) {
                renameId = entry.id;
                renameFocused = false;
                strncpy_s(renameBuffer, sizeof(renameBuffer), LabelOf(entry).c_str(), _TRUNCATE);
            }
            if (!circuit) {
                ImGui::Separator();
                if (group && ImGui::MenuItem(TR(u8"Add Group inside"))) OpenCreate(std::stoi(entry.id));
                if (ImGui::MenuItem(TR(u8"Add Group here"))) OpenCreate(entry.groupId);
            }
            if (group) {
                ImGui::Separator();
                if (ImGui::MenuItem(circuit ? TR(u8"Unmark Logic Circuit") : TR(u8"Mark as Logic Circuit")))
                    Blueprints::SetLogicCircuit(entry.id, !circuit);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(TR(u8"Remove"), nullptr, false, !Protected(entry.type))) SendCommand((u8"explorer_remove|" + entry.id).c_str());
            ImGui::EndPopup();
        }

        void RenameRow(ObjectEntry& entry, float indent) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
            if (!renameFocused) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText(u8"##rename", renameBuffer, sizeof(renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                entry.name = renameBuffer;
                rowsDirty = true;
                SendCommand((u8"explorer_rename|" + entry.id + u8"|" + renameBuffer).c_str());
                renameId.clear();
                renameFocused = false;
                return;
            }
            if (!typing) typing = ImGui::IsItemActive();
            if (ImGui::IsItemActive()) renameFocused = true;
            else if (renameFocused) {
                renameId.clear();
                renameFocused = false;
            }
        }

        void Toggle(const std::string& id) {
            if (!expanded.erase(id)) expanded.insert(id);
            rowsDirty = true;
        }

        void RowWidget(int rowIndex) {
            const Row& row = rows[rowIndex];
            ObjectEntry& entry = objects[row.index];
            ImGui::PushID(entry.id.c_str());
            float arrow = ImGui::GetFontSize();
            float indent = row.depth * ImGui::GetStyle().IndentSpacing;
            if (renameId == entry.id) {
                RenameRow(entry, indent + arrow);
                ImGui::PopID();
                return;
            }
            bool selected = selectedIds.count(entry.id) > 0;
            bool open = row.parent && expanded.count(entry.id);
            float top = ImGui::GetCursorScreenPos().y;
            if (ImGui::Selectable(u8"##row", selected, ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick)) {
                if (row.parent && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) Toggle(entry.id);
                else Click(rowIndex);
            }
            ImVec2 min = ImGui::GetItemRectMin();
            ImGui::SetItemTooltip(u8"%s\nID %s", entry.type.c_str(), entry.id.c_str());
            DragSource(entry, row.index);
            DropTarget(entry, row.index);
            ContextMenu(entry);

            ImDrawList* draw = ImGui::GetWindowDrawList();
            float x = min.x + indent;
            if (row.parent) {
                ImGui::SetCursorScreenPos(ImVec2(x, top));
                if (ImGui::InvisibleButton(u8"##expand", ImVec2(arrow, ImGui::GetTextLineHeight()))) Toggle(entry.id);
                ImGui::RenderArrow(draw, ImVec2(x + arrow * 0.15f, top + ImGui::GetTextLineHeight() * 0.15f),
                    ImGui::GetColorU32(ImGuiCol_Text), open ? ImGuiDir_Down : ImGuiDir_Right, 0.7f);
            }
            x += arrow;
            const char* icon = IconOf(entry);
            draw->AddText(ImVec2(x, top), ImGui::GetColorU32(ColorOf(entry)), icon);
            x += ImGui::CalcTextSize(icon).x + ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;
            draw->AddText(ImVec2(x, top), ImGui::GetColorU32(ImGuiCol_Text), LabelOf(entry).c_str());
            ImGui::PopID();
        }

        void Toolbar() {
            float buttons = 0.0f;
            for (const char* icon : {ICON_FA_PLUS, ICON_FA_ARROW_DOWN_WIDE_SHORT, ICON_FA_ROTATE})
                buttons += ImGui::CalcTextSize(icon).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetNextItemWidth(-buttons);
            if (ImGui::InputTextWithHint(u8"##search", TR(u8"Search"), filter, sizeof(filter))) rowsDirty = true;
            if (!typing) typing = ImGui::IsItemActive();

            ImGui::SameLine();
            if (ui::IconButton(u8"add", ICON_FA_PLUS, TR(u8"Add object"))) ImGui::OpenPopup(u8"##add");
            if (ImGui::BeginPopup(u8"##add")) {
                int target = TargetGroup();
                ImGui::BeginDisabled(Blueprints::IsLogicCircuit(std::to_string(target)));
                if (ImGui::MenuItem((std::string(ICON_FA_LAYER_GROUP u8"  ") + TR(u8"Group")).c_str())) OpenCreate(target);
                if (ImGui::MenuItem((std::string(ICON_FA_NETWORK_WIRED u8"  ") + TR(u8"Logic Circuit")).c_str())) Blueprints::CreateLogicCircuit(target);
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }

            ImGui::SameLine();
            if (ui::IconButton(u8"sort", ICON_FA_ARROW_DOWN_WIDE_SHORT, TR(u8"Sort"), sortMode != SortMode::Custom || sortDescending)) ImGui::OpenPopup(u8"##sort");
            if (ImGui::BeginPopup(u8"##sort")) {
                SortMode before = sortMode;
                if (ImGui::MenuItem(TR(u8"Custom order"), nullptr, sortMode == SortMode::Custom)) sortMode = SortMode::Custom;
                if (ImGui::MenuItem(TR(u8"Alphabetical"), nullptr, sortMode == SortMode::Name)) sortMode = SortMode::Name;
                if (ImGui::MenuItem(TR(u8"Distance from camera"), nullptr, sortMode == SortMode::Distance)) sortMode = SortMode::Distance;
                ImGui::Separator();
                if (ImGui::MenuItem(TR(u8"Descending"), nullptr, sortDescending)) {
                    sortDescending = !sortDescending;
                    rowsDirty = true;
                }
                if (sortMode != before) rowsDirty = true;
                ImGui::EndPopup();
            }

            ImGui::SameLine();
            if (ui::IconButton(u8"refresh", ICON_FA_ROTATE, TR(u8"Refresh"))) SendCommand(u8"explorer_refresh");
        }

        void List() {
            ImGui::BeginChild(u8"##rows");
            if (objects.empty()) ui::EmptyState(TR(u8"No objects"));
            else if (rows.empty()) ui::EmptyState(TR(u8"Nothing matches the search"));
            else {
                float rowHeight = ImGui::GetTextLineHeightWithSpacing();
                if (revealSelected && selectedIds.size() == 1)
                    for (int i = 0; i < (int)rows.size(); i++)
                        if (objects[rows[i].index].id == *selectedIds.begin()) {
                            ImGui::SetScrollY((std::max)(0.0f, i * rowHeight - (ImGui::GetWindowHeight() - rowHeight) * 0.5f));
                            anchorRow = i;
                            break;
                        }
                ImGuiListClipper clipper;
                clipper.Begin((int)rows.size(), rowHeight);
                while (clipper.Step())
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) RowWidget(i);
            }
            revealSelected = false;

            if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->InnerRect, ImGui::GetID(u8"##background"))) {
                std::string root = std::to_string(rootGroupId);
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(u8"INVENTORY_ITEM"))
                    SendCommand((std::string(u8"world_place_item|") + (const char*)payload->Data).c_str());
                else if (rootGroupId >= 0) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(u8"EXPLORER_ITEMS"))
                        SendCommand((u8"explorer_move_to_group|" + root + u8"|" + (const char*)payload->Data).c_str());
                    else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(u8"EXPLORER_ITEM"))
                        SendCommand((u8"explorer_move_to_group|" + root + u8"|" + objects[*(const int*)payload->Data].id).c_str());
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::EndChild();
        }

        void CreateModal() {
            if (createRequested) {
                ui::OpenModal(u8"NewGroup");
                createRequested = false;
            }
            if (!ui::BeginModal(u8"NewGroup", TR(u8"New Group"), 380.0f)) return;
            if (ui::BeginProperties(u8"##group")) {
                ui::Property(TR(u8"Parent Group ID"));
                ImGui::InputInt(u8"##parent", &createParent);
                if (const ObjectEntry* parent = FindById(std::to_string(createParent))) ui::Hint(LabelOf(*parent).c_str());
                ui::Property(TR(u8"Position"));
                ImGui::DragFloat3(u8"##position", createPosition, 0.5f);
                ui::Property(TR(u8"Rotation (euler)"));
                ImGui::DragFloat3(u8"##rotation", createRotation, 1.0f, -360.0f, 360.0f);
                ui::Property(TR(u8"Scale"));
                ImGui::DragFloat3(u8"##scale", createScale, 0.01f, 0.01f, 100.0f);
                ui::Property(TR(u8"Local owner"));
                ImGui::Checkbox(u8"##local", &createLocalOwner);
                ui::EndProperties();
            }
            if (ui::ModalFooter(TR(u8"Create"), TR(u8"Cancel")) == 1) {
                char command[512];
                snprintf(command, sizeof(command), "explorer_create_group|%d|%s|%s|%s|%s|%s|%s|%s|%s|%s|%d", createParent,
                    Number(createPosition[0]).c_str(), Number(createPosition[1]).c_str(), Number(createPosition[2]).c_str(),
                    Number(createRotation[0]).c_str(), Number(createRotation[1]).c_str(), Number(createRotation[2]).c_str(),
                    Number(createScale[0]).c_str(), Number(createScale[1]).c_str(), Number(createScale[2]).c_str(), createLocalOwner ? 1 : 0);
                SendCommand(command);
            }
            ui::EndModal();
        }

    }

    void GroupingWarning() {
        if (advGroupingWarnPending) {
            ui::OpenModal(u8"AdvancedGrouping");
            advGroupingWarnPending = false;
        }
        if (!ui::BeginModal(u8"AdvancedGrouping", TR(u8"Advanced Grouping"), 420.0f)) return;
        ImGui::TextWrapped(u8"%s", TR(u8"This is a powerful feature that allows placing any object inside any other object, bypassing normal grouping rules."));
        ImGui::Spacing();
        ImGui::TextWrapped(u8"%s", TR(u8"Used irresponsibly, it can easily corrupt your world or cause unexpected behavior. Proceed with caution."));
        ImGui::Spacing();
        ImGui::Checkbox(TR(u8"Don't show this warning again"), &advGroupingWarnSkip);
        int result = ui::ModalFooter(TR(u8"Enable anyway"), TR(u8"Cancel"));
        if (result == 1) {
            advancedGrouping = true;
            Appearance::SaveSettings();
        }
        else if (result == 2) advGroupingWarnSkip = false;
        ui::EndModal();
    }

    void LoadFromFile(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) return;
        json list = json::parse(file, nullptr, false);
        if (list.is_discarded() || !list.is_array()) return;
        objects.clear();
        for (const json& item : list) {
            int id = item.value("id", 0);
            if (id == 0) continue;
            ObjectEntry entry;
            entry.id = std::to_string(id);
            entry.order = item.value("order", 0);
            entry.name = item.value("name", "");
            entry.type = item.value("type", "");
            entry.itemId = item.value("itemId", -1);
            entry.groupId = item.value("groupId", -1);
            entry.isGroup = item.value("isGroup", false);
            entry.x = item.value("x", 0.f);
            entry.y = item.value("y", 0.f);
            entry.z = item.value("z", 0.f);
            if (entry.itemId >= 0) {
                const Inventory::InventoryItem* asset = Inventory::FindById(std::to_string(entry.itemId));
                if (asset && !asset->name.empty()) entry.name = asset->name;
            }
            objects.push_back(entry);
        }
        std::stable_sort(objects.begin(), objects.end(), [](const ObjectEntry& a, const ObjectEntry& b) { return a.order < b.order; });
        Rebuild();
        for (auto it = selectedIds.begin(); it != selectedIds.end();)
            it = indexById.count(*it) ? std::next(it) : selectedIds.erase(it);
    }

    bool Protected(const std::string& type) {
        return !Appearance::allowDangerousDelete
            && (type == "CubeModelPrototypeTerrain" || type == "GameOptionsDataObject" || type == "GamePassProgressionDataObject");
    }

    void SetSelected(const std::string& id) {
        selectedIds = {id};
        Reveal(id);
        revealSelected = true;
    }

    void Select(const std::string& id) {
        SetSelected(id);
        SendCommand((u8"explorer_select|" + id).c_str());
    }

    void SetRootGroupId(int id) {
        rootGroupId = id;
        Rebuild();
    }

    int RootGroupId() { return rootGroupId; }

    void SetCameraPos(float x, float y, float z) {
        camX = x; camY = y; camZ = z;
        if (sortMode == SortMode::Distance) rowsDirty = true;
    }

    void GetCameraPos(float& x, float& y, float& z) { x = camX; y = camY; z = camZ; }

    const std::set<std::string>& GetSelectedIds() { return selectedIds; }

    const ObjectEntry* FindById(const std::string& id) {
        auto found = indexById.find(id);
        return found == indexById.end() ? nullptr : &objects[found->second];
    }

    const std::vector<ObjectEntry>& GetAllObjects() { return objects; }

    void DeselectAll() {
        selectedIds.clear();
        anchorRow = -1;
    }

    void Render() {
        if (ui::BeginPanel(u8"Explorer", ICON_FA_SITEMAP, TR(u8"Hierarchy"), nullptr)) {
            Toolbar();
            BuildRows();
            List();
        }
        ui::EndPanel();
        CreateModal();
    }
}
