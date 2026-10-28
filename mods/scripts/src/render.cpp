#include "render.h"
#include "api.h"
#include "frame.h"
#include "game.h"
#include "paint.h"
#include "runtime.h"
#include "textures.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

namespace render {
    namespace {
        using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
        using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);

        constexpr int PresentSlot = 8;
        constexpr int Present1Slot = 22;
        constexpr UINT Instances = 256;

        struct Texture {
            ID3D11Texture2D* texture = nullptr;
            ID3D11ShaderResourceView* view = nullptr;
        };

        struct Gpu {
            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* context = nullptr;
            ID3D11VertexShader* vertexShader = nullptr;
            ID3D11PixelShader* pixelShader = nullptr;
            ID3D11InputLayout* layout = nullptr;
            ID3D11Buffer* constants = nullptr;
            ID3D11Buffer* vertices = nullptr;
            ID3D11Buffer* indices = nullptr;
            UINT vertexCapacity = 0;
            UINT indexCapacity = 0;
            ID3D11BlendState* blend = nullptr;
            ID3D11RasterizerState* raster = nullptr;
            ID3D11DepthStencilState* depth = nullptr;
            ID3D11SamplerState* sampler = nullptr;
            IDXGISwapChain* chain = nullptr;
            ID3D11RenderTargetView* target = nullptr;
            UINT width = 0;
            UINT height = 0;
            bool broken = false;
            std::map<ImTextureID, Texture> textures;
            std::unique_ptr<frame::Frame> current;
        };

        struct State {
            UINT scissorCount, viewportCount;
            D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
            D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
            ID3D11RasterizerState* raster;
            ID3D11BlendState* blend;
            FLOAT blendFactor[4];
            UINT sampleMask;
            UINT stencilRef;
            ID3D11DepthStencilState* depth;
            ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
            ID3D11DepthStencilView* depthView;
            ID3D11ShaderResourceView* resource;
            ID3D11SamplerState* sampler;
            ID3D11PixelShader* pixelShader;
            ID3D11VertexShader* vertexShader;
            ID3D11GeometryShader* geometryShader;
            ID3D11HullShader* hullShader;
            ID3D11DomainShader* domainShader;
            UINT pixelCount, vertexCount, geometryCount, hullCount, domainCount;
            ID3D11ClassInstance* pixelInstances[Instances];
            ID3D11ClassInstance* vertexInstances[Instances];
            ID3D11ClassInstance* geometryInstances[Instances];
            ID3D11ClassInstance* hullInstances[Instances];
            ID3D11ClassInstance* domainInstances[Instances];
            D3D11_PRIMITIVE_TOPOLOGY topology;
            ID3D11Buffer* indexBuffer;
            ID3D11Buffer* vertexBuffer;
            ID3D11Buffer* vertexConstants;
            UINT indexOffset, vertexStride, vertexOffset;
            DXGI_FORMAT indexFormat;
            ID3D11InputLayout* layout;
        };

        HWND window = nullptr;
        WNDPROC previous = nullptr;
        bool hooked = false;
        bool shown = false;
        bool active = false;
        std::atomic<bool> quitting{false};

        std::mutex exchange;
        std::unique_ptr<frame::Frame> pending;
        std::vector<textures::Op> uploads;

        Gpu gpu;
        State saved;
        thread_local int presenting = 0;

        void** presentTable = nullptr;
        void** present1Table = nullptr;
        PresentFn present = nullptr;
        Present1Fn present1 = nullptr;

        template <typename T> void Free(T*& object) {
            if (object) object->Release();
            object = nullptr;
        }

        BOOL CALLBACK Visit(HWND hwnd, LPARAM found) {
            DWORD process = 0;
            GetWindowThreadProcessId(hwnd, &process);
            wchar_t name[64] = L"";
            GetClassNameW(hwnd, name, 64);
            if (process != GetCurrentProcessId() || wcscmp(name, L"UnityWndClass") != 0) return TRUE;
            *reinterpret_cast<HWND*>(found) = hwnd;
            return FALSE;
        }

        HWND FindGame() {
            HWND found = nullptr;
            EnumWindows(Visit, reinterpret_cast<LPARAM>(&found));
            return found;
        }

        const char* KeyName(WPARAM key, char* buffer) {
            if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) {
                buffer[0] = (char)key;
                buffer[1] = 0;
                return buffer;
            }
            if (key >= VK_F1 && key <= VK_F24) {
                std::snprintf(buffer, 8, "F%d", (int)(key - VK_F1 + 1));
                return buffer;
            }
            switch (key) {
                case VK_SPACE: return "Space";
                case VK_RETURN: return "Enter";
                case VK_ESCAPE: return "Escape";
                case VK_TAB: return "Tab";
                case VK_BACK: return "Backspace";
                case VK_SHIFT: return "Shift";
                case VK_CONTROL: return "Control";
                case VK_MENU: return "Alt";
                case VK_LEFT: return "Left";
                case VK_RIGHT: return "Right";
                case VK_UP: return "Up";
                case VK_DOWN: return "Down";
                case VK_DELETE: return "Delete";
                case VK_INSERT: return "Insert";
                case VK_HOME: return "Home";
                case VK_END: return "End";
                case VK_PRIOR: return "PageUp";
                case VK_NEXT: return "PageDown";
                default: return nullptr;
            }
        }

        void Keys(UINT message, WPARAM wParam) {
            char buffer[8];
            switch (message) {
                case WM_KEYDOWN:
                case WM_SYSKEYDOWN:
                case WM_KEYUP:
                case WM_SYSKEYUP:
                    if (const char* name = KeyName(wParam, buffer)) api::Key(name, message == WM_KEYDOWN || message == WM_SYSKEYDOWN);
                    break;
                case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: api::Key("MouseLeft", true); break;
                case WM_LBUTTONUP: api::Key("MouseLeft", false); break;
                case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: api::Key("MouseRight", true); break;
                case WM_RBUTTONUP: api::Key("MouseRight", false); break;
                case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: api::Key("MouseMiddle", true); break;
                case WM_MBUTTONUP: api::Key("MouseMiddle", false); break;
                case WM_KILLFOCUS: api::ReleaseKeys(); break;
            }
        }

        LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
            if (!quitting && active) {
                ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam);
                ImGuiIO& io = ImGui::GetIO();
                bool typing = shown && io.WantTextInput;
                if (!typing) Keys(message, wParam);
                if (shown) {
                    bool mouse = message >= WM_MOUSEFIRST && message <= WM_MOUSELAST && message != WM_MOUSEMOVE;
                    bool keyboard = message >= WM_KEYFIRST && message <= WM_KEYLAST;
                    if ((mouse && io.WantCaptureMouse) || (keyboard && typing)) return 0;
                }
            }
            return CallWindowProcW(previous, hwnd, message, wParam, lParam);
        }

        bool Compile(const char* source, const char* profile, ID3DBlob** blob) {
            return SUCCEEDED(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "main", profile, 0, 0, blob, nullptr));
        }

        bool CreateObjects() {
            static const char* vertexSource =
                "cbuffer vertexBuffer : register(b0) { float4x4 ProjectionMatrix; };"
                "struct VS_INPUT { float2 pos : POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };"
                "struct PS_INPUT { float4 pos : SV_POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };"
                "PS_INPUT main(VS_INPUT input) { PS_INPUT output; output.pos = mul(ProjectionMatrix, float4(input.pos.xy, 0.f, 1.f));"
                " output.col = input.col; output.uv = input.uv; return output; }";
            static const char* pixelSource =
                "struct PS_INPUT { float4 pos : SV_POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };"
                "sampler sampler0; Texture2D texture0;"
                "float4 main(PS_INPUT input) : SV_Target { return input.col * texture0.Sample(sampler0, input.uv); }";

            ID3DBlob* vertexBlob = nullptr;
            ID3DBlob* pixelBlob = nullptr;
            bool created = Compile(vertexSource, "vs_4_0", &vertexBlob) && Compile(pixelSource, "ps_4_0", &pixelBlob)
                && SUCCEEDED(gpu.device->CreateVertexShader(vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &gpu.vertexShader))
                && SUCCEEDED(gpu.device->CreatePixelShader(pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize(), nullptr, &gpu.pixelShader));
            if (created) {
                D3D11_INPUT_ELEMENT_DESC elements[] = {
                    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)offsetof(ImDrawVert, pos), D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)offsetof(ImDrawVert, uv), D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, (UINT)offsetof(ImDrawVert, col), D3D11_INPUT_PER_VERTEX_DATA, 0},
                };
                created = SUCCEEDED(gpu.device->CreateInputLayout(elements, 3, vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), &gpu.layout));
            }
            Free(vertexBlob);
            Free(pixelBlob);
            if (!created) return false;

            D3D11_BUFFER_DESC constants = {};
            constants.ByteWidth = sizeof(float) * 16;
            constants.Usage = D3D11_USAGE_DYNAMIC;
            constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            D3D11_BLEND_DESC blend = {};
            blend.RenderTarget[0].BlendEnable = TRUE;
            blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

            D3D11_RASTERIZER_DESC raster = {};
            raster.FillMode = D3D11_FILL_SOLID;
            raster.CullMode = D3D11_CULL_NONE;
            raster.ScissorEnable = TRUE;
            raster.DepthClipEnable = TRUE;

            D3D11_DEPTH_STENCIL_DESC depth = {};
            depth.DepthEnable = FALSE;
            depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
            depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
            depth.FrontFace.StencilFailOp = depth.FrontFace.StencilDepthFailOp = depth.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
            depth.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
            depth.BackFace = depth.FrontFace;

            D3D11_SAMPLER_DESC sampler = {};
            sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.ComparisonFunc = D3D11_COMPARISON_ALWAYS;

            return SUCCEEDED(gpu.device->CreateBuffer(&constants, nullptr, &gpu.constants))
                && SUCCEEDED(gpu.device->CreateBlendState(&blend, &gpu.blend))
                && SUCCEEDED(gpu.device->CreateRasterizerState(&raster, &gpu.raster))
                && SUCCEEDED(gpu.device->CreateDepthStencilState(&depth, &gpu.depth))
                && SUCCEEDED(gpu.device->CreateSamplerState(&sampler, &gpu.sampler));
        }

        DXGI_FORMAT ViewFormat(DXGI_FORMAT format) {
            switch (format) {
                case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
                case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
                case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
                case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
                default: return DXGI_FORMAT_UNKNOWN;
            }
        }

        bool CreateTarget(IDXGISwapChain* chain) {
            ID3D11Texture2D* buffer = nullptr;
            if (FAILED(chain->GetBuffer(0, IID_PPV_ARGS(&buffer)))) return false;
            D3D11_TEXTURE2D_DESC desc;
            buffer->GetDesc(&desc);
            D3D11_RENDER_TARGET_VIEW_DESC view = {};
            view.Format = ViewFormat(desc.Format);
            view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            HRESULT result = gpu.device->CreateRenderTargetView(buffer, view.Format == DXGI_FORMAT_UNKNOWN ? nullptr : &view, &gpu.target);
            buffer->Release();
            gpu.width = desc.Width;
            gpu.height = desc.Height;
            return SUCCEEDED(result);
        }

        bool Grow(ID3D11Buffer*& buffer, UINT& capacity, UINT needed, UINT stride, UINT bind) {
            if (buffer && capacity >= needed) return true;
            Free(buffer);
            capacity = needed + needed / 2 + 1024;
            D3D11_BUFFER_DESC desc = {};
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.ByteWidth = capacity * stride;
            desc.BindFlags = bind;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (SUCCEEDED(gpu.device->CreateBuffer(&desc, nullptr, &buffer))) return true;
            capacity = 0;
            return false;
        }

        bool Fill(ID3D11Buffer* buffer, const void* data, size_t size) {
            D3D11_MAPPED_SUBRESOURCE mapped;
            if (FAILED(gpu.context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
            std::memcpy(mapped.pData, data, size);
            gpu.context->Unmap(buffer, 0);
            return true;
        }

        void Save(ID3D11DeviceContext* context, State& state) {
            state.scissorCount = state.viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
            context->RSGetScissorRects(&state.scissorCount, state.scissors);
            context->RSGetViewports(&state.viewportCount, state.viewports);
            context->RSGetState(&state.raster);
            context->OMGetBlendState(&state.blend, state.blendFactor, &state.sampleMask);
            context->OMGetDepthStencilState(&state.depth, &state.stencilRef);
            context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, state.targets, &state.depthView);
            context->PSGetShaderResources(0, 1, &state.resource);
            context->PSGetSamplers(0, 1, &state.sampler);
            state.pixelCount = state.vertexCount = state.geometryCount = state.hullCount = state.domainCount = Instances;
            context->PSGetShader(&state.pixelShader, state.pixelInstances, &state.pixelCount);
            context->VSGetShader(&state.vertexShader, state.vertexInstances, &state.vertexCount);
            context->GSGetShader(&state.geometryShader, state.geometryInstances, &state.geometryCount);
            context->HSGetShader(&state.hullShader, state.hullInstances, &state.hullCount);
            context->DSGetShader(&state.domainShader, state.domainInstances, &state.domainCount);
            context->VSGetConstantBuffers(0, 1, &state.vertexConstants);
            context->IAGetPrimitiveTopology(&state.topology);
            context->IAGetIndexBuffer(&state.indexBuffer, &state.indexFormat, &state.indexOffset);
            context->IAGetVertexBuffers(0, 1, &state.vertexBuffer, &state.vertexStride, &state.vertexOffset);
            context->IAGetInputLayout(&state.layout);
        }

        void Release(ID3D11ClassInstance** instances, UINT count) {
            for (UINT i = 0; i < count; i++) Free(instances[i]);
        }

        void Restore(ID3D11DeviceContext* context, State& state) {
            context->RSSetScissorRects(state.scissorCount, state.scissors);
            context->RSSetViewports(state.viewportCount, state.viewports);
            context->RSSetState(state.raster);
            context->OMSetBlendState(state.blend, state.blendFactor, state.sampleMask);
            context->OMSetDepthStencilState(state.depth, state.stencilRef);
            context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, state.targets, state.depthView);
            context->PSSetShaderResources(0, 1, &state.resource);
            context->PSSetSamplers(0, 1, &state.sampler);
            context->PSSetShader(state.pixelShader, state.pixelInstances, state.pixelCount);
            context->VSSetShader(state.vertexShader, state.vertexInstances, state.vertexCount);
            context->GSSetShader(state.geometryShader, state.geometryInstances, state.geometryCount);
            context->HSSetShader(state.hullShader, state.hullInstances, state.hullCount);
            context->DSSetShader(state.domainShader, state.domainInstances, state.domainCount);
            context->VSSetConstantBuffers(0, 1, &state.vertexConstants);
            context->IASetPrimitiveTopology(state.topology);
            context->IASetIndexBuffer(state.indexBuffer, state.indexFormat, state.indexOffset);
            context->IASetVertexBuffers(0, 1, &state.vertexBuffer, &state.vertexStride, &state.vertexOffset);
            context->IASetInputLayout(state.layout);

            Free(state.raster);
            Free(state.blend);
            Free(state.depth);
            for (ID3D11RenderTargetView*& target : state.targets) Free(target);
            Free(state.depthView);
            Free(state.resource);
            Free(state.sampler);
            Free(state.pixelShader);
            Free(state.vertexShader);
            Free(state.geometryShader);
            Free(state.hullShader);
            Free(state.domainShader);
            Release(state.pixelInstances, state.pixelCount);
            Release(state.vertexInstances, state.vertexCount);
            Release(state.geometryInstances, state.geometryCount);
            Release(state.hullInstances, state.hullCount);
            Release(state.domainInstances, state.domainCount);
            Free(state.vertexConstants);
            Free(state.indexBuffer);
            Free(state.vertexBuffer);
            Free(state.layout);
        }

        void Draw(const frame::Frame& frame) {
            if (frame.vertices.empty() || frame.indices.empty() || frame.size.x <= 0 || frame.size.y <= 0) return;
            ID3D11DeviceContext* context = gpu.context;
            if (!Grow(gpu.vertices, gpu.vertexCapacity, (UINT)frame.vertices.size(), sizeof(ImDrawVert), D3D11_BIND_VERTEX_BUFFER)) return;
            if (!Grow(gpu.indices, gpu.indexCapacity, (UINT)frame.indices.size(), sizeof(ImDrawIdx), D3D11_BIND_INDEX_BUFFER)) return;
            float left = 0.0f, right = frame.size.x, top = 0.0f, bottom = frame.size.y;
            float projection[4][4] = {
                {2.0f / (right - left), 0.0f, 0.0f, 0.0f},
                {0.0f, 2.0f / (top - bottom), 0.0f, 0.0f},
                {0.0f, 0.0f, 0.5f, 0.0f},
                {(right + left) / (left - right), (top + bottom) / (bottom - top), 0.5f, 1.0f},
            };
            if (!Fill(gpu.vertices, frame.vertices.data(), frame.vertices.size() * sizeof(ImDrawVert))) return;
            if (!Fill(gpu.indices, frame.indices.data(), frame.indices.size() * sizeof(ImDrawIdx))) return;
            if (!Fill(gpu.constants, projection, sizeof(projection))) return;

            Save(context, saved);
            D3D11_VIEWPORT viewport = {0.0f, 0.0f, (float)gpu.width, (float)gpu.height, 0.0f, 1.0f};
            context->RSSetViewports(1, &viewport);
            context->OMSetRenderTargets(1, &gpu.target, nullptr);
            UINT stride = sizeof(ImDrawVert), offset = 0;
            context->IASetInputLayout(gpu.layout);
            context->IASetVertexBuffers(0, 1, &gpu.vertices, &stride, &offset);
            context->IASetIndexBuffer(gpu.indices, sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(gpu.vertexShader, nullptr, 0);
            context->VSSetConstantBuffers(0, 1, &gpu.constants);
            context->PSSetShader(gpu.pixelShader, nullptr, 0);
            context->PSSetSamplers(0, 1, &gpu.sampler);
            context->GSSetShader(nullptr, nullptr, 0);
            context->HSSetShader(nullptr, nullptr, 0);
            context->DSSetShader(nullptr, nullptr, 0);
            const float factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            context->OMSetBlendState(gpu.blend, factor, 0xffffffff);
            context->OMSetDepthStencilState(gpu.depth, 0);
            context->RSSetState(gpu.raster);

            float scaleX = gpu.width / frame.size.x, scaleY = gpu.height / frame.size.y;
            for (const frame::Command& command : frame.commands) {
                auto texture = gpu.textures.find(command.texture);
                if (texture == gpu.textures.end()) continue;
                LONG x1 = (LONG)(command.clip.x * scaleX), y1 = (LONG)(command.clip.y * scaleY);
                LONG x2 = (LONG)(command.clip.z * scaleX), y2 = (LONG)(command.clip.w * scaleY);
                if (x1 < 0) x1 = 0;
                if (y1 < 0) y1 = 0;
                if (x2 > (LONG)gpu.width) x2 = (LONG)gpu.width;
                if (y2 > (LONG)gpu.height) y2 = (LONG)gpu.height;
                if (x2 <= x1 || y2 <= y1) continue;
                D3D11_RECT rect = {x1, y1, x2, y2};
                context->RSSetScissorRects(1, &rect);
                context->PSSetShaderResources(0, 1, &texture->second.view);
                context->DrawIndexed(command.count, command.first, command.base);
            }
            Restore(context, saved);
        }

        void Apply(std::vector<textures::Op>& ops) {
            for (textures::Op& op : ops) {
                if (op.kind == textures::Op::Create) {
                    if (op.width <= 0 || op.height <= 0) continue;
                    D3D11_TEXTURE2D_DESC desc = {};
                    desc.Width = (UINT)op.width;
                    desc.Height = (UINT)op.height;
                    desc.MipLevels = 1;
                    desc.ArraySize = 1;
                    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    desc.SampleDesc.Count = 1;
                    desc.Usage = D3D11_USAGE_DEFAULT;
                    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                    D3D11_SUBRESOURCE_DATA data = {op.pixels.data(), (UINT)op.width * 4, 0};
                    Texture texture;
                    if (FAILED(gpu.device->CreateTexture2D(&desc, &data, &texture.texture))) continue;
                    if (FAILED(gpu.device->CreateShaderResourceView(texture.texture, nullptr, &texture.view))) {
                        Free(texture.texture);
                        continue;
                    }
                    gpu.textures[(ImTextureID)op.id] = texture;
                }
                else if (op.kind == textures::Op::Update) {
                    auto found = gpu.textures.find((ImTextureID)op.id);
                    if (found == gpu.textures.end() || op.width <= 0 || op.height <= 0) continue;
                    D3D11_BOX box = {(UINT)op.x, (UINT)op.y, 0, (UINT)(op.x + op.width), (UINT)(op.y + op.height), 1};
                    gpu.context->UpdateSubresource(found->second.texture, 0, &box, op.pixels.data(), (UINT)op.width * 4, 0);
                }
                else {
                    auto found = gpu.textures.find((ImTextureID)op.id);
                    if (found == gpu.textures.end()) continue;
                    Free(found->second.view);
                    Free(found->second.texture);
                    gpu.textures.erase(found);
                }
            }
        }

        bool Adopt(IDXGISwapChain* chain) {
            ID3D11Device* device = nullptr;
            if (FAILED(chain->GetDevice(IID_PPV_ARGS(&device)))) {
                gpu.broken = true;
                runtime::Log("the game does not draw with direct3d 11, scripts cannot draw");
                return false;
            }
            if (gpu.device && device != gpu.device) {
                device->Release();
                gpu.broken = true;
                runtime::Log("the game changed its direct3d device, scripts stop drawing");
                return false;
            }
            if (!gpu.device) {
                gpu.device = device;
                gpu.device->GetImmediateContext(&gpu.context);
                if (!CreateObjects()) {
                    gpu.broken = true;
                    runtime::Log("could not set up drawing for scripts");
                    return false;
                }
            }
            else device->Release();
            gpu.chain = chain;
            DXGI_SWAP_CHAIN_DESC desc;
            if (SUCCEEDED(chain->GetDesc(&desc)))
                runtime::Log("scripts draw into the game at " + std::to_string(desc.BufferDesc.Width) + "x" + std::to_string(desc.BufferDesc.Height));
            return true;
        }

        bool GameChain(IDXGISwapChain* chain) {
            DXGI_SWAP_CHAIN_DESC desc;
            return SUCCEEDED(chain->GetDesc(&desc)) && desc.OutputWindow == window;
        }

        void Paint(IDXGISwapChain* chain) {
            if (quitting || gpu.broken) return;
            if (chain != gpu.chain && (!GameChain(chain) || !Adopt(chain))) return;
            Work work = Take();
            Apply(work.ops);
            if (work.frame) gpu.current = std::move(work.frame);
            if (!gpu.current || gpu.current->commands.empty() || !CreateTarget(chain)) return;
            Draw(*gpu.current);
            Free(gpu.target);
        }

        HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* chain, UINT interval, UINT flags) {
            if (presenting++ == 0 && !(flags & DXGI_PRESENT_TEST)) Paint(chain);
            HRESULT result = present(chain, interval, flags);
            presenting--;
            return result;
        }

        HRESULT STDMETHODCALLTYPE HookedPresent1(IDXGISwapChain1* chain, UINT interval, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters) {
            if (presenting++ == 0 && !(flags & DXGI_PRESENT_TEST)) Paint(chain);
            HRESULT result = present1(chain, interval, flags, parameters);
            presenting--;
            return result;
        }

        template <typename T> void Swap(void** table, int slot, T hook, T& original) {
            DWORD protection = 0;
            if (!VirtualProtect(&table[slot], sizeof(void*), PAGE_READWRITE, &protection)) return;
            original = reinterpret_cast<T>(table[slot]);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            table[slot] = reinterpret_cast<void*>(hook);
            VirtualProtect(&table[slot], sizeof(void*), protection, &protection);
        }

        template <typename T> void Put(void** table, int slot, T original) {
            DWORD protection = 0;
            if (!table || !original || !VirtualProtect(&table[slot], sizeof(void*), PAGE_READWRITE, &protection)) return;
            table[slot] = reinterpret_cast<void*>(original);
            VirtualProtect(&table[slot], sizeof(void*), protection, &protection);
        }

        LRESULT CALLBACK Probe(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        bool Hook() {
            WNDCLASSEXW type = {sizeof(type)};
            type.lpfnWndProc = Probe;
            type.hInstance = GetModuleHandleW(nullptr);
            type.lpszClassName = L"OpenKogamaScriptsProbe";
            RegisterClassExW(&type);
            HWND probe = CreateWindowExW(0, type.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, type.hInstance, nullptr);
            if (!probe) return false;
            DXGI_SWAP_CHAIN_DESC desc = {};
            desc.BufferCount = 1;
            desc.BufferDesc.Width = 64;
            desc.BufferDesc.Height = 64;
            desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.OutputWindow = probe;
            desc.SampleDesc.Count = 1;
            desc.Windowed = TRUE;
            desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            IDXGISwapChain* chain = nullptr;
            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* context = nullptr;
            HRESULT result = E_FAIL;
            for (D3D_DRIVER_TYPE driver : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
                result = D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &context);
                if (SUCCEEDED(result)) break;
            }
            if (SUCCEEDED(result)) {
                presentTable = *reinterpret_cast<void***>(chain);
                Swap(presentTable, PresentSlot, &HookedPresent, present);
                IDXGISwapChain1* chain1 = nullptr;
                if (SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&chain1)))) {
                    present1Table = *reinterpret_cast<void***>(chain1);
                    Swap(present1Table, Present1Slot, &HookedPresent1, present1);
                    chain1->Release();
                }
            }
            Free(chain);
            Free(context);
            Free(device);
            DestroyWindow(probe);
            UnregisterClassW(type.lpszClassName, type.hInstance);
            return SUCCEEDED(result) && present;
        }

        void Submit(std::unique_ptr<frame::Frame> next) {
            std::vector<textures::Op> ops = textures::Take();
            if (!hooked) return;
            std::lock_guard<std::mutex> guard(exchange);
            pending = std::move(next);
            uploads.insert(uploads.end(), std::make_move_iterator(ops.begin()), std::make_move_iterator(ops.end()));
        }
    }

    Work Take() {
        Work work;
        std::lock_guard<std::mutex> guard(exchange);
        work.frame = std::move(pending);
        work.ops.swap(uploads);
        return work;
    }

    HWND GameWindow() { return window; }

    bool Quitting() { return quitting; }

    bool Start() {
        window = FindGame();
        if (!window) return false;
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
        io.BackendRendererName = "openkogama-scripts";
        io.ConfigErrorRecoveryEnableAssert = false;
        io.ConfigErrorRecoveryEnableTooltip = false;
        api::Style();
        ImGui_ImplWin32_Init(window);
        previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(Procedure)));
        std::string graphics = game::GraphicsVersion();
        runtime::Log("the game draws with " + (graphics.empty() ? std::string("something unknown") : graphics));
        hooked = Hook();
        if (paint9::Hook()) hooked = true;
        if (GetModuleHandleW(L"d3d9.dll") && graphics.rfind("Direct3D 9", 0) == 0 && game::NativeTexture(paint9::Adopt)) hooked = true;
        if (!hooked) runtime::Log("could not reach the game's drawing, scripts run without a hud");
        return true;
    }

    void Frame() {
        if (!window) return;
        active = runtime::Count() > 0;
        if (!active) {
            if (shown) {
                shown = false;
                Submit(std::make_unique<frame::Frame>());
            }
            return;
        }
        api::LoadFonts();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        runtime::Draw();
        ImGui::Render();
        Submit(frame::Build(ImGui::GetDrawData()));
        shown = true;
    }

    void Stop() {
        quitting = true;
        if (window && previous) SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous));
        Put(presentTable, PresentSlot, present);
        Put(present1Table, Present1Slot, present1);
        paint9::Unhook();
    }
}
