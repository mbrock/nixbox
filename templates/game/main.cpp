// A whole-screen Direct3D 12 game on CoreApplication: no XAML, no IDL.
// The left stick spins the cube.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>

#include <d3dx12.h>
#include <dxgi1_4.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.UI.Core.h>

#include <cubePixelShader.h>
#include <cubeVertexShader.h>

using namespace winrt;
using namespace winrt::Windows::ApplicationModel::Activation;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::Gaming::Input;
using namespace winrt::Windows::Graphics::Display;
using namespace winrt::Windows::UI::Core;

namespace {
struct Vertex { float position[3], color[3], normal[3]; };

// Separate vertices per face preserve flat normals and distinct face colors.
constexpr Vertex vertices[] = {
    {{-1,-1,-1}, {0.15f,0.85f,1}, {0,0,-1}}, {{-1,1,-1}, {0.15f,0.85f,1}, {0,0,-1}},
    {{1,1,-1}, {0.15f,0.85f,1}, {0,0,-1}}, {{1,-1,-1}, {0.15f,0.85f,1}, {0,0,-1}},
    {{1,-1,1}, {0.65f,0.35f,1}, {0,0,1}}, {{1,1,1}, {0.65f,0.35f,1}, {0,0,1}},
    {{-1,1,1}, {0.65f,0.35f,1}, {0,0,1}}, {{-1,-1,1}, {0.65f,0.35f,1}, {0,0,1}},
    {{-1,-1,1}, {1,0.4f,0.35f}, {-1,0,0}}, {{-1,1,1}, {1,0.4f,0.35f}, {-1,0,0}},
    {{-1,1,-1}, {1,0.4f,0.35f}, {-1,0,0}}, {{-1,-1,-1}, {1,0.4f,0.35f}, {-1,0,0}},
    {{1,-1,-1}, {0.25f,0.9f,0.65f}, {1,0,0}}, {{1,1,-1}, {0.25f,0.9f,0.65f}, {1,0,0}},
    {{1,1,1}, {0.25f,0.9f,0.65f}, {1,0,0}}, {{1,-1,1}, {0.25f,0.9f,0.65f}, {1,0,0}},
    {{-1,1,-1}, {1,0.8f,0.3f}, {0,1,0}}, {{-1,1,1}, {1,0.8f,0.3f}, {0,1,0}},
    {{1,1,1}, {1,0.8f,0.3f}, {0,1,0}}, {{1,1,-1}, {1,0.8f,0.3f}, {0,1,0}},
    {{-1,-1,1}, {0.3f,0.5f,1}, {0,-1,0}}, {{-1,-1,-1}, {0.3f,0.5f,1}, {0,-1,0}},
    {{1,-1,-1}, {0.3f,0.5f,1}, {0,-1,0}}, {{1,-1,1}, {0.3f,0.5f,1}, {0,-1,0}},
};
constexpr uint16_t indices[] = {
    0,1,2, 0,2,3, 4,5,6, 4,6,7, 8,9,10, 8,10,11,
    12,13,14, 12,14,15, 16,17,18, 16,18,19, 20,21,22, 20,22,23,
};

constexpr UINT FrameCount = 2;
constexpr DXGI_FORMAT BackBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr DXGI_FORMAT DepthFormat = DXGI_FORMAT_D32_FLOAT;

// The mesh never changes, so it stays in an upload heap the GPU reads directly.
com_ptr<ID3D12Resource> UploadBuffer(ID3D12Device* device, void const* data, UINT64 size) {
    const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    const auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);
    com_ptr<ID3D12Resource> buffer;
    check_hresult(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), buffer.put_void()));
    void* mapped = nullptr;
    const CD3DX12_RANGE noRead(0, 0);
    check_hresult(buffer->Map(0, &noRead, &mapped));
    std::memcpy(mapped, data, size);
    buffer->Unmap(0, nullptr);
    return buffer;
}
} // namespace

// Errors escaping the game end the process, so record them first: Device
// Portal's file explorer shows the app's LocalState/error.txt.
void Record(hresult_error const& error) {
    const auto path =
        winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path() + L"\\error.txt";
    char code[16];
    std::snprintf(code, sizeof(code), "0x%08X: ", uint32_t(error.code().value));
    const auto text = code + to_string(error.message()) + "\r\n";
    handle file(CreateFile2(path.c_str(), GENERIC_WRITE, 0, CREATE_ALWAYS, nullptr));
    DWORD written = 0;
    WriteFile(file.get(), text.data(), DWORD(text.size()), &written, nullptr);
}

struct Game : implements<Game, IFrameworkViewSource, IFrameworkView> {
    IFrameworkView CreateView() { return *this; }

    void Initialize(CoreApplicationView const& view) {
        view.Activated([](auto&&, auto&&) { CoreWindow::GetForCurrentThread().Activate(); });
    }

    void SetWindow(CoreWindow const& window) {
        m_window = window;
        window.SizeChanged([this](auto&&, auto&&) { m_resize = true; });
        window.Closed([this](auto&&, auto&&) { m_closed = true; });
    }

    void Load(hstring const&) {}
    void Uninitialize() {}

    void Run() {
        try {
            Loop();
        } catch (hresult_error const& error) {
            Record(error);
            throw;
        }
    }

  private:
    void Loop() {
        CreateDevice();
        LARGE_INTEGER frequency{}, last{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&last);
        float angle = 0;
        while (!m_closed) {
            m_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            const float seconds = float(now.QuadPart - last.QuadPart) / float(frequency.QuadPart);
            last = now;
            float spin = 0.65f;
            auto gamepads = Gamepad::Gamepads();
            if (gamepads.Size() > 0)
                spin += 4.0f * float(gamepads.GetAt(0).GetCurrentReading().LeftThumbstickX);
            angle += spin * seconds;
            if (m_resize) Resize();
            Render(angle);
        }
        WaitForGpu();
    }

    void CreateDevice() {
        check_hresult(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
            m_device.put_void()));
        const D3D12_COMMAND_QUEUE_DESC queue{D3D12_COMMAND_LIST_TYPE_DIRECT};
        check_hresult(m_device->CreateCommandQueue(&queue, __uuidof(ID3D12CommandQueue),
            m_queue.put_void()));
        for (auto& allocator : m_allocators)
            check_hresult(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                __uuidof(ID3D12CommandAllocator), allocator.put_void()));
        check_hresult(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            m_allocators[0].get(), nullptr, __uuidof(ID3D12GraphicsCommandList),
            m_commands.put_void()));
        check_hresult(m_commands->Close());
        check_hresult(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
            m_fence.put_void()));
        m_fenceEvent.attach(CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS));
        if (!m_fenceEvent) throw_last_error();

        D3D12_DESCRIPTOR_HEAP_DESC heap{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FrameCount};
        check_hresult(m_device->CreateDescriptorHeap(&heap, __uuidof(ID3D12DescriptorHeap),
            m_rtvHeap.put_void()));
        m_rtvStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        heap = {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1};
        check_hresult(m_device->CreateDescriptorHeap(&heap, __uuidof(ID3D12DescriptorHeap),
            m_dsvHeap.put_void()));

        // The shader's Scene cbuffer (b0) is fed as four root constants per draw.
        CD3DX12_ROOT_PARAMETER scene;
        scene.InitAsConstants(4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
        const CD3DX12_ROOT_SIGNATURE_DESC root(1, &scene, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        com_ptr<ID3DBlob> signature, error;
        check_hresult(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1,
            signature.put(), error.put()));
        check_hresult(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
            signature->GetBufferSize(), __uuidof(ID3D12RootSignature), m_rootSignature.put_void()));

        const D3D12_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
        pipeline.pRootSignature = m_rootSignature.get();
        pipeline.VS = {cubeVertexShader, sizeof(cubeVertexShader)};
        pipeline.PS = {cubePixelShader, sizeof(cubePixelShader)};
        pipeline.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        pipeline.SampleMask = UINT_MAX;
        pipeline.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pipeline.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        pipeline.InputLayout = {layout, ARRAYSIZE(layout)};
        pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline.NumRenderTargets = 1;
        pipeline.RTVFormats[0] = BackBufferFormat;
        pipeline.DSVFormat = DepthFormat;
        pipeline.SampleDesc.Count = 1;
        check_hresult(m_device->CreateGraphicsPipelineState(&pipeline,
            __uuidof(ID3D12PipelineState), m_pipeline.put_void()));

        m_vertices = UploadBuffer(m_device.get(), vertices, sizeof(vertices));
        m_vertexView = {m_vertices->GetGPUVirtualAddress(), sizeof(vertices), sizeof(Vertex)};
        m_indices = UploadBuffer(m_device.get(), indices, sizeof(indices));
        m_indexView = {m_indices->GetGPUVirtualAddress(), sizeof(indices), DXGI_FORMAT_R16_UINT};
    }

    void Resize() {
        // Bounds are in view pixels; the swap chain wants physical ones.
        const auto bounds = m_window.Bounds();
        const double scale = DisplayInformation::GetForCurrentView().RawPixelsPerViewPixel();
        const UINT width = std::max(1u, UINT(std::lround(bounds.Width * scale)));
        const UINT height = std::max(1u, UINT(std::lround(bounds.Height * scale)));
        WaitForGpu();
        for (auto& target : m_renderTargets) target = nullptr;
        m_depth = nullptr;
        if (m_swapChain) {
            check_hresult(m_swapChain->ResizeBuffers(FrameCount, width, height, BackBufferFormat, 0));
        } else {
            com_ptr<IDXGIFactory2> factory;
            check_hresult(CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), factory.put_void()));
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width = width;
            desc.Height = height;
            desc.Format = BackBufferFormat;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = FrameCount;
            desc.Scaling = DXGI_SCALING_STRETCH;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            com_ptr<IDXGISwapChain1> swapChain;
            check_hresult(factory->CreateSwapChainForCoreWindow(m_queue.get(),
                winrt::get_unknown(m_window), &desc, nullptr, swapChain.put()));
            m_swapChain = swapChain.as<IDXGISwapChain3>();
        }

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
        for (UINT i = 0; i < FrameCount; ++i) {
            check_hresult(m_swapChain->GetBuffer(i, __uuidof(ID3D12Resource),
                m_renderTargets[i].put_void()));
            m_device->CreateRenderTargetView(m_renderTargets[i].get(), nullptr, rtv);
            rtv.Offset(1, m_rtvStride);
        }
        const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
        const auto depth = CD3DX12_RESOURCE_DESC::Tex2D(DepthFormat, width, height, 1, 1,
            1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
        const CD3DX12_CLEAR_VALUE clear(DepthFormat, 1, 0);
        check_hresult(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &depth,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, __uuidof(ID3D12Resource), m_depth.put_void()));
        m_device->CreateDepthStencilView(m_depth.get(), nullptr,
            m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
        m_viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, float(width), float(height));
        m_scissor = CD3DX12_RECT(0, 0, LONG(width), LONG(height));
        m_resize = false;
    }

    void Render(float angle) {
        // Reuse this back buffer's allocator only after the GPU finished its last frame.
        const UINT frame = m_swapChain->GetCurrentBackBufferIndex();
        WaitForFence(m_frameFence[frame]);
        check_hresult(m_allocators[frame]->Reset());
        check_hresult(m_commands->Reset(m_allocators[frame].get(), m_pipeline.get()));

        auto* target = m_renderTargets[frame].get();
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(target, D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_commands->ResourceBarrier(1, &barrier);
        const CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
            frame, m_rtvStride);
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
        const float background[] = {0.025f, 0.045f, 0.075f, 1};
        m_commands->ClearRenderTargetView(rtv, background, 0, nullptr);
        m_commands->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
        m_commands->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        m_commands->RSSetViewports(1, &m_viewport);
        m_commands->RSSetScissorRects(1, &m_scissor);
        m_commands->SetGraphicsRootSignature(m_rootSignature.get());
        const float scene[] = {angle, m_viewport.Width / m_viewport.Height, 0, 0};
        m_commands->SetGraphicsRoot32BitConstants(0, 4, scene, 0);
        m_commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commands->IASetVertexBuffers(0, 1, &m_vertexView);
        m_commands->IASetIndexBuffer(&m_indexView);
        m_commands->DrawIndexedInstanced(ARRAYSIZE(indices), 1, 0, 0, 0);
        barrier = CD3DX12_RESOURCE_BARRIER::Transition(target, D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);
        m_commands->ResourceBarrier(1, &barrier);
        check_hresult(m_commands->Close());
        ID3D12CommandList* lists[] = {m_commands.get()};
        m_queue->ExecuteCommandLists(1, lists);

        // A lost device ends the game here; a real one would recreate its resources.
        check_hresult(m_swapChain->Present(1, 0));
        m_frameFence[frame] = ++m_fenceValue;
        check_hresult(m_queue->Signal(m_fence.get(), m_fenceValue));
    }

    // A removed device reports UINT64_MAX as its completed value, so these never hang.
    void WaitForFence(UINT64 value) {
        if (m_fence->GetCompletedValue() >= value) return;
        check_hresult(m_fence->SetEventOnCompletion(value, m_fenceEvent.get()));
        WaitForSingleObjectEx(m_fenceEvent.get(), INFINITE, FALSE);
    }

    void WaitForGpu() {
        check_hresult(m_queue->Signal(m_fence.get(), ++m_fenceValue));
        WaitForFence(m_fenceValue);
    }

    CoreWindow m_window{nullptr};
    bool m_resize = true;
    bool m_closed = false;

    com_ptr<ID3D12Device> m_device;
    com_ptr<ID3D12CommandQueue> m_queue;
    com_ptr<ID3D12CommandAllocator> m_allocators[FrameCount];
    com_ptr<ID3D12GraphicsCommandList> m_commands;
    com_ptr<ID3D12Fence> m_fence;
    handle m_fenceEvent;
    UINT64 m_fenceValue = 0;
    UINT64 m_frameFence[FrameCount]{};

    com_ptr<IDXGISwapChain3> m_swapChain;
    com_ptr<ID3D12Resource> m_renderTargets[FrameCount];
    com_ptr<ID3D12Resource> m_depth;
    com_ptr<ID3D12DescriptorHeap> m_rtvHeap, m_dsvHeap;
    UINT m_rtvStride = 0;

    com_ptr<ID3D12RootSignature> m_rootSignature;
    com_ptr<ID3D12PipelineState> m_pipeline;
    com_ptr<ID3D12Resource> m_vertices, m_indices;
    D3D12_VERTEX_BUFFER_VIEW m_vertexView{};
    D3D12_INDEX_BUFFER_VIEW m_indexView{};
    D3D12_VIEWPORT m_viewport{};
    D3D12_RECT m_scissor{};
};

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        init_apartment();
        CoreApplication::Run(make<Game>());
    } catch (hresult_error const& error) {
        Record(error);
        throw;
    }
}
