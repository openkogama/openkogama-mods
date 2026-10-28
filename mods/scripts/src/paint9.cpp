#include "paint.h"
#include "runtime.h"
#include <d3d9.h>
#include <atomic>
#include <cstring>
#include <map>
#include <string>

namespace paint9 {
    namespace {
        using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
        using ResetFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
        using CreateFn = IDirect3D9*(WINAPI*)(UINT);
        using CreateExFn = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);

        constexpr int ResetSlot = 16;
        constexpr int PresentSlot = 17;
        constexpr DWORD VertexFormat = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;

        struct Vertex {
            float x, y, z;
            D3DCOLOR color;
            float u, v;
        };

        struct Texture {
            IDirect3DTexture9* texture = nullptr;
            int width = 0;
            int height = 0;
            std::vector<unsigned char> pixels;
            bool dirty = true;
        };

        struct Table {
            void** slots = nullptr;
            PresentFn present = nullptr;
            ResetFn reset = nullptr;
        };

        Table tables[2];
        IDirect3DDevice9* device = nullptr;
        IDirect3DVertexBuffer9* vertices = nullptr;
        IDirect3DIndexBuffer9* indices = nullptr;
        UINT vertexCapacity = 0;
        UINT indexCapacity = 0;
        bool broken = false;
        std::map<ImTextureID, Texture> textures;
        std::unique_ptr<frame::Frame> current;
        thread_local int presenting = 0;

        template <typename T> void Free(T*& object) {
            if (object) object->Release();
            object = nullptr;
        }

        void Drop() {
            Free(vertices);
            Free(indices);
            vertexCapacity = indexCapacity = 0;
            for (auto& entry : textures) {
                Free(entry.second.texture);
                entry.second.dirty = true;
            }
        }

        void Copy(Texture& texture, int x, int y, int width, int height, const std::vector<unsigned char>& rgba) {
            for (int row = 0; row < height; row++)
                for (int column = 0; column < width; column++) {
                    const unsigned char* source = rgba.data() + ((size_t)row * width + column) * 4;
                    unsigned char* target = texture.pixels.data() + ((size_t)(y + row) * texture.width + x + column) * 4;
                    target[0] = source[2];
                    target[1] = source[1];
                    target[2] = source[0];
                    target[3] = source[3];
                }
            texture.dirty = true;
        }

        void Apply(std::vector<textures::Op>& ops) {
            for (textures::Op& op : ops) {
                if (op.kind == textures::Op::Create) {
                    if (op.width <= 0 || op.height <= 0) continue;
                    Texture& texture = textures[(ImTextureID)op.id];
                    Free(texture.texture);
                    texture.width = op.width;
                    texture.height = op.height;
                    texture.pixels.assign((size_t)op.width * op.height * 4, 0);
                    Copy(texture, 0, 0, op.width, op.height, op.pixels);
                }
                else if (op.kind == textures::Op::Update) {
                    auto found = textures.find((ImTextureID)op.id);
                    if (found == textures.end() || op.x < 0 || op.y < 0 || op.x + op.width > found->second.width || op.y + op.height > found->second.height) continue;
                    Copy(found->second, op.x, op.y, op.width, op.height, op.pixels);
                }
                else {
                    auto found = textures.find((ImTextureID)op.id);
                    if (found == textures.end()) continue;
                    Free(found->second.texture);
                    textures.erase(found);
                }
            }
        }

        IDirect3DTexture9* Upload(Texture& texture) {
            if (!texture.dirty && texture.texture) return texture.texture;
            if (!texture.texture && FAILED(device->CreateTexture((UINT)texture.width, (UINT)texture.height, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture.texture, nullptr)))
                return nullptr;
            D3DLOCKED_RECT locked;
            if (FAILED(texture.texture->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD))) return nullptr;
            for (int row = 0; row < texture.height; row++)
                std::memcpy(static_cast<unsigned char*>(locked.pBits) + (size_t)row * locked.Pitch, texture.pixels.data() + (size_t)row * texture.width * 4, (size_t)texture.width * 4);
            texture.texture->UnlockRect(0);
            texture.dirty = false;
            return texture.texture;
        }

        bool Grow(const frame::Frame& frame) {
            UINT needVertices = (UINT)frame.vertices.size(), needIndices = (UINT)frame.indices.size();
            if (!vertices || vertexCapacity < needVertices) {
                Free(vertices);
                vertexCapacity = needVertices + needVertices / 2 + 1024;
                if (FAILED(device->CreateVertexBuffer(vertexCapacity * sizeof(Vertex), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, VertexFormat, D3DPOOL_DEFAULT, &vertices, nullptr))) {
                    vertexCapacity = 0;
                    return false;
                }
            }
            if (!indices || indexCapacity < needIndices) {
                Free(indices);
                indexCapacity = needIndices + needIndices / 2 + 2048;
                D3DFORMAT format = sizeof(ImDrawIdx) == 2 ? D3DFMT_INDEX16 : D3DFMT_INDEX32;
                if (FAILED(device->CreateIndexBuffer(indexCapacity * sizeof(ImDrawIdx), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, format, D3DPOOL_DEFAULT, &indices, nullptr))) {
                    indexCapacity = 0;
                    return false;
                }
            }
            Vertex* vertex = nullptr;
            if (FAILED(vertices->Lock(0, (UINT)(frame.vertices.size() * sizeof(Vertex)), reinterpret_cast<void**>(&vertex), D3DLOCK_DISCARD))) return false;
            for (const ImDrawVert& source : frame.vertices) {
                ImU32 c = source.col;
                vertex->x = source.pos.x;
                vertex->y = source.pos.y;
                vertex->z = 0.0f;
                vertex->color = (c & 0xFF00FF00) | ((c & 0x00FF0000) >> 16) | ((c & 0x000000FF) << 16);
                vertex->u = source.uv.x;
                vertex->v = source.uv.y;
                vertex++;
            }
            vertices->Unlock();
            void* index = nullptr;
            if (FAILED(indices->Lock(0, (UINT)(frame.indices.size() * sizeof(ImDrawIdx)), &index, D3DLOCK_DISCARD))) return false;
            std::memcpy(index, frame.indices.data(), frame.indices.size() * sizeof(ImDrawIdx));
            indices->Unlock();
            return true;
        }

        void Setup(const frame::Frame& frame, UINT width, UINT height) {
            D3DVIEWPORT9 viewport = {0, 0, width, height, 0.0f, 1.0f};
            device->SetViewport(&viewport);
            device->SetPixelShader(nullptr);
            device->SetVertexShader(nullptr);
            device->SetFVF(VertexFormat);
            device->SetStreamSource(0, vertices, 0, sizeof(Vertex));
            device->SetIndices(indices);
            const DWORD states[][2] = {
                {D3DRS_FILLMODE, D3DFILL_SOLID}, {D3DRS_SHADEMODE, D3DSHADE_GOURAUD}, {D3DRS_ZWRITEENABLE, FALSE},
                {D3DRS_ALPHATESTENABLE, FALSE}, {D3DRS_CULLMODE, D3DCULL_NONE}, {D3DRS_ZENABLE, FALSE},
                {D3DRS_ALPHABLENDENABLE, TRUE}, {D3DRS_BLENDOP, D3DBLENDOP_ADD}, {D3DRS_SRCBLEND, D3DBLEND_SRCALPHA},
                {D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA}, {D3DRS_SEPARATEALPHABLENDENABLE, TRUE}, {D3DRS_SRCBLENDALPHA, D3DBLEND_ONE},
                {D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA}, {D3DRS_SCISSORTESTENABLE, TRUE}, {D3DRS_FOGENABLE, FALSE},
                {D3DRS_RANGEFOGENABLE, FALSE}, {D3DRS_SPECULARENABLE, FALSE}, {D3DRS_STENCILENABLE, FALSE},
                {D3DRS_CLIPPING, TRUE}, {D3DRS_LIGHTING, FALSE}, {D3DRS_SRGBWRITEENABLE, FALSE}, {D3DRS_COLORWRITEENABLE, 0xF},
            };
            for (const auto& state : states) device->SetRenderState((D3DRENDERSTATETYPE)state[0], state[1]);
            device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
            device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
            device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
            device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
            device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
            device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
            device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
            device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            float offsetX = 0.5f * frame.size.x / width, offsetY = 0.5f * frame.size.y / height;
            float left = offsetX, right = frame.size.x + offsetX, top = offsetY, bottom = frame.size.y + offsetY;
            D3DMATRIX identity = {{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}};
            D3DMATRIX projection = {{{
                2.0f / (right - left), 0.0f, 0.0f, 0.0f,
                0.0f, 2.0f / (top - bottom), 0.0f, 0.0f,
                0.0f, 0.0f, 0.5f, 0.0f,
                (left + right) / (left - right), (top + bottom) / (bottom - top), 0.5f, 1.0f,
            }}};
            device->SetTransform(D3DTS_WORLD, &identity);
            device->SetTransform(D3DTS_VIEW, &identity);
            device->SetTransform(D3DTS_PROJECTION, &projection);
        }

        void Draw(const frame::Frame& frame) {
            if (frame.vertices.empty() || frame.indices.empty() || frame.size.x <= 0 || frame.size.y <= 0) return;
            IDirect3DSurface9* backbuffer = nullptr;
            if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer))) return;
            D3DSURFACE_DESC desc;
            backbuffer->GetDesc(&desc);
            IDirect3DStateBlock9* saved = nullptr;
            IDirect3DSurface9* target = nullptr;
            if (!Grow(frame) || FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved))) {
                Free(backbuffer);
                return;
            }
            device->GetRenderTarget(0, &target);
            device->SetRenderTarget(0, backbuffer);
            if (SUCCEEDED(device->BeginScene())) {
                Setup(frame, desc.Width, desc.Height);
                float scaleX = desc.Width / frame.size.x, scaleY = desc.Height / frame.size.y;
                UINT count = (UINT)frame.vertices.size();
                for (const frame::Command& command : frame.commands) {
                    auto texture = textures.find(command.texture);
                    if (texture == textures.end()) continue;
                    IDirect3DTexture9* uploaded = Upload(texture->second);
                    if (!uploaded) continue;
                    RECT rect = {(LONG)(command.clip.x * scaleX), (LONG)(command.clip.y * scaleY), (LONG)(command.clip.z * scaleX), (LONG)(command.clip.w * scaleY)};
                    if (rect.right <= rect.left || rect.bottom <= rect.top) continue;
                    device->SetScissorRect(&rect);
                    device->SetTexture(0, uploaded);
                    device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, command.base, 0, count - (UINT)command.base, command.first, command.count / 3);
                }
                device->EndScene();
            }
            device->SetRenderTarget(0, target);
            saved->Apply();
            Free(saved);
            Free(target);
            Free(backbuffer);
        }

        HWND WindowOf(IDirect3DDevice9* target) {
            D3DDEVICE_CREATION_PARAMETERS creation;
            if (SUCCEEDED(target->GetCreationParameters(&creation)) && creation.hFocusWindow) return creation.hFocusWindow;
            IDirect3DSwapChain9* chain = nullptr;
            if (FAILED(target->GetSwapChain(0, &chain))) return nullptr;
            D3DPRESENT_PARAMETERS parameters;
            HWND window = SUCCEEDED(chain->GetPresentParameters(&parameters)) ? parameters.hDeviceWindow : nullptr;
            chain->Release();
            return window;
        }

        void Paint(IDirect3DDevice9* target) {
            if (render::Quitting() || broken) return;
            if (target != device) {
                HWND window = WindowOf(target);
                if (!window || (window != render::GameWindow() && !IsChild(window, render::GameWindow()) && !IsChild(render::GameWindow(), window))) return;
                Drop();
                device = target;
                runtime::Log("scripts draw into the game with direct3d 9");
            }
            render::Work work = render::Take();
            Apply(work.ops);
            if (work.frame) current = std::move(work.frame);
            if (current && !current->commands.empty()) Draw(*current);
        }

        HRESULT Present(int table, IDirect3DDevice9* target, const RECT* source, const RECT* destination, HWND window, const RGNDATA* dirty) {
            if (presenting++ == 0) Paint(target);
            HRESULT result = tables[table].present(target, source, destination, window, dirty);
            presenting--;
            return result;
        }

        HRESULT Reset(int table, IDirect3DDevice9* target, D3DPRESENT_PARAMETERS* parameters) {
            if (target == device) Drop();
            return tables[table].reset(target, parameters);
        }

        HRESULT STDMETHODCALLTYPE Present0(IDirect3DDevice9* target, const RECT* source, const RECT* destination, HWND window, const RGNDATA* dirty) {
            return Present(0, target, source, destination, window, dirty);
        }

        HRESULT STDMETHODCALLTYPE Present1(IDirect3DDevice9* target, const RECT* source, const RECT* destination, HWND window, const RGNDATA* dirty) {
            return Present(1, target, source, destination, window, dirty);
        }

        HRESULT STDMETHODCALLTYPE Reset0(IDirect3DDevice9* target, D3DPRESENT_PARAMETERS* parameters) { return Reset(0, target, parameters); }

        HRESULT STDMETHODCALLTYPE Reset1(IDirect3DDevice9* target, D3DPRESENT_PARAMETERS* parameters) { return Reset(1, target, parameters); }

        template <typename T> void Swap(void** slots, int slot, T hook, T& original) {
            DWORD protection = 0;
            if (!VirtualProtect(&slots[slot], sizeof(void*), PAGE_READWRITE, &protection)) return;
            original = reinterpret_cast<T>(slots[slot]);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            slots[slot] = reinterpret_cast<void*>(hook);
            VirtualProtect(&slots[slot], sizeof(void*), protection, &protection);
        }

        template <typename T> void Put(void** slots, int slot, T original) {
            DWORD protection = 0;
            if (!slots || !original || !VirtualProtect(&slots[slot], sizeof(void*), PAGE_READWRITE, &protection)) return;
            slots[slot] = reinterpret_cast<void*>(original);
            VirtualProtect(&slots[slot], sizeof(void*), protection, &protection);
        }

        bool Patch(IDirect3DDevice9* dummy) {
            void** slots = *reinterpret_cast<void***>(dummy);
            for (Table& table : tables)
                if (table.slots == slots) return true;
            int index = tables[0].slots ? 1 : 0;
            if (tables[index].slots) return false;
            tables[index].slots = slots;
            if (index == 0) {
                Swap(slots, ResetSlot, &Reset0, tables[0].reset);
                Swap(slots, PresentSlot, &Present0, tables[0].present);
            }
            else {
                Swap(slots, ResetSlot, &Reset1, tables[1].reset);
                Swap(slots, PresentSlot, &Present1, tables[1].present);
            }
            return true;
        }

        LRESULT CALLBACK Probe(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }

    bool Hook() {
        HMODULE library = GetModuleHandleW(L"d3d9.dll");
        if (!library) return false;
        auto create = reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(library, "Direct3DCreate9")));
        auto createEx = reinterpret_cast<CreateExFn>(reinterpret_cast<void*>(GetProcAddress(library, "Direct3DCreate9Ex")));
        if (!create) return false;
        WNDCLASSEXW type = {sizeof(type)};
        type.lpfnWndProc = Probe;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"OpenKogamaScriptsProbe9";
        RegisterClassExW(&type);
        HWND probe = CreateWindowExW(0, type.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, type.hInstance, nullptr);
        if (!probe) return false;
        D3DPRESENT_PARAMETERS parameters = {};
        parameters.Windowed = TRUE;
        parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        parameters.BackBufferFormat = D3DFMT_UNKNOWN;
        parameters.hDeviceWindow = probe;
        bool hooked = false;
        if (IDirect3D9* direct = create(D3D_SDK_VERSION)) {
            for (D3DDEVTYPE kind : {D3DDEVTYPE_HAL, D3DDEVTYPE_NULLREF}) {
                IDirect3DDevice9* dummy = nullptr;
                if (FAILED(direct->CreateDevice(D3DADAPTER_DEFAULT, kind, probe, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &parameters, &dummy))) continue;
                hooked = Patch(dummy) || hooked;
                dummy->Release();
                break;
            }
            direct->Release();
        }
        IDirect3D9Ex* directEx = nullptr;
        if (createEx && SUCCEEDED(createEx(D3D_SDK_VERSION, &directEx))) {
            IDirect3DDevice9Ex* dummy = nullptr;
            if (SUCCEEDED(directEx->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, probe, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &parameters, nullptr, &dummy))) {
                hooked = Patch(dummy) || hooked;
                dummy->Release();
            }
            directEx->Release();
        }
        DestroyWindow(probe);
        UnregisterClassW(type.lpszClassName, type.hInstance);
        return hooked;
    }

    void Unhook() {
        Put(tables[0].slots, PresentSlot, tables[0].present);
        Put(tables[0].slots, ResetSlot, tables[0].reset);
        Put(tables[1].slots, PresentSlot, tables[1].present);
        Put(tables[1].slots, ResetSlot, tables[1].reset);
    }
}
