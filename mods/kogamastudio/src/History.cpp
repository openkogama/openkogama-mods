#include "stdafx.h"
#include "History.h"
#include "MenuHelpers.h"
#include "UILocale.h"
#include <vector>
#include <string>
#include <mutex>
#include "IconsFontAwesome6.h"
#include "ui.h"

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

    static const char* IconOf(const Entry& e)
    {
        if (e.hasPosition) return ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT;
        if (e.hasRotation) return ICON_FA_ROTATE;
        if (e.hasLinkAdded || e.hasObjLinkAdded) return ICON_FA_LINK;
        if (e.hasLinkRemoved || e.hasObjLinkRemoved) return ICON_FA_LINK_SLASH;
        return ICON_FA_CIRCLE_INFO;
    }

    bool CanUndo()
    {
        std::lock_guard<std::mutex> lock(mtx);
        for (const auto& e : entries)
            if (e.isUndoable() && !e.undone) return true;
        return false;
    }

    bool CanRedo()
    {
        std::lock_guard<std::mutex> lock(mtx);
        for (const auto& e : entries)
            if (e.isUndoable() && e.undone) return true;
        return false;
    }

    void Undo()
    {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto& e : entries)
        {
            if (!e.isUndoable() || e.undone) continue;
            e.undone = true;
            e.undoOrder = ++undoCounter;
            SendUndo(e);
            return;
        }
    }

    void Redo()
    {
        std::lock_guard<std::mutex> lock(mtx);
        Entry* target = nullptr;
        for (auto& e : entries)
            if (e.isUndoable() && e.undone && (!target || e.undoOrder > target->undoOrder)) target = &e;
        if (!target) return;
        target->undone = false;
        target->undoOrder = 0;
        SendRedo(*target);
    }

    void Render()
    {
        if (ui::BeginPanel(u8"History", ICON_FA_CLOCK, TR(u8"History"), nullptr))
        {
            ImGui::BeginDisabled(!CanUndo());
            if (ui::IconButton(u8"undo", ICON_FA_ROTATE_LEFT, TR(u8"Undo"))) Undo();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!CanRedo());
            if (ui::IconButton(u8"redo", ICON_FA_ROTATE_RIGHT, TR(u8"Redo"))) Redo();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(entries.empty());
            if (ui::IconButton(u8"clear", ICON_FA_TRASH, TR(u8"Clear")))
            {
                std::lock_guard<std::mutex> lock(mtx);
                entries.clear();
                undoCounter = 0;
            }
            ImGui::EndDisabled();

            ImGui::BeginChild(u8"##entries");
            std::lock_guard<std::mutex> lock(mtx);
            if (entries.empty()) ui::EmptyState(TR(u8"No actions"));
            float button = ImGui::GetFrameHeight();
            for (int i = 0; i < (int)entries.size(); ++i)
            {
                Entry& e = entries[i];
                ImGui::PushID(i);
                bool undone = e.undone;
                if (undone) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted((std::string(IconOf(e)) + u8"  " + e.display).c_str());
                if (undone) ImGui::PopStyleColor();
                if (e.isUndoable())
                {
                    ImGui::SameLine(ImGui::GetContentRegionMax().x - button);
                    if (undone ? ui::IconButton(u8"redo", ICON_FA_ROTATE_RIGHT, TR(u8"Redo")) : ui::IconButton(u8"undo", ICON_FA_ROTATE_LEFT, TR(u8"Undo")))
                    {
                        e.undone = !undone;
                        e.undoOrder = undone ? 0 : ++undoCounter;
                        if (undone) SendRedo(e);
                        else SendUndo(e);
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        ui::EndPanel();
    }
}
