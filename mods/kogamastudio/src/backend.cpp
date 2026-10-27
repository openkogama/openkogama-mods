#include "stdafx.h"
#include "bridge.h"
#include "overlay.h"
#include "world.h"
#include "cubes.h"
#include "okmod.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

namespace {
    constexpr int KeyF2 = 283;
    constexpr int KeyA = 97;
    constexpr int KeyD = 100;
    constexpr int KeyE = 101;
    constexpr int KeyQ = 113;
    constexpr int KeyS = 115;
    constexpr int KeyW = 119;
    constexpr int KeyLeftShift = 304;
    constexpr int RightMouse = 1;
    constexpr double ReportSeconds = 0.5;
    constexpr double Degrees = 57.29577951308232;
    constexpr double MaxPitch = 89.0;
    constexpr double SprintFactor = 3.0;

    constexpr int ScaleArgument = 6;
    constexpr int DeltaArgument = 1;
    constexpr int GridSizeArgument = 6;
    constexpr int RotationSpeedArgument = 6;
    constexpr int CameraTypeArgument = 1;
    constexpr int BlueModeArgument = 1;
    constexpr int FloatValueArgument = 6;
    constexpr int FloatMinArgument = 7;
    constexpr int IntValueArgument = 2;
    constexpr int IntMinArgument = 3;
    constexpr int MaxArgument = 8;

    const char* const GameModes[] = {"Edit", "Play", "CharacterEditor", "SpacePlay", "SpaceEdit", "None"};

    const OkMod* api = nullptr;
    bool announced = false;
    bool antiAfk = false;
    bool blueMode = false;
    int64_t forcedCamera = -1;
    double fov = -1.0;
    double zoom = 1.0;
    double baseFov = -1.0;
    double distance = -1.0;
    int frames = 0;
    std::chrono::steady_clock::time_point reportedAt = std::chrono::steady_clock::now();

    struct Options {
        bool editSpeed = false;
        double editSpeedFactor = 2.0;
        bool modelScale = false;
        double modelScaleValue = 4.0;
        bool customGrid = false;
        double gridSize = 1.0;
        bool rotationStep = false;
        double rotationStepSize = 15.0;
        bool singleSidePainting = false;
        bool unlimitedConfig = false;
        double configMin = 0.0;
        double configMax = 1.0;
    } options;

    struct Freecam {
        bool enabled = false;
        bool ready = false;
        bool rightClick = true;
        double speed = 20.0;
        double sensitivity = 2.0;
        double yaw = 0.0;
        double pitch = 0.0;
        ok_vec3 position{0, 0, 0};
    } freecam;

    struct Vec3 { float x, y, z; };

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

    int64_t Pointer(const void* value) { return (int64_t)(uintptr_t)value; }

    bool On(const std::string& value) { return value == "true" || value == "True" || value == "1"; }

    int64_t EnumValue(const char* type, const char* name) { return (int64_t)api->field_num(0, type, name, 0); }

    ok_object Get(ok_object self, const char* type, const char* property) {
        return self ? (ok_object)api->invoke(self, type, property, 0, nullptr, "", 'o') : 0;
    }

    BOOL CALLBACK Pick(HWND hwnd, LPARAM found) {
        DWORD owner = 0;
        wchar_t name[32];
        GetWindowThreadProcessId(hwnd, &owner);
        if (owner != GetCurrentProcessId() || !IsWindowVisible(hwnd)) return TRUE;
        if (!GetClassNameW(hwnd, name, 32) || wcscmp(name, L"UnityWndClass") != 0) return TRUE;
        *(HWND*)found = hwnd;
        return FALSE;
    }

    void StartOverlay() {
        if (overlay::Running()) return;
        HWND game = nullptr;
        EnumWindows(Pick, (LPARAM)&game);
        if (game) overlay::Start(game);
    }

    ok_object CameraManager() { return api->call_static_obj("MVGameControllerBase", "get_MainCameraManager"); }

    bool Key(int code) {
        int64_t arg = code;
        return api->invoke(0, "UnityEngine.Input", "GetKey", 1, &arg, "i", 'i') & 0xFF;
    }

    double Axis(const char* name) {
        int64_t arg = Pointer(name);
        return FromBits(api->invoke(0, "UnityEngine.Input", "GetAxis", 1, &arg, "s", 'i'));
    }

    ok_vec3 Rotate(const ok_quat& q, const ok_vec3& v) {
        ok_vec3 t{2 * (q.y * v.z - q.z * v.y), 2 * (q.z * v.x - q.x * v.z), 2 * (q.x * v.y - q.y * v.x)};
        return {v.x + q.w * t.x + (q.y * t.z - q.z * t.y), v.y + q.w * t.y + (q.z * t.x - q.x * t.z), v.z + q.w * t.z + (q.x * t.y - q.y * t.x)};
    }

    void ApplyCamera() {
        ok_object manager = CameraManager();
        if (!manager) return;
        if (fov > 0 || zoom != 1.0) {
            if (baseFov <= 0) baseFov = FromBits(api->invoke(manager, "MainCameraManager", "get_FieldOfView", 0, nullptr, "", 'i'));
            double wanted = (fov > 0 ? fov : baseFov) / (zoom > 0.01 ? zoom : 1.0);
            int64_t arg = FloatBits(wanted);
            api->invoke(manager, "MainCameraManager", "set_FieldOfView", 1, &arg, "f", 'v');
        }
        if (distance > 0) {
            ok_object camera = Get(manager, "MainCameraManager", "get_CurrentCamera");
            if (camera) api->set_field_num(camera, "PlaymodeCamera", "distanceToAvatar", distance);
        }
    }

    void ResetFov() {
        ok_object manager = CameraManager();
        if (manager && baseFov > 0 && fov <= 0 && zoom == 1.0) {
            int64_t arg = FloatBits(baseFov);
            api->invoke(manager, "MainCameraManager", "set_FieldOfView", 1, &arg, "f", 'v');
            baseFov = -1.0;
        }
    }

    void SetCameraMode(int64_t mode) {
        ok_object manager = CameraManager();
        ok_object controller = manager ? api->field_obj(manager, "MainCameraManager", "cameraController") : 0;
        if (!controller) return;
        forcedCamera = mode;
        api->invoke(controller, "MVCameraController", "SetCamera", 1, &mode, "i", 'v');
    }

    void Handle(const std::string& line) {
        size_t bar = line.find('|');
        std::string command = line.substr(0, bar);
        std::string value = bar == std::string::npos ? std::string() : line.substr(bar + 1);
        double number = std::atof(value.c_str());

        if (command == "option_no_build_limit") api->flag_set("no_build_limit", On(value));
        else if (command == "option_single_side_painting") options.singleSidePainting = On(value);
        else if (command == "option_destructibles_unlock") api->flag_set("destructibles_unlock", On(value));
        else if (command == "option_anti_afk") {
            antiAfk = On(value);
            int64_t kick = !antiAfk;
            api->invoke(0, "AwayMonitor", "set_IdleKickEnabled", 1, &kick, "i", 'v');
        }
        else if (command == "option_blue_mode") blueMode = On(value);
        else if (command == "option_custom_grid_size_enabled") options.customGrid = On(value);
        else if (command == "option_custom_grid_size") options.gridSize = number;
        else if (command == "option_custom_rot_step_enabled") options.rotationStep = On(value);
        else if (command == "option_custom_rot_step_size") options.rotationStepSize = number;
        else if (command == "option_custom_speed_enabled") options.editSpeed = On(value);
        else if (command == "option_custom_speed_value") options.editSpeedFactor = number;
        else if (command == "option_unlimited_config_enabled") options.unlimitedConfig = On(value);
        else if (command == "option_unlimited_config_min") options.configMin = number;
        else if (command == "option_unlimited_config_max") options.configMax = number;
        else if (command == "option_custom_model_scale_enabled") options.modelScale = On(value);
        else if (command == "option_custom_model_scale_value") options.modelScaleValue = number;
        else if (command == "option_unlimited_muzzle_position") api->flag_set("unlimited_muzzle_position", On(value));
        else if (command == "camera_fov") fov = number;
        else if (command == "camera_fov_reset") { fov = -1.0; ResetFov(); }
        else if (command == "camera_zoom") zoom = number;
        else if (command == "camera_zoom_reset") { zoom = 1.0; ResetFov(); }
        else if (command == "camera_distance") distance = number;
        else if (command == "camera_distance_reset") distance = -1.0;
        else if (command == "camera_freecam_enable") { freecam.enabled = true; freecam.ready = false; }
        else if (command == "camera_freecam_disable") { freecam.enabled = false; freecam.ready = false; }
        else if (command == "camera_freecam_speed") freecam.speed = number;
        else if (command == "camera_freecam_sensitivity") freecam.sensitivity = number;
        else if (command == "camera_freecam_require_rmb") freecam.rightClick = On(value);
        else if (command == "camera_set_mode") SetCameraMode(std::atoi(value.c_str()));
        else if (command == "camera_mode_reset") forcedCamera = -1;
        else if (!world::Handle(command, value) && !cubes::Handle(command, value)) DebugLog("[backend] not handled yet: %s\n", line.c_str());
    }

    void Report() {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - reportedAt).count();
        if (elapsed < ReportSeconds) return;
        double fps = frames / elapsed;
        frames = 0;
        reportedAt = now;
        int64_t mode = api->call_static_i("MVGameControllerBase", "get_GameMode");
        const char* name = mode >= 0 && mode < (int64_t)(sizeof(GameModes) / sizeof(GameModes[0])) ? GameModes[mode] : "None";
        char line[256];
        std::snprintf(line, sizeof(line), "gameinfo_update|fps=%.1f,gamemode=%s", fps, name);
        bridge::ToMenu(line + world::Report());
    }

    void OnFrame(ok_call*, void*) {
        frames++;
        StartOverlay();
        if (!announced && overlay::Running()) {
            announced = true;
            bridge::ToMenu("cursor|true");
            bridge::ToMenu("game_initialized");
        }
        int64_t key = KeyF2;
        if (api->invoke(0, "UnityEngine.Input", "GetKeyDown", 1, &key, "i", 'i') & 0xFF) bridge::ToMenu("key_down|F2");

        std::string command;
        while (bridge::NextForGame(command)) Handle(command);
        if (antiAfk) {
            int64_t off = 0;
            api->invoke(0, "AwayMonitor", "set_IdleKickEnabled", 1, &off, "i", 'v');
        }
        ApplyCamera();
        world::Tick();
        cubes::Tick();
        Report();
    }

    void OnLateFrame(ok_call*, void*) {
        if (!freecam.enabled) return;
        ok_object transform = Get(CameraManager(), "MainCameraManager", "get_ProtectedTransform");
        if (!transform) return;
        if (!freecam.ready) {
            ok_quat rotation{0, 0, 0, 1};
            api->unbox(Get(transform, "ProtectedTransform", "get_position"), &freecam.position, sizeof(freecam.position));
            api->unbox(Get(transform, "ProtectedTransform", "get_rotation"), &rotation, sizeof(rotation));
            ok_vec3 forward = Rotate(rotation, {0, 0, 1});
            freecam.yaw = std::atan2(forward.x, forward.z) * Degrees;
            freecam.pitch = -std::asin(std::clamp((double)forward.y, -1.0, 1.0)) * Degrees;
            freecam.ready = true;
            return;
        }

        int64_t button = RightMouse;
        if (!freecam.rightClick || (api->invoke(0, "UnityEngine.Input", "GetMouseButton", 1, &button, "i", 'i') & 0xFF)) {
            freecam.yaw += Axis("Mouse X") * freecam.sensitivity;
            freecam.pitch = std::clamp(freecam.pitch - Axis("Mouse Y") * freecam.sensitivity, -MaxPitch, MaxPitch);
        }
        double yaw = freecam.yaw / Degrees, pitch = freecam.pitch / Degrees;
        ok_vec3 forward{(float)(std::sin(yaw) * std::cos(pitch)), (float)-std::sin(pitch), (float)(std::cos(yaw) * std::cos(pitch))};
        ok_vec3 right{(float)std::cos(yaw), 0, (float)-std::sin(yaw)};
        double dx = 0, dy = 0, dz = 0;
        if (Key(KeyW)) { dx += forward.x; dy += forward.y; dz += forward.z; }
        if (Key(KeyS)) { dx -= forward.x; dy -= forward.y; dz -= forward.z; }
        if (Key(KeyD)) { dx += right.x; dz += right.z; }
        if (Key(KeyA)) { dx -= right.x; dz -= right.z; }
        if (Key(KeyE)) dy += 1;
        if (Key(KeyQ)) dy -= 1;
        double length = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (length > 0) {
            double delta = FromBits(api->invoke(0, "UnityEngine.Time", "get_deltaTime", 0, nullptr, "", 'i'));
            double step = freecam.speed * delta * (Key(KeyLeftShift) ? SprintFactor : 1.0) / length;
            freecam.position.x += (float)(dx * step);
            freecam.position.y += (float)(dy * step);
            freecam.position.z += (float)(dz * step);
        }
        double sx = std::sin(pitch / 2), cx = std::cos(pitch / 2), sy = std::sin(yaw / 2), cy = std::cos(yaw / 2);
        ok_quat rotation{(float)(cy * sx), (float)(sy * cx), (float)(-sy * sx), (float)(cy * cx)};
        int64_t position = Pointer(&freecam.position), turn = Pointer(&rotation);
        api->invoke(transform, "ProtectedTransform", "set_position", 1, &position, "p", 'v');
        api->invoke(transform, "ProtectedTransform", "set_rotation", 1, &turn, "p", 'v');
    }

    void OnQuit(ok_call*, void*) { overlay::Release(); }

    void OnSelect(ok_call* call, void*) { world::Selected(api->arg_object(call, 0), (int)api->arg_int(call, 1)); }

    void OnWorldChanged(ok_call*, void*) { world::Changed(); }

    void OnTranslateStart(ok_call* call, void*) { world::TranslateStart(api->arg_object(call, 1)); }

    void OnTranslateEnd(ok_call*, void*) { world::TranslateEnd(); }

    void OnRotationChange(ok_call* call, void*) { world::RotationStart(api->arg_object(call, 0)); }

    void OnLinkAdded(ok_call* call, void*) { world::LinkAdded(api->arg_object(call, 1)); }

    void OnLinkRemoved(ok_call* call, void*) { world::LinkRemoved(api->arg_object(call, 2), api->arg_object(call, 3)); }

    void OnObjectLinkAdded(ok_call* call, void*) { world::ObjectLinkAdded(api->arg_object(call, 1)); }

    void OnObjectLinkRemoved(ok_call* call, void*) { world::ObjectLinkRemoved(api->arg_object(call, 2), api->arg_object(call, 3)); }

    void OnMoveCharacter(ok_call* call, void*) {
        if (!options.editSpeed) return;
        Vec3* delta = (Vec3*)(uintptr_t)api->arg_int(call, DeltaArgument);
        if (!delta) return;
        float factor = (float)options.editSpeedFactor;
        delta->x *= factor;
        delta->y *= factor;
        delta->z *= factor;
    }

    void OnAddPrototype(ok_call* call, void*) {
        if (options.modelScale) api->set_arg_int(call, ScaleArgument, FloatBits(options.modelScaleValue));
    }

    void OnTranslate(ok_call* call, void*) {
        if (options.customGrid) api->set_field_num(api->arg_object(call, 0), "ESTranslate", "gridSize", options.gridSize);
    }

    void OnGridPoint(ok_call* call, void*) {
        if (options.customGrid && options.gridSize != 0) api->set_arg_int(call, GridSizeArgument, FloatBits(options.gridSize));
    }

    void OnRotateStep(ok_call* call, void*) {
        if (!options.rotationStep) return;
        double speed = FromBits(api->arg_int(call, RotationSpeedArgument));
        double step = options.rotationStepSize;
        api->set_arg_int(call, RotationSpeedArgument, FloatBits(speed > 0 ? step : speed < 0 ? -step : 0.0));
    }

    void OnSetCamera(ok_call* call, void*) {
        if (forcedCamera >= 0) api->set_arg_int(call, CameraTypeArgument, forcedCamera);
    }

    void OnBlueMode(ok_call* call, void*) {
        if (!blueMode) api->set_arg_int(call, BlueModeArgument, 0);
    }

    void OnSliderFloat(ok_call* call, void*) {
        if (!options.unlimitedConfig) return;
        double low = options.configMin, high = options.configMax;
        double value = FromBits(api->arg_int(call, FloatValueArgument));
        api->set_arg_int(call, FloatMinArgument, FloatBits(low));
        api->set_arg_int(call, MaxArgument, FloatBits(high));
        api->set_arg_int(call, FloatValueArgument, FloatBits(std::clamp(value, low, (std::max)(low, high))));
    }

    void OnSliderInt(ok_call* call, void*) {
        if (!options.unlimitedConfig) return;
        double low = options.configMin, high = options.configMax;
        double value = (int32_t)api->arg_int(call, IntValueArgument);
        api->set_arg_int(call, IntMinArgument, (int32_t)low);
        api->set_arg_int(call, MaxArgument, (int32_t)high);
        api->set_arg_int(call, IntValueArgument, (int32_t)std::clamp(value, low, (std::max)(low, high)));
    }

    void OnSliderChanged(ok_call* call, void*) {
        if (!options.unlimitedConfig) return;
        ok_object slider = api->field_obj(api->arg_object(call, 0), "SettingsSlider", "slider");
        if (!slider) return;
        double low = options.configMin, high = options.configMax;
        int64_t lowBits = FloatBits(low), highBits = FloatBits(high);
        api->invoke(slider, "UnityEngine.UI.Slider", "set_minValue", 1, &lowBits, "f", 'v');
        api->invoke(slider, "UnityEngine.UI.Slider", "set_maxValue", 1, &highBits, "f", 'v');
        double value = FromBits(api->invoke(slider, "UnityEngine.UI.Slider", "get_value", 0, nullptr, "", 'i'));
        double clamped = std::clamp(value, low, (std::max)(low, high));
        if (clamped == value) return;
        int64_t clampedBits = FloatBits(clamped);
        api->invoke(slider, "UnityEngine.UI.Slider", "set_value", 1, &clampedBits, "f", 'v');
    }

    void OnPaintCubes(ok_call* call, void*) {
        if (!options.singleSidePainting) return;
        ok_object tool = api->arg_object(call, 0), machine = api->arg_object(call, 1);
        if (!tool || !machine) return;
        api->cancel(call);

        int64_t control = EnumValue("KogamaControls", "PointerSelect");
        bool pressed = api->invoke(0, "MVInputWrapper", "GetBooleanControl", 1, &control, "i", 'i') & 0xFF;
        if (api->field_num(tool, "CubeModelTool", "waitForMouseUp", 0) != 0) {
            api->set_field_num(tool, "CubeModelTool", "waitForMouseUp", pressed);
            return;
        }

        ok_object selected = Get(machine, "CubeModelingStateMachine", "get_SelectedCube");
        ok_object model = Get(machine, "CubeModelingStateMachine", "get_TargetCubeModel");
        bool painting = false;
        void* position = selected ? api->field_ptr(selected, "CubePickingInfo", "iLocalPos") : nullptr;
        if (pressed && position && model) {
            int64_t face = (int64_t)api->field_num(selected, "CubePickingInfo", "pickedFace", 0);
            int64_t material = api->invoke(machine, "CubeModelingStateMachine", "get_CurrentMaterialId", 0, nullptr, "", 'i') & 0xFF;
            int64_t audio[] = {Pointer(position), EnumValue("AudioActions", "CubeAdded")};
            api->invoke(machine, "CubeModelingStateMachine", "HandleAudio", 2, audio, "pi", 'v');
            int64_t paint[] = {Pointer(position), face, material};
            api->invoke(model, "MVCubeModelBase", "SetMaterial", 3, paint, "pii", 'v');
            int64_t change[] = {api->invoke(model, "MVCubeModelBase", "get_CubeCount", 0, nullptr, "", 'i'), EnumValue("EditCubeChange", "CubePainted")};
            api->invoke(0, "CubeModelTool", "SendCubeEvent", 2, change, "ii", 'v');
            painting = true;
        }
        ok_object cursor = api->field_obj(tool, "PaintCubes", "paintCursor");
        int64_t update[] = {(int64_t)selected, (int64_t)model, painting};
        if (cursor) api->invoke(cursor, "PaintCursor", "UpdateCursor", 3, update, "ooi", 'v');
    }

    int Start(const OkMod* host) {
        if (host->size < sizeof(OkMod)) {
            host->log(OK_LOG_ERROR, "kogamastudio needs a newer okmod.dll");
            return 1;
        }
        api = host;
        world::Attach(host);
        cubes::Attach(host);
        globals::mainModule = GetModuleHandleW(L"kogamastudio.dll");
        api->on("frame", OnFrame, nullptr);
        api->on("frame.late", OnLateFrame, nullptr);
        api->on("app.quit", OnQuit, nullptr);
        api->on("editor.select", OnSelect, nullptr);
        api->on("world.object_added", OnWorldChanged, nullptr);
        api->on("world.object_removed", OnWorldChanged, nullptr);
        api->on("world.loaded", OnWorldChanged, nullptr);
        api->on("editor.translate_start", OnTranslateStart, nullptr);
        api->on("editor.translate_end", OnTranslateEnd, nullptr);
        api->on("editor.rotate_step", OnRotationChange, nullptr);
        api->on("editor.rotation_reset", OnRotationChange, nullptr);
        api->on("world.link_added", OnLinkAdded, nullptr);
        api->on("world.link_removed", OnLinkRemoved, nullptr);
        api->on("world.object_link_added", OnObjectLinkAdded, nullptr);
        api->on("world.object_link_removed", OnObjectLinkRemoved, nullptr);
        api->on("editor.move_character", OnMoveCharacter, nullptr);
        api->on("editor.add_prototype", OnAddPrototype, nullptr);
        api->on("editor.translate", OnTranslate, nullptr);
        api->on("editor.grid_point", OnGridPoint, nullptr);
        api->on("editor.rotate_step", OnRotateStep, nullptr);
        api->on("editor.paint_cubes", OnPaintCubes, nullptr);
        api->on("camera.set", OnSetCamera, nullptr);
        api->on("camera.blue_mode", OnBlueMode, nullptr);
        api->on("settings.slider_float", OnSliderFloat, nullptr);
        api->on("settings.slider_int", OnSliderInt, nullptr);
        api->on("settings.slider_changed", OnSliderChanged, nullptr);
        api->log(OK_LOG_INFO, "kogamastudio ready, F2 opens the menu");
        return 0;
    }
}

extern "C" {
OKMOD_MOD(Start)
}
