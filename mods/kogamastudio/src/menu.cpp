#include "stdafx.h"
#include "imgui/imgui_internal.h"
#include "ResourcePackLoader.h"
#include "pipe.h"
#include "MenuHelpers.h"
#include <chrono>
#include <thread>
#include <filesystem>
#include "Tools.h"
#include "ResourcePacks.h"
#include "About.h"
#include "Translate.h"
#include "Properties.h"
#include "Clipboard.h"
#include "Appearance.h"
#include "Console.h"
#include "Players.h"
#include "Camera.h"
#include "Explorer.h"
#include "GameInfo.h"
#include "Inventory.h"
#include "History.h"
#include "Worlds.h"
#include "Welcome.h"
#include "Blueprints.h"
#include "Performance.h"
#include "overlay.h"

namespace menu {
    bool isOpen = false;
    bool viewportHovered = false;
    bool blockMouseInput = false;
    bool welcomeOpen = false;
    ImVec2 viewportImageMin = {};
    ImVec2 viewportImageMax = {};
    float menuBarHeight = 0.0f;
    bool captionDrag = false;
    float test = 0.0f;
    static bool noTitleBar = false;
    static HMODULE hModule = nullptr;

    namespace Windows {
        static bool settingsOpen = false;
        static bool consoleOpen = false;
    }

    enum class Glyph { Minimize, Maximize, Restore, Close };

    static bool CaptionButton(const char* id, Glyph glyph, float width, float height)
    {
        ImVec2 at = ImGui::GetCursorScreenPos();
        bool pressed = ImGui::InvisibleButton(id, ImVec2(width, height));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (ImGui::IsItemHovered())
        {
            ImU32 fill = glyph == Glyph::Close ? IM_COL32(232, 17, 35, 255) : ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered);
            draw->AddRectFilled(at, ImVec2(at.x + width, at.y + height), fill);
        }
        ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
        float size = (float)(int)(height * 0.22f);
        float x = (float)(int)(at.x + width * 0.5f) + 0.5f, y = (float)(int)(at.y + height * 0.5f) + 0.5f;
        switch (glyph)
        {
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

    static void Caption()
    {
        float height = ImGui::GetWindowHeight();
        float button = (float)(int)(height * 1.75f);
        float width = ImGui::GetWindowDrawList()->GetClipRectMax().x - ImGui::GetWindowPos().x;
        float buttons = button * 3.0f;
        const char* title = overlay::Title();
        ImVec2 text = ImGui::CalcTextSize(title);
        float x = (width - text.x) * 0.5f;
        if (*title && x > ImGui::GetCursorPosX() && x + text.x < width - buttons)
        {
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

    void Init() {
        static float scale = 1.0f;

        static bool initializationNotification = false;

        if (!hModule) {
            hModule = GetModuleHandle(u8"KogamaStudio.dll");
            if (hModule) {
                DebugLog(u8"[menu] KogamaStudio.dll loaded\n");
            }
        }

        ImGuiIO& io = ImGui::GetIO();


        // Style setup (one-time)
        static bool styled = false;
        if (!styled) {
            Appearance::LoadSettings();
            ImGui::GetStyle().ScaleAllSizes(scale);
            styled = true;
            DebugLog(u8"[menu] Style applied.\n");
        }

        // Welcome window on first run (shows before F2)
        Welcome::Init();
        welcomeOpen = Welcome::IsOpen();
        if (welcomeOpen) {
            Welcome::Render();
            return;
        }

        if (!isOpen) {
            viewportHovered = false;
            return;
        }

        if (!pipe::openMenu) {
            viewportImageMin = {};
            viewportImageMax = {};
            return;
        }

        if (typing) {
            io.WantCaptureKeyboard = true;
        }
        else {
            io.WantCaptureKeyboard = false;
        }
        typing = false;

        ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);
        ImGui::SetNextWindowViewport(mainViewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin(u8"##DockSpaceHost", nullptr,
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
        ImGui::PopStyleVar(3);
        ImGuiID dockId = ImGui::GetID(u8"MainDockSpace");
        ImGui::DockSpace(dockId, ImVec2(0.0f, 0.0f));

        static bool dockLayoutBuilt = false;
        if (!dockLayoutBuilt)
        {
            dockLayoutBuilt = true;
            if (ImGui::DockBuilderGetNode(dockId) != nullptr)
                goto dockLayoutDone; // layout loaded from ini, skip default build
            ImGui::DockBuilderRemoveNode(dockId);
            ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockId, mainViewport->WorkSize);

            ImGuiID dockRight, dockLeft;
            ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Right, 0.32f, &dockRight, &dockLeft);

            ImGuiID dockRightTop, dockRightBot;
            ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.5f, &dockRightBot, &dockRightTop);

            ImGuiID dockRightMid, dockRightBottom;
            ImGui::DockBuilderSplitNode(dockRightBot, ImGuiDir_Down, 0.5f, &dockRightBottom, &dockRightMid);

            ImGui::DockBuilderDockWindow(u8"###Viewport",      dockLeft);
            // right top: explorer + tools
            ImGui::DockBuilderDockWindow(u8"###Explorer",      dockRightTop);
            ImGui::DockBuilderDockWindow(u8"###Tools",         dockRightTop);
            // right mid: properties + clipboard + history
            ImGui::DockBuilderDockWindow(u8"###Properties",    dockRightMid);
            ImGui::DockBuilderDockWindow(u8"###Clipboard",     dockRightMid);
            ImGui::DockBuilderDockWindow(u8"###History",       dockRightMid);
            // right bottom: secondary panels
            ImGui::DockBuilderDockWindow(u8"###GameInfo",      dockRightBottom);
            ImGui::DockBuilderDockWindow(u8"###Inventory",     dockRightBottom);
            ImGui::DockBuilderDockWindow(u8"###Players",       dockRightBottom);
            ImGui::DockBuilderDockWindow(u8"###Camera",        dockRightBottom);
            ImGui::DockBuilderDockWindow(u8"###ResourcePacks", dockRightBottom);
            ImGui::DockBuilderDockWindow(u8"###Translate",     dockRightBottom);
            ImGui::DockBuilderDockWindow(u8"###About",         dockRightBottom);
            ImGui::DockBuilderDockWindow(u8"###Console",       dockRightBottom);
            ImGui::DockBuilderDockWindow(u8"###Worlds",        dockRightBottom);
            ImGui::DockBuilderDockWindow(u8"###Blueprints",    dockLeft);
            ImGui::DockBuilderDockWindow(u8"###Performance",   dockRightBottom);
            ImGui::DockBuilderFinish(dockId);
            dockLayoutDone:;
        }

        ImGui::End();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        if (ImGui::Begin((std::string(TR(u8"Viewport")) + u8"###Viewport").c_str(), nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImVec2 contentSize = ImGui::GetContentRegionAvail();
            ImVec2 contentOrigin = ImGui::GetCursorScreenPos();
            if (contentSize.x > 0 && contentSize.y > 0)
            {
                // Letterbox: scale game frame to fit panel while preserving aspect ratio
                UINT frameW = 0, frameH = 0;
                hooks_dx11::GetGameFrameSize(frameW, frameH);
                float aspect = (frameW > 0 && frameH > 0) ? (float)frameW / (float)frameH : 16.0f / 9.0f;

                float imgW = contentSize.x;
                float imgH = contentSize.x / aspect;
                if (imgH > contentSize.y) { imgH = contentSize.y; imgW = contentSize.y * aspect; }

                float offX = (contentSize.x - imgW) * 0.5f;
                float offY = (contentSize.y - imgH) * 0.5f;

                ID3D11ShaderResourceView* srv = hooks_dx11::GetGameFrameSRV();
                if (srv)
                {
                    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + offX, ImGui::GetCursorPosY() + offY));
                    ImGui::Image((ImTextureID)(void*)srv, ImVec2(imgW, imgH));
                    viewportHovered = ImGui::IsItemHovered();
                    viewportImageMin = ImGui::GetItemRectMin();
                    viewportImageMax = ImGui::GetItemRectMax();
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(u8"INVENTORY_ITEM"))
                            SendCommand((std::string(u8"world_place_item|") + (const char*)payload->Data).c_str());
                        ImGui::EndDragDropTarget();
                    }
                }
                else
                {
                    viewportHovered = ImGui::IsWindowHovered();
                    viewportImageMin = contentOrigin;
                    viewportImageMax = ImVec2(contentOrigin.x + contentSize.x, contentOrigin.y + contentSize.y);
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();

        // --- Main panels (all manage their own Begin/End) ---
        bool playLocked = (GameInfo::gameMode == "Play" && !Appearance::IsExtendedActive());

        if (!playLocked) {
            Explorer::Render();
            Tools::Render();
            Properties::Render();
            Clipboard::Render();
            History::Render();
            Inventory::Render();
            Players::Render();
            Worlds::Render();
            Blueprints::Render();
        }

        GameInfo::Render();
        CameraPanel::Render();
        ResourcePacks::Render();
        Translate::Render();
        About::Render();
        Performance::Render();

        // Block viewport input when Blueprints panel is hovered/focused
        if (Blueprints::g_wantBlockViewport)
            viewportHovered = false;

        static bool prevViewportHovered = false;
        if (viewportHovered != prevViewportHovered)
        {
            prevViewportHovered = viewportHovered;
            SendCommand(viewportHovered ? u8"viewport_cursor|false" : u8"viewport_cursor|true");
        }

        if (ImGui::BeginMainMenuBar())
        {
            menuBarHeight = ImGui::GetWindowSize().y;
            if (ImGui::MenuItem(TR(u8"Settings")))
                Windows::settingsOpen = !Windows::settingsOpen;
            if (ImGui::MenuItem(TR(u8"Console")))
                Windows::consoleOpen = !Windows::consoleOpen;
            Caption();
            ImGui::EndMainMenuBar();
        }

        if (Windows::settingsOpen) {
            ImGui::OpenPopup(u8"##SettingsModal");
            Windows::settingsOpen = false;
        }
        ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(u8"##SettingsModal", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
            // Header
            ImGui::Text(TR(u8"Settings"));
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
            if (ImGui::SmallButton(u8"X"))
                ImGui::CloseCurrentPopup();
            ImGui::Separator();
            Appearance::Render(true);
            ImGui::EndPopup();
        }
        if (Windows::consoleOpen) AppLog::Render();

    }
}
