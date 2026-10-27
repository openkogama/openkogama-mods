#include "stdafx.h"
#include "Camera.h"
#include "MenuHelpers.h"
#include "UILocale.h"

using namespace menu;

namespace CameraPanel {

    static bool freecamEnabled = false;
    static float freecamSpeed = 20.0f;
    static float freecamSensitivity = 2.0f;
    static bool freecamRequireRmb = true;
    static bool customFovEnabled = false;
    static float fov = 60.0f;
    static bool customDistanceEnabled = false;
    static float cameraDistance = 5.0f;
    static bool zoomEnabled = false;
    static float zoomLevel = 2.0f;

    static const char* cameraModeNames[] = {
        "Third Person",
    };
    static const int cameraModeValues[] = { 0 };
    static const int cameraModeCount = 1;
    static int selectedCameraMode = -1;

    void Render()
    {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
        if (ImGui::Begin((std::string(TR(u8"View")) + u8"###Camera").c_str(), nullptr, flags))
        {
            // freecam
            if (ImGui::Checkbox(TR(u8"Freecam"), &freecamEnabled))
            {
                if (freecamEnabled) SendCommand(u8"camera_freecam_enable");
                else                SendCommand(u8"camera_freecam_disable");
            }

            if (freecamEnabled)
            {
                ImGui::Indent();
                DragFloatInput(TR(u8"Speed##fcam_speed"), freecamSpeed, u8"camera_freecam_speed", typing, 0.5f, 120.0f, u8"%.1f");
                DragFloatInput(TR(u8"Sensitivity##fcam_sens"), freecamSensitivity, u8"camera_freecam_sensitivity", typing, 0.05f, 120.0f, u8"%.2f");
                if (ImGui::Checkbox(TR(u8"Rotate with RMB"), &freecamRequireRmb))
                    SendCommand(freecamRequireRmb ? u8"camera_freecam_require_rmb|true" : u8"camera_freecam_require_rmb|false");
                ImGui::Unindent();
            }

            ImGui::Separator();

            // fov
            if (ImGui::Checkbox(TR(u8"Custom FOV"), &customFovEnabled))
            {
                if (customFovEnabled) SendCommand((std::string(u8"camera_fov|") + FloatToStr(fov)).c_str());
                else                  SendCommand(u8"camera_fov_reset");
            }

            if (customFovEnabled)
            {
                ImGui::Indent();
                DragFloatInput(u8"FOV##cam_fov", fov, u8"camera_fov", typing, 0.5f, 120.0f, u8"%.1f");
                ImGui::Unindent();
            }

            ImGui::Separator();

            // custom distance
            if (ImGui::Checkbox(TR(u8"Custom Camera Distance"), &customDistanceEnabled))
            {
                if (customDistanceEnabled)
                    SendCommand((std::string(u8"camera_distance|") + FloatToStr(cameraDistance)).c_str());
                else
                    SendCommand(u8"camera_distance_reset");
            }

            if (customDistanceEnabled)
            {
                ImGui::Indent();
                DragFloatInput(u8"Distance##cam_dist", cameraDistance, u8"camera_distance", typing, 0.1f, 120.0f, u8"%.1f");
                ImGui::Unindent();
            }

            ImGui::Separator();

            // camera mode
            ImGui::Text(TR(u8"Camera Mode"));
            ImGui::SetNextItemWidth(-1);
            int previewIdx = selectedCameraMode;
            const char* previewLabel = (previewIdx >= 0 && previewIdx < cameraModeCount) ? cameraModeNames[previewIdx] : TR(u8"Default");
            if (ImGui::BeginCombo(u8"##cam_mode", previewLabel))
            {
                if (ImGui::Selectable(TR(u8"Default"), selectedCameraMode == -1))
                {
                    selectedCameraMode = -1;
                    SendCommand(u8"camera_mode_reset");
                }
                for (int i = 0; i < cameraModeCount; i++)
                {
                    bool sel = (selectedCameraMode == i);
                    if (ImGui::Selectable(cameraModeNames[i], sel))
                    {
                        selectedCameraMode = i;
                        SendCommand((std::string(u8"camera_set_mode|") + std::to_string(cameraModeValues[i])).c_str());
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::Separator();

            // zoom
            if (ImGui::Checkbox(TR(u8"Zoom"), &zoomEnabled))
            {
                if (zoomEnabled)
                {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%.2f", zoomLevel);
                    SendCommand((std::string(u8"camera_zoom|") + buf).c_str());
                }
                else
                    SendCommand(u8"camera_zoom_reset");
            }

            if (zoomEnabled)
            {
                ImGui::Indent();
                DragFloatInput(u8"##zoom_val", zoomLevel, u8"camera_zoom", typing);
                ImGui::Unindent();
            }
        }
        ImGui::End();
    }

    void ResetModeSelection()
    {
        selectedCameraMode = -1;
    }

}
