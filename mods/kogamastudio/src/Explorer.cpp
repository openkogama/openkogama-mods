#include "stdafx.h"
#include "imgui/imgui_internal.h"
#include "Explorer.h"
#include "Blueprints.h"
#include "Appearance.h"
#include "MenuHelpers.h"
#include "UILocale.h"
#include "Inventory.h"
#include "IconsFontAwesome6.h"
#include "json.hpp"
#include <fstream>
#include <algorithm>
#include <map>
#include <set>

using json = nlohmann::json;
using namespace menu;

namespace Explorer {

    static std::vector<ObjectEntry> objectList;
    static std::map<std::string, std::vector<int>> childrenMap;
    static int rootGroupId = -1;
    static char filterBuf[128] = "";
    static std::string selectedId;
    static std::set<std::string> selectedIds;
    static int lastClickedIdx = -1;
    static bool scrollToSelected = false;
    static std::string renameTargetId;
    static char renameBuf[256] = "";
    static bool renameFocused = false;

    enum class SortMode { Custom = 0, Alpha = 1, Distance = 2 };
    static SortMode sortMode = SortMode::Custom;
    static bool     sortDesc = false;
    static float    camX = 0.f, camY = 0.f, camZ = 0.f;
    static float    addCubeScale = 1.0f;
    // Settings
    bool advancedGrouping        = false;
    bool advGroupingWarnSkip     = false;
    bool advGroupingWarnPending  = false;

    // --- Creation modal state ---
    static bool  createModalOpen  = false;
    static int   createParentId   = -1;
    static float createPos[3]     = {0.f, 10.f, 0.f};
    static float createRot[3]     = {0.f, 0.f, 0.f};
    static float createScale[3]   = {1.f, 1.f, 1.f};
    static bool  createLocalOwner = false;

    // Locale-safe float formatter
    static std::string FF(float v)
    {
        char buf[32]; snprintf(buf, sizeof(buf), "%.6g", v);
        for (char* p = buf; *p; ++p) if (*p == ',') *p = '.';
        return buf;
    }

    static void OpenCreateModal(int parentId)
    {
        createParentId = parentId;
        createPos[0]   = 0.f; createPos[1] = 10.f; createPos[2] = 0.f;
        createRot[0]   = createRot[1] = createRot[2] = 0.f;
        createScale[0] = createScale[1] = createScale[2] = 1.f;
        createLocalOwner  = false;
        createModalOpen   = true;
    }

    static void RenderCreateModal()
    {
        if (!createModalOpen) return;
        ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        bool open = true;
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking;
        if (!ImGui::Begin(ICON_FA_LAYER_GROUP u8" New Group###create_modal", &open, flags))
        {
            ImGui::End();
            if (!open) createModalOpen = false;
            return;
        }

        ImGui::Text(TR(u8"Parent Group ID"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.f);
        ImGui::InputInt(u8"##cpg", &createParentId);
        if (createParentId >= 0)
        {
            const ObjectEntry* pg = FindById(std::to_string(createParentId));
            if (pg) { ImGui::SameLine(); ImGui::TextDisabled("(%s)", pg->name.c_str()); }
        }
        ImGui::Separator();

        ImGui::DragFloat3(TR(u8"Position"),          createPos,   0.5f);
        ImGui::DragFloat3(TR(u8"Rotation (euler)"),  createRot,   1.f,  -360.f, 360.f);
        ImGui::DragFloat3(TR(u8"Scale"),             createScale, 0.01f, 0.01f, 100.f);
        ImGui::Checkbox(TR(u8"Local owner"), &createLocalOwner);

        ImGui::Separator();
        bool confirm = ImGui::Button(TR(u8"Create"), ImVec2(150.f, 0));
        ImGui::SameLine();
        bool cancel  = ImGui::Button(TR(u8"Cancel"), ImVec2(150.f, 0));

        if (confirm)
        {
            char cmd[512];
            int lo = createLocalOwner ? 1 : 0;
            snprintf(cmd, sizeof(cmd), "explorer_create_group|%d|%s|%s|%s|%s|%s|%s|%s|%s|%s|%d",
                createParentId,
                FF(createPos[0]).c_str(), FF(createPos[1]).c_str(), FF(createPos[2]).c_str(),
                FF(createRot[0]).c_str(), FF(createRot[1]).c_str(), FF(createRot[2]).c_str(),
                FF(createScale[0]).c_str(), FF(createScale[1]).c_str(), FF(createScale[2]).c_str(), lo);
            SendCommand(cmd);
            createModalOpen = false;
        }
        if (cancel) createModalOpen = false;

        ImGui::End();
    }

    void SetCameraPos(float x, float y, float z) { camX = x; camY = y; camZ = z; }
    void GetCameraPos(float& x, float& y, float& z) { x = camX; y = camY; z = camZ; }

    static float DistSq(const ObjectEntry& e)
    {
        float dx = e.x - camX, dy = e.y - camY, dz = e.z - camZ;
        return dx * dx + dy * dy + dz * dz;
    }

    const std::set<std::string>& GetSelectedIds() { return selectedIds; }
    void DeselectAll() { selectedIds.clear(); selectedId.clear(); }

    const ObjectEntry* FindById(const std::string& id)
    {
        for (const auto& obj : objectList)
            if (obj.id == id) return &obj;
        return nullptr;
    }

    const std::vector<ObjectEntry>& GetAllObjects() { return objectList; }

    static void HandleSelection(int idx)
    {
        ImGuiIO& io = ImGui::GetIO();
        const std::string& id = objectList[idx].id;

        if (io.KeyShift && lastClickedIdx >= 0)
        {
            int lo = lastClickedIdx < idx ? lastClickedIdx : idx;
            int hi = lastClickedIdx > idx ? lastClickedIdx : idx;
            if (!io.KeyCtrl) selectedIds.clear();
            for (int i = lo; i <= hi; i++)
                selectedIds.insert(objectList[i].id);
        }
        else if (io.KeyCtrl)
        {
            if (selectedIds.count(id)) selectedIds.erase(id);
            else                       selectedIds.insert(id);
            lastClickedIdx = idx;
        }
        else
        {
            selectedIds.clear();
            selectedIds.insert(id);
            selectedId     = id;
            lastClickedIdx = idx;
        }
    }

    void SetRootGroupId(int id) { rootGroupId = id; }

    void SetSelected(const std::string& id)
    {
        selectedId = id;
        selectedIds.clear();
        selectedIds.insert(id);
        scrollToSelected = true;
    }

    static void RebuildChildrenMap()
    {
        childrenMap.clear();
        for (int i = 0; i < (int)objectList.size(); ++i)
            childrenMap[std::to_string(objectList[i].groupId)].push_back(i);

        for (auto& kv : childrenMap)
            std::sort(kv.second.begin(), kv.second.end(),
                [](int a, int b) { return objectList[a].order < objectList[b].order; });
    }

    void LoadFromFile(const std::string& path)
    {
        std::ifstream file(path);
        if (!file.is_open()) return;

        auto j = json::parse(file, nullptr, false);
        if (j.is_discarded() || !j.is_array()) return;

        objectList.clear();
        for (auto& item : j)
        {
            int rawId = item.value("id", 0);
            if (rawId == 0) continue;

            ObjectEntry obj;
            obj.id      = std::to_string(rawId);
            obj.order   = item.value("order", 0);
            obj.name    = item.value("name", "");
            obj.type    = item.value("type", "");
            obj.itemId  = item.value("itemId", -1);
            obj.groupId = item.value("groupId", -1);
            obj.isGroup = item.value("isGroup", false);
            obj.x       = item.value("x", 0.f);
            obj.y       = item.value("y", 0.f);
            obj.z       = item.value("z", 0.f);

            if (obj.itemId >= 0)
            {
                const Inventory::InventoryItem* inv = Inventory::FindById(std::to_string(obj.itemId));
                if (inv && !inv->name.empty())
                    obj.name = inv->name;
            }

            objectList.push_back(obj);
        }

        std::sort(objectList.begin(), objectList.end(),
            [](const ObjectEntry& a, const ObjectEntry& b) { return a.order < b.order; });

        RebuildChildrenMap();
    }

    enum class ItemCategory {
        Group, CubeModel, Terrain, SpawnPoint, Pickup, Blueprint,
        Logic, Light, Sound, Particle, Vehicle, Water, Skybox,
        Avatar, Special, Unknown
    };

    static ItemCategory GetCategory(const std::string& type)
    {
        if (type == "Group")                         return ItemCategory::Group;
        if (type == "CubeModel" ||
            type == "RoundCube")                     return ItemCategory::CubeModel;
        if (type == "CubeModelPrototypeTerrain" ||
            type == "CubeModelTerrainFineGrained")   return ItemCategory::Terrain;
        if (type == "SpawnPoint"    ||
            type == "SpawnPointRed" ||
            type == "SpawnPointGreen" ||
            type == "SpawnPointBlue" ||
            type == "SpawnPointYellow")              return ItemCategory::SpawnPoint;
        if (type == "PickupItemHealthPack"   ||
            type == "PickupItemCenterGun"    ||
            type == "PickupItemImpulseGun"   ||
            type == "PickupItemBazookaGun"   ||
            type == "PickupItemRailGun"      ||
            type == "PickupItemSpawner"      ||
            type == "PickupCubeGun"          ||
            type == "PickupMeleeWeapon"      ||
            type == "PickupCostume"          ||
            type == "PickupCustomGun"        ||
            type == "CollectibleItem"        ||
            type == "CollectTheItemCollectable" ||
            type == "CollectTheItemCollectableInstance" ||
            type == "GameCoin"               ||
            type == "GameCoinChest"          ||
            type == "GamePoint"              ||
            type == "GamePointChest")        return ItemCategory::Pickup;
        if (type == "Blueprint"              ||
            type == "BlueprintActivator"     ||
            type == "BlueprintFire"          ||
            type == "BlueprintSmoke"         ||
            type == "BlueprintExplosion"     ||
            type == "BlueprintDoor"          ||
            type == "BlueprintMeleeWeapon"   ||
            type == "BlueprintCostume"       ||
            type == "BlueprintCustomGun")    return ItemCategory::Blueprint;
        if (type == "Action"           ||
            type == "Mover"            ||
            type == "Path"             ||
            type == "PathNode"         ||
            type == "TriggerBox"       ||
            type == "TriggerCube"      ||
            type == "ToggleBox"        ||
            type == "PulseBox"         ||
            type == "RandomBox"        ||
            type == "Negate"           ||
            type == "And"              ||
            type == "Battery"          ||
            type == "TimeTrigger"      ||
            type == "PressurePlate"    ||
            type == "ModelToggle"      ||
            type == "ModelTransparency"||
            type == "CountingCube"     ||
            type == "GravityCube"      ||
            type == "ShootableButton"  ||
            type == "UseLever"         ||
            type == "WindTurbine"      ||
            type == "TextMsg"          ||
            type == "Teleporter"       ||
            type == "Goal"             ||
            type == "Flag"             ||
            type == "CheckPoint"       ||
            type == "KillLimit"        ||
            type == "OculusKillLimit"  ||
            type == "TeamEditor"       ||
            type == "CollectTheItem"   ||
            type == "CollectTheItemDropOff" ||
            type == "TimeAttackFlag"   ||
            type == "Door"             ||
            type == "Explosives"       ||
            type == "TestLogicCube"    ||
            type == "SentryGun"        ||
            type == "Skybox")          return ItemCategory::Logic;
        if (type == "PointLight"  ||
            type == "LightPreset" ||
            type == "Theme"       ||
            type == "CameraSettings") return ItemCategory::Light;
        if (type == "SoundEmitter" ||
            type == "GlobalSoundEmitter") return ItemCategory::Sound;
        if (type == "ParticleEmitter" ||
            type == "Fire"            ||
            type == "Smoke")          return ItemCategory::Particle;
        if (type == "HoverCraft"   ||
            type == "MonoPlane"    ||
            type == "JetPack"      ||
            type == "HamsterWheel" ||
            type == "Ghost"        ||
            type == "AdvancedGhost"||
            type == "ShrinkGun"    ||
            type == "WorldObjectSpawnerVehicle" ||
            type == "WorldObjectSpawnerVehicleEnergy" ||
            type == "VehicleEnergy")  return ItemCategory::Vehicle;
        if (type == "WaterPlane" ||
            type == "WaterPlanePreset") return ItemCategory::Water;
        if (type == "PlayModeAvatar"  ||
            type == "BuildModeAvatar" ||
            type == "AvatarSpawnRoleCreator") return ItemCategory::Avatar;
        if (type == "GameOptionsDataObject" ||
            type == "GamePassProgressionDataObject" ||
            type == "Skybox")         return ItemCategory::Special;
        return ItemCategory::Unknown;
    }

    static const char* TypeIcon(const std::string& type, const std::string& id = "")
    {
        // Logic circuit groups get a special icon
        if (!id.empty() && GetCategory(type) == ItemCategory::Group && Blueprints::IsLogicCircuit(id))
            return ICON_FA_NETWORK_WIRED;
        switch (GetCategory(type))
        {
        case ItemCategory::Group:       return ICON_FA_LAYER_GROUP;
        case ItemCategory::CubeModel:   return ICON_FA_CUBE;
        case ItemCategory::Terrain:     return ICON_FA_MOUNTAIN;
        case ItemCategory::SpawnPoint:  return ICON_FA_LOCATION_DOT;
        case ItemCategory::Pickup:      return ICON_FA_STAR;
        case ItemCategory::Blueprint:   return ICON_FA_WRENCH;
        case ItemCategory::Logic:       return ICON_FA_GEAR;
        case ItemCategory::Light:       return ICON_FA_LIGHTBULB;
        case ItemCategory::Sound:       return ICON_FA_VOLUME_HIGH;
        case ItemCategory::Particle:    return ICON_FA_FIRE;
        case ItemCategory::Vehicle:     return ICON_FA_GAMEPAD;
        case ItemCategory::Water:       return ICON_FA_CIRCLE;
        case ItemCategory::Avatar:      return ICON_FA_USER;
        case ItemCategory::Special:     return ICON_FA_GEARS;
        default:                        return ICON_FA_CIRCLE;
        }
    }

    static ImVec4 TypeColor(const std::string& type, const std::string& id = "")
    {
        if (!id.empty() && GetCategory(type) == ItemCategory::Group && Blueprints::IsLogicCircuit(id))
            return ImVec4(0.30f, 0.90f, 0.80f, 1.f); // cyan/teal for KogamaStudio special groups
        switch (GetCategory(type))
        {
        case ItemCategory::Group:       return ImVec4(1.00f, 1.00f, 1.00f, 1.f);
        case ItemCategory::CubeModel:   return ImVec4(0.60f, 0.60f, 0.60f, 1.f);
        case ItemCategory::Terrain:     return ImVec4(0.55f, 0.75f, 0.45f, 1.f);
        case ItemCategory::SpawnPoint:  return ImVec4(0.30f, 0.85f, 0.50f, 1.f);
        case ItemCategory::Pickup:      return ImVec4(1.00f, 0.85f, 0.20f, 1.f);
        case ItemCategory::Blueprint:   return ImVec4(0.40f, 0.85f, 1.00f, 1.f);
        case ItemCategory::Logic:       return ImVec4(1.00f, 0.65f, 0.25f, 1.f);
        case ItemCategory::Light:       return ImVec4(1.00f, 0.95f, 0.60f, 1.f);
        case ItemCategory::Sound:       return ImVec4(0.70f, 0.50f, 1.00f, 1.f);
        case ItemCategory::Particle:    return ImVec4(1.00f, 0.40f, 0.20f, 1.f);
        case ItemCategory::Vehicle:     return ImVec4(0.40f, 0.75f, 1.00f, 1.f);
        case ItemCategory::Water:       return ImVec4(0.20f, 0.70f, 1.00f, 1.f);
        case ItemCategory::Avatar:      return ImVec4(0.90f, 0.60f, 0.90f, 1.f);
        case ItemCategory::Special:     return ImVec4(0.80f, 0.80f, 0.80f, 1.f);
        default:                        return ImVec4(0.45f, 0.45f, 0.45f, 1.f);
        }
    }

    static void RenderTree(const std::string& parentId);

    static void RenderItem(int idx)
    {
        ObjectEntry& obj = objectList[idx];
        std::string label   = obj.name.empty() ? obj.type : obj.name;
        std::string display = std::string(TypeIcon(obj.type, obj.id)) + " " + label;

        if (renameTargetId == obj.id)
        {
            if (!renameFocused) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_Header));
            ImGui::PushStyleColor(ImGuiCol_NavHighlight, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            bool confirmed = ImGui::InputText(("##rename_" + obj.id).c_str(), renameBuf, sizeof(renameBuf),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            if (confirmed)
            {
                obj.name = renameBuf;
                SendCommand((u8"explorer_rename|" + obj.id + u8"|" + renameBuf).c_str());
                renameTargetId.clear();
                renameFocused = false;
            }
            if (!typing) typing = ImGui::IsItemActive();
            if (ImGui::IsItemActive()) renameFocused = true;
            else if (renameFocused) { renameTargetId.clear(); renameFocused = false; }
            return;
        }

        bool isSelected = selectedIds.count(obj.id) > 0;
        ImGui::PushStyleColor(ImGuiCol_Text, TypeColor(obj.type, obj.id));
        bool clicked = ImGui::Selectable((display + u8"##" + obj.id).c_str(), isSelected,
            ImGuiSelectableFlags_AllowOverlap);
        ImGui::PopStyleColor();

        if (clicked)
        {
            HandleSelection(idx);
            if (selectedIds.size() == 1)
            {
                scrollToSelected = false;
                SendCommand((u8"explorer_select|" + obj.id).c_str());
            }
        }

        if (isSelected && scrollToSelected) { ImGui::SetScrollHereY(0.5f); scrollToSelected = false; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"ID: %s", obj.id.c_str());

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            bool isMulti = selectedIds.size() > 1 && selectedIds.count(obj.id) > 0;
            if (isMulti)
            {
                std::string payload;
                for (const auto& sid : selectedIds)
                {
                    if (!payload.empty()) payload += ",";
                    payload += sid;
                }
                ImGui::SetDragDropPayload(u8"EXPLORER_ITEMS", payload.c_str(), payload.size() + 1);
                ImGui::Text(u8"%zu objects", selectedIds.size());
            }
            else
            {
                ImGui::SetDragDropPayload(u8"EXPLORER_ITEM", &idx, sizeof(int));
                ImGui::Text(u8"%s", display.c_str());
            }
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget())
        {
            {
                ImVec2 itemMin = ImGui::GetItemRectMin();
                ImVec2 itemMax = ImGui::GetItemRectMax();
                bool before = ImGui::GetMousePos().y < (itemMin.y + itemMax.y) * 0.5f;
                float lineY = before ? itemMin.y : itemMax.y;
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(itemMin.x, lineY), ImVec2(itemMax.x, lineY),
                    IM_COL32(255, 255, 255, 220), 2.0f);

                if (const ImGuiPayload* payload2 = ImGui::AcceptDragDropPayload(u8"EXPLORER_ITEM",
                    ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
                {
                    int src      = *(const int*)payload2->Data;
                    int insertAt = before ? idx : idx + 1;
                    if (src < insertAt) insertAt--;
                    if (src != insertAt && src >= 0 && src < (int)objectList.size())
                    {
                        ObjectEntry tmp = objectList[src];
                        objectList.erase(objectList.begin() + src);
                        objectList.insert(objectList.begin() + insertAt, tmp);
                        RebuildChildrenMap();

                        std::string order;
                        for (int i = 0; i < (int)objectList.size(); i++)
                        {
                            if (i > 0) order += ",";
                            order += objectList[i].id;
                        }
                        SendCommand((std::string(u8"explorer_set_order|") + order).c_str());
                    }
                }
            }
            bool isGroup = obj.type.find("Group") != std::string::npos;
            if (advancedGrouping || isGroup)
            {
                if (const ImGuiPayload* payload2 = ImGui::AcceptDragDropPayload(u8"EXPLORER_ITEMS",
                    ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
                {
                    SendCommand((std::string(u8"explorer_move_to_group|") + obj.id + u8"|" + (const char*)payload2->Data).c_str());
                }
                if (const ImGuiPayload* payload2 = ImGui::AcceptDragDropPayload(u8"INVENTORY_ITEM",
                    ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
                {
                    SendCommand((std::string(u8"world_place_item_in_group|") + (const char*)payload2->Data + u8"|" + obj.id).c_str());
                }
            }
            ImGui::EndDragDropTarget();
        }


        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem(TR(u8"Rename")))
            {
                renameTargetId = obj.id;
                renameFocused  = false;
                strncpy_s(renameBuf, sizeof(renameBuf), label.c_str(), _TRUNCATE);
            }
            ImGui::Separator();
            if (ImGui::MenuItem((std::string(ICON_FA_LAYER_GROUP) + u8"  " + TR(u8"Add Group here")).c_str()))
                { OpenCreateModal(obj.groupId); ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }

    static void RenderGroupNode(int idx)
    {
        ObjectEntry& obj = objectList[idx];
        std::string label   = obj.name.empty() ? obj.type : obj.name;
        std::string display = std::string(TypeIcon(obj.type, obj.id)) + " " + label;

        if (renameTargetId == obj.id)
        {
            if (!renameFocused) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_Header));
            ImGui::PushStyleColor(ImGuiCol_NavHighlight, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            bool confirmed = ImGui::InputText(("##rename_" + obj.id).c_str(), renameBuf, sizeof(renameBuf),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            if (confirmed)
            {
                obj.name = renameBuf;
                SendCommand((u8"explorer_rename|" + obj.id + u8"|" + renameBuf).c_str());
                renameTargetId.clear();
                renameFocused = false;
            }
            if (!typing) typing = ImGui::IsItemActive();
            if (ImGui::IsItemActive()) renameFocused = true;
            else if (renameFocused) { renameTargetId.clear(); renameFocused = false; }
            return;
        }

        bool isSelected = selectedIds.count(obj.id) > 0;

        ImGui::Spacing();

        bool hasChildren = false;
        if (childrenMap.count(obj.id) > 0)
        {
            if (advancedGrouping)
            {
                hasChildren = true;
            }
            else
            {
                for (int cidx : childrenMap.at(obj.id))
                {
                    if (objectList[cidx].groupId == rootGroupId) { hasChildren = true; break; }
                    const ObjectEntry* parent = FindById(std::to_string(objectList[cidx].groupId));
                    if (parent && parent->type.find("Group") != std::string::npos) { hasChildren = true; break; }
                }
            }
        }

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
            | ImGuiTreeNodeFlags_SpanAvailWidth
            | ImGuiTreeNodeFlags_AllowOverlap;
        if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
        if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

        ImGui::PushStyleColor(ImGuiCol_Text, TypeColor(obj.type, obj.id));
        bool open = ImGui::TreeNodeEx((display + u8"##" + obj.id).c_str(), flags);
        ImGui::PopStyleColor();

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            HandleSelection(idx);
            if (selectedIds.size() == 1)
            {
                scrollToSelected = false;
                SendCommand((u8"explorer_select|" + obj.id).c_str());
            }
        }
        if (isSelected && scrollToSelected) { ImGui::SetScrollHereY(0.5f); scrollToSelected = false; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"ID: %s", obj.id.c_str());

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            bool isMulti = selectedIds.size() > 1 && selectedIds.count(obj.id) > 0;
            if (isMulti)
            {
                std::string payload;
                for (const auto& sid : selectedIds) { if (!payload.empty()) payload += ","; payload += sid; }
                ImGui::SetDragDropPayload(u8"EXPLORER_ITEMS", payload.c_str(), payload.size() + 1);
                ImGui::Text(u8"%zu objects", selectedIds.size());
            }
            else
            {
                ImGui::SetDragDropPayload(u8"EXPLORER_ITEM", &idx, sizeof(int));
                ImGui::Text(u8"%s", display.c_str());
            }
            ImGui::EndDragDropSource();
        }

        bool nodeIsGroup = obj.type.find("Group") != std::string::npos;
        bool isLC = Blueprints::IsLogicCircuit(obj.id);
        bool canDropInto = (advancedGrouping || nodeIsGroup) && !isLC;
        if (ImGui::BeginDragDropTarget())
        {
            ImVec2 itemMin  = ImGui::GetItemRectMin();
            ImVec2 itemMax  = ImGui::GetItemRectMax();
            float  mouseY   = ImGui::GetMousePos().y;
            float  h        = itemMax.y - itemMin.y;
            // For groups: top/bottom 30% = reorder, center = drop into
            // For non-groups: always reorder
            bool before = mouseY < itemMin.y + h * 0.30f;
            bool after  = mouseY > itemMax.y - h * 0.30f;
            bool reorder = !canDropInto || before || after;

            if (reorder)
            {
                bool placeBefore = canDropInto ? before : (mouseY < (itemMin.y + itemMax.y) * 0.5f);
                float lineY = placeBefore ? itemMin.y : itemMax.y;
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(itemMin.x, lineY), ImVec2(itemMax.x, lineY),
                    IM_COL32(255, 255, 255, 220), 2.0f);

                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(u8"EXPLORER_ITEM",
                    ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
                {
                    int src      = *(const int*)payload->Data;
                    int insertAt = placeBefore ? idx : idx + 1;
                    if (src < insertAt) insertAt--;
                    if (src != insertAt && src >= 0 && src < (int)objectList.size())
                    {
                        ObjectEntry tmp = objectList[src];
                        objectList.erase(objectList.begin() + src);
                        objectList.insert(objectList.begin() + insertAt, tmp);
                        RebuildChildrenMap();
                        std::string order;
                        for (int i = 0; i < (int)objectList.size(); i++)
                        {
                            if (i > 0) order += ",";
                            order += objectList[i].id;
                        }
                        SendCommand((std::string(u8"explorer_set_order|") + order).c_str());
                    }
                }
            }
            else
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(u8"EXPLORER_ITEMS"))
                {
                    SendCommand((std::string(u8"explorer_move_to_group|") + obj.id + u8"|" + (const char*)payload->Data).c_str());
                }
                else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(u8"EXPLORER_ITEM"))
                {
                    int src = *(const int*)payload->Data;
                    SendCommand((std::string(u8"explorer_move_to_group|") + obj.id + u8"|" + objectList[src].id).c_str());
                }
                else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(u8"INVENTORY_ITEM"))
                {
                    SendCommand((std::string(u8"world_place_item_in_group|") + (const char*)payload->Data + u8"|" + obj.id).c_str());
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem(TR(u8"Rename")))
            {
                renameTargetId = obj.id;
                renameFocused  = false;
                strncpy_s(renameBuf, sizeof(renameBuf), label.c_str(), _TRUNCATE);
            }

            // Hide group creation options for logic circuit groups
            if (!isLC) {
                ImGui::Separator();
                int gid = std::stoi(obj.id);
                if (ImGui::MenuItem((std::string(ICON_FA_LAYER_GROUP) + u8"  " + TR(u8"Add Group inside")).c_str()))
                    { OpenCreateModal(gid); ImGui::CloseCurrentPopup(); }
                if (ImGui::MenuItem((std::string(ICON_FA_LAYER_GROUP) + u8"  " + TR(u8"Add Group here")).c_str()))
                    { OpenCreateModal(obj.groupId); ImGui::CloseCurrentPopup(); }
            }

            ImGui::Separator();
            if (isLC) {
                if (ImGui::MenuItem((std::string(ICON_FA_NETWORK_WIRED) + u8"  " + TR(u8"Unmark Logic Circuit")).c_str()))
                    Blueprints::SetLogicCircuit(obj.id, false);
            } else {
                if (ImGui::MenuItem((std::string(ICON_FA_NETWORK_WIRED) + u8"  " + TR(u8"Mark as Logic Circuit")).c_str()))
                    Blueprints::SetLogicCircuit(obj.id, true);
            }
            ImGui::EndPopup();
        }

        if (open)
        {
            RenderTree(obj.id);
            ImGui::TreePop();
        }
    }

    static void RenderTree(const std::string& parentId)
    {
        auto it = childrenMap.find(parentId);
        if (it == childrenMap.end()) return;
        for (int idx : it->second)
        {
            if (!advancedGrouping && objectList[idx].groupId != rootGroupId)
            {
                const ObjectEntry* parent = FindById(std::to_string(objectList[idx].groupId));
                if (!parent || parent->type.find("Group") == std::string::npos) continue;
            }
            RenderGroupNode(idx);
        }
    }

    void Render()
    {
        if (!ImGui::Begin((std::string(TR(u8"Explorer")) + u8"###Explorer").c_str(), nullptr, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::End();
            return;
        }

        if (ImGui::Button(TR(u8"Refresh"))) SendCommand(u8"explorer_refresh");
        ImGui::SameLine();
        if (ImGui::Button(u8"+" u8"##add_btn"))
            ImGui::OpenPopup(u8"##add_type_popup");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(TR(u8"Add object"));
        if (ImGui::BeginPopup(u8"##add_type_popup"))
        {
            int targetId = rootGroupId;
            if (!selectedId.empty())
            {
                const ObjectEntry* sel = FindById(selectedId);
                if (sel && sel->type.find("Group") != std::string::npos)
                    targetId = std::stoi(selectedId);
                else if (sel)
                    targetId = sel->groupId;
            }

            // Check if target is a logic circuit — block creating children inside it
            bool targetIsLC = Blueprints::IsLogicCircuit(std::to_string(targetId));

            ImGui::TextDisabled(u8"Kogama");
            ImGui::Separator();
            ImGui::BeginDisabled(targetIsLC);
            if (ImGui::MenuItem((std::string(ICON_FA_LAYER_GROUP) + u8"  " + TR(u8"Group")).c_str()))
                { OpenCreateModal(targetId); ImGui::CloseCurrentPopup(); }
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::TextDisabled(u8"KogamaStudio");
            ImGui::Separator();
            ImGui::BeginDisabled(targetIsLC);
            if (ImGui::MenuItem((std::string(ICON_FA_PERSON_RUNNING) + u8"  " + TR(u8"NPC")).c_str()))
                { ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem((std::string(ICON_FA_NETWORK_WIRED) + u8"  " + TR(u8"Logic Circuit")).c_str()))
                { Blueprints::CreateLogicCircuit(targetId); ImGui::CloseCurrentPopup(); }
            ImGui::EndDisabled();

            ImGui::EndPopup();
        }
        // Warning modal
        if (advGroupingWarnPending)
        {
            ImGui::OpenPopup(u8"##adv_grouping_warn");
            advGroupingWarnPending = false;
        }
        if (ImGui::BeginPopupModal(u8"##adv_grouping_warn", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.85f, 0.1f, 1.f));
            ImGui::Text(ICON_FA_TRIANGLE_EXCLAMATION u8"  %s", TR(u8"Advanced Grouping"));
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextWrapped(TR(u8"This is a powerful feature that allows placing any object inside any other object, bypassing normal grouping rules."));
            ImGui::Spacing();
            ImGui::TextWrapped(TR(u8"Used irresponsibly, it can easily corrupt your world or cause unexpected behavior. Proceed with caution."));
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Checkbox(TR(u8"Don't show this warning again"), &advGroupingWarnSkip);
            ImGui::Spacing();
            if (ImGui::Button(TR(u8"Enable anyway"), ImVec2(140, 0)))
            {
                advancedGrouping = true;
                Appearance::SaveSettings();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(TR(u8"Cancel"), ImVec2(80, 0)))
            {
                advGroupingWarnSkip = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FILTER))
            ImGui::OpenPopup(u8"##sort_popup");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(TR(u8"Sort"));
        if (ImGui::BeginPopup(u8"##sort_popup"))
        {
            ImGui::TextDisabled(TR(u8"Sort by"));
            ImGui::Separator();
            if (ImGui::RadioButton(TR(u8"Custom order"), sortMode == SortMode::Custom))
                sortMode = SortMode::Custom;
            if (ImGui::RadioButton(TR(u8"Alphabetical"), sortMode == SortMode::Alpha))
                sortMode = SortMode::Alpha;
            if (ImGui::RadioButton(TR(u8"Distance from camera"), sortMode == SortMode::Distance))
                sortMode = SortMode::Distance;
            ImGui::Separator();
            bool asc = !sortDesc;
            if (ImGui::RadioButton(TR(u8"Ascending"),  asc))  sortDesc = false;
            if (ImGui::RadioButton(TR(u8"Descending"), sortDesc)) sortDesc = true;
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText(u8"##explorer_filter", filterBuf, sizeof(filterBuf));
        if (!typing) typing = ImGui::IsItemActive();

        ImGui::Separator();
        ImGui::TextDisabled("%s: %zu", TR(u8"Objects"), objectList.size());

        ImGui::BeginChild(u8"##explorer_list", ImVec2(0, 0), false);

        if (objectList.empty())
        {
            ImGui::TextDisabled(TR(u8"No objects"));
        }
        else
        {
            std::string filter = filterBuf;
            std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

            bool useFlat = !filter.empty() || sortMode != SortMode::Custom || sortDesc;
            if (useFlat)
            {
                // Build index list, apply filter
                std::vector<int> indices;
                indices.reserve(objectList.size());
                for (int i = 0; i < (int)objectList.size(); ++i)
                {
                    if (!filter.empty())
                    {
                        const ObjectEntry& obj = objectList[i];
                        std::string label = obj.name.empty() ? obj.type : obj.name;
                        std::transform(label.begin(), label.end(), label.begin(), ::tolower);
                        if (label.find(filter) == std::string::npos && obj.id.find(filter) == std::string::npos)
                            continue;
                    }
                    indices.push_back(i);
                }

                // Sort by chosen mode (custom = already in order, no re-sort needed)
                if (sortMode == SortMode::Alpha)
                {
                    std::sort(indices.begin(), indices.end(), [](int a, int b) {
                        const std::string& na = objectList[a].name.empty() ? objectList[a].type : objectList[a].name;
                        const std::string& nb = objectList[b].name.empty() ? objectList[b].type : objectList[b].name;
                        std::string la = na, lb = nb;
                        std::transform(la.begin(), la.end(), la.begin(), ::tolower);
                        std::transform(lb.begin(), lb.end(), lb.begin(), ::tolower);
                        return la < lb;
                    });
                }
                else if (sortMode == SortMode::Distance)
                {
                    std::sort(indices.begin(), indices.end(), [](int a, int b) {
                        return DistSq(objectList[a]) < DistSq(objectList[b]);
                    });
                }

                if (sortDesc) std::reverse(indices.begin(), indices.end());

                for (int i : indices)
                    RenderItem(i);
            }
            else if (rootGroupId >= 0)
            {
                RenderTree(std::to_string(rootGroupId));
            }
            else
            {
                for (int i = 0; i < (int)objectList.size(); ++i)
                    RenderItem(i);
            }
        }

        if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->Rect(), ImGui::GetID(u8"##explorer_list")))
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(u8"INVENTORY_ITEM"))
            {
                SendCommand((std::string(u8"world_place_item|") + (const char*)payload->Data).c_str());
            }
            else if (rootGroupId >= 0)
            {
                std::string root = std::to_string(rootGroupId);
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(u8"EXPLORER_ITEMS"))
                    SendCommand((std::string(u8"explorer_move_to_group|") + root + u8"|" + (const char*)payload->Data).c_str());
                else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(u8"EXPLORER_ITEM"))
                    SendCommand((std::string(u8"explorer_move_to_group|") + root + u8"|" + objectList[*(const int*)payload->Data].id).c_str());
            }
            ImGui::EndDragDropTarget();
        }


        ImGui::EndChild();
        ImGui::End();

        RenderCreateModal();
    }
}
