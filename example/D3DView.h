#pragma once

#include "pch.h"
#include <d3d11.h>
#include <dxgi1_3.h>

namespace hello {

// All panel, swap-chain, and immediate-context access stays on the UI thread.
// This small demo renders on XAML's frame callback, without a worker thread.
class D3DView {
  public:
    D3DView(winrt::Windows::UI::Xaml::Controls::SwapChainPanel const& panel,
            winrt::Windows::UI::Xaml::Controls::TextBlock const& status);
    ~D3DView();
    D3DView(D3DView const&) = delete;
    D3DView& operator=(D3DView const&) = delete;

  private:
    void Start();
    void Stop();
    void Frame();
    void CreateDevice();
    void Resize();
    void Draw(float angle);
    void ReleaseDevice();

    winrt::Windows::UI::Xaml::Controls::SwapChainPanel m_panel{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_status{nullptr};
    winrt::Windows::UI::Xaml::FrameworkElement::Loaded_revoker m_loaded;
    winrt::Windows::UI::Xaml::FrameworkElement::Unloaded_revoker m_unloaded;
    winrt::Windows::UI::Xaml::FrameworkElement::SizeChanged_revoker m_sizeChanged;
    winrt::Windows::UI::Xaml::Controls::SwapChainPanel::CompositionScaleChanged_revoker m_scaleChanged;
    winrt::Windows::UI::Xaml::Media::CompositionTarget::Rendering_revoker m_rendering;
    bool m_resize = true;
    bool m_presented = false;
    LARGE_INTEGER m_start{}, m_frequency{};

    winrt::com_ptr<ID3D11Device> m_device;
    winrt::com_ptr<ID3D11DeviceContext> m_context;
    winrt::com_ptr<IDXGISwapChain2> m_swapChain;
    winrt::com_ptr<ID3D11RenderTargetView> m_target;
    winrt::com_ptr<ID3D11DepthStencilView> m_depth;
    winrt::com_ptr<ID3D11VertexShader> m_vertexShader;
    winrt::com_ptr<ID3D11PixelShader> m_pixelShader;
    winrt::com_ptr<ID3D11InputLayout> m_inputLayout;
    winrt::com_ptr<ID3D11Buffer> m_vertices, m_indices, m_scene;
    winrt::com_ptr<ID3D11RasterizerState> m_rasterizer;
    D3D11_VIEWPORT m_viewport{};
};

} // namespace hello
