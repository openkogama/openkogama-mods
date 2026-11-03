#include "tools.h"
#include "files.h"
#include "game.h"
#include "okmod.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <optional>

namespace tools {
    namespace {
        constexpr int ThirdPerson = 0;
        constexpr int SelectionLimit = 4096;
        constexpr int64_t HasCubeModel = 2;
        constexpr int KeyCodeLimit = 520;
        constexpr int TextureLimit = 16384;

        const OkMod* api = nullptr;
        std::optional<float> fov;
        std::optional<float> distance;
        std::optional<bool> fog;
        std::optional<float> fogDensity;
        std::optional<float> sensitivityBase;
        bool antiAfk = false;
        bool antiAfkWas = false;
        bool crosshair = false;
        bool fogRemembered = false;
        float rememberedDensity = 0.007f;
        float crosshairColor[4] = {0, 1, 0, 1};
        int previewId = 0;
        bool fastRespawn = false;
        bool fastRespawnSafe = false;
        constexpr int64_t DeadMode = 2;

        int64_t Pointer(const void* value) { return (int64_t)(uintptr_t)value; }

        int64_t FloatBits(double value) {
            float narrow = (float)value;
            int64_t bits = 0;
            std::memcpy(&bits, &narrow, 4);
            return bits;
        }

        double FromBits(int64_t bits) {
            int32_t raw = (int32_t)bits;
            float value;
            std::memcpy(&value, &raw, 4);
            return value;
        }

        ok_object Call(ok_object self, const char* type, const char* method) {
            return self ? (ok_object)api->invoke(self, type, method, 0, nullptr, "", 'o') : 0;
        }

        ok_object Static(const char* type, const char* method) { return api ? api->call_static_obj(type, method) : 0; }

        int64_t Int(ok_object self, const char* type, const char* method) { return api->invoke(self, type, method, 0, nullptr, "", 'i'); }

        bool Flag(ok_object self, const char* type, const char* method) { return (Int(self, type, method) & 0xFF) != 0; }

        float Float(ok_object self, const char* type, const char* method) { return (float)FromBits(Int(self, type, method)); }

        void SetFloat(ok_object self, const char* type, const char* method, float value) {
            int64_t bits = FloatBits(value);
            api->invoke(self, type, method, 1, &bits, "f", 'v');
        }

        void SetFlag(ok_object self, const char* type, const char* method, bool value) {
            int64_t argument = value;
            api->invoke(self, type, method, 1, &argument, "i", 'v');
        }

        std::string Text(ok_object text) {
            char buffer[512] = "";
            if (text) api->string_utf8(text, buffer, sizeof(buffer));
            return buffer;
        }

        int Count(ok_object collection) { return collection ? (int)Int(collection, nullptr, "get_Count") : 0; }

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

        ok_object CameraManager() { return Static("MVGameControllerBase", "get_MainCameraManager"); }

        ok_object MainCamera() { return Call(CameraManager(), "MainCameraManager", "get_MainCamera"); }

        ok_object StateMachine() {
            ok_object ui = Static("MVGameControllerBase", "get_EditModeUI");
            return ui && api->is_a(ui, "DesktopEditModeController") ? Call(ui, "DesktopEditModeController", "get_EditModeStateMachine") : 0;
        }

        ok_object Modeling() {
            ok_object machine = StateMachine();
            return machine ? api->field_obj(machine, "EditorStateMachine", "cubeModelingStateMachine") : 0;
        }

        ok_object ThirdPersonCamera() {
            ok_object camera = Call(CameraManager(), "MainCameraManager", "get_CurrentCamera");
            if (!camera || Int(camera, "MVCameraBase", "get_CameraType") != ThirdPerson) return 0;
            return camera;
        }

        ok_object Repository() { return Static("ThemeRepository", "get_Instance"); }

        ok_object ThemeSkybox() {
            ok_object theme = Call(Repository(), "ThemeRepository", "get_CurrentThemeVisualization");
            return theme ? api->field_obj(theme, "CloudyThemeBase", "skybox") : 0;
        }

        ok_object KeyMapping() {
            ok_object map = api->field_obj(0, "MVInputWrapper", "inputMap");
            return map && api->is_a(map, "DesktopDefaultKeyboardMapping") ? api->field_obj(map, "DesktopDefaultKeyboardMapping", "keyMapping") : 0;
        }

        ok_object Keys(ok_object mapping, int control) {
            int64_t key = control;
            if (!mapping || !(api->invoke(mapping, nullptr, "ContainsKey", 1, &key, "i", 'i') & 0xFF)) return 0;
            return (ok_object)api->invoke(mapping, nullptr, "get_Item", 1, &key, "i", 'o');
        }

        void KeepFov() {
            if (!fov) return;
            if (ok_object manager = CameraManager()) SetFloat(manager, "MainCameraManager", "set_FieldOfView", *fov);
        }

        void KeepDistance() {
            if (!distance) return;
            if (ok_object camera = ThirdPersonCamera()) api->set_field_num(camera, "PlaymodeCamera", "distanceToAvatar", *distance);
        }

        void RestoreFogDensity() {
            if (!fogRemembered) return;
            fogRemembered = false;
            ok_object skybox = Static("MVGameControllerBase", "get_SkyboxManager");
            if (!skybox) return;
            api->set_field_num(skybox, "SkyboxManager", "currentFogDensity", rememberedDensity);
            api->set_field_num(skybox, "SkyboxManager", "targetFogDensity", rememberedDensity);
            SetFloat(0, "UnityEngine.RenderSettings", "set_fogDensity", rememberedDensity);
        }

        void KeepFog() {
            if (!fog) return;
            bool on = *fog;
            SetFlag(0, "UnityEngine.RenderSettings", "set_fog", on);
            ok_object skybox = Static("MVGameControllerBase", "get_SkyboxManager");
            if (ok_object themed = ThemeSkybox()) {
                SetFlag(themed, "ThemeSkybox", "set_FogEnabled", on);
                if (on && fogDensity) SetFloat(themed, "ThemeSkybox", "set_FogDensity", *fogDensity);
            }
            if (on && !fogDensity) {
                RestoreFogDensity();
                return;
            }
            float density = on ? *fogDensity : 0.0f;
            if (skybox) {
                if (!fogRemembered) {
                    fogRemembered = true;
                    rememberedDensity = (float)api->field_num(skybox, "SkyboxManager", "currentFogDensity", 0.007);
                }
                api->set_field_num(skybox, "SkyboxManager", "currentFogDensity", density);
                api->set_field_num(skybox, "SkyboxManager", "targetFogDensity", density);
            }
            SetFloat(0, "UnityEngine.RenderSettings", "set_fogDensity", density);
        }

        void KeepAntiAfk() {
            if (antiAfk) SetFlag(0, "AwayMonitor", "set_IdleKickEnabled", false);
            else if (antiAfkWas) SetFlag(0, "AwayMonitor", "set_IdleKickEnabled", true);
            antiAfkWas = antiAfk;
        }

        ok_object FindPreview() {
            if (!previewId) return 0;
            for (ok_object theme : game::FindAll("Theme, Assembly-CSharp"))
                if ((int)Int(theme, "UnityEngine.Object", "GetInstanceID") == previewId) return theme;
            return 0;
        }

        std::string Stamp() {
            std::time_t now = std::time(nullptr);
            std::tm local{};
            localtime_s(&local, &now);
            char name[64];
            std::strftime(name, sizeof(name), "%Y-%m-%d_%H-%M-%S.png", &local);
            return name;
        }
    }

    namespace {
        void OnDeath(ok_call*, void*) {
            if (fastRespawn) Respawn(fastRespawnSafe);
        }

        void OnSuicide(ok_call* call, void*) {
            if (!fastRespawn) return;
            ok_object avatar = api->arg_object(call, 0);
            if (!avatar) return;
            int64_t dead = DeadMode;
            if (api->invoke(avatar, "MVAvatar", "IsInMode", 1, &dead, "i", 'i') & 0xFF) {
                api->cancel(call);
                return;
            }
            ok_object interactable = api->field_obj(avatar, "MVAvatarLocal", "interactableLocal");
            ok_object source = interactable ? Call(interactable, "AvatarInteractable", "get_LastDamageSource") : 0;
            bool fresh = source && !Flag(source, nullptr, "get_Outdated");
            if (fresh) {
                int64_t args[] = {(int64_t)api->field_obj(source, nullptr, "shooter"), (int64_t)api->field_num(source, nullptr, "damageType", 0)};
                api->invoke(interactable, "AvatarInteractable", "DieFromRespawn", 2, args, "oi", 'v');
            }
            else {
                api->invoke(avatar, "MVAvatarLocal", "Die", 0, nullptr, "", 'v');
                Respawn(fastRespawnSafe);
            }
            api->cancel(call);
        }
    }

    void Attach(const OkMod* host) {
        api = host;
        if (!api || sizeof(void*) != 8) return;
        for (const char* hook : {"pvp.killed", "pvp.fell", "pvp.stuck", "pvp.stuck_long"}) api->on(hook, OnDeath, nullptr);
        api->on("pvp.suicide", OnSuicide, nullptr);
    }

    void SetFastRespawn(bool on, bool safe) {
        fastRespawn = on;
        fastRespawnSafe = safe;
    }

    void Apply() {
        if (!api) return;
        KeepFov();
        KeepDistance();
        KeepFog();
        KeepAntiAfk();
        if (crosshair) game::TintCrosshair(crosshairColor);
    }

    void Clear(const std::string& setting) {
        if (setting == "fov") fov.reset();
        else if (setting == "distance") distance.reset();
        else if (setting == "fog") {
            fog.reset();
            fogDensity.reset();
            if (api) {
                RestoreFogDensity();
                if (ok_object themed = ThemeSkybox()) SetFlag(themed, "ThemeSkybox", "set_FogEnabled", true);
            }
        }
        else if (setting == "antiafk") antiAfk = false;
        else if (setting == "crosshair") crosshair = false;
        else if (setting == "sensitivity") HoldSensitivity(0);
        else if (setting == "preview") Preview("");
        else if (setting == "fastrespawn") SetFastRespawn(false, false);
    }

    std::string Mode() {
        if (!api) return {};
        static const char* const names[] = {"edit", "play", "avatar", "play", "edit"};
        int64_t mode = api->call_static_i("MVGameControllerBase", "get_GameMode");
        return mode >= 0 && mode < 5 ? names[mode] : "";
    }

    bool Playing() {
        ok_object game = Static("MVGameControllerBase", "get_Game");
        return game && Flag(game, "MVNetworkGame", "get_IsPlaying");
    }

    bool Target(int& id) {
        ok_object model = Call(Modeling(), "CubeModelingStateMachine", "get_TargetCubeModel");
        if (!model) return false;
        id = (int)api->field_num(model, nullptr, "id", -1);
        return id >= 0;
    }

    bool Selected(std::vector<int>& ids) {
        ok_object set = Call(StateMachine(), "EditorStateMachine", "get_SelectedIDs");
        if (!set) return false;
        int count = std::min(Count(set), SelectionLimit);
        ids.clear();
        if (count <= 0) return true;
        ok_object array = api->array_new("System.Int32", count);
        if (!array) return false;
        int64_t arguments[] = {(int64_t)array, 0};
        api->invoke(set, nullptr, "CopyTo", 2, arguments, "oi", 'v');
        const int32_t* data = (const int32_t*)api->array_data(array);
        if (!data) return false;
        ids.assign(data, data + count);
        return true;
    }

    bool Material(int& id) {
        ok_object machine = Modeling();
        if (!machine) return false;
        id = (int)(Int(machine, "CubeModelingStateMachine", "get_CurrentMaterialId") & 0xFF);
        return true;
    }

    bool SetMaterial(int id) {
        ok_object machine = Modeling();
        if (!machine) return false;
        int64_t argument = id & 0xFF;
        api->invoke(machine, "CubeModelingStateMachine", "set_CurrentMaterialId", 1, &argument, "i", 'v');
        return true;
    }

    bool Fov(float& value) {
        ok_object manager = CameraManager();
        if (!manager) return false;
        value = Float(manager, "MainCameraManager", "get_FieldOfView");
        return true;
    }

    void HoldFov(float value) {
        fov = value;
        if (api) KeepFov();
    }

    bool Distance(float& value) {
        ok_object camera = ThirdPersonCamera();
        if (!camera) return false;
        value = (float)api->field_num(camera, "PlaymodeCamera", "distanceToAvatar", 0);
        return true;
    }

    void HoldDistance(float value) {
        distance = value;
        if (api) KeepDistance();
    }

    bool Far(float& value) {
        ok_object camera = MainCamera();
        if (!camera) return false;
        value = Float(camera, "UnityEngine.Camera", "get_farClipPlane");
        return true;
    }

    bool SetFar(float value) {
        ok_object camera = MainCamera();
        if (!camera) return false;
        SetFloat(camera, "UnityEngine.Camera", "set_farClipPlane", value);
        return true;
    }

    bool Near(float& value) {
        ok_object camera = MainCamera();
        if (!camera) return false;
        value = Float(camera, "UnityEngine.Camera", "get_nearClipPlane");
        return true;
    }

    bool SetNear(float value) {
        ok_object camera = MainCamera();
        if (!camera) return false;
        SetFloat(camera, "UnityEngine.Camera", "set_nearClipPlane", value);
        return true;
    }

    bool SetOrthographic(bool on, float size) {
        ok_object manager = CameraManager();
        if (!manager) return false;
        bool any = false;
        for (const char* getter : {"get_MainCamera", "get_SecondaryCamera", "get_TertiaryCamera"}) {
            ok_object camera = Call(manager, "MainCameraManager", getter);
            if (!camera) continue;
            SetFlag(camera, "UnityEngine.Camera", "set_orthographic", on);
            SetFloat(camera, "UnityEngine.Camera", "set_orthographicSize", size);
            any = true;
        }
        return any;
    }

    bool SetShadows(float value) {
        if (!api) return false;
        SetFloat(0, "UnityEngine.QualitySettings", "set_shadowDistance", value);
        return true;
    }

    bool Shadows(float& value) {
        if (!api) return false;
        value = (float)FromBits(api->call_static_i("UnityEngine.QualitySettings", "get_shadowDistance"));
        return true;
    }

    void HoldSensitivity(float multiplier) {
        if (!api) return;
        if (multiplier <= 0) {
            if (sensitivityBase) SetFloat(0, "MVInputWrapper", "set_MouseSensitivityModifier", *sensitivityBase);
            sensitivityBase.reset();
            return;
        }
        if (!sensitivityBase) sensitivityBase = (float)FromBits(api->call_static_i("MVInputWrapper", "get_MouseSensitivityModifier"));
        SetFloat(0, "MVInputWrapper", "set_MouseSensitivityModifier", *sensitivityBase * multiplier);
    }

    bool Fog(bool& on) {
        if (!api) return false;
        on = (api->call_static_i("UnityEngine.RenderSettings", "get_fog") & 0xFF) != 0;
        return true;
    }

    void HoldFog(bool on, bool withDensity, float density) {
        fog = on;
        if (withDensity) fogDensity = density;
        else fogDensity.reset();
        if (api) KeepFog();
    }

    bool Water(bool& reflective) {
        ok_object manager = Static("MVGameControllerBase", "get_WaterPlaneManager");
        ok_object water = manager ? api->field_obj(manager, "WaterPlaneManager", "water") : 0;
        if (!water) return false;
        reflective = api->field_num(water, "Water", "waterMode", 0) >= 1;
        return true;
    }

    bool SetWater(bool reflective) {
        ok_object manager = Static("MVGameControllerBase", "get_WaterPlaneManager");
        ok_object water = manager ? api->field_obj(manager, "WaterPlaneManager", "water") : 0;
        if (!water) return false;
        return api->set_field_num(water, "Water", "waterMode", reflective ? 1 : 0) != 0;
    }

    bool SetResolution(int width, int height, int mode) {
        if (!api) return false;
        uint32_t rate[2] = {0, 1};
        int64_t arguments[] = {width, height, mode, Pointer(rate)};
        api->invoke(0, "UnityEngine.Screen", "SetResolution", 4, arguments, "iiip", 'v');
        return true;
    }

    bool Capture(double superSize, std::string& path) {
        if (!api) return false;
        ok_object manager = CameraManager();
        ok_object main = Call(manager, "MainCameraManager", "get_MainCamera");
        ok_object secondary = Call(manager, "MainCameraManager", "get_SecondaryCamera");
        if (!main) return false;
        int width = std::clamp((int)(api->call_static_i("UnityEngine.Screen", "get_width") * superSize), 16, TextureLimit);
        int height = std::clamp((int)(api->call_static_i("UnityEngine.Screen", "get_height") * superSize), 16, TextureLimit);
        int64_t targetSize[] = {width, height, 24};
        int64_t pictureSize[] = {width, height};
        ok_object target = api->new_object("UnityEngine.RenderTexture", 3, targetSize, "iii");
        ok_object picture = api->new_object("UnityEngine.Texture2D", 2, pictureSize, "ii");
        if (!target || !picture) return false;
        int64_t into = (int64_t)target, none = 0;
        for (ok_object camera : {main, secondary})
            if (camera) api->invoke(camera, "UnityEngine.Camera", "set_targetTexture", 1, &into, "o", 'v');
        for (ok_object camera : {main, secondary})
            if (camera) api->invoke(camera, "UnityEngine.Camera", "Render", 0, nullptr, "", 'v');
        api->invoke(0, "UnityEngine.RenderTexture", "set_active", 1, &into, "o", 'v');
        float rect[4] = {0, 0, (float)width, (float)height};
        int64_t read[] = {Pointer(rect), 0, 0};
        api->invoke(picture, "UnityEngine.Texture2D", "ReadPixels", 3, read, "pii", 'v');
        api->invoke(picture, "UnityEngine.Texture2D", "Apply", 0, nullptr, "", 'v');
        for (ok_object camera : {main, secondary})
            if (camera) api->invoke(camera, "UnityEngine.Camera", "set_targetTexture", 1, &none, "o", 'v');
        api->invoke(0, "UnityEngine.RenderTexture", "set_active", 1, &none, "o", 'v');
        int64_t texture = (int64_t)picture;
        ok_object png = (ok_object)api->invoke(0, "UnityEngine.ImageConversion", "EncodeToPNG", 1, &texture, "o", 'o');
        std::string data = png ? std::string((const char*)api->array_data(png), (size_t)api->array_length(png)) : std::string();
        api->invoke(0, "UnityEngine.Object", "Destroy", 1, &into, "o", 'v');
        api->invoke(0, "UnityEngine.Object", "Destroy", 1, &texture, "o", 'v');
        return !data.empty() && files::Write("Screenshots", Stamp(), data, path);
    }

    bool ThemesEnabled(bool& on) {
        ok_object skybox = Static("MVGameControllerBase", "get_SkyboxManager");
        if (!skybox) return false;
        on = !Flag(skybox, "UnityEngine.Behaviour", "get_enabled");
        return true;
    }

    bool SetThemes(bool on) {
        if (!api) return false;
        if (!on) Preview("");
        for (ok_object theme : game::FindAll("Theme, Assembly-CSharp"))
            api->invoke(theme, nullptr, on ? "Activate" : "Deactivate", 0, nullptr, "", 'v');
        return true;
    }

    bool Preview(const std::string& name) {
        if (!api) return false;
        ok_object repository = Repository();
        if (!repository) return false;
        if (ok_object old = FindPreview()) {
            int64_t argument = (int64_t)old;
            api->invoke(repository, "ThemeRepository", "DestroyTemporary", 1, &argument, "o", 'v');
        }
        previewId = 0;
        if (name.empty()) return true;
        int64_t identifier = Pointer(name.c_str());
        ok_object preview = (ok_object)api->invoke(repository, "ThemeRepository", "CreateTemporaryThemeVisualization", 1, &identifier, "s", 'o');
        if (!preview) return false;
        previewId = (int)Int(preview, "UnityEngine.Object", "GetInstanceID");
        return true;
    }

    std::string CurrentTheme() {
        if (!api) return {};
        ok_object theme = Call(Repository(), "ThemeRepository", "get_CurrentThemeVisualization");
        return theme ? Text(Call(theme, nullptr, "get_Identifier")) : std::string();
    }

    bool Respawn(bool safe) {
        ok_object events = Static("MVGameControllerBase", "get_GameEventManager");
        ok_object commands = events ? api->field_obj(events, "GameEventManager", "AvatarCommandsPlayMode") : 0;
        if (!commands) return false;
        api->invoke(commands, nullptr, "SetToSpawnPoint", 0, nullptr, "", 'v');
        if (safe) {
            ok_object mediator = Static("MVGameControllerBase", "get_SpawnRoleDataMediatorLocal");
            ok_object internal = mediator ? api->field_obj(mediator, nullptr, "reviveState") : 0;
            ok_object variable = internal ? api->field_obj(internal, nullptr, "subscribableVariable") : 0;
            ok_object state = variable ? api->field_obj(variable, nullptr, "value") : 0;
            if (state && Flag(state, "ReviveState", "get_CanSafelySpawn")) {
                int count = Count(api->field_obj(state, "ReviveState", "safePositions"));
                int64_t index = count - 1;
                if (count > 0) api->invoke(commands, nullptr, "MoveBodyToSafeSpot", 1, &index, "i", 'v');
            }
        }
        api->invoke(commands, nullptr, "EnterPlayingState", 0, nullptr, "", 'v');
        return true;
    }

    void HoldAntiAfk(bool on) {
        antiAfk = on;
        if (api) KeepAntiAfk();
    }

    bool Ping(int& milliseconds) {
        ok_object peer = Call(Static("MVGameControllerBase", "get_Game"), "MVNetworkGame", "get_Peer");
        if (!peer) return false;
        milliseconds = (int)Int(peer, nullptr, "get_RoundTripTime");
        return true;
    }

    bool Info(Counts& counts) {
        if (!api) return false;
        ok_object manager = Static("MVGameControllerBase", "get_WOCM");
        ok_object objects = manager ? api->field_obj(manager, "MVWorldObjectClientManager", "worldObjects") : 0;
        if (!objects) return false;
        counts = Counts{};
        counts.objects = Count(objects);
        int64_t flag = HasCubeModel;
        for (ok_object wo : Values(objects, "MVWorldObjectClient")) {
            if (Flag(wo, nullptr, "get_HasInputConnector") || Flag(wo, nullptr, "get_HasObjectConnector") || Flag(wo, nullptr, "get_HasOutputConnector"))
                counts.logic++;
            if (api->invoke(wo, "MVWorldObjectClient", "HasInteractionFlag", 1, &flag, "i", 'i') & 0xFF) counts.models++;
        }
        ok_object game = Static("MVGameControllerBase", "get_Game");
        ok_object network = game ? api->field_obj(game, nullptr, "worldNetwork") : 0;
        if (network) {
            ok_object links = api->field_obj(network, nullptr, "links");
            ok_object objectLinks = api->field_obj(network, nullptr, "objectLinks");
            ok_object inventory = api->field_obj(network, nullptr, "worldInventory");
            counts.links = Count(links ? api->field_obj(links, nullptr, "links") : 0);
            counts.objectLinks = Count(objectLinks ? api->field_obj(objectLinks, nullptr, "objectLinks") : 0);
            counts.unique = Count(inventory ? api->field_obj(inventory, nullptr, "runtimePrototypes") : 0);
        }
        return true;
    }

    bool Notify(const std::string& text) {
        if (!api) return false;
        int64_t argument = Pointer(text.c_str());
        api->invoke(0, "TextCommand", "NotifyUser", 1, &argument, "s", 'v');
        return true;
    }

    void HoldCrosshair(bool on, const float* rgba) {
        crosshair = on;
        if (rgba) std::memcpy(crosshairColor, rgba, sizeof(crosshairColor));
        if (on && api) game::TintCrosshair(crosshairColor);
    }

    bool CrosshairTexture(const std::string& name) {
        if (!api) return false;
        std::string data;
        if (!files::Read("Crosshairs", name, data) || data.empty()) return false;
        ok_object bytes = api->array_new("System.Byte", (int)data.size());
        if (!bytes) return false;
        std::memcpy(api->array_data(bytes), data.data(), data.size());
        int64_t size[] = {2, 2};
        ok_object texture = api->new_object("UnityEngine.Texture2D", 2, size, "ii");
        int64_t load[] = {(int64_t)texture, (int64_t)bytes};
        if (!texture || !(api->invoke(0, "UnityEngine.ImageConversion", "LoadImage", 2, load, "oo", 'i') & 0xFF)) return false;
        ok_object image = game::CrosshairImage();
        if (!image) return false;
        ok_object sprite = Call(image, "UnityEngine.UI.Image", "get_sprite");
        float pivot[2] = {0.5f, 0.5f};
        if (ok_object boxed = sprite ? Call(sprite, "UnityEngine.Sprite", "get_pivot") : 0) api->unbox(boxed, pivot, sizeof(pivot));
        float rect[4] = {0, 0, (float)Int(texture, "UnityEngine.Texture", "get_width"), (float)Int(texture, "UnityEngine.Texture", "get_height")};
        int64_t create[] = {(int64_t)texture, Pointer(rect), Pointer(pivot)};
        ok_object created = (ok_object)api->invoke(0, "UnityEngine.Sprite", "Create", 3, create, "opp", 'o');
        if (!created) return false;
        int64_t argument = (int64_t)created;
        api->invoke(image, "UnityEngine.UI.Image", "set_sprite", 1, &argument, "o", 'v');
        return true;
    }

    int Binding(int control) {
        if (!api) return -1;
        ok_object keys = Keys(KeyMapping(), control);
        if (!keys || api->array_length(keys) < 1) return -1;
        return ((const int32_t*)api->array_data(keys))[0];
    }

    bool Rebind(int control, int key) {
        if (!api) return false;
        ok_object mapping = KeyMapping();
        ok_object keys = mapping ? api->array_new("UnityEngine.KeyCode", 1) : 0;
        if (!keys) return false;
        ((int32_t*)api->array_data(keys))[0] = key;
        int64_t arguments[] = {control, (int64_t)keys};
        api->invoke(mapping, nullptr, "set_Item", 2, arguments, "io", 'v');
        return true;
    }

    bool ResetBinding(int control) {
        if (!api) return false;
        ok_object mapping = KeyMapping();
        ok_object fresh = mapping ? api->new_object("DesktopPlayMode", 0, nullptr, "") : 0;
        ok_object defaults = fresh ? api->field_obj(fresh, "DesktopDefaultKeyboardMapping", "keyMapping") : 0;
        ok_object keys = Keys(defaults, control);
        if (!keys) return false;
        int64_t arguments[] = {control, (int64_t)keys};
        api->invoke(mapping, nullptr, "set_Item", 2, arguments, "io", 'v');
        return true;
    }

    std::string KeyName(int key) {
        char name[64] = "";
        if (api) api->enum_name("UnityEngine.KeyCode", key, name, sizeof(name));
        return name;
    }

    const std::vector<std::pair<int, std::string>>& KeyCodes() {
        static std::vector<std::pair<int, std::string>> codes;
        if (!codes.empty() || !api) return codes;
        for (int key = 0; key <= KeyCodeLimit; key++) {
            std::string name = KeyName(key);
            if (!name.empty() && !std::isdigit((unsigned char)name[0]) && name[0] != '-') codes.emplace_back(key, name);
        }
        return codes;
    }
}
