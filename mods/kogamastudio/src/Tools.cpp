#include "Tools.h"
#include "MenuHelpers.h"
#include "pipe.h"

using namespace menu;

namespace Tools {

    static bool NoBuildLimit = false;
    static bool AntiAFK = false;
    static bool SingleSidePainting = false;
    static bool DestructiblesUnlock = false;
    static bool BlueModeEnabled = false;
    static bool CustomGridSizeEnabled = false;
    static float CustomGridSizeValue = 1.0f;
    static bool CustomRotStepEnabled = false;
    static float CustomRotStepValue = 15.0f;
    static bool CustomSpeedEnabled = false;
    static float CustomSpeedValue = 2.0f;
    static bool UnlimitedConfigEnabled = false;
    static float UnlimitedConfigMin = 0.0f;
    static float UnlimitedConfigMax = 1.0f;
    static int WOId = -1;
    static float ObjectsRotation[3] = {0.0f, 0.0f, 0.0f};
    static char PlayerName[64] = u8"";
    static bool CustomModelScaleEnabled = false;
    static float CustomModelScaleValue = 4.0f;
    static bool CustomWOScaleEnabled = false;
    static float CustomWOScaleValue[3] = { 1.0f, 1.0f, 1.0f };
    static bool UnlimitedMuzzlePosition = false;



    void Render()
    {
        if (!ImGui::Begin((std::string(TR(u8"Tools")) + u8"###Tools").c_str(), nullptr, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::End();
            return;
        }

        // no build limit
        if (ImGui::Checkbox(TR(u8"No Build Limit"), &NoBuildLimit)) {
            if (NoBuildLimit) SendCommand(u8"option_no_build_limit|true");
            else SendCommand(u8"option_no_build_limit|false");
        }

        // single side painting
        if (ImGui::Checkbox(TR(u8"Single Side Painting"), &SingleSidePainting)) {
            if (SingleSidePainting) SendCommand(u8"option_single_side_painting|true");
            else SendCommand(u8"option_single_side_painting|false");
        }

        // anti afk
        if (ImGui::Checkbox(TR(u8"Anti AFK"), &AntiAFK)) {
            if (AntiAFK) SendCommand(u8"option_anti_afk|true");
            else SendCommand(u8"option_anti_afk|false");
        }

        // destructibles unlock
        if (ImGui::Checkbox(TR(u8"Destructibles Unlock"), &DestructiblesUnlock)) {
            if (DestructiblesUnlock) SendCommand(u8"option_destructibles_unlock|true");
            else SendCommand(u8"option_destructibles_unlock|false");
        }

        // blue mode
        if (ImGui::Checkbox(TR(u8"Blue Mode"), &BlueModeEnabled)) {
            if (BlueModeEnabled) SendCommand(u8"option_blue_mode|true");
            else SendCommand(u8"option_blue_mode|false");
        }

        // speed
        if (ImGui::Checkbox(TR(u8"Custom Speed"), &CustomSpeedEnabled)) {
            if (CustomSpeedEnabled) SendCommand(u8"option_custom_speed_enabled|true");
            else SendCommand(u8"option_custom_speed_enabled|false");
        }

        if (CustomSpeedEnabled) {
            DragFloatInput(TR(u8"Speed"), CustomSpeedValue, u8"option_custom_speed_value", typing, 0.1f, 100.0f, u8"%.2f");
        }


        // custom grid size
        if (ImGui::Checkbox(TR(u8"Custom Grid Size"), &CustomGridSizeEnabled)) {
            if (CustomGridSizeEnabled) SendCommand(u8"option_custom_grid_size_enabled|true");
            else SendCommand(u8"option_custom_grid_size_enabled|false");
        }

        if (CustomGridSizeEnabled) {
            DragFloatInput(TR(u8"Size"), CustomGridSizeValue, u8"option_custom_grid_size", typing, 0.1f, 100.0f, u8"%.3f");
        }

        // custom rot step
        if (ImGui::Checkbox(TR(u8"Custom Rotation Step"), &CustomRotStepEnabled)) {
            if (CustomRotStepEnabled) SendCommand(u8"option_custom_rot_step_enabled|true");
            else SendCommand(u8"option_custom_rot_step_enabled|false");
        }

        if (CustomRotStepEnabled) {
            DragFloatInput(TR(u8"Step"), CustomRotStepValue, u8"option_custom_rot_step_size", typing, 1.0f, 100.0f, u8"%.3f");
        }

        // unlimited config
        if (ImGui::Checkbox(TR(u8"Unlimited Config"), &UnlimitedConfigEnabled)) {
            if (UnlimitedConfigEnabled) SendCommand(u8"option_unlimited_config_enabled|true");
            else SendCommand(u8"option_unlimited_config_enabled|false");
        }

        if (UnlimitedConfigEnabled) {
            ImGui::PushItemWidth(100);
            ImGui::PushID(u8"unlimited_min");
            ImGui::DragFloat(TR(u8"Min"), &UnlimitedConfigMin, 0.1f, 0.0f, 0.0f, "%.3f");
            ImGui::PopID();
            ImGui::PopItemWidth();
            if (!typing) typing = ImGui::IsItemActive();
            if (ImGui::IsItemEdited()) {
                SendCommand((u8"option_unlimited_config_min|" + std::to_string(UnlimitedConfigMin)).c_str());
            }

            ImGui::PushItemWidth(100);
            ImGui::PushID(u8"unlimited_max");
            ImGui::DragFloat(TR(u8"Max"), &UnlimitedConfigMax, 0.1f, 0.0f, 0.0f, "%.3f");
            ImGui::PopID();
            ImGui::PopItemWidth();
            if (!typing) typing = ImGui::IsItemActive();
            if (ImGui::IsItemEdited()) {
                SendCommand((u8"option_unlimited_config_max|" + std::to_string(UnlimitedConfigMax)).c_str());
            }
        }

        //custom model scale
        if (ImGui::Checkbox(TR(u8"Custom Model Scale"), &CustomModelScaleEnabled)) {
            if (CustomModelScaleEnabled) SendCommand(u8"option_custom_model_scale_enabled|true");
            else SendCommand(u8"option_custom_model_scale_enabled|false");
        }

        if (CustomModelScaleEnabled) {
            ImGui::PushItemWidth(100);
            ImGui::InputFloat(TR(u8"Scale"), &CustomModelScaleValue, 0.0f, 0.0f, u8"%.6g");
            ImGui::PopItemWidth();
            if (!typing) typing = ImGui::IsItemActive();
            if (ImGui::IsItemEdited()) {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), u8"%.15g", CustomModelScaleValue);
                SendCommand((u8"option_custom_model_scale_value|" + std::string(buffer)).c_str());
            }
        }


        // unlimited muzzle position
        if (ImGui::Checkbox(TR(u8"Unlimited Muzzle Position"), &UnlimitedMuzzlePosition)) {
            if (UnlimitedMuzzlePosition) SendCommand(u8"option_unlimited_muzzle_position|true");
            else SendCommand(u8"option_unlimited_muzzle_position|false");
        }

        if (ImGui::Button("Test")) {
            SendCommand("test");
        }

        ImGui::End();
    }
}
