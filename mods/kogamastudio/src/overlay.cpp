#include "stdafx.h"
#include "overlay.h"
#include "pipe.h"
#include "Appearance.h"
#include "IconsFontAwesome6.h"
#include "fa_compressed.h"
#include <dxgi1_2.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <atomic>
#include <thread>
#include <algorithm>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace hooks_dx11 {
    ID3D11Device* gDevice = nullptr;
    ID3D11DeviceContext* gContext = nullptr;

    ID3D11ShaderResourceView* GetGameFrameSRV() { return nullptr; }

    void GetGameFrameSize(UINT& width, UINT& height) {
        RECT client{};
        GetClientRect(globals::mainWindow, &client);
        width = client.right - client.left;
        height = client.bottom - client.top;
    }
}

namespace overlay {
    namespace {
        constexpr wchar_t ClassName[] = L"OpenKogamaStudio";
        constexpr float FontSize = 13.0f;
        constexpr UINT_PTR LiveTimer = 1;
        constexpr UINT LiveMilliseconds = 16;
        constexpr LONG Grip = 5;
        constexpr LONG MinWidth = 640;
        constexpr LONG MinHeight = 400;
        constexpr int MaxTries = 3;
        constexpr int SettleFrames = 6;
        constexpr DWORD IdleMilliseconds = 250;
        constexpr DWORD HiddenMilliseconds = 30;

        HWND game = nullptr;
        HWND window = nullptr;
        IDXGISwapChain1* chain = nullptr;
        ID3D11RenderTargetView* target = nullptr;
        IDCompositionDevice* composition = nullptr;
        IDCompositionTarget* compositionTarget = nullptr;
        IDCompositionVisual* visual = nullptr;
        std::atomic<bool> running = false;
        std::atomic<bool> failed = false;
        std::string iniPath;
        std::string title;
        RECT placed{};
        RECT hole{};
        RECT cut{};
        RECT given{};
        RECT start{};
        RECT restored{};
        WINDOWPLACEMENT home{sizeof(WINDOWPLACEMENT)};
        std::atomic<bool> docked = false;
        std::atomic<bool> leaving = false;
        bool maximized = false;
        int tries = 0;
        int pending = SettleFrames;
        bool shaped = false;
        bool shown = false;
        bool typing = false;

        RECT ScreenRect(HWND hwnd) {
            RECT client{};
            GetClientRect(hwnd, &client);
            POINT corner{0, 0};
            ClientToScreen(hwnd, &corner);
            return {corner.x, corner.y, corner.x + client.right, corner.y + client.bottom};
        }

        RECT GameRect() { return ScreenRect(game); }

        RECT VisibleFrame(HWND hwnd) {
            RECT bounds{};
            if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds)))) GetWindowRect(hwnd, &bounds);
            return bounds;
        }

        RECT WorkArea() {
            MONITORINFO info{sizeof(info)};
            GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info);
            return info.rcWork;
        }

        RECT Margins(const RECT& outer, const RECT& inner) {
            return {inner.left - outer.left, inner.top - outer.top, outer.right - inner.right, outer.bottom - inner.bottom};
        }

        RECT Outer(HWND hwnd) {
            RECT outer{};
            GetWindowRect(hwnd, &outer);
            return outer;
        }

        RECT Grow(const RECT& rect, const RECT& by) {
            return {rect.left - by.left, rect.top - by.top, rect.right + by.right, rect.bottom + by.bottom};
        }

        void Place(HWND hwnd, const RECT& rect, UINT flags) {
            SetWindowPos(hwnd, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE | flags);
        }

        void CreateTarget() {
            ID3D11Texture2D* buffer = nullptr;
            if (SUCCEEDED(chain->GetBuffer(0, IID_PPV_ARGS(&buffer)))) {
                hooks_dx11::gDevice->CreateRenderTargetView(buffer, nullptr, &target);
                buffer->Release();
            }
        }

        bool CreateDevice(UINT width, UINT height) {
            D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
            if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2,
                    D3D11_SDK_VERSION, &hooks_dx11::gDevice, nullptr, &hooks_dx11::gContext)))
                return false;
            IDXGIDevice* dxgi = nullptr;
            IDXGIAdapter* adapter = nullptr;
            IDXGIFactory2* factory = nullptr;
            bool ready = SUCCEEDED(hooks_dx11::gDevice->QueryInterface(IID_PPV_ARGS(&dxgi)))
                && SUCCEEDED(dxgi->GetAdapter(&adapter))
                && SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory)));
            if (ready) {
                DXGI_SWAP_CHAIN_DESC1 desc{};
                desc.Width = width;
                desc.Height = height;
                desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                desc.BufferCount = 2;
                desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
                desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
                ready = SUCCEEDED(factory->CreateSwapChainForComposition(hooks_dx11::gDevice, &desc, nullptr, &chain))
                    && SUCCEEDED(DCompositionCreateDevice(dxgi, IID_PPV_ARGS(&composition)))
                    && SUCCEEDED(composition->CreateTargetForHwnd(window, TRUE, &compositionTarget))
                    && SUCCEEDED(composition->CreateVisual(&visual))
                    && SUCCEEDED(visual->SetContent(chain))
                    && SUCCEEDED(compositionTarget->SetRoot(visual))
                    && SUCCEEDED(composition->Commit());
            }
            if (factory) factory->Release();
            if (adapter) adapter->Release();
            if (dxgi) dxgi->Release();
            if (ready) CreateTarget();
            return ready && target;
        }

        void Resize(UINT width, UINT height) {
            if (target) {
                target->Release();
                target = nullptr;
            }
            chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
            CreateTarget();
        }

        void SetupImGui() {
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            char localAppData[MAX_PATH] = {};
            SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData);
            std::string configDir = std::string(localAppData) + "\\KogamaStudio\\Config";
            SHCreateDirectoryExA(NULL, configDir.c_str(), NULL);
            iniPath = configDir + "\\studio.ini";
            io.IniFilename = iniPath.c_str();
            io.IniSavingRate = 5.0f;

            Appearance::LoadSettings();
            static const ImWchar glyphRanges[] = {0x0020, 0x00FF, 0x0100, 0x017F, 0x0400, 0x04FF, 0};
            float size = Appearance::fontSize > 0 ? Appearance::fontSize : FontSize;
            bool custom = !Appearance::fontPath.empty() && Appearance::fontSize > 0
                && GetFileAttributesA(Appearance::fontPath.c_str()) != INVALID_FILE_ATTRIBUTES;
            const char* fontPath = custom ? Appearance::fontPath.c_str() : "C:\\Windows\\Fonts\\consola.ttf";
            if (!io.Fonts->AddFontFromFileTTF(fontPath, custom ? size : FontSize, nullptr, glyphRanges))
                io.Fonts->AddFontDefault();

            ImFontConfig icons;
            icons.MergeMode = true;
            icons.PixelSnapH = true;
            static const ImWchar iconRanges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
            io.Fonts->AddFontFromMemoryCompressedBase85TTF(FA_Solid_compressed_data_base85, size, &icons, iconRanges);

            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            ImGui::StyleColorsDark();
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
            ImGui_ImplWin32_Init(window);
            ImGui_ImplDX11_Init(hooks_dx11::gDevice, hooks_dx11::gContext);
        }

        bool Follow() {
            RECT now = docked ? ScreenRect(window) : GameRect();
            int width = now.right - now.left, height = now.bottom - now.top;
            if (width <= 0 || height <= 0 || EqualRect(&now, &placed)) return false;
            bool resized = width != placed.right - placed.left || height != placed.bottom - placed.top;
            placed = now;
            if (!docked) Place(window, now, 0);
            if (!resized) return true;
            Resize(width, height);
            shaped = false;
            return true;
        }

        void Cut() {
            RECT wanted{(LONG)menu::viewportImageMin.x, (LONG)menu::viewportImageMin.y, (LONG)menu::viewportImageMax.x, (LONG)menu::viewportImageMax.y};
            if (!pipe::openMenu) SetRectEmpty(&wanted);
            hole = wanted;
            RECT area = GameRect(), opening{};
            OffsetRect(&area, -placed.left, -placed.top);
            IntersectRect(&opening, &hole, &area);
            if (shaped && EqualRect(&opening, &cut)) return;
            shaped = true;
            cut = opening;
            HRGN region = CreateRectRgn(0, 0, placed.right - placed.left, placed.bottom - placed.top);
            if (!IsRectEmpty(&cut)) {
                HRGN gap = CreateRectRgn(cut.left, cut.top, cut.right, cut.bottom);
                CombineRgn(region, region, gap, RGN_DIFF);
                DeleteObject(gap);
            }
            SetWindowRgn(window, region, TRUE);
        }

        void Undock() {
            if (!docked.exchange(false)) return;
            SetRectEmpty(&given);
            RECT frame = ScreenRect(window);
            if (EqualRect(&frame, &start)) SetWindowPlacement(game, &home);
            else {
                RECT outer = Outer(game);
                Place(game, Grow(frame, Margins(outer, VisibleFrame(game))), 0);
                if (maximized) ShowWindowAsync(game, SW_MAXIMIZE);
            }
            maximized = false;
        }

        void Dock() {
            if (leaving || IsIconic(game)) return;
            if (!pipe::openMenu || !menu::isOpen) {
                Undock();
                return;
            }
            if (IsRectEmpty(&hole)) return;
            if (!docked) {
                if (!GetWindowPlacement(game, &home)) return;
                RECT frame = VisibleFrame(game);
                maximized = IsZoomed(game) != 0;
                if (maximized) ShowWindowAsync(game, SW_SHOWNOACTIVATE);
                start = frame;
                SetRectEmpty(&restored);
                SetRectEmpty(&given);
                docked = true;
                Place(window, frame, 0);
                pending = SettleFrames;
                return;
            }
            if (IsZoomed(game)) return;
            RECT wanted = hole;
            OffsetRect(&wanted, placed.left, placed.top);
            if (!EqualRect(&wanted, &given)) {
                given = wanted;
                tries = 0;
            }
            RECT client = GameRect();
            if (EqualRect(&client, &wanted) || tries >= MaxTries) return;
            tries++;
            Place(game, Grow(wanted, Margins(Outer(game), client)), SWP_ASYNCWINDOWPOS);
            pending = SettleFrames;
        }

        RECT NormalFrame() {
            RECT normal = home.rcNormalPosition;
            MONITORINFO info{sizeof(info)};
            GetMonitorInfoW(MonitorFromRect(&normal, MONITOR_DEFAULTTONEAREST), &info);
            OffsetRect(&normal, info.rcWork.left - info.rcMonitor.left, info.rcWork.top - info.rcMonitor.top);
            RECT outer = Outer(game);
            RECT margins = Margins(outer, VisibleFrame(game));
            return {normal.left + margins.left, normal.top + margins.top, normal.right - margins.right, normal.bottom - margins.bottom};
        }

        void Show(bool visible) {
            if (visible == shown) return;
            shown = visible;
            ShowWindow(window, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
        }

        void Focus() {
            bool wants = ImGui::GetIO().WantTextInput;
            if (wants == typing) return;
            typing = wants;
            SetForegroundWindow(wants ? window : game);
        }

        void Present() {
            ImVec4 back = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
            float opacity = docked ? 1.0f : 0.0f;
            const float clear[4] = {back.x * opacity, back.y * opacity, back.z * opacity, opacity};
            hooks_dx11::gContext->OMSetRenderTargets(1, &target, nullptr);
            hooks_dx11::gContext->ClearRenderTargetView(target, clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            chain->Present(1, 0);
        }

        void Draw() {
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            menu::Init();
            ImGui::Render();
            bool visible = ((pipe::openMenu && menu::isOpen) || menu::welcomeOpen) && !IsIconic(game);
            if (!visible) {
                Dock();
                Show(false);
                return;
            }
            Cut();
            Dock();
            Focus();
            Present();
            Show(true);
            ImGuiIO& io = ImGui::GetIO();
            if (io.WantTextInput || ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left)) pending = (std::max)(pending, 1);
        }

        void Modifiers() {
            ImGuiIO& io = ImGui::GetIO();
            io.AddKeyEvent(ImGuiMod_Ctrl, (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
            io.AddKeyEvent(ImGuiMod_Shift, (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
            io.AddKeyEvent(ImGuiMod_Alt, (GetAsyncKeyState(VK_MENU) & 0x8000) != 0);
        }

        LRESULT HitTest(HWND hwnd, LPARAM lparam) {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            RECT bounds{};
            GetWindowRect(hwnd, &bounds);
            if (!maximized) {
                bool left = point.x < bounds.left + Grip, right = point.x >= bounds.right - Grip;
                bool top = point.y < bounds.top + Grip, bottom = point.y >= bounds.bottom - Grip;
                if (top && left) return HTTOPLEFT;
                if (top && right) return HTTOPRIGHT;
                if (bottom && left) return HTBOTTOMLEFT;
                if (bottom && right) return HTBOTTOMRIGHT;
                if (left) return HTLEFT;
                if (right) return HTRIGHT;
                if (top) return HTTOP;
                if (bottom) return HTBOTTOM;
            }
            ScreenToClient(hwnd, &point);
            if (menu::captionDrag && point.y >= 0 && point.y < (LONG)menu::menuBarHeight) return HTCAPTION;
            return HTCLIENT;
        }

        LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
            switch (message) {
            case WM_NCCALCSIZE:
                return 0;
            case WM_NCHITTEST:
                if (docked) return HitTest(hwnd, lparam);
                break;
            case WM_NCLBUTTONDBLCLK:
                if (wparam == HTCAPTION) {
                    ToggleMaximize();
                    return 0;
                }
                break;
            case WM_GETMINMAXINFO:
                if (!docked) break;
                reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = {MinWidth, MinHeight};
                return 0;
            case WM_ENTERSIZEMOVE:
                SetTimer(hwnd, LiveTimer, LiveMilliseconds, nullptr);
                break;
            case WM_EXITSIZEMOVE: {
                KillTimer(hwnd, LiveTimer);
                RECT now{}, work = WorkArea();
                GetWindowRect(hwnd, &now);
                if (maximized && !EqualRect(&now, &work)) maximized = false;
                break;
            }
            case WM_TIMER:
                if (wparam == LiveTimer) {
                    Follow();
                    Draw();
                    return 0;
                }
                break;
            }
            if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST && GetForegroundWindow() != hwnd) Modifiers();
            if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam)) return 1;
            if (message == WM_KEYDOWN && wparam == VK_F2) {
                pipe::ProcessCommand("key_down|F2");
                return 0;
            }
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }

        void Run() {
            WNDCLASSEXW cls{sizeof(cls)};
            cls.lpfnWndProc = Procedure;
            cls.hInstance = globals::mainModule;
            cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
            cls.lpszClassName = ClassName;
            cls.style = CS_DBLCLKS;
            RegisterClassExW(&cls);

            wchar_t caption[256] = {};
            int length = GetWindowTextW(game, caption, 256);
            int bytes = WideCharToMultiByte(CP_UTF8, 0, caption, length, nullptr, 0, nullptr, nullptr);
            title.assign(bytes, '\0');
            WideCharToMultiByte(CP_UTF8, 0, caption, length, title.data(), bytes, nullptr, nullptr);

            placed = GameRect();
            int width = (std::max)(1L, placed.right - placed.left), height = (std::max)(1L, placed.bottom - placed.top);
            window = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, ClassName, L"KogamaStudio", WS_POPUP | WS_THICKFRAME,
                placed.left, placed.top, width, height, game, nullptr, globals::mainModule, nullptr);
            if (!window || !CreateDevice(width, height)) {
                DebugLog("[overlay] could not create the overlay window\n");
                if (window) DestroyWindow(window);
                window = nullptr;
                failed = true;
                running = false;
                return;
            }
            SetupImGui();
            DebugLog("[overlay] ready\n");

            MSG message;
            int drawn = 0;
            ULONGLONG countedAt = GetTickCount64();
            while (running && IsWindow(game)) {
                if (pending <= 0) MsgWaitForMultipleObjects(0, nullptr, FALSE, shown ? IdleMilliseconds : HiddenMilliseconds, QS_ALLINPUT);
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                    pending = SettleFrames;
                }
                try {
                    if (pipe::ProcessPending()) pending = (std::max)(pending, 1);
                    if (Follow()) pending = SettleFrames;
                    if (pending <= 0 && !shown) continue;
                    if (pending > 0) pending--;
                    Draw();
                    drawn++;
                    if (GetTickCount64() - countedAt >= 10000) {
                        if (shown) DebugLog("[overlay] %d frames in 10 s\n", drawn);
                        drawn = 0;
                        countedAt = GetTickCount64();
                    }
                }
                catch (const std::exception& error) {
                    DebugLog("[overlay] stopped: %s\n", error.what());
                    Undock();
                    ShowWindow(window, SW_HIDE);
                    failed = true;
                    break;
                }
            }
            running = false;
        }
    }

    void Start(HWND gameWindow) {
        if (running.exchange(true)) return;
        game = gameWindow;
        globals::mainWindow = gameWindow;
        std::thread(Run).detach();
    }

    bool Running() { return running; }

    bool Failed() { return failed; }

    void Release() {
        leaving = true;
        if (docked.exchange(false)) SetWindowPlacement(game, &home);
    }

    void Minimize() { ShowWindowAsync(game, SW_MINIMIZE); }

    void ToggleMaximize() {
        if (!docked) return;
        RECT next = IsRectEmpty(&restored) ? NormalFrame() : restored;
        if (!maximized) {
            GetWindowRect(window, &restored);
            next = WorkArea();
        }
        maximized = !maximized;
        Place(window, next, 0);
    }

    void Close() {
        Undock();
        leaving = true;
        PostMessageW(game, WM_CLOSE, 0, 0);
    }

    bool Maximized() { return maximized; }

    const char* Title() { return title.c_str(); }
}
