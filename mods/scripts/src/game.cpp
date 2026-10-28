#include "game.h"
#include "okmod.h"
#include "runtime.h"
#include <cctype>
#include <string>

namespace game {
    namespace {
        const OkMod* api = nullptr;

        ok_object Call(ok_object self, const char* type, const char* method) {
            return self ? (ok_object)api->invoke(self, type, method, 0, nullptr, "", 'o') : 0;
        }

        ok_object Controller(const char* method) {
            for (const char* type : {"MVGameControllerBase", "MVGameController"})
                if (ok_object result = api->call_static_obj(type, method)) return result;
            return 0;
        }

        bool Vector(ok_object boxed, float* value) {
            ok_vec3 vector{0, 0, 0};
            if (!boxed || !api->unbox(boxed, &vector, sizeof(vector))) return false;
            value[0] = vector.x;
            value[1] = vector.y;
            value[2] = vector.z;
            return true;
        }

        bool Position(ok_object object, float* value) {
            if (!object) return false;
            if (Vector(Call(object, nullptr, "get_Position"), value)) return true;
            return Vector(Call(Call(object, "UnityEngine.Component", "get_transform"), "UnityEngine.Transform", "get_position"), value);
        }

        struct Alias {
            const char* name;
            const char* type;
            const char* also;
        };

        const Alias Aliases[] = {
            {"hp", "GameMeterHealth", nullptr},
            {"xp", "GameMeterXP", nullptr},
            {"level", "LevelBadge", nullptr},
            {"coins", "GameMeterGameCoin", nullptr},
            {"points", "GameMeterGamePoints", nullptr},
            {"shield", "GameMeterShield", nullptr},
            {"time", "GameMeterRoundTime", nullptr},
            {"crosshair", "CrossHair", "ammoRoot"},
            {"chat", "ChatControllerUGUI", nullptr},
            {"hotbar", "InventoryController", nullptr},
            {"boosts", "BoostHUDControllerDesktop", nullptr},
            {"edit", "LeavePlayModeButton", nullptr},
            {"play", "EnterPlayModeButton", nullptr},
        };

        constexpr size_t ClassLimit = 128;

        int64_t Pointer(const void* value) { return (int64_t)(uintptr_t)value; }

        bool ClassName(const std::string& name) {
            if (name.empty() || name.size() > ClassLimit) return false;
            for (char c : name)
                if (!std::isalnum((unsigned char)c) && c != '_' && c != '.' && c != '+') return false;
            return true;
        }

        const Alias* AliasOf(const std::string& element) {
            for (const Alias& alias : Aliases)
                if (element == alias.name) return &alias;
            return nullptr;
        }

        std::string ComponentType(const std::string& element) {
            if (const Alias* alias = AliasOf(element)) return alias->type;
            if (element.size() > 1 && element[0] == '@') return element.substr(1);
            return {};
        }

        std::vector<ok_object> Roots() {
            std::vector<ok_object> roots;
            for (const char* getter : {"get_PlayModeUI", "get_EditModeUI"})
                if (ok_object transform = Call(Controller(getter), "UnityEngine.Component", "get_transform")) roots.push_back(transform);
            return roots;
        }

        constexpr int64_t IncludeInactive = 1;
        constexpr int64_t LockedCursor = 1;
        constexpr int64_t FreeLock = 0;

        void SetCursor(int64_t lockState, bool visible) {
            if (api->invoke(0, "UnityEngine.Cursor", "get_lockState", 0, nullptr, "", 'i') != lockState)
                api->invoke(0, "UnityEngine.Cursor", "set_lockState", 1, &lockState, "i", 'v');
            int64_t shown = visible;
            if (((api->invoke(0, "UnityEngine.Cursor", "get_visible", 0, nullptr, "", 'i') & 0xFF) != 0) != visible)
                api->invoke(0, "UnityEngine.Cursor", "set_visible", 1, &shown, "i", 'v');
        }

        bool InMenu(ok_object ui) {
            return ui && ((api->invoke(ui, "ModeControllerBase", "get_IsInLobby", 0, nullptr, "", 'i') & 0xFF) != 0
                || (api->invoke(ui, "ModeControllerBase", "get_IsInPauseMenu", 0, nullptr, "", 'i') & 0xFF) != 0);
        }
        constexpr int64_t ByInstance = 1;

        ok_object SystemType(const std::string& qualified) {
            int64_t name = Pointer(qualified.c_str());
            return (ok_object)api->invoke(0, "System.Type", "GetType", 1, &name, "s", 'o');
        }

        ok_object CanvasType() { return SystemType("UnityEngine.Canvas, UnityEngine.UIModule"); }

        std::vector<ok_object> Interface(const std::string& type, const char* also) {
            std::vector<ok_object> objects;
            ok_object system = SystemType(type + ", Assembly-CSharp");
            ok_object canvas = CanvasType();
            if (!system || !canvas) return objects;
            int64_t search[] = {(int64_t)system, IncludeInactive, ByInstance};
            ok_object found = (ok_object)api->invoke(0, "UnityEngine.Object", "FindObjectsByType", 3, search, "oii", 'o');
            const ok_object* components = (const ok_object*)api->array_data(found);
            int count = api->array_length(found);
            int64_t parent[] = {(int64_t)canvas, IncludeInactive};
            for (int i = 0; components && i < count; i++) {
                if (!api->invoke(components[i], "UnityEngine.Component", "GetComponentInParent", 2, parent, "oi", 'o')) continue;
                if (ok_object object = Call(components[i], "UnityEngine.Component", "get_gameObject")) objects.push_back(object);
                ok_object extra = also ? api->field_obj(components[i], type.c_str(), also) : 0;
                if (extra && api->is_a(extra, "UnityEngine.GameObject")) objects.push_back(extra);
            }
            return objects;
        }

        std::vector<ok_object> Elements(const std::string& element) {
            std::string type = ComponentType(element);
            const Alias* alias = AliasOf(element);
            if (!type.empty()) return ClassName(type) ? Interface(type, alias ? alias->also : nullptr) : std::vector<ok_object>{};
            for (ok_object root : Roots()) {
                ok_object found = root;
                if (!element.empty()) {
                    int64_t path = Pointer(element.c_str());
                    found = (ok_object)api->invoke(root, "UnityEngine.Transform", "Find", 1, &path, "s", 'o');
                }
                if (ok_object object = Call(found, "UnityEngine.Component", "get_gameObject")) return {object};
            }
            return {};
        }

        int Id(ok_object object) { return (int)api->invoke(object, "UnityEngine.Object", "GetInstanceID", 0, nullptr, "", 'i'); }

        bool Active(ok_object object) { return (api->invoke(object, "UnityEngine.GameObject", "get_activeSelf", 0, nullptr, "", 'i') & 0xFF) != 0; }

        void SetActive(ok_object object, bool active) {
            int64_t value = active;
            api->invoke(object, "UnityEngine.GameObject", "SetActive", 1, &value, "i", 'v');
        }

        ok_object TransformOf(ok_object object) { return Call(object, "UnityEngine.GameObject", "get_transform"); }

        bool Scale(ok_object object, float* value) {
            return Vector(Call(TransformOf(object), "UnityEngine.Transform", "get_localScale"), value);
        }

        void SetScale(ok_object object, const float* value) {
            ok_vec3 scale{value[0], value[1], value[2]};
            int64_t arg = Pointer(&scale);
            if (ok_object transform = TransformOf(object)) api->invoke(transform, "UnityEngine.Transform", "set_localScale", 1, &arg, "p", 'v');
        }

        bool Flat(const float* scale) { return scale[0] == 0 && scale[1] == 0 && scale[2] == 0; }

        bool RootCanvas(ok_object object) {
            ok_object canvas = CanvasType();
            ok_object transform = TransformOf(object);
            if (!canvas || !transform) return false;
            int64_t args[] = {(int64_t)canvas, IncludeInactive};
            ok_object nearest = (ok_object)api->invoke(transform, "UnityEngine.Component", "GetComponentInParent", 2, args, "oi", 'o');
            if (!nearest || Id(Call(nearest, "UnityEngine.Component", "get_gameObject")) != Id(object)) return false;
            ok_object parent = Call(transform, "UnityEngine.Transform", "get_parent");
            return !parent || !api->invoke(parent, "UnityEngine.Component", "GetComponentInParent", 2, args, "oi", 'o');
        }

        ok_object LocalPlayer() {
            if (ok_object player = Controller("get_LocalPlayer")) return player;
            return Call(Controller("get_Game"), "MVNetworkGame", "get_LocalPlayer");
        }
    }

    void Attach(const OkMod* host) { api = host; }

    bool PlayerPosition(float* value) {
        if (!api) return false;
        ok_object manager = Controller("get_WOCM");
        ok_object player = LocalPlayer();
        int64_t id = player ? api->invoke(player, "MVPlayer", "get_WoId", 0, nullptr, "", 'i') : 0;
        if (manager && id > 0) {
            ok_object client = (ok_object)api->invoke(manager, "MVWorldObjectClientManager", "GetWorldObjectClient", 1, &id, "i", 'o');
            if (Vector(Call(client, "MVWorldObjectClient", "get_Position"), value)) return true;
        }
        ok_object avatar = Call(manager, "MVWorldObjectClientManager", "get_AvatarLocal");
        if (!avatar) avatar = api->call_static_obj("GameDB", "get_LocalAvatar");
        return Position(avatar, value);
    }

    std::string PlayerName() {
        if (!api) return {};
        ok_object player = LocalPlayer();
        ok_object name = Call(player, "MVPlayer", "get_Username");
        if (!name) {
            ok_object profile = Call(player, "MVPlayer", "get_UserProfileData");
            name = profile ? api->field_obj(profile, nullptr, "UserName") : 0;
            if (!name) name = Call(profile, nullptr, "get_UserName");
        }
        char text[256] = "";
        if (name) api->string_utf8(name, text, sizeof(text));
        return text;
    }

    bool CameraPosition(float* value) {
        if (!api) return false;
        ok_object transform = Call(Controller("get_MainCameraManager"), "MainCameraManager", "get_ProtectedTransform");
        if (Vector(Call(transform, "ProtectedTransform", "get_position"), value)) return true;
        return Position(api->call_static_obj("UnityEngine.Camera", "get_main"), value);
    }

    std::string GraphicsVersion() {
        if (!api) return {};
        ok_object version = api->call_static_obj("UnityEngine.SystemInfo", "get_graphicsDeviceVersion");
        char text[256] = "";
        if (version) api->string_utf8(version, text, sizeof(text));
        return text;
    }

    std::vector<Part> Parts(const std::string& element) {
        std::vector<Part> parts;
        if (!api) return parts;
        for (ok_object object : Elements(element)) {
            float scale[3] = {1, 1, 1};
            Scale(object, scale);
            bool active = (api->invoke(object, "UnityEngine.GameObject", "get_activeInHierarchy", 0, nullptr, "", 'i') & 0xFF) != 0;
            parts.push_back({Id(object), Active(object) && !Flat(scale), active});
        }
        return parts;
    }

    void Hide(const std::string& element, std::map<int, Saved>& saved) {
        if (!api) return;
        static const float Zero[3] = {0, 0, 0};
        for (ok_object object : Elements(element)) {
            int id = Id(object);
            auto known = saved.find(id);
            if (known == saved.end()) {
                Saved state;
                state.canvas = element.empty() || RootCanvas(object);
                state.active = Active(object);
                Scale(object, state.scale);
                known = saved.emplace(id, state).first;
            }
            if (known->second.canvas) {
                if (Active(object)) SetActive(object, false);
                continue;
            }
            float scale[3] = {0, 0, 0};
            if (Scale(object, scale) && !Flat(scale)) SetScale(object, Zero);
        }
    }

    void Restore(const std::string& element, const std::map<int, Saved>& saved) {
        if (!api) return;
        for (ok_object object : Elements(element)) {
            auto known = saved.find(Id(object));
            if (known == saved.end()) continue;
            if (!known->second.canvas) SetScale(object, known->second.scale);
            else if (known->second.active) SetActive(object, true);
        }
    }

    bool CursorLocked() {
        return api && api->invoke(0, "UnityEngine.Cursor", "get_lockState", 0, nullptr, "", 'i') == LockedCursor;
    }

    void FreeCursor() {
        if (api) SetCursor(FreeLock, true);
    }

    void LockCursor() {
        if (!api) return;
        ok_object ui = Controller("get_PlayModeUI");
        ok_object state = api->field_obj(ui, "DesktopPlayModeController", "playModeState");
        bool playing = state && (api->invoke(state, "UnityEngine.GameObject", "get_activeInHierarchy", 0, nullptr, "", 'i') & 0xFF) != 0;
        if (playing && !InMenu(ui)) SetCursor(LockedCursor, false);
    }

    void SuppressInput() {
        if (!api) return;
        api->invoke(0, "MVInputWrapper", "SuppressAllInput", 0, nullptr, "", 'v');
        api->invoke(0, "MVInputWrapper", "SuppressInGameInput", 0, nullptr, "", 'v');
    }

    bool Children(const std::string& element, std::vector<std::string>& names) {
        std::vector<ok_object> objects = api ? Elements(element) : std::vector<ok_object>{};
        ok_object transform = objects.empty() ? 0 : Call(objects.front(), "UnityEngine.GameObject", "get_transform");
        if (!transform) return false;
        int64_t count = api->invoke(transform, "UnityEngine.Transform", "get_childCount", 0, nullptr, "", 'i');
        for (int64_t i = 0; i < count; i++) {
            ok_object child = (ok_object)api->invoke(transform, "UnityEngine.Transform", "GetChild", 1, &i, "i", 'o');
            ok_object name = Call(child, "UnityEngine.Object", "get_name");
            char text[256] = "";
            if (name) api->string_utf8(name, text, sizeof(text));
            names.push_back(text);
        }
        return true;
    }

    bool NativeTexture(bool (*use)(void* native)) {
        if (!api) return false;
        int64_t sizes[2] = {2, 2};
        ok_object texture = api->new_object("UnityEngine.Texture2D", 2, sizes, "ii");
        if (!texture) {
            runtime::Log("unity made no texture to find the game's device");
            return false;
        }
        api->invoke(texture, "UnityEngine.Texture2D", "Apply", 0, nullptr, "", 'v');
        void* native = nullptr;
        ok_object boxed = (ok_object)api->invoke(texture, "UnityEngine.Texture", "GetNativeTexturePtr", 0, nullptr, "", 'o');
        if (boxed) api->unbox(boxed, &native, sizeof(native));
        if (!native) runtime::Log("unity gave no native texture");
        bool used = native && use(native);
        int64_t argument = (int64_t)texture;
        api->invoke(0, "UnityEngine.Object", "Destroy", 1, &argument, "o", 'v');
        return used;
    }
}
