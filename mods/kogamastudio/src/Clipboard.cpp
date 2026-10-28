#include "Clipboard.h"
#include "Properties.h"
#include "MenuHelpers.h"
#include "pipe.h"
#include "IconsFontAwesome6.h"
#include "ui.h"
#include <commdlg.h>

using namespace menu;

namespace Clipboard {
    int cubeCount = 0;
    int offsetX = 0;
    int offsetY = 0;
    int offsetZ = 0;
    int rotationX = 0;
    int rotationY = 0;
    int rotationZ = 0;
    bool mirrorX = false;
    bool mirrorY = false;
    bool mirrorZ = false;
    int clipMinX = 0, clipMaxX = 0;
    int clipMinY = 0, clipMaxY = 0;
    int clipMinZ = 0, clipMaxZ = 0;

    namespace {
        constexpr const char* Axes = "xyz";
        bool clearBeforePaste = false;

        std::string ModelsFolder() {
            char folder[MAX_PATH] = {};
            SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, folder);
            std::string path = std::string(folder) + "\\KogamaStudio\\models";
            SHCreateDirectoryExA(NULL, path.c_str(), NULL);
            return path;
        }

        std::string PickFile(bool save) {
            char file[MAX_PATH] = "model.kscubes";
            if (!save) file[0] = 0;
            std::string folder = ModelsFolder();
            OPENFILENAMEA dialog = {};
            dialog.lStructSize = sizeof(dialog);
            dialog.lpstrFilter = "KS Cubes (*.kscubes)\0*.kscubes\0All Files\0*.*\0";
            dialog.lpstrFile = file;
            dialog.nMaxFile = MAX_PATH;
            dialog.lpstrInitialDir = folder.c_str();
            dialog.lpstrDefExt = "kscubes";
            dialog.Flags = save ? OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR : OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
            return (save ? GetSaveFileNameA(&dialog) : GetOpenFileNameA(&dialog)) ? std::string(file) : std::string();
        }

        void SendAxis(const char* prefix, int axis, const std::string& value) {
            SendCommand((std::string(prefix) + Axes[axis] + u8"|" + value).c_str());
        }

        void Axis3(const char* id, const char* prefix, int* values[3]) {
            int edited[3] = {*values[0], *values[1], *values[2]};
            ImGui::InputInt3(id, edited);
            if (!typing) typing = ImGui::IsItemActive();
            for (int axis = 0; axis < 3; axis++)
                if (edited[axis] != *values[axis]) {
                    *values[axis] = edited[axis];
                    SendAxis(prefix, axis, std::to_string(edited[axis]));
                }
        }

        void Toolbar(bool model) {
            ImGui::BeginDisabled(!model);
            if (ui::IconButton(u8"copy", ICON_FA_COPY, TR(u8"Copy cubes from the selected model"))) SendCommand(u8"clipboard_copy_model");
            ImGui::SameLine();
            ImGui::BeginDisabled(pipe::isPasteBuilding || cubeCount == 0);
            if (ui::IconButton(u8"paste", ICON_FA_PASTE, TR(u8"Paste cubes into the selected model"))) SendCommand(u8"clipboard_paste_model");
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(cubeCount == 0);
            if (ui::IconButton(u8"clear", ICON_FA_TRASH, TR(u8"Clear"))) SendCommand(u8"clipboard_clear");
            ImGui::SameLine();
            if (ui::IconButton(u8"save", ICON_FA_FLOPPY_DISK, TR(u8"Save .kscubes"))) {
                std::string file = PickFile(true);
                if (!file.empty()) SendCommand((u8"clipboard_save_to_file|" + file).c_str());
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::IconButton(u8"load", ICON_FA_FOLDER_OPEN, TR(u8"Load .kscubes"))) {
                std::string file = PickFile(false);
                if (!file.empty()) SendCommand((u8"clipboard_load_from_file|" + file).c_str());
            }
            ImGui::SameLine();
            if (cubeCount > 0) ImGui::TextDisabled(u8"%d %s", cubeCount, TR(u8"cubes"));
            else ImGui::TextDisabled(u8"%s", TR(u8"Empty"));
        }

        void Progress() {
            if (!pipe::isPasteBuilding) return;
            float progress = pipe::pasteTotalCubes > 0 ? (float)pipe::pastePlacedCubes / (float)pipe::pasteTotalCubes : 0.0f;
            char overlay[64];
            snprintf(overlay, sizeof(overlay), "%d / %d", pipe::pastePlacedCubes, pipe::pasteTotalCubes);
            float cancel = ImGui::CalcTextSize(TR(u8"Cancel")).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
            ImGui::ProgressBar(progress, ImVec2(-cancel, 0.0f), overlay);
            ImGui::SameLine();
            if (ui::Button(TR(u8"Cancel"), ui::Tone::Danger)) SendCommand(u8"clipboard_paste_cancel");
            if (pipe::pasteEtaSeconds >= 0.0f) {
                int left = (int)pipe::pasteEtaSeconds;
                if (left >= 60) ui::Hint((std::string(TR(u8"Time left:")) + u8" " + std::to_string(left / 60) + u8"m " + std::to_string(left % 60) + u8"s").c_str());
                else ui::Hint((std::string(TR(u8"Time left:")) + u8" " + std::to_string(left) + u8"s").c_str());
            }
        }

        void Transform() {
            if (!ui::Section(TR(u8"Paste")) || !ui::BeginProperties(u8"##paste")) return;
            ui::Property(TR(u8"Clear Model Before Paste"));
            if (ImGui::Checkbox(u8"##clear", &clearBeforePaste))
                SendCommand(clearBeforePaste ? u8"clipboard_clear_before_paste|true" : u8"clipboard_clear_before_paste|false");

            ui::Property(TR(u8"Offset"));
            int* offset[3] = {&offsetX, &offsetY, &offsetZ};
            Axis3(u8"##offset", u8"clipboard_offset_", offset);

            ui::Property(TR(u8"Rotation"));
            const char* turns[] = {u8"0°", u8"90°", u8"180°", u8"270°"};
            int* rotation[3] = {&rotationX, &rotationY, &rotationZ};
            float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemInnerSpacing.x * 2.0f) / 3.0f;
            for (int axis = 0; axis < 3; axis++) {
                if (axis) ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::SetNextItemWidth(width);
                ImGui::PushID(axis);
                if (ImGui::Combo(u8"##turn", rotation[axis], turns, 4)) SendAxis(u8"clipboard_rotation_", axis, std::to_string(*rotation[axis] * 90));
                ImGui::PopID();
            }

            ui::Property(TR(u8"Mirror"));
            bool* mirror[3] = {&mirrorX, &mirrorY, &mirrorZ};
            for (int axis = 0; axis < 3; axis++) {
                if (axis) ImGui::SameLine();
                char label[8];
                snprintf(label, sizeof(label), "%c##m", 'X' + axis);
                if (ImGui::Checkbox(label, mirror[axis])) SendAxis(u8"clipboard_mirror_", axis, *mirror[axis] ? u8"true" : u8"false");
            }

            ui::Property(TR(u8"Keep from"));
            int* low[3] = {&clipMinX, &clipMinY, &clipMinZ};
            Axis3(u8"##low", u8"clipboard_clip_min_", low);
            ui::Property(TR(u8"Keep to"));
            int* high[3] = {&clipMaxX, &clipMaxY, &clipMaxZ};
            Axis3(u8"##high", u8"clipboard_clip_max_", high);
            ui::EndProperties();
            ui::Hint(TR(u8"Keep from and to limit the pasted cubes on an axis. Leave both at 0 to keep everything."));
        }
    }

    void Render() {
        if (ui::BeginPanel(u8"Clipboard", ICON_FA_PASTE, TR(u8"Clipboard"), nullptr)) {
            Toolbar(Properties::isModel && Properties::targetObjectId != u8"-1");
            Progress();
            ImGui::Spacing();
            Transform();
        }
        ui::EndPanel();
    }
}
