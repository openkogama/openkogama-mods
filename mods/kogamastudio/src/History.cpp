#include "stdafx.h"
#include "History.h"
#include "MenuHelpers.h"
#include "UILocale.h"
#include <vector>
#include <string>
#include <mutex>

namespace History {

    static const int MAX_ENTRIES = 100;

    struct Entry {
        std::string display;
        std::string objectId;
        // position
        float oldX = 0, oldY = 0, oldZ = 0;
        float newX = 0, newY = 0, newZ = 0;
        bool hasPosition = false;
        // rotation (quaternion)
        float oldQx = 0, oldQy = 0, oldQz = 0, oldQw = 1;
        float newQx = 0, newQy = 0, newQz = 0, newQw = 1;
        bool hasRotation = false;
        // link (regular)
        int linkId = -1, outputWOID = -1, inputWOID = -1;
        bool hasLinkAdded = false, hasLinkRemoved = false;
        // object link
        int objLinkId = -1, connectorWOID = -1, objWOID = -1;
        bool hasObjLinkAdded = false, hasObjLinkRemoved = false;
        // undo state
        bool undone = false;
        int undoOrder = 0;

        bool isUndoable() const {
            return hasPosition || hasRotation
                || hasLinkAdded || hasLinkRemoved
                || hasObjLinkAdded || hasObjLinkRemoved;
        }
    };

    static std::vector<Entry> entries;
    static std::mutex mtx;
    static int undoCounter = 0;

    void Add(const std::string& action)
    {
        Entry e;
        e.display = action;

        std::lock_guard<std::mutex> lock(mtx);
        entries.insert(entries.begin(), e);
        if ((int)entries.size() > MAX_ENTRIES)
            entries.resize(MAX_ENTRIES);
    }

    void AddTransform(const std::string& objectId,
                      float oldX, float oldY, float oldZ,
                      float newX, float newY, float newZ)
    {
        char display[64];
        snprintf(display, sizeof(display), "Moved #%s", objectId.c_str());

        Entry e;
        e.display = display;
        e.objectId = objectId;
        e.oldX = oldX; e.oldY = oldY; e.oldZ = oldZ;
        e.newX = newX; e.newY = newY; e.newZ = newZ;
        e.hasPosition = true;

        std::lock_guard<std::mutex> lock(mtx);
        entries.insert(entries.begin(), e);
        if ((int)entries.size() > MAX_ENTRIES)
            entries.resize(MAX_ENTRIES);
    }

    void AddRotation(const std::string& objectId,
                     float oldQx, float oldQy, float oldQz, float oldQw,
                     float newQx, float newQy, float newQz, float newQw)
    {
        char display[64];
        snprintf(display, sizeof(display), "Rotated #%s", objectId.c_str());

        Entry e;
        e.display = display;
        e.objectId = objectId;
        e.oldQx = oldQx; e.oldQy = oldQy; e.oldQz = oldQz; e.oldQw = oldQw;
        e.newQx = newQx; e.newQy = newQy; e.newQz = newQz; e.newQw = newQw;
        e.hasRotation = true;

        std::lock_guard<std::mutex> lock(mtx);
        entries.insert(entries.begin(), e);
        if ((int)entries.size() > MAX_ENTRIES)
            entries.resize(MAX_ENTRIES);
    }

    void AddLinkAdded(int outputWOID, int inputWOID)
    {
        char display[64];
        snprintf(display, sizeof(display), "Link added (%d -> %d)", outputWOID, inputWOID);
        Entry e;
        e.display = display;
        e.outputWOID = outputWOID; e.inputWOID = inputWOID;
        e.hasLinkAdded = true;
        std::lock_guard<std::mutex> lock(mtx);
        entries.insert(entries.begin(), e);
        if ((int)entries.size() > MAX_ENTRIES) entries.resize(MAX_ENTRIES);
    }

    void AddLinkRemoved(int outputWOID, int inputWOID)
    {
        char display[64];
        snprintf(display, sizeof(display), "Link removed (%d -> %d)", outputWOID, inputWOID);
        Entry e;
        e.display = display;
        e.outputWOID = outputWOID; e.inputWOID = inputWOID;
        e.hasLinkRemoved = true;
        std::lock_guard<std::mutex> lock(mtx);
        entries.insert(entries.begin(), e);
        if ((int)entries.size() > MAX_ENTRIES) entries.resize(MAX_ENTRIES);
    }

    void AddObjectLinkAdded(int connectorWOID, int objectWOID)
    {
        char display[64];
        snprintf(display, sizeof(display), "Object link added (%d -> %d)", connectorWOID, objectWOID);
        Entry e;
        e.display = display;
        e.connectorWOID = connectorWOID; e.objWOID = objectWOID;
        e.hasObjLinkAdded = true;
        std::lock_guard<std::mutex> lock(mtx);
        entries.insert(entries.begin(), e);
        if ((int)entries.size() > MAX_ENTRIES) entries.resize(MAX_ENTRIES);
    }

    void AddObjectLinkRemoved(int connectorWOID, int objectWOID)
    {
        char display[64];
        snprintf(display, sizeof(display), "Object link removed (%d -> %d)", connectorWOID, objectWOID);
        Entry e;
        e.display = display;
        e.connectorWOID = connectorWOID; e.objWOID = objectWOID;
        e.hasObjLinkRemoved = true;
        std::lock_guard<std::mutex> lock(mtx);
        entries.insert(entries.begin(), e);
        if ((int)entries.size() > MAX_ENTRIES) entries.resize(MAX_ENTRIES);
    }

    static void SendUndo(Entry& e)
    {
        char cmd[256];
        if (e.hasPosition)
            snprintf(cmd, sizeof(cmd), "history_restore_position|%s|%.3f|%.3f|%.3f",
                e.objectId.c_str(), e.oldX, e.oldY, e.oldZ);
        else if (e.hasRotation)
            snprintf(cmd, sizeof(cmd), "history_restore_rotation|%s|%.5f|%.5f|%.5f|%.5f",
                e.objectId.c_str(), e.oldQx, e.oldQy, e.oldQz, e.oldQw);
        else if (e.hasLinkAdded)
            snprintf(cmd, sizeof(cmd), "history_restore_remove_link|%d|%d", e.outputWOID, e.inputWOID);
        else if (e.hasLinkRemoved)
            snprintf(cmd, sizeof(cmd), "history_restore_add_link|%d|%d", e.outputWOID, e.inputWOID);
        else if (e.hasObjLinkAdded)
            snprintf(cmd, sizeof(cmd), "history_restore_remove_object_link|%d|%d", e.connectorWOID, e.objWOID);
        else if (e.hasObjLinkRemoved)
            snprintf(cmd, sizeof(cmd), "history_restore_add_object_link|%d|%d", e.connectorWOID, e.objWOID);
        menu::SendCommand(cmd);
    }

    static void SendRedo(Entry& e)
    {
        char cmd[256];
        if (e.hasPosition)
            snprintf(cmd, sizeof(cmd), "history_restore_position|%s|%.3f|%.3f|%.3f",
                e.objectId.c_str(), e.newX, e.newY, e.newZ);
        else if (e.hasRotation)
            snprintf(cmd, sizeof(cmd), "history_restore_rotation|%s|%.5f|%.5f|%.5f|%.5f",
                e.objectId.c_str(), e.newQx, e.newQy, e.newQz, e.newQw);
        else if (e.hasLinkAdded)
            snprintf(cmd, sizeof(cmd), "history_restore_add_link|%d|%d", e.outputWOID, e.inputWOID);
        else if (e.hasLinkRemoved)
            snprintf(cmd, sizeof(cmd), "history_restore_remove_link|%d|%d", e.outputWOID, e.inputWOID);
        else if (e.hasObjLinkAdded)
            snprintf(cmd, sizeof(cmd), "history_restore_add_object_link|%d|%d", e.connectorWOID, e.objWOID);
        else if (e.hasObjLinkRemoved)
            snprintf(cmd, sizeof(cmd), "history_restore_remove_object_link|%d|%d", e.connectorWOID, e.objWOID);
        menu::SendCommand(cmd);
    }

    void Render()
    {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
        if (ImGui::Begin((std::string(TR(u8"History")) + u8"###History").c_str(), nullptr, flags))
        {
            bool canUndo = false, canRedo = false;
            {
                std::lock_guard<std::mutex> lock(mtx);
                for (const auto& e : entries)
                {
                    if (!e.isUndoable()) continue;
                    if (!e.undone) { canUndo = true; break; }
                }
                for (const auto& e : entries)
                {
                    if (!e.isUndoable()) continue;
                    if (e.undone) { canRedo = true; break; }
                }
            }

            if (!canUndo) ImGui::BeginDisabled();
            if (ImGui::Button(TR(u8"Undo")))
            {
                std::lock_guard<std::mutex> lock(mtx);
                for (auto& e : entries)
                {
                    if (!e.isUndoable() || e.undone) continue;
                    e.undone = true;
                    e.undoOrder = ++undoCounter;
                    SendUndo(e);
                    break;
                }
            }
            if (!canUndo) ImGui::EndDisabled();

            ImGui::SameLine();

            if (!canRedo) ImGui::BeginDisabled();
            if (ImGui::Button(TR(u8"Redo")))
            {
                std::lock_guard<std::mutex> lock(mtx);
                Entry* target = nullptr;
                for (auto& e : entries)
                {
                    if (!e.isUndoable() || !e.undone) continue;
                    if (!target || e.undoOrder > target->undoOrder)
                        target = &e;
                }
                if (target)
                {
                    target->undone = false;
                    target->undoOrder = 0;
                    SendRedo(*target);
                }
            }
            if (!canRedo) ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button(TR(u8"Clear")))
            {
                std::lock_guard<std::mutex> lock(mtx);
                entries.clear();
                undoCounter = 0;
            }

            ImGui::Separator();
            ImGui::BeginChild(u8"##history_list", ImVec2(0, 0), false);

            if (entries.empty())
            {
                ImGui::TextDisabled(TR(u8"No actions"));
            }
            else
            {
                std::vector<Entry*> snapshot;
                {
                    std::lock_guard<std::mutex> lock(mtx);
                    for (auto& e : entries)
                        snapshot.push_back(&e);
                }

                for (int i = 0; i < (int)snapshot.size(); ++i)
                {
                    Entry& e = *snapshot[i];
                    bool wasUndone = e.undone; // capture BEFORE drawing — must stay consistent for Begin/EndDisabled

                    if (wasUndone)
                        ImGui::TextDisabled("%s", e.display.c_str());
                    else
                        ImGui::TextUnformatted(e.display.c_str());

                    if (e.isUndoable())
                    {
                        ImGui::SameLine();
                        std::string uid = std::to_string(i);

                        if (wasUndone) ImGui::BeginDisabled();
                        if (ImGui::SmallButton(("U##" + uid).c_str()))
                        {
                            std::lock_guard<std::mutex> lock(mtx);
                            e.undone = true;
                            e.undoOrder = ++undoCounter;
                            SendUndo(e);
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                            ImGui::SetTooltip(TR(u8"Undo"));
                        if (wasUndone) ImGui::EndDisabled();

                        ImGui::SameLine();

                        if (!wasUndone) ImGui::BeginDisabled();
                        if (ImGui::SmallButton(("R##" + uid).c_str()))
                        {
                            std::lock_guard<std::mutex> lock(mtx);
                            e.undone = false;
                            e.undoOrder = 0;
                            SendRedo(e);
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                            ImGui::SetTooltip(TR(u8"Redo"));
                        if (!wasUndone) ImGui::EndDisabled();
                    }
                }
            }

            ImGui::EndChild();
        }
        ImGui::End();
    }

}
