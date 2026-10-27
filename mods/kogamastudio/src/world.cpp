#include "stdafx.h"
#include "world.h"
#include "bridge.h"
#include "json.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace world {
    namespace {
        using Clock = std::chrono::steady_clock;
        using json = nlohmann::json;

        constexpr double Degrees = 57.29577951308232;
        constexpr double RefreshDelay = 0.25;
        constexpr double SettleSeconds = 2.0;
        constexpr double PollSeconds = 0.5;
        constexpr double PlayersSeconds = 5.0;
        constexpr int CameraFrames = 30;

        const OkMod* api = nullptr;
        int planet = -1;
        int pendingSelect = -1;
        int selected = -1;
        bool dirty = false;
        bool loaded = false;
        int settledCount = -1;
        double settledFor = 0.0;
        int frames = 0;
        std::unordered_map<int64_t, std::string> typeNames;
        int moving = -1;
        ok_vec3 movedFrom{0, 0, 0};
        int turning = -1;
        ok_quat turnedFrom{0, 0, 0, 1};
        bool quietLink = false;
        bool quietObjectLink = false;
        bool inventoryShown = false;
        Clock::time_point changedAt = Clock::now();
        Clock::time_point polledAt = Clock::now();
        Clock::time_point playersAt = Clock::now();

        double Since(Clock::time_point at) { return std::chrono::duration<double>(Clock::now() - at).count(); }

        int64_t Pointer(const void* value) { return (int64_t)(uintptr_t)value; }

        ok_object Call(ok_object self, const char* type, const char* method) {
            return self ? (ok_object)api->invoke(self, type, method, 0, nullptr, "", 'o') : 0;
        }

        int64_t Number(ok_object self, const char* type, const char* method) {
            return self ? api->invoke(self, type, method, 0, nullptr, "", 'i') : 0;
        }

        std::string Text(ok_object str) {
            char buf[512];
            api->string_utf8(str, buf, sizeof(buf));
            return buf;
        }

        std::vector<std::string> Split(const std::string& text, char separator) {
            std::vector<std::string> parts;
            size_t start = 0;
            while (start <= text.size()) {
                size_t end = text.find(separator, start);
                if (end == std::string::npos) end = text.size();
                parts.push_back(text.substr(start, end - start));
                start = end + 1;
            }
            return parts;
        }

        std::vector<int> Ids(const std::string& text) {
            std::vector<int> ids;
            for (const std::string& part : Split(text, ','))
                if (!part.empty()) ids.push_back(std::atoi(part.c_str()));
            return ids;
        }

        ok_object Wocm() { return api->call_static_obj("MVGameControllerBase", "get_WOCM"); }
        ok_object Game() { return api->call_static_obj("MVGameControllerBase", "get_Game"); }
        ok_object Operations() { return api->call_static_obj("MVGameControllerBase", "get_OperationRequests"); }

        ok_object Object(int id) {
            ok_object wocm = Wocm();
            int64_t arg = id;
            return wocm ? (ok_object)api->invoke(wocm, "MVWorldObjectClientManager", "GetWorldObjectClient", 1, &arg, "i", 'o') : 0;
        }

        ok_object Dictionary() { return api->field_obj(Wocm(), "MVWorldObjectClientManager", "worldObjects"); }

        std::vector<int> Keys(ok_object dictionary) {
            std::vector<int> keys;
            int count = (int)Number(dictionary, nullptr, "get_Count");
            ok_object collection = Call(dictionary, nullptr, "get_Keys");
            ok_object array = count > 0 ? api->array_new("System.Int32", count) : 0;
            if (!collection || !array) return keys;
            int64_t args[] = {(int64_t)array, 0};
            api->invoke(collection, nullptr, "CopyTo", 2, args, "oi", 'v');
            const int32_t* data = (const int32_t*)api->array_data(array);
            keys.assign(data, data + api->array_length(array));
            return keys;
        }

        std::vector<ok_object> Values(ok_object dictionary, const char* type) {
            std::vector<ok_object> values;
            int count = (int)Number(dictionary, nullptr, "get_Count");
            ok_object collection = Call(dictionary, nullptr, "get_Values");
            ok_object array = count > 0 ? api->array_new(type, count) : 0;
            if (!collection || !array) return values;
            int64_t args[] = {(int64_t)array, 0};
            api->invoke(collection, nullptr, "CopyTo", 2, args, "oi", 'v');
            const ok_object* data = (const ok_object*)api->array_data(array);
            values.assign(data, data + api->array_length(array));
            return values;
        }

        ok_vec3 Vector(ok_object boxed) {
            ok_vec3 value{0, 0, 0};
            api->unbox(boxed, &value, sizeof(value));
            return value;
        }

        ok_vec3 Position(ok_object wo) { return Vector(Call(wo, "MVWorldObjectClient", "get_Position")); }

        ok_quat Rotation(ok_object wo) {
            ok_quat value{0, 0, 0, 1};
            api->unbox(Call(wo, "MVWorldObjectClient", "get_Rotation"), &value, sizeof(value));
            return value;
        }

        double Angle(double radians) {
            double degrees = std::fmod(radians * Degrees, 360.0);
            return degrees < 0 ? degrees + 360.0 : degrees;
        }

        ok_vec3 Euler(const ok_quat& q) {
            double pitch = std::asin(std::clamp(2.0 * (q.w * q.x - q.y * q.z), -1.0, 1.0));
            double yaw = std::atan2(2.0 * (q.w * q.y + q.x * q.z), 1.0 - 2.0 * (q.x * q.x + q.y * q.y));
            double roll = std::atan2(2.0 * (q.w * q.z + q.x * q.y), 1.0 - 2.0 * (q.x * q.x + q.z * q.z));
            return {(float)Angle(pitch), (float)Angle(yaw), (float)Angle(roll)};
        }

        ok_quat FromEuler(double x, double y, double z) {
            double cx = std::cos(x / Degrees / 2), sx = std::sin(x / Degrees / 2);
            double cy = std::cos(y / Degrees / 2), sy = std::sin(y / Degrees / 2);
            double cz = std::cos(z / Degrees / 2), sz = std::sin(z / Degrees / 2);
            return {(float)(cy * sx * cz + sy * cx * sz), (float)(sy * cx * cz - cy * sx * sz),
                (float)(cy * cx * sz - sy * sx * cz), (float)(cy * cx * cz + sy * sx * sz)};
        }

        std::string TypeName(ok_object wo) {
            int64_t type = Number(wo, "MV.WorldObject.MVWorldObject", "get_WorldObjectType");
            auto known = typeNames.find(type);
            if (known != typeNames.end()) return known->second;
            char name[96];
            api->enum_name("MV.WorldObject.WorldObjectType", type, name, sizeof(name));
            std::string text = *name ? name : std::to_string(type);
            typeNames[type] = text;
            return text;
        }

        int ItemId(ok_object wo) { return (int)Number(wo, "MV.WorldObject.MVWorldObject", "get_ItemId"); }
        int GroupId(ok_object wo) { return (int)Number(wo, "MV.WorldObject.MVWorldObject", "get_GroupId"); }

        int RootGroup() {
            ok_object root = Call(Wocm(), "MVWorldObjectClientManager", "get_RootGroup");
            return root ? (int)Number(root, "MV.WorldObject.MVWorldObject", "get_Id") : -1;
        }

        int PlanetId() {
            ok_object session = api->call_static_obj("MVGameControllerBase", "get_GameSessionData");
            return session ? (int)api->field_num(session, "GameSessionData", "planetID", -1) : -1;
        }

        ok_object LocalObject() {
            ok_object player = api->call_static_obj("MVGameControllerBase", "get_LocalPlayer");
            return player ? Object((int)Number(player, "MVPlayer", "get_WoId")) : 0;
        }

        ok_object EditUi() {
            ok_object ui = api->call_static_obj("MVGameControllerBase", "get_EditModeUI");
            return ui && api->is_a(ui, "DesktopEditModeController") ? ui : 0;
        }

        ok_object Selection() {
            ok_object machine = Call(EditUi(), "DesktopEditModeController", "get_EditModeStateMachine");
            return machine ? api->field_obj(machine, "EditorStateMachine", "selectionController") : 0;
        }

        int Id(ok_object wo) { return wo ? (int)Number(wo, "MV.WorldObject.MVWorldObject", "get_Id") : -1; }

        int FieldInt(ok_object obj, const char* name) { return obj ? (int)api->field_num(obj, nullptr, name, 0) : 0; }

        std::string Clean(std::string text) {
            for (char& c : text)
                if (c == '\r' || c == '\n' || c == '|') c = ' ';
            size_t first = text.find_first_not_of(' '), last = text.find_last_not_of(' ');
            return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
        }

        ok_object Item(ok_object list, int index) {
            int64_t arg = index;
            return (ok_object)api->invoke(list, nullptr, "get_Item", 1, &arg, "i", 'o');
        }

        ok_object Member(ok_object self, const char* name) {
            if (!self) return 0;
            if (ok_object value = api->field_obj(self, nullptr, name)) return value;
            std::string getter = std::string("get_") + name;
            return (ok_object)api->invoke(self, nullptr, getter.c_str(), 0, nullptr, "", 'o');
        }

        ok_object ModelOf(ok_object wo, int depth = 0) {
            struct Path { const char* type; const char* first; const char* second; };
            static const Path paths[] = {
                {"MVSentryGunBlueprint", "EditableCubesWO", nullptr},
                {"MVMovingPlatformGroup", "Platform", "CubeModel"},
                {"MVRotator", "CubeModel", nullptr},
                {"CollectTheItemCollectable", "editableCubeModelWrapper", "CubeModel"},
                {"CollectTheItemDropOff", "editableCubeModelWrapper", "CubeModel"},
                {"MVAdvancedGhost", "editableCubeModelWrapper", "CubeModel"},
                {"MVJetPack", "editableCubeModelWrapper", "CubeModel"},
                {"MVHoverCraft", "editableCubeModelWrapper", "CubeModel"},
                {"Assets.Scripts.WorldObjectTypes.EditablePickupItem.MVEditablePickupItemBaseBlueprint", "editableCubeModel", nullptr},
                {"WorldObjectTypes.VehicleEnergy.MVWorldObjectSpawnerVehicleEnergy", "vehicleEnergyChild", "CubeModelInstance"},
                {"WorldObjectTypes.MVDoor.MVDoorBlueprint", "DoorLogic", "doorModelInstance"},
            };
            if (!wo || depth > 4) return 0;
            if (api->is_a(wo, "MVWorldObjectSpawnerVehicle"))
                return ModelOf(Object((int)Number(wo, nullptr, "get_SpawnWorldObjectID")), depth + 1);
            for (const Path& path : paths) {
                if (!api->is_a(wo, path.type)) continue;
                ok_object model = Member(wo, path.first);
                return path.second ? Member(model, path.second) : model;
            }
            return api->is_a(wo, "MVCubeModelInstance") ? wo : 0;
        }

        double AngleBetween(const ok_quat& a, const ok_quat& b) {
            double dot = std::fabs((double)a.x * b.x + (double)a.y * b.y + (double)a.z * b.z + (double)a.w * b.w);
            return 2.0 * std::acos(std::min(dot, 1.0)) * Degrees;
        }

        std::string ProjectFile() {
            char base[MAX_PATH] = {};
            SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, base);
            std::string folder = std::string(base) + "\\KogamaStudio\\Projects\\" + std::to_string(planet);
            SHCreateDirectoryExA(nullptr, folder.c_str(), nullptr);
            return folder + "\\objects.json";
        }

        void Announce(const std::string& path) {
            bridge::ToMenu("explorer_ready|" + path + "|" + std::to_string(RootGroup()) + "|0");
        }

        json Read() {
            std::ifstream file(ProjectFile());
            if (!file) return json::array();
            json list = json::parse(file, nullptr, false);
            return list.is_array() ? list : json::array();
        }

        void Write(const json& list) {
            std::string path = ProjectFile();
            std::ofstream file(path, std::ios::trunc);
            if (!file) return;
            file << list.dump();
            file.close();
            Announce(path);
        }

        void Fill(json& entry, ok_object wo) {
            ok_vec3 position = Position(wo);
            entry["groupId"] = GroupId(wo);
            entry["isGroup"] = api->is_a(wo, "MVGroup") != 0;
            entry["x"] = position.x;
            entry["y"] = position.y;
            entry["z"] = position.z;
        }

        void Refresh() {
            if (planet < 0 || !Wocm()) return;
            std::vector<int> live = Keys(Dictionary());
            std::unordered_set<int> alive(live.begin(), live.end());
            json list = Read();
            std::stable_sort(list.begin(), list.end(), [](const json& a, const json& b) {
                int left = a.value("order", 0), right = b.value("order", 0);
                return left != right ? left < right : a.value("id", 0) < b.value("id", 0);
            });
            json kept = json::array();
            std::unordered_set<int> saved;
            std::unordered_map<std::string, int> counts;
            for (json& entry : list) {
                int id = entry.value("id", -1);
                if (loaded && !alive.count(id)) continue;
                saved.insert(id);
                counts[entry.value("type", std::string())]++;
                if (ok_object wo = alive.count(id) ? Object(id) : 0) Fill(entry, wo);
                kept.push_back(entry);
            }
            std::sort(live.begin(), live.end());
            for (int id : live) {
                if (saved.count(id)) continue;
                ok_object wo = Object(id);
                if (!wo) continue;
                std::string type = TypeName(wo);
                int count = counts[type]++;
                json entry = {{"id", id}, {"type", type}, {"name", count == 0 ? type : type + " " + std::to_string(count)}, {"itemId", ItemId(wo)}};
                Fill(entry, wo);
                kept.push_back(entry);
            }
            for (size_t i = 0; i < kept.size(); i++) kept[i]["order"] = (int)i;
            Write(kept);
        }

        void SetOrder(const std::string& value) {
            if (planet < 0) return;
            std::vector<int> order = Ids(value);
            json list = Read();
            json sorted = json::array();
            std::unordered_set<int> placed;
            for (int id : order)
                for (const json& entry : list)
                    if (entry.value("id", -1) == id && placed.insert(id).second) sorted.push_back(entry);
            for (const json& entry : list)
                if (!placed.count(entry.value("id", -1))) sorted.push_back(entry);
            for (size_t i = 0; i < sorted.size(); i++) sorted[i]["order"] = (int)i;
            Write(sorted);
        }

        void Rename(const std::string& value) {
            size_t bar = value.find('|');
            if (planet < 0 || bar == std::string::npos) return;
            int id = std::atoi(value.substr(0, bar).c_str());
            std::string name = value.substr(bar + 1);
            json list = Read();
            for (json& entry : list) {
                if (entry.value("id", -1) != id) continue;
                if (name.empty()) {
                    entry["name"] = entry.value("type", std::string());
                    if (ok_object wo = Object(id)) entry["itemId"] = ItemId(wo);
                }
                else {
                    entry["name"] = name;
                    entry["itemId"] = -1;
                }
                Write(list);
                return;
            }
        }

        void SendProperties(int id) {
            ok_object wo = Object(id);
            if (!wo) return;
            selected = id;
            ok_vec3 position = Position(wo);
            ok_vec3 euler = Euler(Rotation(wo));
            ok_object cubes = ModelOf(wo);
            bool model = cubes != 0;
            int prototype = model ? (int)Number(cubes, "MVCubeModelBase", "get_Pid") : -1;
            char line[512];
            std::snprintf(line, sizeof(line), "properties_update|%d|%d|%d|%.4f|%.4f|%.4f|%.3f|%.3f|%.3f|%d|%d|%s",
                id, ItemId(wo), GroupId(wo), position.x, position.y, position.z, euler.x, euler.y, euler.z,
                model ? 1 : 0, prototype, TypeName(wo).c_str());
            bridge::ToMenu(line);
        }

        void Sync(int id, ok_object wo) {
            ok_object operations = Operations();
            ok_object transform = Call(wo, "MVWorldObjectClient", "get_Transform");
            int64_t args[] = {id, 0, (int64_t)transform};
            if (operations) api->invoke(operations, nullptr, "TransferOwnership", 3, args, "iio", 'v');
        }

        void SetPosition(int id, ok_vec3 position) {
            ok_object wo = Object(id);
            if (!wo) return;
            int64_t arg = Pointer(&position);
            api->invoke(wo, "MVWorldObjectClient", "set_Position", 1, &arg, "p", 'v');
            Sync(id, wo);
        }

        void SetRotation(int id, ok_quat rotation) {
            ok_object wo = Object(id);
            if (!wo) return;
            int64_t arg = Pointer(&rotation);
            api->invoke(wo, "MVWorldObjectClient", "set_Rotation", 1, &arg, "p", 'v');
            Sync(id, wo);
        }

        bool Vector3Of(const std::string& value, ok_vec3& out) {
            std::vector<std::string> parts = Split(value, '|');
            if (parts.size() != 3) return false;
            out = {(float)std::atof(parts[0].c_str()), (float)std::atof(parts[1].c_str()), (float)std::atof(parts[2].c_str())};
            return true;
        }

        void Remove(int id) {
            ok_object wo = Object(id);
            ok_object wocm = Wocm();
            if (!wo || !wocm) return;
            void* error = nullptr;
            int64_t args[] = {(int64_t)wocm, Pointer(&error)};
            api->invoke(wo, "MVWorldObjectClient", "Delete", 2, args, "op", 'i');
        }

        void TeleportTo(int id) {
            ok_object target = Object(id);
            ok_object local = LocalObject();
            if (!target || !local) return;
            ok_vec3 position = Position(target);
            int64_t arg = Pointer(&position);
            api->invoke(local, "MVWorldObjectClient", "set_Position", 1, &arg, "p", 'v');
        }

        void Select(int id) {
            ok_object selection = Selection();
            if (!selection) {
                SendProperties(id);
                return;
            }
            api->invoke(selection, "SelectionController", "ExitGroupToRoot", 0, nullptr, "", 'i');
            int64_t args[] = {id, 0, 1};
            api->invoke(selection, "SelectionController", "SelectWO", 3, args, "iii", 'o');
        }

        void SetVisible(int id, bool visible) {
            ok_object wo = Object(id);
            ok_object go = wo ? api->field_obj(wo, "MVWorldObjectClient", "gameObject") : 0;
            int64_t arg = visible;
            if (go) api->invoke(go, "UnityEngine.GameObject", "SetActive", 1, &arg, "i", 'v');
        }

        std::vector<ok_object> Players() {
            ok_object container = api->field_obj(Game(), "MVNetworkGame", "playerContainer");
            return Values(api->field_obj(container, "MVPlayerContainer", "players"), "MVPlayer");
        }

        std::string PlayerName(ok_object player) {
            ok_object profile = Call(player, "MVPlayer", "get_UserProfileData");
            return profile ? Text(api->field_obj(profile, nullptr, "UserName")) : std::string();
        }

        void SendPlayers() {
            std::string entries;
            for (ok_object player : Players()) {
                if (!entries.empty()) entries += ",";
                entries += std::to_string(Number(player, "MVPlayer", "get_WoId")) + ":" + PlayerName(player);
            }
            bridge::ToMenu("players_list|" + entries);
        }

        void SendCamera() {
            ok_object manager = api->call_static_obj("MVGameControllerBase", "get_MainCameraManager");
            ok_object camera = Call(manager, "MainCameraManager", "get_MainCamera");
            ok_object transform = Call(camera, "UnityEngine.Component", "get_transform");
            if (!transform) return;
            ok_vec3 position = Vector(Call(transform, "UnityEngine.Transform", "get_position"));
            char line[128];
            std::snprintf(line, sizeof(line), "camera_pos|%.2f|%.2f|%.2f", position.x, position.y, position.z);
            bridge::ToMenu(line);
        }

        void RefreshInventory() {
            ok_object repository = Call(EditUi(), "DesktopEditModeController", "get_PlayerShopInventoryRepository");
            ok_object categories = Call(repository, nullptr, "get_InventoryCategories");
            int count = (int)Number(categories, nullptr, "get_Count");
            if (count <= 0) return;
            ok_object keys = api->array_new("InventoryCategoryType", count);
            ok_object labels = api->array_new("System.String", count);
            int64_t keyArgs[] = {(int64_t)keys, 0}, labelArgs[] = {(int64_t)labels, 0};
            api->invoke(Call(categories, nullptr, "get_Keys"), nullptr, "CopyTo", 2, keyArgs, "oi", 'v');
            api->invoke(Call(categories, nullptr, "get_Values"), nullptr, "CopyTo", 2, labelArgs, "oi", 'v');
            const int32_t* ids = (const int32_t*)api->array_data(keys);
            const ok_object* names = (const ok_object*)api->array_data(labels);
            bridge::ToMenu("inventory_clear");
            for (int i = 0; i < count; i++) {
                int64_t arg = (int64_t)names[i];
                ok_object items = (ok_object)api->invoke(repository, nullptr, "GetInventoryItemsInCategorySlow", 1, &arg, "o", 'o');
                std::string label = Clean(Text(names[i]));
                int total = (int)Number(items, nullptr, "get_Count");
                for (int j = 0; j < total; j++) {
                    ok_object item = Item(items, j);
                    if (!item) continue;
                    char head[160];
                    std::snprintf(head, sizeof(head), "inventory_item|%d|", FieldInt(item, "itemID"));
                    std::string line = head + Clean(Text(api->field_obj(item, nullptr, "name"))) + "|" + std::to_string(FieldInt(item, "itemTypeID"))
                        + "|" + std::to_string(ids[i]) + "|" + label
                        + "|" + (FieldInt(item, "resellable") ? "True" : "False")
                        + "|" + std::to_string(FieldInt(item, "priceGold"))
                        + "|" + (FieldInt(item, "purchased") ? "True" : "False")
                        + "|" + std::to_string(FieldInt(item, "authorProfileID"))
                        + "|" + std::to_string(FieldInt(item, "slotPosition"))
                        + "|" + Clean(Text(api->field_obj(item, nullptr, "description")));
                    bridge::ToMenu(line);
                }
            }
            inventoryShown = true;
        }

        void AddToInventory(int id) {
            int64_t arg = id;
            api->flag_set("atlas_check_skip", 1);
            api->invoke(Operations(), nullptr, "AddWorldObjectToInventory", 1, &arg, "i", 'v');
            api->flag_set("atlas_check_skip", 0);
        }

        void RemoveFromInventory(int item) {
            int64_t arg = item;
            api->invoke(Operations(), nullptr, "RemoveItemFromInventory", 1, &arg, "i", 'v');
        }

        void PlaceItem(int item, int group) {
            ok_object manager = api->call_static_obj("MVGameControllerBase", "get_MainCameraManager");
            ok_object transform = Call(Call(manager, "MainCameraManager", "get_MainCamera"), "UnityEngine.Component", "get_transform");
            ok_vec3 position{0, 0, 0};
            if (transform) {
                ok_vec3 at = Vector(Call(transform, "UnityEngine.Transform", "get_position"));
                ok_vec3 forward = Vector(Call(transform, "UnityEngine.Transform", "get_forward"));
                position = {at.x + forward.x * 5.0f, at.y + forward.y * 5.0f, at.z + forward.z * 5.0f};
            }
            ok_quat identity{0, 0, 0, 1};
            int64_t args[] = {item, group, Pointer(&position), Pointer(&identity), 1, 1, 0};
            api->invoke(Operations(), nullptr, "AddItemToWorld", 7, args, "iippiii", 'v');
        }

        void FlushRotation() {
            if (turning < 0) return;
            int id = turning;
            turning = -1;
            ok_object wo = Object(id);
            if (!wo) return;
            ok_quat now = Rotation(wo);
            if (AngleBetween(turnedFrom, now) < 0.1) return;
            char line[320];
            std::snprintf(line, sizeof(line), "history_rotation|%d|%.5f|%.5f|%.5f|%.5f|%.5f|%.5f|%.5f|%.5f",
                id, turnedFrom.x, turnedFrom.y, turnedFrom.z, turnedFrom.w, now.x, now.y, now.z, now.w);
            bridge::ToMenu(line);
        }

        void AddLink(int output, int input) {
            if (!Object(output) || !Object(input)) return;
            ok_object link = api->new_object("MV.WorldObject.Link", 0, nullptr, "");
            if (!link) return;
            api->set_field_num(link, nullptr, "outputWOID", output);
            api->set_field_num(link, nullptr, "inputWOID", input);
            quietLink = true;
            int64_t arg = (int64_t)link;
            api->invoke(Operations(), nullptr, "AddLink", 1, &arg, "o", 'i');
        }

        void RemoveLink(int output, int input) {
            ok_object links = Call(Object(output), "MV.WorldObject.MVWorldObject", "get_OutputLinkRefs");
            int count = (int)Number(links, nullptr, "get_Count");
            for (int i = 0; i < count; i++) {
                ok_object link = Item(links, i);
                if (!link || FieldInt(link, "outputWOID") != output || FieldInt(link, "inputWOID") != input) continue;
                int64_t arg = FieldInt(link, "id");
                quietLink = true;
                api->invoke(Operations(), nullptr, "RemoveLink", 1, &arg, "i", 'v');
                return;
            }
        }

        void AddObjectLink(int connector, int object) {
            ok_object link = api->new_object("MV.WorldObject.ObjectLink", 0, nullptr, "");
            if (!link) return;
            api->set_field_num(link, nullptr, "objectConnectorWOID", connector);
            api->set_field_num(link, nullptr, "objectWOID", object);
            quietObjectLink = true;
            int64_t arg = (int64_t)link;
            api->invoke(Operations(), nullptr, "AddObjectLink", 1, &arg, "o", 'i');
        }

        int FindObjectLink(ok_object wo, int connector, int object) {
            ok_object links = Call(wo, "MV.WorldObject.MVWorldObject", "get_ObjectLinkRefs");
            int count = (int)Number(links, nullptr, "get_Count");
            for (int i = 0; i < count; i++) {
                ok_object link = Item(links, i);
                if (link && FieldInt(link, "objectConnectorWOID") == connector && FieldInt(link, "objectWOID") == object) return FieldInt(link, "id");
            }
            return -1;
        }

        void RemoveObjectLink(int connector, int object) {
            int found = FindObjectLink(Object(connector), connector, object);
            if (found < 0) found = FindObjectLink(Object(object), connector, object);
            if (found < 0)
                for (int id : Keys(Dictionary()))
                    if ((found = FindObjectLink(Object(id), connector, object)) >= 0) break;
            if (found < 0) return;
            int64_t arg = found;
            quietObjectLink = true;
            api->invoke(Operations(), nullptr, "RemoveObjectLink", 1, &arg, "i", 'v');
        }

        void HistoryRotation(const std::string& value) {
            std::vector<std::string> parts = Split(value, '|');
            if (parts.size() != 7) return;
            double v[6];
            for (int i = 0; i < 6; i++) v[i] = std::atof(parts[i + 1].c_str());
            ok_quat before = FromEuler(v[0], v[1], v[2]), after = FromEuler(v[3], v[4], v[5]);
            char line[320];
            std::snprintf(line, sizeof(line), "history_rotation|%d|%.5f|%.5f|%.5f|%.5f|%.5f|%.5f|%.5f|%.5f",
                std::atoi(parts[0].c_str()), before.x, before.y, before.z, before.w, after.x, after.y, after.z, after.w);
            bridge::ToMenu(line);
        }

        void Poll() {
            int now = PlanetId();
            if (now != planet) {
                planet = now;
                loaded = false;
                settledCount = -1;
                settledFor = 0.0;
                selected = -1;
                inventoryShown = false;
                if (planet >= 0 && GetFileAttributesA(ProjectFile().c_str()) != INVALID_FILE_ATTRIBUTES) Announce(ProjectFile());
            }
            if (!inventoryShown && planet >= 0) RefreshInventory();
            if (planet < 0 || loaded) return;
            int count = (int)Number(Dictionary(), nullptr, "get_Count");
            if (count > 0 && count == settledCount) settledFor += PollSeconds;
            else {
                settledCount = count;
                settledFor = 0.0;
            }
            if (settledFor < SettleSeconds) return;
            loaded = true;
            dirty = false;
            Refresh();
        }
    }

    void Attach(const OkMod* host) { api = host; }

    bool Handle(const std::string& command, const std::string& value) {
        int id = std::atoi(value.c_str());
        ok_vec3 vector{};
        if (command == "explorer_refresh") Refresh();
        else if (command == "explorer_set_order") SetOrder(value);
        else if (command == "explorer_rename") Rename(value);
        else if (command == "explorer_select") Select(id);
        else if (command == "properties_position") { if (selected >= 0 && Vector3Of(value, vector)) SetPosition(selected, vector); }
        else if (command == "properties_rotation") { if (selected >= 0 && Vector3Of(value, vector)) SetRotation(selected, FromEuler(vector.x, vector.y, vector.z)); }
        else if (command == "properties_remove") { if (selected >= 0) Remove(selected); }
        else if (command == "recovery_teleport_to_object" || command == "teleport_player") TeleportTo(id);
        else if (command == "object_set_visible") {
            std::vector<std::string> parts = Split(value, '|');
            if (parts.size() == 2) SetVisible(std::atoi(parts[0].c_str()), parts[1] == "true");
        }
        else if (command == "object_set_visible_batch") {
            std::vector<std::string> parts = Split(value, '|');
            if (parts.size() == 2)
                for (int each : Ids(parts[1])) SetVisible(each, parts[0] == "true");
        }
        else if (command == "inventory_refresh") RefreshInventory();
        else if (command == "inventory_remove") RemoveFromInventory(id);
        else if (command == "inventory_add_object") AddToInventory(id);
        else if (command == "world_place_item") PlaceItem(id, RootGroup());
        else if (command == "world_place_item_in_group") {
            std::vector<std::string> parts = Split(value, '|');
            if (parts.size() == 2) PlaceItem(std::atoi(parts[0].c_str()), std::atoi(parts[1].c_str()));
        }
        else if (command == "properties_history_rotation") HistoryRotation(value);
        else if (command.rfind("history_restore_", 0) == 0) {
            std::vector<std::string> parts = Split(value, '|');
            std::vector<double> v;
            for (const std::string& part : parts) v.push_back(std::atof(part.c_str()));
            int first = parts.empty() ? -1 : std::atoi(parts[0].c_str());
            int second = parts.size() > 1 ? std::atoi(parts[1].c_str()) : -1;
            if (command == "history_restore_position" && v.size() >= 4) SetPosition(first, {(float)v[1], (float)v[2], (float)v[3]});
            else if (command == "history_restore_rotation" && v.size() >= 5) SetRotation(first, {(float)v[1], (float)v[2], (float)v[3], (float)v[4]});
            else if (command == "history_restore_add_link" && v.size() >= 2) AddLink(first, second);
            else if (command == "history_restore_remove_link" && v.size() >= 2) RemoveLink(first, second);
            else if (command == "history_restore_add_object_link" && v.size() >= 2) AddObjectLink(first, second);
            else if (command == "history_restore_remove_object_link" && v.size() >= 2) RemoveObjectLink(first, second);
            else return false;
        }
        else return false;
        return true;
    }

    void Tick() {
        frames++;
        FlushRotation();
        if (Since(polledAt) >= PollSeconds) {
            polledAt = Clock::now();
            Poll();
        }
        if (dirty && loaded && Since(changedAt) >= RefreshDelay) {
            dirty = false;
            Refresh();
        }
        if (pendingSelect >= 0) {
            int id = pendingSelect;
            pendingSelect = -1;
            SendProperties(id);
            bridge::ToMenu("explorer_set_selected|" + std::to_string(id));
        }
        if (Since(playersAt) >= PlayersSeconds) {
            playersAt = Clock::now();
            SendPlayers();
        }
        if (frames % CameraFrames == 0) SendCamera();
    }

    void Selected(ok_object, int id) { pendingSelect = id; }

    ok_object SelectedModel() { return selected >= 0 ? ModelOf(Object(selected)) : 0; }

    int SelectedId() { return selected; }

    ok_object ModelById(int id) { return id >= 0 ? ModelOf(Object(id)) : 0; }

    std::vector<ok_object> DictionaryValues(ok_object dictionary, const char* type) { return Values(dictionary, type); }

    ok_object DictionaryKeys(ok_object dictionary, const char* type) {
        int count = (int)Number(dictionary, nullptr, "get_Count");
        ok_object collection = Call(dictionary, nullptr, "get_Keys");
        ok_object array = count > 0 ? api->array_new(type, count) : 0;
        if (!collection || !array) return 0;
        int64_t args[] = {(int64_t)array, 0};
        api->invoke(collection, nullptr, "CopyTo", 2, args, "oi", 'v');
        return array;
    }

    void TranslateStart(ok_object machine) {
        ok_object wo = Call(machine, "EditorStateMachine", "get_SingleSelectedWO");
        moving = Id(wo);
        if (wo) movedFrom = Position(wo);
    }

    void TranslateEnd() {
        if (moving < 0) return;
        int id = moving;
        moving = -1;
        ok_object wo = Object(id);
        if (!wo) return;
        ok_vec3 now = Position(wo);
        double dx = now.x - movedFrom.x, dy = now.y - movedFrom.y, dz = now.z - movedFrom.z;
        if (std::sqrt(dx * dx + dy * dy + dz * dz) < 0.05) return;
        char line[256];
        std::snprintf(line, sizeof(line), "history_transform|%d|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f",
            id, movedFrom.x, movedFrom.y, movedFrom.z, now.x, now.y, now.z);
        bridge::ToMenu(line);
    }

    void RotationStart(ok_object helper) {
        FlushRotation();
        ok_object machine = helper ? api->field_obj(helper, nullptr, "editorStateMachine") : 0;
        ok_object wo = Call(machine, "EditorStateMachine", "get_SingleSelectedWO");
        if (!wo) return;
        turning = Id(wo);
        turnedFrom = Rotation(wo);
    }

    void LinkAdded(ok_object link) {
        if (quietLink) {
            quietLink = false;
            return;
        }
        if (!link) return;
        bridge::ToMenu("history_link_added|" + std::to_string(FieldInt(link, "outputWOID")) + "|" + std::to_string(FieldInt(link, "inputWOID")));
    }

    void LinkRemoved(ok_object output, ok_object input) {
        if (quietLink) {
            quietLink = false;
            return;
        }
        if (!output || !input) return;
        bridge::ToMenu("history_link_removed|" + std::to_string(Id(output)) + "|" + std::to_string(Id(input)));
    }

    void ObjectLinkAdded(ok_object link) {
        if (quietObjectLink) {
            quietObjectLink = false;
            return;
        }
        if (!link) return;
        bridge::ToMenu("history_object_link_added|" + std::to_string(FieldInt(link, "objectConnectorWOID")) + "|" + std::to_string(FieldInt(link, "objectWOID")));
    }

    void ObjectLinkRemoved(ok_object connector, ok_object object) {
        if (quietObjectLink) {
            quietObjectLink = false;
            return;
        }
        if (!connector || !object) return;
        bridge::ToMenu("history_object_link_removed|" + std::to_string(Id(connector)) + "|" + std::to_string(Id(object)));
    }

    void Changed() {
        dirty = true;
        changedAt = Clock::now();
    }

    std::string Report() {
        std::vector<ok_object> players = Players();
        ok_object local = api->call_static_obj("MVGameControllerBase", "get_LocalPlayer");
        int localWo = local ? (int)Number(local, "MVPlayer", "get_WoId") : -1;
        std::string localName;
        for (ok_object player : players)
            if (Number(player, "MVPlayer", "get_WoId") == localWo) localName = PlayerName(player);
        ok_object session = api->call_static_obj("MVGameControllerBase", "get_GameSessionData");
        ok_object peer = Call(Game(), "MVNetworkGame", "get_Peer");
        std::string version = Text((ok_object)api->invoke(0, "UnityEngine.Application", "get_version", 0, nullptr, "", 'o'));
        char line[1024];
        std::snprintf(line, sizeof(line), ",ping=%d,worldobjects=%d,version=%s,players=%d,localplayer=%s,serverip=%s,profileid=%d,planetid=%d,region=%s,language=%s",
            peer ? (int)Number(peer, nullptr, "get_RoundTripTime") : 0,
            (int)Number(Dictionary(), nullptr, "get_Count"),
            version.c_str(), (int)players.size(), localName.c_str(),
            Text(api->field_obj(session, "GameSessionData", "serverIP")).c_str(),
            session ? (int)api->field_num(session, "GameSessionData", "profileID", 0) : 0,
            planet,
            Text(api->field_obj(session, "GameSessionData", "region")).c_str(),
            Text(api->field_obj(session, "GameSessionData", "language")).c_str());
        return line;
    }
}
