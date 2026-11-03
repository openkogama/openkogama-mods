#include "build.h"
#include "okmod.h"
#include "runtime.h"
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

namespace build {
    namespace {
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

        struct Number {
            bool on = false;
            double value = 0;
        };

        struct Vec3 {
            float x, y, z;
        };

        using Clock = std::chrono::steady_clock;

        const OkMod* api = nullptr;
        bool constraint = true;
        bool slidersClamp = true;
        bool limit = false;
        bool materials = false;
        bool muzzle = false;
        bool face = false;
        bool blueAllowed = true;
        Number grid, scale, step, speed;
        bool slidersOn = false;
        double slidersLow = 0;
        double slidersHigh = 1;

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

        int64_t EnumValue(const char* type, const char* name) { return (int64_t)api->field_num(0, type, name, 0); }

        ok_object Get(ok_object self, const char* type, const char* property) {
            return self ? (ok_object)api->invoke(self, type, property, 0, nullptr, "", 'o') : 0;
        }

        void Flag(const char* name, bool on) {
            if (api) api->flag_set(name, on);
        }

        void Param(const char* flag, const char* name, const Number& number) {
            if (!api) return;
            api->flag_set(flag, number.on);
            api->param_set_num(name, number.value);
        }

        struct SpeedCheck {
            Clock::time_point since = Clock::now();
            bool have = false;
            float last[3] = {0, 0, 0};
            double lastAsked = 0;
            double lastScaled = 0;
            double asked = 0;
            double scaled = 0;
            double moved = 0;
            int calls = 0;
        } check;

        double Length(const Vec3& v) { return std::sqrt((double)v.x * v.x + (double)v.y * v.y + (double)v.z * v.z); }

        bool AvatarPosition(ok_object editMode, float* position) {
            ok_object avatar = editMode ? api->field_obj(editMode, nullptr, "buildModeAvatar") : 0;
            ok_object boxed = avatar ? (ok_object)api->invoke(avatar, nullptr, "get_WorldPosition", 0, nullptr, "", 'o') : 0;
            return boxed && api->unbox(boxed, position, sizeof(float) * 3);
        }

        void Measure(ok_object editMode, double asked, double scaled) {
            float position[3];
            bool known = AvatarPosition(editMode, position);
            if (known && check.have) {
                double dx = position[0] - check.last[0], dy = position[1] - check.last[1], dz = position[2] - check.last[2];
                check.moved += std::sqrt(dx * dx + dy * dy + dz * dz);
                check.asked += check.lastAsked;
                check.scaled += check.lastScaled;
            }
            check.calls++;
            check.have = known;
            if (known) std::memcpy(check.last, position, sizeof(position));
            check.lastAsked = asked;
            check.lastScaled = scaled;
            Clock::time_point now = Clock::now();
            if (now - check.since < std::chrono::seconds(3)) return;
            if (check.asked > 0.001)
                runtime::Log("edit speed x" + std::to_string(speed.value) + ": " + std::to_string(check.calls) + " moves, game wanted " + std::to_string(check.asked)
                    + ", after scaling " + std::to_string(check.scaled) + ", avatar moved " + std::to_string(check.moved));
            check.since = now;
            check.asked = check.scaled = check.moved = 0;
            check.calls = 0;
        }

        void OnMoveCharacter(ok_call* call, void*) {
            ok_object self = api->arg_object(call, 0);
            if (!constraint && self) {
                int64_t off = 0;
                api->invoke(self, nullptr, "set_MovementConstrained", 1, &off, "i", 'v');
            }
            if (!speed.on) {
                check.have = false;
                return;
            }
            Vec3* delta = (Vec3*)(uintptr_t)api->arg_int(call, DeltaArgument);
            if (!delta) return;
            double asked = Length(*delta);
            float factor = (float)speed.value;
            delta->x *= factor;
            delta->y *= factor;
            delta->z *= factor;
            Measure(self, asked, Length(*delta));
        }

        void OnAddPrototype(ok_call* call, void*) {
            if (scale.on) api->set_arg_int(call, ScaleArgument, FloatBits(scale.value));
        }

        void OnTranslate(ok_call* call, void*) {
            if (grid.on) api->set_field_num(api->arg_object(call, 0), "ESTranslate", "gridSize", grid.value);
        }

        void OnGridPoint(ok_call* call, void*) {
            if (grid.on) api->set_arg_int(call, GridSizeArgument, FloatBits(grid.value));
        }

        void OnRotateStep(ok_call* call, void*) {
            if (!step.on) return;
            double turn = FromBits(api->arg_int(call, RotationSpeedArgument));
            api->set_arg_int(call, RotationSpeedArgument, FloatBits(turn > 0 ? step.value : turn < 0 ? -step.value : 0.0));
        }

        void OnBlueMode(ok_call* call, void*) {
            if (!blueAllowed) api->set_arg_int(call, BlueModeArgument, 0);
        }

        void OnSliderFloat(ok_call* call, void*) {
            if (!slidersOn) return;
            double value = FromBits(api->arg_int(call, FloatValueArgument));
            api->set_arg_int(call, FloatMinArgument, FloatBits(slidersLow));
            api->set_arg_int(call, MaxArgument, FloatBits(slidersHigh));
            if (slidersClamp) api->set_arg_int(call, FloatValueArgument, FloatBits(std::clamp(value, slidersLow, slidersHigh)));
        }

        void OnSliderInt(ok_call* call, void*) {
            if (!slidersOn) return;
            double value = (int32_t)api->arg_int(call, IntValueArgument);
            api->set_arg_int(call, IntMinArgument, (int32_t)slidersLow);
            api->set_arg_int(call, MaxArgument, (int32_t)slidersHigh);
            if (slidersClamp) api->set_arg_int(call, IntValueArgument, (int32_t)std::clamp(value, slidersLow, slidersHigh));
        }

        void OnSliderChanged(ok_call* call, void*) {
            if (!slidersOn) return;
            ok_object slider = api->field_obj(api->arg_object(call, 0), "SettingsSlider", "slider");
            if (!slider) return;
            int64_t lowBits = FloatBits(slidersLow), highBits = FloatBits(slidersHigh);
            api->invoke(slider, "UnityEngine.UI.Slider", "set_minValue", 1, &lowBits, "f", 'v');
            api->invoke(slider, "UnityEngine.UI.Slider", "set_maxValue", 1, &highBits, "f", 'v');
            double value = FromBits(api->invoke(slider, "UnityEngine.UI.Slider", "get_value", 0, nullptr, "", 'i'));
            double clamped = slidersClamp ? std::clamp(value, slidersLow, slidersHigh) : value;
            if (clamped == value) return;
            int64_t clampedBits = FloatBits(clamped);
            api->invoke(slider, "UnityEngine.UI.Slider", "set_value", 1, &clampedBits, "f", 'v');
        }

        void OnPaintCubes(ok_call* call, void*) {
            if (!face) return;
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
                int64_t side = (int64_t)api->field_num(selected, "CubePickingInfo", "pickedFace", 0);
                int64_t material = api->invoke(machine, "CubeModelingStateMachine", "get_CurrentMaterialId", 0, nullptr, "", 'i') & 0xFF;
                int64_t audio[] = {Pointer(position), EnumValue("AudioActions", "CubeAdded")};
                api->invoke(machine, "CubeModelingStateMachine", "HandleAudio", 2, audio, "pi", 'v');
                int64_t paint[] = {Pointer(position), side, material};
                api->invoke(model, "MVCubeModelBase", "SetMaterial", 3, paint, "pii", 'v');
                int64_t change[] = {api->invoke(model, "MVCubeModelBase", "get_CubeCount", 0, nullptr, "", 'i'), EnumValue("EditCubeChange", "CubePainted")};
                api->invoke(0, "CubeModelTool", "SendCubeEvent", 2, change, "ii", 'v');
                painting = true;
            }
            ok_object cursor = api->field_obj(tool, "PaintCubes", "paintCursor");
            int64_t update[] = {(int64_t)selected, (int64_t)model, painting};
            if (cursor) api->invoke(cursor, "PaintCursor", "UpdateCursor", 3, update, "ooi", 'v');
        }
    }

    void Attach(const OkMod* host) {
        api = host;
        if (!api || sizeof(void*) != 8) return;
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
    }

    void Reset(const std::string& setting) {
        if (setting == "limit") SetLimit(false);
        else if (setting == "materials") SetMaterials(false);
        else if (setting == "muzzle") SetMuzzle(false);
        else if (setting == "face") SetFace(false);
        else if (setting == "blue") SetBlue(true);
        else if (setting == "grid") SetGrid(false, 0);
        else if (setting == "scale") SetScale(false, 0);
        else if (setting == "step") SetStep(false, 0);
        else if (setting == "speed") SetSpeed(false, 0);
        else if (setting == "sliders") SetSliders(false, 0, 1);
        else if (setting == "constraint") constraint = true;
        else if (setting == "clamp") slidersClamp = true;
    }

    bool Limit() { return limit; }

    void SetLimit(bool on) {
        limit = on;
        Flag("no_build_limit", on);
    }

    bool Materials() { return materials; }

    void SetMaterials(bool on) {
        materials = on;
        Flag("destructibles_unlock", on);
    }

    bool Muzzle() { return muzzle; }

    void SetMuzzle(bool on) {
        muzzle = on;
        Flag("unlimited_muzzle_position", on);
    }

    bool Face() { return face; }

    void SetFace(bool on) { face = on; }

    bool Blue() { return blueAllowed; }

    void SetBlue(bool allowed) { blueAllowed = allowed; }

    bool Grid(double& size) {
        size = grid.value;
        return grid.on;
    }

    void SetGrid(bool on, double size) {
        grid = {on, size};
        Param("custom_grid", "custom_grid_size", grid);
    }

    bool Scale(double& value) {
        value = scale.value;
        return scale.on;
    }

    void SetScale(bool on, double value) {
        scale = {on, value};
        Param("model_scale", "model_scale_value", scale);
    }

    bool Step(double& degrees) {
        degrees = step.value;
        return step.on;
    }

    void SetStep(bool on, double degrees) { step = {on, degrees}; }

    bool Speed(double& factor) {
        factor = speed.value;
        return speed.on;
    }

    void SetSpeed(bool on, double factor) { speed = {on, factor}; }

    bool Sliders(double& low, double& high) {
        low = slidersLow;
        high = slidersHigh;
        return slidersOn;
    }

    void SetSliders(bool on, double low, double high) {
        slidersOn = on;
        slidersLow = low;
        slidersHigh = high;
    }

    bool Constraint() { return constraint; }

    void SetConstraint(bool on) { constraint = on; }

    bool SlidersClamp() { return slidersClamp; }

    void SetSlidersClamp(bool on) { slidersClamp = on; }
}
