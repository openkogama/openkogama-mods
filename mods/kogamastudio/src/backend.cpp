#include "stdafx.h"
#include "bridge.h"
#include "overlay.h"
#include "world.h"
#include "cubes.h"
#include "okmod.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>

namespace {
    constexpr int KeyF2 = 283;
    constexpr double ReportSeconds = 0.5;

    constexpr int ScaleArgument = 6;
    constexpr int DeltaArgument = 1;
    constexpr int GridSizeArgument = 6;
    constexpr int RotationSpeedArgument = 6;
    constexpr int BlueModeArgument = 1;
    constexpr int FloatValueArgument = 6;
    constexpr int FloatMinArgument = 7;
    constexpr int IntValueArgument = 2;
    constexpr int IntMinArgument = 3;
    constexpr int MaxArgument = 8;

    const char* const GameModes[] = {"Edit", "Play", "CharacterEditor", "SpacePlay", "SpaceEdit", "None"};

    const OkMod* api = nullptr;
    bool announced = false;
    bool blueMode = false;
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
        if (overlay::Running() || overlay::Failed()) return;
        HWND game = nullptr;
        EnumWindows(Pick, (LPARAM)&game);
        if (game) overlay::Start(game);
    }

    void Handle(const std::string& line) {
        size_t bar = line.find('|');
        std::string command = line.substr(0, bar);
        std::string value = bar == std::string::npos ? std::string() : line.substr(bar + 1);
        double number = std::atof(value.c_str());

        if (command == "option_no_build_limit") api->flag_set("no_build_limit", On(value));
        else if (command == "option_single_side_painting") options.singleSidePainting = On(value);
        else if (command == "option_destructibles_unlock") api->flag_set("destructibles_unlock", On(value));
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
        world::Tick();
        cubes::Tick();
        Report();
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
        api->on("app.quit", OnQuit, nullptr);
        api->on("editor.select", OnSelect, nullptr);
        api->on("world.object_added", OnWorldChanged, nullptr);
        api->on("world.object_removed", OnWorldChanged, nullptr);
        api->on("world.object_transferred", OnWorldChanged, nullptr);
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
