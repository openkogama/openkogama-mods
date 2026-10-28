#include "game.h"
#include "okmod.h"
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

        bool Position(ok_object component, float* value) {
            return Vector(Call(Call(component, "UnityEngine.Component", "get_transform"), "UnityEngine.Transform", "get_position"), value);
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

    bool NativeTexture(bool (*use)(void* native)) {
        if (!api) return false;
        int64_t sizes[2] = {2, 2};
        ok_object texture = api->new_object("UnityEngine.Texture2D", 2, sizes, "ii");
        if (!texture) return false;
        api->invoke(texture, "UnityEngine.Texture2D", "Apply", 0, nullptr, "", 'v');
        void* native = nullptr;
        ok_object boxed = (ok_object)api->invoke(texture, "UnityEngine.Texture", "GetNativeTexturePtr", 0, nullptr, "", 'o');
        if (boxed) api->unbox(boxed, &native, sizeof(native));
        bool used = native && use(native);
        int64_t argument = (int64_t)texture;
        api->invoke(0, "UnityEngine.Object", "Destroy", 1, &argument, "o", 'v');
        return used;
    }
}
