#include "game.h"
#include "okmod.h"

namespace game {
    namespace {
        const OkMod* api = nullptr;

        ok_object Call(ok_object self, const char* type, const char* method) {
            return self ? (ok_object)api->invoke(self, type, method, 0, nullptr, "", 'o') : 0;
        }

        bool Vector(ok_object boxed, float* value) {
            ok_vec3 vector{0, 0, 0};
            if (!boxed || !api->unbox(boxed, &vector, sizeof(vector))) return false;
            value[0] = vector.x;
            value[1] = vector.y;
            value[2] = vector.z;
            return true;
        }

        ok_object LocalPlayer() { return api->call_static_obj("MVGameControllerBase", "get_LocalPlayer"); }
    }

    void Attach(const OkMod* host) { api = host; }

    bool PlayerPosition(float* value) {
        if (!api) return false;
        ok_object player = LocalPlayer();
        ok_object manager = api->call_static_obj("MVGameControllerBase", "get_WOCM");
        if (!player || !manager) return false;
        int64_t id = api->invoke(player, "MVPlayer", "get_WoId", 0, nullptr, "", 'i');
        ok_object avatar = (ok_object)api->invoke(manager, "MVWorldObjectClientManager", "GetWorldObjectClient", 1, &id, "i", 'o');
        return Vector(Call(avatar, "MVWorldObjectClient", "get_Position"), value);
    }

    std::string PlayerName() {
        if (!api) return {};
        ok_object profile = Call(LocalPlayer(), "MVPlayer", "get_UserProfileData");
        ok_object name = profile ? api->field_obj(profile, nullptr, "UserName") : 0;
        char text[256] = "";
        if (name) api->string_utf8(name, text, sizeof(text));
        return text;
    }

    bool CameraPosition(float* value) {
        if (!api) return false;
        ok_object manager = api->call_static_obj("MVGameControllerBase", "get_MainCameraManager");
        ok_object transform = Call(manager, "MainCameraManager", "get_ProtectedTransform");
        return Vector(Call(transform, "ProtectedTransform", "get_position"), value);
    }
}
