#include "stdafx.h"
#include "Performance.h"
#include "Explorer.h"
#include "MenuHelpers.h"
#include "Translate.h"
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cmath>

namespace Performance {

    static bool enabled = false;
    static float renderDistance = 150.0f;

    // Track which objects we've hidden so we can restore them
    static std::unordered_set<std::string> hiddenObjects;

    static float DistanceSq(float x1, float y1, float z1, float x2, float y2, float z2) {
        float dx = x1 - x2, dy = y1 - y2, dz = z1 - z2;
        return dx * dx + dy * dy + dz * dz;
    }

    static void UpdateCulling() {
        if (!enabled) {
            // Restore all hidden objects at once
            if (!hiddenObjects.empty()) {
                std::string ids;
                for (auto& id : hiddenObjects) {
                    if (!ids.empty()) ids += ",";
                    ids += id;
                }
                hiddenObjects.clear();
                std::string cmd = "object_set_visible_batch|true|" + ids;
                menu::SendCommand(cmd.c_str());
            }
            return;
        }

        float camX, camY, camZ;
        Explorer::GetCameraPos(camX, camY, camZ);

        float hideSq = renderDistance * renderDistance;
        // Hysteresis: show objects only when they're 15% closer than hide threshold
        float showSq = (renderDistance * 0.85f) * (renderDistance * 0.85f);

        const auto& objects = Explorer::GetAllObjects();

        // Build set of IDs that have children - skip them (only cull leaves)
        std::unordered_set<std::string> parentIds;
        for (const auto& obj : objects)
            if (obj.groupId >= 0)
                parentIds.insert(std::to_string(obj.groupId));

        // Process all objects - no limit
        std::string showIds, hideIds;

        for (const auto& obj : objects) {
            if (obj.type.find("CubeModelPrototypeTerrain") != std::string::npos)
                continue;
            if (obj.type.find("Group") != std::string::npos)
                continue;
            if (parentIds.count(obj.id))
                continue;

            float dSq = DistanceSq(obj.x, obj.y, obj.z, camX, camY, camZ);
            bool isHidden = hiddenObjects.count(obj.id) > 0;

            if (dSq > hideSq && !isHidden) {
                if (!hideIds.empty()) hideIds += ",";
                hideIds += obj.id;
                hiddenObjects.insert(obj.id);
            } else if (dSq < showSq && isHidden) {
                if (!showIds.empty()) showIds += ",";
                showIds += obj.id;
                hiddenObjects.erase(obj.id);
            }
        }

        if (!showIds.empty()) {
            std::string cmd = "object_set_visible_batch|true|" + showIds;
            menu::SendCommand(cmd.c_str());
        }
        if (!hideIds.empty()) {
            std::string cmd = "object_set_visible_batch|false|" + hideIds;
            menu::SendCommand(cmd.c_str());
        }
    }

    // Throttle
    static double lastUpdateTime = -1.0;
    static float updateInterval = 0.25f;

    void Render() {
        if (!ImGui::Begin((std::string(TR(u8"Performance")) + u8"###Performance").c_str(), nullptr, ImGuiWindowFlags_NoCollapse)) {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted(TR(u8"Render Distance Culling"));
        ImGui::Separator();

        bool changed = ImGui::Checkbox(TR(u8"Enable culling"), &enabled);

        ImGui::SliderFloat(TR(u8"Render distance"), &renderDistance, 10.0f, 500.0f, "%.0f");
        ImGui::SliderFloat(TR(u8"Update interval (s)"), &updateInterval, 0.05f, 2.0f, "%.2f");

        if (enabled) {
            ImGui::Spacing();
            ImGui::Text(u8"%s: %d", TR(u8"Hidden objects"), (int)hiddenObjects.size());
        }

        double now = ImGui::GetTime();
        if (enabled) {
            if (lastUpdateTime < 0 || (now - lastUpdateTime) >= updateInterval) {
                lastUpdateTime = now;
                UpdateCulling();
            }
        } else if (changed && !enabled) {
            lastUpdateTime = -1.0;
            UpdateCulling(); // restore all at once
        }

        ImGui::End();
    }
}
