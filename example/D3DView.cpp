#include "pch.h"
#include "D3DView.h"

#include <windows.ui.xaml.media.dxinterop.h>
#include <CubeVertexShader.h>
#include <CubePixelShader.h>
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace winrt;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Media;

namespace hello {
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

constexpr DXGI_FORMAT BackBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr DXGI_FORMAT DepthFormat = DXGI_FORMAT_D32_FLOAT;

D3D12_RESOURCE_DESC TextureDesc(UINT64 width, UINT height, DXGI_FORMAT format,
                                D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = flags;
    return desc;
}

// The mesh never changes, so it stays in an upload heap the GPU reads directly
// rather than being copied into a default heap.
com_ptr<ID3D12Resource> UploadBuffer(ID3D12Device* device, void const* data, UINT64 size) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    com_ptr<ID3D12Resource> buffer;
    check_hresult(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), buffer.put_void()));
    void* mapped = nullptr;
    const D3D12_RANGE noRead{0, 0};
    check_hresult(buffer->Map(0, &noRead, &mapped));
    std::memcpy(mapped, data, size);
    buffer->Unmap(0, nullptr);
    return buffer;
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                  D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}
}

D3DView::D3DView(Controls::SwapChainPanel const& panel, Controls::TextBlock const& status)
    : m_panel(panel), m_status(status) {
    QueryPerformanceFrequency(&m_frequency);
    QueryPerformanceCounter(&m_start);
    m_loaded = m_panel.Loaded(auto_revoke, [this](auto&&, auto&&) { Start(); });
    m_unloaded = m_panel.Unloaded(auto_revoke, [this](auto&&, auto&&) { Stop(); });
    m_sizeChanged = m_panel.SizeChanged(auto_revoke, [this](auto&&, auto&&) { m_resize = true; });
    m_scaleChanged = m_panel.CompositionScaleChanged(auto_revoke,
        [this](auto&&, auto&&) { m_resize = true; });
}

D3DView::~D3DView() {
    Stop();
    // Resources must outlive any GPU work that still references them.
    WaitForGpu();
    // Loaded/size revokers are destroyed before the panel member. The COM
    // interop method returns an HRESULT; no exception may escape destruction.
    if (auto native = m_panel.try_as<ISwapChainPanelNative>())
        native->SetSwapChain(nullptr);
}

void D3DView::Start() {
    Stop();
    m_rendering = CompositionTarget::Rendering(auto_revoke, [this](auto&&, auto&&) { Frame(); });
}

void D3DView::Stop() { m_rendering.revoke(); }

void D3DView::CreateDevice() {
    check_hresult(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
        m_device.put_void()));

    D3D12_COMMAND_QUEUE_DESC queue{};
    queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    check_hresult(m_device->CreateCommandQueue(&queue, __uuidof(ID3D12CommandQueue),
        m_queue.put_void()));
    for (auto& allocator : m_allocators)
        check_hresult(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            __uuidof(ID3D12CommandAllocator), allocator.put_void()));
    check_hresult(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_allocators[0].get(), nullptr, __uuidof(ID3D12GraphicsCommandList), m_commands.put_void()));
    check_hresult(m_commands->Close());
    check_hresult(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
        m_fence.put_void()));
    m_fenceValue = 0;
    std::fill(std::begin(m_frameFence), std::end(m_frameFence), 0);
    m_fenceEvent.attach(CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS));
    if (!m_fenceEvent) throw_last_error();

    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.NumDescriptors = FrameCount;
    check_hresult(m_device->CreateDescriptorHeap(&heap, __uuidof(ID3D12DescriptorHeap),
        m_rtvHeap.put_void()));
    m_rtvStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heap.NumDescriptors = 1;
    check_hresult(m_device->CreateDescriptorHeap(&heap, __uuidof(ID3D12DescriptorHeap),
        m_dsvHeap.put_void()));

    // The shader's Scene cbuffer (b0) is fed as four root constants per draw.
    D3D12_ROOT_PARAMETER scene{};
    scene.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    scene.Constants.ShaderRegister = 0;
    scene.Constants.Num32BitValues = 4;
    scene.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_ROOT_SIGNATURE_DESC root{};
    root.NumParameters = 1;
    root.pParameters = &scene;
    root.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
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
    pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline.SampleMask = UINT_MAX;
    pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipeline.RasterizerState.DepthClipEnable = TRUE;
    pipeline.DepthStencilState.DepthEnable = TRUE;
    pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
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
    m_resize = true;
}

void D3DView::Resize() {
    const float scaleX = m_panel.CompositionScaleX(), scaleY = m_panel.CompositionScaleY();
    const UINT width = std::max(1u, static_cast<UINT>(std::ceil(m_panel.ActualWidth() * scaleX)));
    const UINT height = std::max(1u, static_cast<UINT>(std::ceil(m_panel.ActualHeight() * scaleY)));
    // ResizeBuffers requires every back-buffer reference, including the GPU's, to be gone.
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
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        // In Direct3D 12 the swap chain presents through the command queue.
        com_ptr<IDXGISwapChain1> swapChain;
        check_hresult(factory->CreateSwapChainForComposition(m_queue.get(), &desc, nullptr,
            swapChain.put()));
        m_swapChain = swapChain.as<IDXGISwapChain3>();
        check_hresult(m_panel.as<ISwapChainPanelNative>()->SetSwapChain(m_swapChain.get()));
    }
    DXGI_MATRIX_3X2_F inverseScale{1 / scaleX, 0, 0, 1 / scaleY, 0, 0};
    check_hresult(m_swapChain->SetMatrixTransform(&inverseScale));

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < FrameCount; ++i) {
        check_hresult(m_swapChain->GetBuffer(i, __uuidof(ID3D12Resource),
            m_renderTargets[i].put_void()));
        m_device->CreateRenderTargetView(m_renderTargets[i].get(), nullptr, rtv);
        rtv.ptr += m_rtvStride;
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    const auto depth = TextureDesc(width, height, DepthFormat,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DepthFormat;
    clear.DepthStencil.Depth = 1;
    check_hresult(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &depth,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, __uuidof(ID3D12Resource), m_depth.put_void()));
    m_device->CreateDepthStencilView(m_depth.get(), nullptr,
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
    m_viewport = {0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    m_scissor = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    m_resize = false;
}

void D3DView::Draw(float angle) {
    // Reuse this back buffer's allocator only after the GPU finished its last frame.
    m_frame = m_swapChain->GetCurrentBackBufferIndex();
    WaitForFence(m_frameFence[m_frame]);
    auto* allocator = m_allocators[m_frame].get();
    check_hresult(allocator->Reset());
    check_hresult(m_commands->Reset(allocator, m_pipeline.get()));

    auto* target = m_renderTargets[m_frame].get();
    auto barrier = Transition(target, D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_commands->ResourceBarrier(1, &barrier);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += m_frame * m_rtvStride;
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
    barrier = Transition(target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_commands->ResourceBarrier(1, &barrier);
    check_hresult(m_commands->Close());
    ID3D12CommandList* lists[] = {m_commands.get()};
    m_queue->ExecuteCommandLists(1, lists);
}

// A removed device reports UINT64_MAX as its completed value, so these never hang.
void D3DView::WaitForFence(UINT64 value) noexcept {
    if (m_fence->GetCompletedValue() >= value) return;
    if (SUCCEEDED(m_fence->SetEventOnCompletion(value, m_fenceEvent.get())))
        WaitForSingleObjectEx(m_fenceEvent.get(), INFINITE, FALSE);
}

void D3DView::WaitForGpu() noexcept {
    if (m_queue && m_fence && SUCCEEDED(m_queue->Signal(m_fence.get(), m_fenceValue + 1)))
        WaitForFence(++m_fenceValue);
}

void D3DView::ReleaseDevice() {
    WaitForGpu();
    check_hresult(m_panel.as<ISwapChainPanelNative>()->SetSwapChain(nullptr));
    m_swapChain = nullptr;
    for (auto& target : m_renderTargets) target = nullptr;
    m_depth = nullptr;
    m_rtvHeap = nullptr;
    m_dsvHeap = nullptr;
    m_vertices = nullptr;
    m_indices = nullptr;
    m_pipeline = nullptr;
    m_rootSignature = nullptr;
    m_commands = nullptr;
    for (auto& allocator : m_allocators) allocator = nullptr;
    m_fence = nullptr;
    m_fenceEvent.close();
    m_queue = nullptr;
    m_device = nullptr;
    m_presented = false;
}

void D3DView::Frame() {
    if (m_panel.ActualWidth() <= 0 || m_panel.ActualHeight() <= 0)
        return;
    try {
        if (!m_device) CreateDevice();
        if (m_resize) Resize();
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const float seconds = static_cast<float>(now.QuadPart - m_start.QuadPart) / m_frequency.QuadPart;
        Draw(seconds * 0.65f);
        const HRESULT result = m_swapChain->Present(1, 0);
        if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET) {
            ReleaseDevice();
            return;
        }
        check_hresult(result);
        m_frameFence[m_frame] = ++m_fenceValue;
        check_hresult(m_queue->Signal(m_fence.get(), m_fenceValue));
        if (!m_presented) {
            m_status.Text(L"Direct3D 12 · hardware · rotating cube");
            m_status.Foreground(SolidColorBrush{winrt::Windows::UI::Colors::LightGreen()});
            m_presented = true;
        }
    } catch (hresult_error const& error) {
        Stop();
        m_status.Text(L"Direct3D 12 failed: " + error.message());
        m_status.Foreground(SolidColorBrush{winrt::Windows::UI::Colors::Red()});
    }
}

} // namespace hello
