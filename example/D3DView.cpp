#include "pch.h"
#include "D3DView.h"

#include <windows.ui.xaml.media.dxinterop.h>
#include <CubeVertexShader.h>
#include <CubePixelShader.h>
#include <algorithm>
#include <cmath>

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
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
        m_device.put(), nullptr, m_context.put()));

    check_hresult(m_device->CreateVertexShader(cubeVertexShader, sizeof(cubeVertexShader),
        nullptr, m_vertexShader.put()));
    check_hresult(m_device->CreatePixelShader(cubePixelShader, sizeof(cubePixelShader),
        nullptr, m_pixelShader.put()));
    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
    };
    check_hresult(m_device->CreateInputLayout(layout, ARRAYSIZE(layout), cubeVertexShader,
        sizeof(cubeVertexShader), m_inputLayout.put()));

    D3D11_BUFFER_DESC buffer{};
    buffer.Usage = D3D11_USAGE_IMMUTABLE;
    buffer.ByteWidth = sizeof(vertices);
    buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = vertices;
    check_hresult(m_device->CreateBuffer(&buffer, &data, m_vertices.put()));
    buffer.ByteWidth = sizeof(indices);
    buffer.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = indices;
    check_hresult(m_device->CreateBuffer(&buffer, &data, m_indices.put()));
    buffer.ByteWidth = 16;
    buffer.Usage = D3D11_USAGE_DEFAULT;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    check_hresult(m_device->CreateBuffer(&buffer, nullptr, m_scene.put()));
    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.DepthClipEnable = TRUE;
    check_hresult(m_device->CreateRasterizerState(&rasterizer, m_rasterizer.put()));
    m_resize = true;
}

void D3DView::Resize() {
    const float scaleX = m_panel.CompositionScaleX(), scaleY = m_panel.CompositionScaleY();
    const UINT width = std::max(1u, static_cast<UINT>(std::ceil(m_panel.ActualWidth() * scaleX)));
    const UINT height = std::max(1u, static_cast<UINT>(std::ceil(m_panel.ActualHeight() * scaleY)));
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_target = nullptr;
    m_depth = nullptr;
    if (m_swapChain) {
        check_hresult(m_swapChain->ResizeBuffers(2, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0));
    } else {
        auto dxgiDevice = m_device.as<IDXGIDevice>();
        com_ptr<IDXGIAdapter> adapter;
        check_hresult(dxgiDevice->GetAdapter(adapter.put()));
        com_ptr<IDXGIFactory2> factory;
        check_hresult(adapter->GetParent(__uuidof(IDXGIFactory2), factory.put_void()));
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        com_ptr<IDXGISwapChain1> swapChain;
        check_hresult(factory->CreateSwapChainForComposition(m_device.get(), &desc, nullptr,
            swapChain.put()));
        m_swapChain = swapChain.as<IDXGISwapChain2>();
        check_hresult(m_panel.as<ISwapChainPanelNative>()->SetSwapChain(m_swapChain.get()));
    }
    DXGI_MATRIX_3X2_F inverseScale{1 / scaleX, 0, 0, 1 / scaleY, 0, 0};
    check_hresult(m_swapChain->SetMatrixTransform(&inverseScale));
    com_ptr<ID3D11Texture2D> backBuffer;
    check_hresult(m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), backBuffer.put_void()));
    check_hresult(m_device->CreateRenderTargetView(backBuffer.get(), nullptr, m_target.put()));
    D3D11_TEXTURE2D_DESC depth{};
    depth.Width = width;
    depth.Height = height;
    depth.MipLevels = depth.ArraySize = 1;
    depth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depth.SampleDesc.Count = 1;
    depth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    com_ptr<ID3D11Texture2D> depthTexture;
    check_hresult(m_device->CreateTexture2D(&depth, nullptr, depthTexture.put()));
    check_hresult(m_device->CreateDepthStencilView(depthTexture.get(), nullptr, m_depth.put()));
    m_viewport = {0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    m_resize = false;
}

void D3DView::Draw(float angle) {
    const float background[] = {0.025f, 0.045f, 0.075f, 1};
    m_context->ClearRenderTargetView(m_target.get(), background);
    m_context->ClearDepthStencilView(m_depth.get(), D3D11_CLEAR_DEPTH, 1, 0);
    const float scene[] = {angle, m_viewport.Width / m_viewport.Height, 0, 0};
    m_context->UpdateSubresource(m_scene.get(), 0, nullptr, scene, 0, 0);
    auto* target = m_target.get();
    m_context->OMSetRenderTargets(1, &target, m_depth.get());
    m_context->RSSetViewports(1, &m_viewport);
    m_context->RSSetState(m_rasterizer.get());
    m_context->IASetInputLayout(m_inputLayout.get());
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    auto* vertexBuffer = m_vertices.get();
    const UINT stride = sizeof(Vertex), offset = 0;
    m_context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    m_context->IASetIndexBuffer(m_indices.get(), DXGI_FORMAT_R16_UINT, 0);
    m_context->VSSetShader(m_vertexShader.get(), nullptr, 0);
    auto* sceneBuffer = m_scene.get();
    m_context->VSSetConstantBuffers(0, 1, &sceneBuffer);
    m_context->PSSetShader(m_pixelShader.get(), nullptr, 0);
    m_context->DrawIndexed(ARRAYSIZE(indices), 0, 0);
}

void D3DView::ReleaseDevice() {
    check_hresult(m_panel.as<ISwapChainPanelNative>()->SetSwapChain(nullptr));
    m_context->ClearState();
    m_swapChain = nullptr;
    m_target = nullptr;
    m_depth = nullptr;
    m_vertexShader = nullptr;
    m_pixelShader = nullptr;
    m_inputLayout = nullptr;
    m_vertices = nullptr;
    m_indices = nullptr;
    m_scene = nullptr;
    m_rasterizer = nullptr;
    m_context = nullptr;
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
        if (!m_presented) {
            m_status.Text(L"Direct3D 11 · hardware · rotating cube");
            m_status.Foreground(SolidColorBrush{winrt::Windows::UI::Colors::LightGreen()});
            m_presented = true;
        }
    } catch (hresult_error const& error) {
        Stop();
        m_status.Text(L"Direct3D failed: " + error.message());
        m_status.Foreground(SolidColorBrush{winrt::Windows::UI::Colors::Red()});
    }
}

} // namespace hello
