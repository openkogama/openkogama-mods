#include "editor.h"
#include "okmod.h"
#include <cstring>
#include <map>
#include <vector>

namespace editor {
    namespace {
        constexpr int EditMode = 0;
        constexpr int64_t LeftControl = 306;
        constexpr int64_t PointerSelect = 5;
        constexpr int64_t Selectable = 1;
        constexpr int64_t HasCubeModel = 2;
        constexpr int64_t AddLinkEvent = 39;
        constexpr int64_t ValidLink = 9;
        constexpr int InputConnector = 1;
        constexpr int OutputConnector = 2;
        constexpr int64_t PickMask = -262149;
        constexpr size_t HitSize = 0x58;
        constexpr size_t HitWoId = 0x28;
        constexpr int LinkLoopSlot = 3;
        constexpr int SeveritySlot = 8;
        constexpr double FlagSeconds = 1.0;

        const OkMod* api = nullptr;
        bool multiSelect = false;
        bool ctrlSelect = false;
        bool fastLinks = false;
        bool objectLinks = false;
        bool linkFix = false;
        bool flagsOn = false;
        bool flagsOverride = false;
        uint64_t flagsMask = 0;
        std::map<int, uint64_t> originalFlags;
        double flagClock = 0;
        int tempInput = -1;
        int tempOutput = -1;
        int tempClicks = 0;
        int editedGroup = 0;

        int64_t Pointer(const void* value) { return (int64_t)(uintptr_t)value; }

        int64_t FloatBits(double value) {
            float narrow = (float)value;
            int64_t bits = 0;
            std::memcpy(&bits, &narrow, 4);
            return bits;
        }

        ok_object Call(ok_object self, const char* type, const char* method) {
            return self ? (ok_object)api->invoke(self, type, method, 0, nullptr, "", 'o') : 0;
        }

        bool Flag(ok_object self, const char* type, const char* method) {
            return self && (api->invoke(self, type, method, 0, nullptr, "", 'i') & 0xFF) != 0;
        }

        int GameMode() { return (int)api->call_static_i("MVGameControllerBase", "get_GameMode"); }

        bool Playing() { return Flag(api->call_static_obj("MVGameControllerBase", "get_Game"), "MVNetworkGame", "get_IsPlaying"); }

        ok_object Manager() { return api->call_static_obj("MVGameControllerBase", "get_WOCM"); }

        ok_object Object(int id) {
            ok_object manager = Manager();
            int64_t argument = id;
            return manager ? (ok_object)api->invoke(manager, "MVWorldObjectClientManager", "GetWorldObjectClient", 1, &argument, "i", 'o') : 0;
        }

        int IdOf(ok_object wo) { return wo ? (int)api->field_num(wo, nullptr, "id", -1) : -1; }

        bool HasFlag(ok_object wo, int64_t flag) {
            return wo && (api->invoke(wo, "MVWorldObjectClient", "HasInteractionFlag", 1, &flag, "i", 'i') & 0xFF) != 0;
        }

        bool KeyHeld(int64_t key) { return (api->invoke(0, "UnityEngine.Input", "GetKey", 1, &key, "i", 'i') & 0xFF) != 0; }

        bool ControlDown(int64_t control) { return (api->invoke(0, "MVInputWrapper", "GetBooleanControlDown", 1, &control, "i", 'i') & 0xFF) != 0; }

        ok_object CameraManager() { return api->call_static_obj("MVGameControllerBase", "get_MainCameraManager"); }

        ok_object StateMachine() {
            ok_object ui = api->call_static_obj("MVGameControllerBase", "get_EditModeUI");
            return ui && api->is_a(ui, "DesktopEditModeController") ? Call(ui, "DesktopEditModeController", "get_EditModeStateMachine") : 0;
        }

        ok_object Selection() {
            ok_object machine = StateMachine();
            return machine ? api->field_obj(machine, "EditorStateMachine", "selectionController") : 0;
        }

        ok_object Selected(ok_object controller) { return controller ? api->field_obj(controller, "SelectionController", "selectedIDs") : 0; }

        int Count(ok_object collection) { return collection ? (int)api->invoke(collection, nullptr, "get_Count", 0, nullptr, "", 'i') : 0; }

        bool Contains(ok_object set, int id) {
            int64_t argument = id;
            return set && (api->invoke(set, nullptr, "Contains", 1, &argument, "i", 'i') & 0xFF) != 0;
        }

        void Remove(ok_object set, int id) {
            int64_t argument = id;
            if (set) api->invoke(set, nullptr, "Remove", 1, &argument, "i", 'i');
        }

        std::vector<int> Items(ok_object set) {
            std::vector<int> ids;
            int count = Count(set);
            ok_object array = count > 0 ? api->array_new("System.Int32", count) : 0;
            if (!array) return ids;
            int64_t args[] = {(int64_t)array, 0};
            api->invoke(set, nullptr, "CopyTo", 2, args, "oi", 'v');
            const int32_t* data = (const int32_t*)api->array_data(array);
            if (data) ids.assign(data, data + count);
            return ids;
        }

        std::vector<ok_object> Values(ok_object dictionary, const char* type) {
            std::vector<ok_object> values;
            int count = Count(dictionary);
            ok_object collection = Call(dictionary, nullptr, "get_Values");
            ok_object array = count > 0 ? api->array_new(type, count) : 0;
            if (!collection || !array) return values;
            int64_t args[] = {(int64_t)array, 0};
            api->invoke(collection, nullptr, "CopyTo", 2, args, "oi", 'v');
            const ok_object* data = (const ok_object*)api->array_data(array);
            if (data) values.assign(data, data + api->array_length(array));
            return values;
        }

        void Deselect(int id) {
            if (ok_object wo = Object(id)) api->invoke(wo, nullptr, "DeSelect", 0, nullptr, "", 'v');
        }

        void DeselectAll(ok_object controller) {
            ok_object set = Selected(controller);
            for (int id : Items(set)) Deselect(id);
            if (set) api->invoke(set, nullptr, "Clear", 0, nullptr, "", 'v');
        }

        void DeselectAllExcept(ok_object controller, int keep) {
            ok_object set = Selected(controller);
            if (!Contains(set, keep)) {
                DeselectAll(controller);
                return;
            }
            for (int id : Items(set)) {
                if (id == keep) continue;
                Deselect(id);
                Remove(set, id);
            }
        }

        bool PickHit(uint8_t* hit) {
            std::memset(hit, 0, HitSize);
            *(int32_t*)(hit + HitWoId) = -1;
            int64_t args[] = {Pointer(hit), 0, PickMask};
            return (api->invoke(0, "EditModeObjectPicker", "Pick", 3, args, "poi", 'i') & 0xFF) != 0;
        }

        int HitId(const uint8_t* hit) { return *(const int32_t*)(hit + HitWoId); }

        void OnSelectHit(ok_call* call, void*) {
            if ((!ctrlSelect && !multiSelect) || GameMode() != EditMode) return;
            ok_object controller = api->arg_object(call, 0);
            const uint8_t* hit = (const uint8_t*)(uintptr_t)api->arg_int(call, 1);
            if (!controller || !hit) return;
            int id = HitId(hit);
            bool ctrl = ctrlSelect && KeyHeld(LeftControl);
            bool add = multiSelect || ctrl;
            api->set_arg_int(call, 2, add ? 1 : 0);
            ok_object set = Selected(controller);
            bool contains = Contains(set, id);
            if (!add && !contains) DeselectAllExcept(controller, id);
            if (contains && ctrl) {
                Deselect(id);
                Remove(set, id);
                api->cancel(call);
            }
        }

        void OnDeselect(ok_call* call, void*) {
            if ((!ctrlSelect && !multiSelect) || GameMode() != EditMode) return;
            if (multiSelect || Count(Selected(api->arg_object(call, 0))) > 1) api->cancel(call);
        }

        void OnLinkLoop(ok_call* call, void*) {
            if (!fastLinks) return;
            bool* loop = (bool*)(uintptr_t)api->arg_int(call, LinkLoopSlot);
            if (loop) *loop = false;
            api->cancel(call);
        }

        void OnLinkValidate(ok_call* call, void*) {
            if (!fastLinks) return;
            int32_t* severity = (int32_t*)(uintptr_t)api->arg_int(call, SeveritySlot);
            if (severity) *severity = 0;
            api->set_result_int(call, ValidLink);
            api->cancel(call);
        }

        void OnObjectLinkTarget(ok_call* call, void*) {
            if (!objectLinks) return;
            api->set_result_int(call, HasFlag(api->arg_object(call, 1), HasCubeModel) ? 1 : 0);
            api->cancel(call);
        }

        void OnPushState(ok_call* call, void*) {
            if (linkFix && api->arg_int(call, 1) == AddLinkEvent) api->cancel(call);
        }

        bool OverUi() {
            ok_object system = api->call_static_obj("UnityEngine.EventSystems.EventSystem", "get_current");
            return Flag(system, nullptr, "IsPointerOverGameObject");
        }

        int OverConnector(ok_object wo) {
            ok_vec3 point{0, 0, 0};
            ok_object boxed = (ok_object)api->invoke(0, "MVInputWrapper", "GetPointerPosition", 0, nullptr, "", 'o');
            if (!boxed || !api->unbox(boxed, &point, sizeof(point))) return 0;
            int64_t argument = Pointer(&point);
            if (Flag(wo, nullptr, "get_HasInputConnector") && (api->invoke(wo, "MVWorldObjectClient", "IsPointOverInputConnector", 1, &argument, "p", 'i') & 0xFF))
                return InputConnector;
            if (Flag(wo, nullptr, "get_HasOutputConnector") && (api->invoke(wo, "MVWorldObjectClient", "IsPointOverOutputConnector", 1, &argument, "p", 'i') & 0xFF))
                return OutputConnector;
            return 0;
        }

        ok_object NewLink(int input, int output) {
            ok_object link = api->new_object("Link", 0, nullptr, "");
            if (!link) return 0;
            api->set_field_num(link, "Link", "inputWOID", input);
            api->set_field_num(link, "Link", "outputWOID", output);
            return link;
        }

        void ShowTempLink(ok_object link) {
            ok_object lines = Call(CameraManager(), "MainCameraManager", "get_LineDrawManager");
            int64_t argument = (int64_t)link;
            if (lines) api->invoke(lines, "LineDrawManager", "SetTempLink", 1, &argument, "o", 'v');
        }

        void ResetTempLink() {
            if (tempClicks == 0) return;
            ShowTempLink(0);
            tempInput = tempOutput = -1;
            tempClicks = 0;
        }

        void LinkClick(ok_object wo, int connector) {
            int id = IdOf(wo);
            if (connector == InputConnector) tempInput = id;
            else tempOutput = id;
            tempClicks++;
            ShowTempLink(NewLink(tempInput, tempOutput));
            if (tempClicks < 2) return;
            if (tempInput != -1 && tempOutput != -1) {
                ok_object requests = api->call_static_obj("MVGameControllerBase", "get_OperationRequests");
                int64_t argument = (int64_t)NewLink(tempInput, tempOutput);
                if (requests && argument) api->invoke(requests, nullptr, "AddLink", 1, &argument, "o", 'i');
            }
            ShowTempLink(0);
            tempInput = tempOutput = -1;
            tempClicks = 0;
        }

        void TickLinks() {
            if (!linkFix || GameMode() != EditMode) return;
            if (Playing()) {
                ResetTempLink();
                return;
            }
            if (!ControlDown(PointerSelect) || OverUi()) return;
            uint8_t hit[HitSize];
            if (PickHit(hit) && HitId(hit) != -1) {
                ok_object wo = Object(HitId(hit));
                int connector = wo ? OverConnector(wo) : 0;
                if (connector) {
                    LinkClick(wo, connector);
                    return;
                }
            }
            ResetTempLink();
        }

        void TickSelection() {
            if (!ctrlSelect || GameMode() != EditMode) return;
            ok_object controller = Selection();
            if (Count(Selected(controller)) <= 1 || !ControlDown(PointerSelect) || OverUi()) return;
            uint8_t hit[HitSize];
            bool picked = PickHit(hit);
            ok_object wo = picked ? Object(HitId(hit)) : 0;
            if (!wo || !HasFlag(wo, Selectable)) DeselectAll(controller);
        }

        std::vector<ok_object> Objects() {
            ok_object manager = Manager();
            ok_object dictionary = manager ? api->field_obj(manager, "MVWorldObjectClientManager", "worldObjects") : 0;
            return dictionary ? Values(dictionary, "MVWorldObjectClient") : std::vector<ok_object>{};
        }

        uint64_t ReadFlags(ok_object wo) { return (uint64_t)api->field_num(wo, "MVWorldObjectClient", "interactionFlags", 0); }

        void WriteFlags(ok_object wo, uint64_t flags) { api->set_field_num(wo, "MVWorldObjectClient", "interactionFlags", (double)flags); }

        void ApplyFlags() {
            for (ok_object wo : Objects()) {
                int id = IdOf(wo);
                if (id < 0) continue;
                auto found = originalFlags.find(id);
                uint64_t base = found != originalFlags.end() ? found->second : ReadFlags(wo);
                if (found == originalFlags.end()) originalFlags[id] = base;
                uint64_t wanted = flagsOverride ? flagsMask : (base | flagsMask);
                if (ReadFlags(wo) != wanted) WriteFlags(wo, wanted);
            }
        }

        void RestoreFlags() {
            for (const auto& entry : originalFlags)
                if (ok_object wo = Object(entry.first)) WriteFlags(wo, entry.second);
            originalFlags.clear();
        }

        void Highlight(int groupId, bool on) {
            ok_object group = Object(groupId);
            ok_object children = group ? api->field_obj(group, "MVGroup", "children") : 0;
            if (!children) return;
            for (ok_object child : Values(children, "MVWorldObjectClient")) {
                ok_object transform = Call(child, "UnityEngine.Component", "get_transform");
                int64_t args[] = {(int64_t)transform, on ? 1 : 0};
                if (transform) api->invoke(0, "SharedCubeFunctions", "SetLayerRecursively", 2, args, "oi", 'v');
            }
        }

        void TickGroup() {
            if (!editedGroup || InGroup()) return;
            Highlight(editedGroup, false);
            editedGroup = 0;
        }
    }

    void Attach(const OkMod* host) {
        api = host;
        if (!api || sizeof(void*) != 8) return;
        api->on("editor.select_hit", OnSelectHit, nullptr);
        api->on("editor.deselect_all", OnDeselect, nullptr);
        api->on("editor.deselect_except", OnDeselect, nullptr);
        api->on("editor.deselect_wo", OnDeselect, nullptr);
        api->on("editor.link_loop", OnLinkLoop, nullptr);
        api->on("editor.link_validate", OnLinkValidate, nullptr);
        api->on("editor.object_link_enabler", OnObjectLinkTarget, nullptr);
        api->on("editor.object_link_transparency", OnObjectLinkTarget, nullptr);
        api->on("editor.push_state", OnPushState, nullptr);
    }

    void Tick(double dt) {
        if (!api) return;
        TickSelection();
        TickLinks();
        TickGroup();
        if (!flagsOn) return;
        flagClock += dt;
        if (flagClock < FlagSeconds) return;
        flagClock = 0;
        ApplyFlags();
    }

    void Reset(const std::string& setting) {
        if (setting == "multiselect") multiSelect = false;
        else if (setting == "ctrlselect") ctrlSelect = false;
        else if (setting == "fastlinks") fastLinks = false;
        else if (setting == "objectlinks") objectLinks = false;
        else if (setting == "linkfix") SetLinkFix(false);
        else if (setting == "flags") SetFlags(false, 0, false);
    }

    bool MultiSelect() { return multiSelect; }

    void SetMultiSelect(bool on) { multiSelect = on; }

    bool CtrlSelect() { return ctrlSelect; }

    void SetCtrlSelect(bool on) { ctrlSelect = on; }

    bool FastLinks() { return fastLinks; }

    void SetFastLinks(bool on) { fastLinks = on; }

    bool ObjectLinks() { return objectLinks; }

    void SetObjectLinks(bool on) { objectLinks = on; }

    bool LinkFix() { return linkFix; }

    void SetLinkFix(bool on) {
        if (!on && api) ResetTempLink();
        linkFix = on;
    }

    bool Flags(uint64_t& mask, bool& override) {
        mask = flagsMask;
        override = flagsOverride;
        return flagsOn;
    }

    void SetFlags(bool on, uint64_t mask, bool override) {
        if (api && flagsOn) RestoreFlags();
        flagsOn = on;
        flagsMask = mask;
        flagsOverride = override;
        flagClock = 0;
        if (api && on) ApplyFlags();
    }

    bool Pick(int& material, bool& unlocked) {
        if (!api || GameMode() != EditMode || Playing()) return false;
        ok_object machine = StateMachine();
        ok_object modeling = machine ? api->field_obj(machine, "EditorStateMachine", "cubeModelingStateMachine") : 0;
        ok_object selected = Call(modeling, "CubeModelingStateMachine", "get_SelectedCube");
        ok_object cube = selected ? api->field_obj(selected, "CubePickingInfo", "cube") : 0;
        int face = selected ? (int)api->field_num(selected, "CubePickingInfo", "pickedFace", -1) : -1;
        ok_object materials = cube ? api->field_obj(cube, "CubeBase", "faceMaterials") : 0;
        if (!materials || face < 0 || face >= api->array_length(materials)) return false;
        material = ((const uint8_t*)api->array_data(materials))[face];
        ok_object repository = api->field_obj(0, "MVMaterialRepository", "instance");
        int64_t argument = material;
        unlocked = repository && (api->invoke(repository, "MVMaterialRepository", "IsMaterialUnlocked", 1, &argument, "i", 'i') & 0xFF) != 0;
        return true;
    }

    bool Focus(int id) {
        if (!api) return false;
        ok_object wo = Object(id);
        ok_object camera = Call(CameraManager(), "MainCameraManager", "get_CurrentCamera");
        if (!wo || !camera) return false;
        ok_vec3 zero{0, 0, 0};
        int64_t args[] = {(int64_t)wo, FloatBits(2.0), Pointer(&zero), Pointer(&zero)};
        api->invoke(camera, nullptr, "FocusOnObject", 4, args, "ofpp", 'v');
        return true;
    }

    bool EnterGroup(int id) {
        if (!api || GameMode() != EditMode) return false;
        ok_object wo = Object(id);
        ok_object controller = Selection();
        if (!wo || !controller || !api->is_a(wo, "MVGroup")) return false;
        int64_t argument = (int64_t)wo;
        api->invoke(controller, "SelectionController", "EnterGroup", 1, &argument, "o", 'v');
        int64_t blue = 1;
        if (ok_object manager = CameraManager()) api->invoke(manager, "MainCameraManager", "set_BlueModeEnabled", 1, &blue, "i", 'v');
        Focus(id);
        if (editedGroup && editedGroup != id) Highlight(editedGroup, false);
        Highlight(id, true);
        editedGroup = id;
        return true;
    }

    bool DeselectAll() {
        if (!api || GameMode() != EditMode) return false;
        ok_object controller = Selection();
        if (!controller) return false;
        DeselectAll(controller);
        return true;
    }

    bool Owns(int id) {
        if (!api) return false;
        ok_object wo = Object(id);
        if (!wo) return false;
        ok_object player = api->call_static_obj("MVGameControllerBase", "get_LocalPlayer");
        if (HasFlag(wo, 4)) return player && api->invoke(player, nullptr, "get_PlanetOwnership", 0, nullptr, "", 'i') == 2;
        ok_object prototype = Call(wo, nullptr, "get_PrototypeCubeModel");
        if (!prototype) return false;
        int author = (int)api->invoke(prototype, nullptr, "get_AuthorProfileID", 0, nullptr, "", 'i');
        int me = player ? (int)api->invoke(player, nullptr, "get_ProfileID", 0, nullptr, "", 'i') : -2;
        return author == -1 || author == me;
    }

    bool InGroup() {
        if (!api) return false;
        ok_object machine = StateMachine();
        return machine && !Flag(machine, "EditorStateMachine", "get_ParentGroupIsRoot");
    }
}
