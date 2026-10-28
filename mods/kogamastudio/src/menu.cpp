#include "stdafx.h"
#include "imgui/imgui_internal.h"
#include "pipe.h"
#include "MenuHelpers.h"
#include "IconsFontAwesome6.h"
#include "ui.h"
#include "Tools.h"
#include "Properties.h"
#include "Clipboard.h"
#include "Appearance.h"
#include "Console.h"
#include "Players.h"
#include "Explorer.h"
#include "GameInfo.h"
#include "Inventory.h"
#include "History.h"
#include "Welcome.h"
#include "Blueprints.h"
#include "overlay.h"
#include "version.h"
#include <algorithm>

namespace menu {
    bool isOpen = false;
    bool viewportHovered = false;
    bool blockMouseInput = false;
    bool welcomeOpen = false;
    ImVec2 viewportImageMin = {};
    ImVec2 viewportImageMax = {};
    float menuBarHeight = 0.0f;
    bool captionDrag = false;

    namespace {
        constexpr const char* DockName = u8"StudioDock";

        enum class Glyph { Minimize, Maximize, Restore, Close };

        struct Panel {
            const char* window;
            const char* title;
            void (*render)();
            bool open;
        };

        void RenderConsole() { AppLog::Render(); }

        Panel panels[] = {
            {u8"###Explorer",      u8"Hierarchy",      Explorer::Render,     true},
            {u8"###Properties",    u8"Inspector",      Properties::Render,   true},
            {u8"###Tools",         u8"Build",          Tools::Render,        true},
            {u8"###Inventory",     u8"Assets",         Inventory::Render,    true},
            {u8"###Clipboard",     u8"Clipboard",      Clipboard::Render,    true},
            {u8"###History",       u8"History",        History::Render,      true},
            {u8"###Players",       u8"Players",        Players::Render,      true},
            {u8"###Blueprints",    u8"Logic",          Blueprints::Render,   true},
            {u8"###Console",       u8"Console",        RenderConsole,        false},
        };

        bool layoutReset = false;
        int focusViewport = 3;
        bool settingsRequested = false;
        bool aboutRequested = false;

        bool CaptionButton(const char* id, Glyph glyph, float width, float height) {
            ImVec2 at = ImGui::GetCursorScreenPos();
            bool pressed = ImGui::InvisibleButton(id, ImVec2(width, height));
            ImDrawList* draw = ImGui::GetWindowDrawList();
            if (ImGui::IsItemHovered()) {
                ImU32 fill = glyph == Glyph::Close ? IM_COL32(232, 17, 35, 255) : ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered);
                draw->AddRectFilled(at, ImVec2(at.x + width, at.y + height), fill);
            }
            ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
            float size = (float)(int)(height * 0.22f);
            float x = (float)(int)(at.x + width * 0.5f) + 0.5f, y = (float)(int)(at.y + height * 0.5f) + 0.5f;
            switch (glyph) {
            case Glyph::Minimize:
                draw->AddLine(ImVec2(x - size, y), ImVec2(x + size + 1, y), ink);
                break;
            case Glyph::Maximize:
                draw->AddRect(ImVec2(x - size, y - size), ImVec2(x + size, y + size), ink);
                break;
            case Glyph::Restore:
                draw->AddRect(ImVec2(x - size + 2, y - size), ImVec2(x + size, y + size - 2), ink);
                draw->AddRectFilled(ImVec2(x - size, y - size + 2), ImVec2(x + size - 2, y + size), ImGui::GetColorU32(ImGuiCol_MenuBarBg));
                draw->AddRect(ImVec2(x - size, y - size + 2), ImVec2(x + size - 2, y + size), ink);
                break;
            case Glyph::Close:
                draw->AddLine(ImVec2(x - size, y - size), ImVec2(x + size, y + size), ink);
                draw->AddLine(ImVec2(x + size, y - size), ImVec2(x - size, y + size), ink);
                break;
            }
            return pressed;
        }

        void Caption() {
            float height = ImGui::GetWindowHeight();
            float button = (float)(int)(height * 1.75f);
            float width = ImGui::GetWindowDrawList()->GetClipRectMax().x - ImGui::GetWindowPos().x;
            float buttons = button * 3.0f;
            const char* title = overlay::Title();
            ImVec2 text = ImGui::CalcTextSize(title);
            float x = (width - text.x) * 0.5f;
            if (*title && x > ImGui::GetCursorPosX() && x + text.x < width - buttons) {
                ImGui::SetCursorPosX(x);
                ImGui::TextUnformatted(title);
            }
            ImGui::SetCursorPos(ImVec2(width - buttons, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));
            if (CaptionButton(u8"##minimize", Glyph::Minimize, button, height)) overlay::Minimize();
            if (CaptionButton(u8"##maximize", overlay::Maximized() ? Glyph::Restore : Glyph::Maximize, button, height)) overlay::ToggleMaximize();
            if (CaptionButton(u8"##close", Glyph::Close, button, height)) overlay::Close();
            ImGui::PopStyleVar();
            captionDrag = ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive();
        }

        void MenuBar() {
            if (!ImGui::BeginMainMenuBar()) return;
            menuBarHeight = ImGui::GetWindowSize().y;
            if (ImGui::BeginMenu(TR(u8"Edit"))) {
                if (ImGui::MenuItem(TR(u8"Undo"), nullptr, false, History::CanUndo())) History::Undo();
                if (ImGui::MenuItem(TR(u8"Redo"), nullptr, false, History::CanRedo())) History::Redo();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(TR(u8"View"))) {
                for (Panel& panel : panels)
                    ImGui::MenuItem(TR(panel.title), nullptr, &panel.open);
                ImGui::Separator();
                if (ImGui::MenuItem(TR(u8"Reset layout"))) layoutReset = true;
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem(TR(u8"Settings"))) settingsRequested = true;
            if (ImGui::BeginMenu(TR(u8"Help"))) {
                if (ImGui::MenuItem(TR(u8"About"))) aboutRequested = true;
                ImGui::EndMenu();
            }
            Caption();
            ImGui::EndMainMenuBar();
        }

        void StatusBar() {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            float height = ImGui::GetFrameHeight();
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
            if (ImGui::BeginViewportSideBar(u8"##status", viewport, ImGuiDir_Down, height, flags)) {
                if (ImGui::BeginMenuBar()) {
                    ImGui::TextDisabled(u8"%s", GameInfo::gameMode.empty() ? u8"-" : GameInfo::gameMode.c_str());
                    if (ImGui::BeginItemTooltip()) {
                        if (ui::BeginProperties(u8"##session")) {
                            ui::Property(TR(u8"Player"));
                            ImGui::TextUnformatted(GameInfo::localPlayer.c_str());
                            ui::Property(TR(u8"Profile ID"));
                            ImGui::Text(u8"%d", GameInfo::profileID);
                            ui::Property(TR(u8"Planet ID"));
                            ImGui::Text(u8"%d", GameInfo::planetID);
                            ui::Property(TR(u8"Server IP"));
                            ImGui::TextUnformatted(GameInfo::serverIP.c_str());
                            ui::Property(TR(u8"Region"));
                            ImGui::TextUnformatted(GameInfo::region.c_str());
                            ui::Property(TR(u8"Language"));
                            ImGui::TextUnformatted(GameInfo::language.c_str());
                            ui::EndProperties();
                        }
                        ImGui::EndTooltip();
                    }
                    ImGui::Separator();
                    ImGui::TextDisabled(u8"%.0f %s", GameInfo::fps, TR(u8"FPS"));
                    ImGui::Separator();
                    ImGui::TextDisabled(u8"%d ms", GameInfo::ping);
                    ImGui::Separator();
                    ImGui::TextDisabled(u8"%s: %d", TR(u8"Objects"), GameInfo::worldObjects);
                    ImGui::Separator();
                    ImGui::TextDisabled(u8"%s: %d", TR(u8"Players"), GameInfo::playerCount);
                    const char* version = GameInfo::gameVersion.c_str();
                    float right = ImGui::CalcTextSize(version).x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
                    ImGui::SameLine(ImGui::GetWindowWidth() - right);
                    ImGui::TextDisabled(u8"%s", version);
                    ImGui::EndMenuBar();
                }
            }
            ImGui::End();
        }

        void BuildLayout(ImGuiID dock, ImVec2 size) {
            ImGui::DockBuilderRemoveNode(dock);
            ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dock, size);
            ImGuiID left, rest, right, center, rightTop, rightBottom, bottom, main;
            ImGui::DockBuilderSplitNode(dock, ImGuiDir_Left, 0.2f, &left, &rest);
            ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, 0.3f, &right, &center);
            ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.45f, &rightBottom, &rightTop);
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.3f, &bottom, &main);
            ImGui::DockBuilderDockWindow(u8"###Viewport", main);
            ImGui::DockBuilderDockWindow(u8"###Blueprints", main);
            ImGui::DockBuilderDockWindow(u8"###Explorer", left);
            ImGui::DockBuilderDockWindow(u8"###Properties", rightTop);
            ImGui::DockBuilderDockWindow(u8"###Tools", rightBottom);
            for (const char* window : {u8"###Inventory", u8"###Clipboard", u8"###History", u8"###Players", u8"###Console"})
                ImGui::DockBuilderDockWindow(window, bottom);
            ImGui::DockBuilderFinish(dock);
            focusViewport = 3;
        }

        void Dockspace() {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGuiID dock = ImGui::GetID(DockName);
            if (layoutReset || !ImGui::DockBuilderGetNode(dock)) {
                layoutReset = false;
                BuildLayout(dock, viewport->WorkSize);
            }
            ImGui::DockSpaceOverViewport(dock, viewport, ImGuiDockNodeFlags_NoWindowMenuButton);
        }

        void Viewport() {
            viewportImageMin = {};
            viewportImageMax = {};
            viewportHovered = false;
            if (focusViewport > 0) {
                ImGui::SetNextWindowFocus();
                focusViewport--;
            }
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            bool visible = ui::BeginPanel(u8"Viewport", ICON_FA_GAMEPAD, TR(u8"Viewport"), nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleVar();
            if (visible) {
                ImVec2 size = ImGui::GetContentRegionAvail();
                ImVec2 origin = ImGui::GetCursorScreenPos();
                if (size.x > 0 && size.y > 0) {
                    viewportImageMin = origin;
                    viewportImageMax = ImVec2(origin.x + size.x, origin.y + size.y);
                    viewportHovered = ImGui::IsWindowHovered();
                    ImGui::InvisibleButton(u8"##game", size);
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(u8"INVENTORY_ITEM"))
                            SendCommand((std::string(u8"world_place_item|") + (const char*)payload->Data).c_str());
                        ImGui::EndDragDropTarget();
                    }
                }
            }
            ui::EndPanel();
        }

        void Panels() {
            for (Panel& panel : panels)
                if (panel.open) panel.render();
        }

        void Modals() {
            if (settingsRequested) {
                ui::OpenModal(u8"Settings");
                settingsRequested = false;
            }
            if (aboutRequested) {
                ui::OpenModal(u8"About");
                aboutRequested = false;
            }
            if (ui::BeginModal(u8"Settings", TR(u8"Settings"), 620.0f)) {
                ImGui::BeginChild(u8"##settings", ImVec2(0.0f, (std::min)(380.0f, ImGui::GetMainViewport()->WorkSize.y * 0.55f)));
                Appearance::Render();
                ImGui::EndChild();
                Explorer::GroupingWarning();
                ui::ModalFooter(TR(u8"Close"));
                ui::EndModal();
            }
            if (ui::BeginModal(u8"About", TR(u8"About"), 360.0f)) {
                ImGui::TextUnformatted(u8"KogamaStudio v" KS_VERSION);
                ImGui::Spacing();
                ImGui::TextWrapped(u8"%s Becko, MauryDev, mindiro", TR(u8"Special Thanks to:"));
                ui::ModalFooter(TR(u8"Close"));
                ui::EndModal();
            }
        }

        void ViewportCursor() {
            if (Blueprints::g_wantBlockViewport) viewportHovered = false;
            static bool reported = false;
            if (viewportHovered == reported) return;
            reported = viewportHovered;
            SendCommand(viewportHovered ? u8"viewport_cursor|false" : u8"viewport_cursor|true");
        }
    }

    void ShowPanel(const char* window) {
        for (Panel& panel : panels)
            if (!strcmp(panel.window, window)) panel.open = true;
    }

    void Init() {
        static bool styled = false;
        if (!styled) {
            Appearance::LoadSettings();
            styled = true;
        }

        Welcome::Init();
        welcomeOpen = Welcome::IsOpen();
        if (welcomeOpen) {
            Welcome::Render();
            return;
        }

        if (!isOpen || !pipe::openMenu) {
            viewportHovered = false;
            viewportImageMin = {};
            viewportImageMax = {};
            return;
        }

        ImGui::GetIO().WantCaptureKeyboard = typing;
        typing = false;

        MenuBar();
        StatusBar();
        Dockspace();
        Viewport();
        Panels();
        Modals();
        ViewportCursor();
    }
}
