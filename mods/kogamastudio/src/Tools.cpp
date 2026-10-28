#include "Tools.h"
#include "MenuHelpers.h"
#include "pipe.h"
#include "IconsFontAwesome6.h"
#include "ui.h"

using namespace menu;

namespace Tools {
    namespace {
        struct Option {
            const char* label;
            const char* command;
            bool on;
        };

        struct Amount {
            const char* label;
            const char* toggle;
            const char* command;
            bool on;
            float value;
            float speed;
            const char* format;
        };

        Option noBuildLimit{u8"No Build Limit", u8"option_no_build_limit", false};
        Option singleSidePainting{u8"Single Side Painting", u8"option_single_side_painting", false};
        Option destructiblesUnlock{u8"Destructibles Unlock", u8"option_destructibles_unlock", false};
        Option unlimitedMuzzle{u8"Unlimited Muzzle Position", u8"option_unlimited_muzzle_position", false};
        Option blueMode{u8"Blue Mode", u8"option_blue_mode", false};

        Amount gridSize{u8"Grid Size", u8"option_custom_grid_size_enabled", u8"option_custom_grid_size", false, 1.0f, 0.1f, "%.3f"};
        Amount rotationStep{u8"Rotation Step", u8"option_custom_rot_step_enabled", u8"option_custom_rot_step_size", false, 15.0f, 1.0f, "%.3f"};
        Amount editSpeed{u8"Edit Speed", u8"option_custom_speed_enabled", u8"option_custom_speed_value", false, 2.0f, 0.1f, "%.2f"};
        Amount modelScale{u8"Model Scale", u8"option_custom_model_scale_enabled", u8"option_custom_model_scale_value", false, 4.0f, 0.1f, "%.6g"};

        bool unlimitedConfig = false;
        float configMin = 0.0f;
        float configMax = 1.0f;

        void Send(const char* command, const std::string& value) { SendCommand((std::string(command) + u8"|" + value).c_str()); }

        std::string Number(float value) {
            char text[32];
            snprintf(text, sizeof(text), "%.15g", value);
            for (char* at = text; *at; at++) if (*at == ',') *at = '.';
            return text;
        }

        void Flag(Option& option) {
            ui::Property(TR(option.label));
            ImGui::PushID(option.command);
            if (ImGui::Checkbox(u8"##on", &option.on)) Send(option.command, option.on ? u8"true" : u8"false");
            ImGui::PopID();
        }

        void Value(Amount& amount) {
            ui::Property(TR(amount.label));
            ImGui::PushID(amount.command);
            if (ImGui::Checkbox(u8"##on", &amount.on)) Send(amount.toggle, amount.on ? u8"true" : u8"false");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::BeginDisabled(!amount.on);
            if (ImGui::DragFloat(u8"##value", &amount.value, amount.speed, 0.0f, 0.0f, amount.format)) Send(amount.command, Number(amount.value));
            if (!typing) typing = ImGui::IsItemActive();
            ImGui::EndDisabled();
            ImGui::PopID();
        }

        void Config() {
            ui::Property(TR(u8"Unlimited Config"));
            if (ImGui::Checkbox(u8"##config", &unlimitedConfig)) Send(u8"option_unlimited_config_enabled", unlimitedConfig ? u8"true" : u8"false");
            ImGui::SameLine();
            ImGui::BeginDisabled(!unlimitedConfig);
            float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            ImGui::SetNextItemWidth(half);
            if (ImGui::DragFloat(u8"##min", &configMin, 0.1f, 0.0f, 0.0f, "%.3f")) Send(u8"option_unlimited_config_min", Number(configMin));
            ImGui::SetItemTooltip(u8"%s", TR(u8"Min"));
            if (!typing) typing = ImGui::IsItemActive();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat(u8"##max", &configMax, 0.1f, 0.0f, 0.0f, "%.3f")) Send(u8"option_unlimited_config_max", Number(configMax));
            ImGui::SetItemTooltip(u8"%s", TR(u8"Max"));
            if (!typing) typing = ImGui::IsItemActive();
            ImGui::EndDisabled();
        }
    }

    void Render() {
        if (ui::BeginPanel(u8"Tools", ICON_FA_HAMMER, TR(u8"Build"), nullptr)) {
            if (ui::Section(TR(u8"Placement")) && ui::BeginProperties(u8"##placement")) {
                Value(gridSize);
                Value(rotationStep);
                Value(editSpeed);
                Value(modelScale);
                ui::EndProperties();
            }
            if (ui::Section(TR(u8"Limits")) && ui::BeginProperties(u8"##limits")) {
                Flag(noBuildLimit);
                Config();
                Flag(unlimitedMuzzle);
                Flag(destructiblesUnlock);
                ui::EndProperties();
            }
            if (ui::Section(TR(u8"Editing")) && ui::BeginProperties(u8"##editing")) {
                Flag(singleSidePainting);
                Flag(blueMode);
                ui::EndProperties();
            }
        }
        ui::EndPanel();
    }
}
