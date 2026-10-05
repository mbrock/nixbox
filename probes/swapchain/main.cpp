// Which swapchains reach the screen: cycles through sizes, formats, and
// colour spaces every eight seconds, drawing a one-pixel stripe pattern so a
// console screenshot shows whether the output kept every swapchain pixel.
// Each phase's band has its own colour; the report, with the HDMI modes and
// the measured present cadence, goes to LocalState/swapchain.txt.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>

#include <d3dx12.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Display.Core.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.UI.Core.h>

#include <patternPixelShader.h>
#include <patternVertexShader.h>

using namespace winrt;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::Graphics::Display;
using namespace winrt::Windows::Graphics::Display::Core;
using namespace winrt::Windows::UI::Core;

namespace {

constexpr UINT FrameCount = 2;
constexpr double PhaseSeconds = 8;

struct Phase {
    const char *name;
    UINT width, height;
    DXGI_FORMAT format;
    DXGI_COLOR_SPACE_TYPE colorSpace;
    float tag[3];
};

const Phase phases[] = {
    {"4K BGRA8 sRGB", 3840, 2160, DXGI_FORMAT_B8G8R8A8_UNORM,
     DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, {1, 0, 0}},
    {"1080p BGRA8 sRGB", 1920, 1080, DXGI_FORMAT_B8G8R8A8_UNORM,
     DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, {0, 1, 0}},
    {"4K RGB10A2 HDR10", 3840, 2160, DXGI_FORMAT_R10G10B10A2_UNORM,
     DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, {0, 0, 1}},
    {"4K RGBA16F scRGB", 3840, 2160, DXGI_FORMAT_R16G16B16A16_FLOAT,
     DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, {1, 1, 0}},
    {"1440p BGRA8 sRGB", 2560, 1440, DXGI_FORMAT_B8G8R8A8_UNORM,
     DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, {1, 0, 1}},
};

const char *colorSpaceName(DXGI_COLOR_SPACE_TYPE space)
{
    switch (space) {
    case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709: return "sRGB";
    case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020: return "HDR10";
    case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709: return "scRGB";
    default: return "?";
    }
}

// SMPTE ST 2084 encoding of an absolute luminance.
float pq(float nits)
{
    const float m1 = 0.1593017578125f, m2 = 78.84375f;
    const float c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;
    const float y = std::pow(nits / 10000.0f, m1);
    return std::pow((c1 + c2 * y) / (1 + c3 * y), m2);
}

// The "white" value that shows about 200 nits in each colour space.
float whiteFor(DXGI_COLOR_SPACE_TYPE space)
{
    if (space == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
        return pq(200);
    if (space == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)
        return 200.0f / 80.0f;
    return 1;
}

std::vector<std::string> lines;

void writeReport()
{
    const auto path = to_string(
        winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path()) + "\\swapchain.txt";
    if (FILE *file = std::fopen(path.c_str(), "w")) {
        for (const std::string &text : lines)
            std::fprintf(file, "%s\n", text.c_str());
        std::fclose(file);
    }
}

void line(const char *format, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    lines.emplace_back(buffer);
    writeReport();
}

const char *hdmiColorSpace(HdmiDisplayColorSpace space)
{
    switch (space) {
    case HdmiDisplayColorSpace::RgbLimited: return "RGB limited";
    case HdmiDisplayColorSpace::RgbFull: return "RGB full";
    case HdmiDisplayColorSpace::BT2020: return "BT.2020";
    case HdmiDisplayColorSpace::BT709: return "BT.709";
    }
    return "?";
}

void describeMode(const char *label, HdmiDisplayMode const &mode)
{
    line("%s %ux%u @ %.3f Hz, %u bpp, %s, HDR10 %d, SDR luminance %d, 2086 metadata %d", label,
         mode.ResolutionWidthInRawPixels(), mode.ResolutionHeightInRawPixels(), mode.RefreshRate(),
         mode.BitsPerPixel(), hdmiColorSpace(mode.ColorSpace()), mode.IsSmpte2084Supported(),
         mode.IsSdrLuminanceSupported(), mode.Is2086MetadataSupported());
}

void describeDisplay(CoreWindow const &window)
{
    const auto bounds = window.Bounds();
    line("window bounds: %.0f x %.0f view pixels", bounds.Width, bounds.Height);
    try {
        auto display = DisplayInformation::GetForCurrentView();
        line("view: %u x %u raw pixels, %.2f raw per view pixel", display.ScreenWidthInRawPixels(),
             display.ScreenHeightInRawPixels(), display.RawPixelsPerViewPixel());
    } catch (hresult_error const &error) {
        line("DisplayInformation: 0x%08x", uint32_t(error.code().value));
    }
    try {
        auto hdmi = HdmiDisplayInformation::GetForCurrentView();
        if (!hdmi) {
            line("HDMI: no information for this view");
            return;
        }
        describeMode("HDMI current:", hdmi.GetCurrentDisplayMode());
        for (auto const &mode : hdmi.GetSupportedDisplayModes())
            describeMode("  supported:", mode);
    } catch (hresult_error const &error) {
        line("HDMI: 0x%08x", uint32_t(error.code().value));
    }
}

struct Probe : implements<Probe, IFrameworkViewSource, IFrameworkView> {
    IFrameworkView CreateView() { return *this; }

    void Initialize(CoreApplicationView const &view)
    {
        view.Activated([](auto &&, auto &&) { CoreWindow::GetForCurrentThread().Activate(); });
    }

    void SetWindow(CoreWindow const &window)
    {
        m_window = window;
        window.Closed([this](auto &&, auto &&) { m_closed = true; });
    }

    void Load(hstring const &) {}
    void Uninitialize() {}

    void Run()
    {
        try {
            Loop();
        } catch (hresult_error const &error) {
            line("error 0x%08x: %s", uint32_t(error.code().value), to_string(error.message()).c_str());
        }
    }

  private:
    void Loop()
    {
        describeDisplay(m_window);
        CreateDevice();
        LARGE_INTEGER frequency{}, start{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start);
        int current = -1;
        while (!m_closed) {
            m_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            const double seconds = double(now.QuadPart - start.QuadPart) / double(frequency.QuadPart);
            const int phase = int(seconds / PhaseSeconds) % int(std::size(phases));
            if (phase != current) {
                if (current >= 0)
                    ReportCadence(current);
                if (seconds < PhaseSeconds * std::size(phases))
                    line("-- %.1f s: phase %d, %s", seconds, phase, phases[phase].name);
                current = phase;
                m_ready = Configure(phases[phase], seconds < PhaseSeconds * std::size(phases));
                m_intervals.clear();
                m_lastPresent = 0;
            }
            if (!m_ready) {
                Sleep(16);
                continue;
            }
            Render(phases[phase]);
            QueryPerformanceCounter(&now);
            if (m_lastPresent)
                m_intervals.push_back(1000.0 * double(now.QuadPart - m_lastPresent)
                                      / double(frequency.QuadPart));
            m_lastPresent = now.QuadPart;
        }
        WaitForGpu();
    }

    void ReportCadence(int phase)
    {
        if (m_reported[phase] || m_intervals.size() < 10)
            return;
        m_reported[phase] = true;
        // Skip the first frames after a resize.
        std::vector<double> sorted(m_intervals.begin() + 5, m_intervals.end());
        std::sort(sorted.begin(), sorted.end());
        double sum = 0;
        for (double ms : sorted)
            sum += ms;
        const double mean = sum / sorted.size();
        line("  present: %zu frames, mean %.3f ms (%.2f Hz), median %.3f, min %.3f, max %.3f",
             sorted.size(), mean, 1000.0 / mean, sorted[sorted.size() / 2], sorted.front(),
             sorted.back());
    }

    void CreateDevice()
    {
        check_hresult(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                        m_device.put_void()));
        const D3D12_COMMAND_QUEUE_DESC queue{D3D12_COMMAND_LIST_TYPE_DIRECT};
        check_hresult(m_device->CreateCommandQueue(&queue, __uuidof(ID3D12CommandQueue),
                                                   m_queue.put_void()));
        for (auto &allocator : m_allocators)
            check_hresult(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                           __uuidof(ID3D12CommandAllocator),
                                                           allocator.put_void()));
        check_hresult(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  m_allocators[0].get(), nullptr,
                                                  __uuidof(ID3D12GraphicsCommandList),
                                                  m_commands.put_void()));
        check_hresult(m_commands->Close());
        check_hresult(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                            m_fence.put_void()));
        m_fenceEvent.attach(CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS));

        D3D12_DESCRIPTOR_HEAP_DESC heap{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FrameCount};
        check_hresult(m_device->CreateDescriptorHeap(&heap, __uuidof(ID3D12DescriptorHeap),
                                                     m_rtvHeap.put_void()));
        m_rtvStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        CD3DX12_ROOT_PARAMETER constants;
        constants.InitAsConstants(8, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
        const CD3DX12_ROOT_SIGNATURE_DESC root(1, &constants);
        com_ptr<ID3DBlob> signature, error;
        check_hresult(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1,
                                                  signature.put(), error.put()));
        check_hresult(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
                                                    signature->GetBufferSize(),
                                                    __uuidof(ID3D12RootSignature),
                                                    m_root.put_void()));

        for (const DXGI_FORMAT format : {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM,
                                         DXGI_FORMAT_R16G16B16A16_FLOAT}) {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = m_root.get();
            desc.VS = {patternVertexShader, sizeof(patternVertexShader)};
            desc.PS = {patternPixelShader, sizeof(patternPixelShader)};
            desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            desc.SampleMask = UINT_MAX;
            desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            desc.DepthStencilState.DepthEnable = FALSE;
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.NumRenderTargets = 1;
            desc.RTVFormats[0] = format;
            desc.SampleDesc.Count = 1;
            com_ptr<ID3D12PipelineState> pipeline;
            check_hresult(m_device->CreateGraphicsPipelineState(
                &desc, __uuidof(ID3D12PipelineState), pipeline.put_void()));
            m_pipelines.push_back({format, pipeline});
        }
    }

    // Creates or resizes the swapchain for this phase; reports what DXGI says
    // the first time through the cycle.
    bool Configure(Phase const &phase, bool report)
    {
        WaitForGpu();
        for (auto &target : m_targets)
            target = nullptr;
        HRESULT hr;
        if (m_swapChain) {
            hr = m_swapChain->ResizeBuffers(FrameCount, phase.width, phase.height, phase.format, 0);
        } else {
            com_ptr<IDXGIFactory2> factory;
            check_hresult(CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), factory.put_void()));
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width = phase.width;
            desc.Height = phase.height;
            desc.Format = phase.format;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = FrameCount;
            desc.Scaling = DXGI_SCALING_STRETCH;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            com_ptr<IDXGISwapChain1> swapChain;
            hr = factory->CreateSwapChainForCoreWindow(m_queue.get(), winrt::get_unknown(m_window),
                                                       &desc, nullptr, swapChain.put());
            if (SUCCEEDED(hr))
                m_swapChain = swapChain.as<IDXGISwapChain3>();
        }
        if (FAILED(hr)) {
            if (report)
                line("  swapchain %ux%u: FAILED 0x%08x", phase.width, phase.height, uint32_t(hr));
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 actual{};
        m_swapChain->GetDesc1(&actual);
        UINT support = 0;
        m_swapChain->CheckColorSpaceSupport(phase.colorSpace, &support);
        const HRESULT set = (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)
                                ? m_swapChain->SetColorSpace1(phase.colorSpace)
                                : E_NOTIMPL;
        if (report) {
            line("  swapchain %ux%u format %d: ok", actual.Width, actual.Height, actual.Format);
            line("  colour space %s: support 0x%x, set %s (0x%08x)", colorSpaceName(phase.colorSpace),
                 support, SUCCEEDED(set) ? "ok" : "no", uint32_t(set));
            com_ptr<IDXGISwapChain4> four = m_swapChain.try_as<IDXGISwapChain4>();
            if (four && phase.colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
                && SUCCEEDED(set)) {
                DXGI_HDR_METADATA_HDR10 metadata{};
                metadata.MaxMasteringLuminance = 1000;
                metadata.MaxContentLightLevel = 1000;
                metadata.MaxFrameAverageLightLevel = 200;
                const HRESULT meta = four->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10,
                                                          sizeof metadata, &metadata);
                line("  HDR10 metadata: 0x%08x", uint32_t(meta));
            }
        }

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
        for (UINT i = 0; i < FrameCount; ++i) {
            check_hresult(m_swapChain->GetBuffer(i, __uuidof(ID3D12Resource), m_targets[i].put_void()));
            m_device->CreateRenderTargetView(m_targets[i].get(), nullptr, rtv);
            rtv.Offset(1, m_rtvStride);
        }
        m_white = SUCCEEDED(set) ? whiteFor(phase.colorSpace) : 1;
        return true;
    }

    void Render(Phase const &phase)
    {
        const UINT frame = m_swapChain->GetCurrentBackBufferIndex();
        WaitForFence(m_frameFence[frame]);
        check_hresult(m_allocators[frame]->Reset());
        ID3D12PipelineState *pipeline = nullptr;
        for (auto &[format, candidate] : m_pipelines)
            if (format == phase.format)
                pipeline = candidate.get();
        check_hresult(m_commands->Reset(m_allocators[frame].get(), pipeline));

        auto *target = m_targets[frame].get();
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(target, D3D12_RESOURCE_STATE_PRESENT,
                                                            D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_commands->ResourceBarrier(1, &barrier);
        const CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
                                                frame, m_rtvStride);
        m_commands->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        const CD3DX12_VIEWPORT viewport(0.0f, 0.0f, float(phase.width), float(phase.height));
        const CD3DX12_RECT scissor(0, 0, LONG(phase.width), LONG(phase.height));
        m_commands->RSSetViewports(1, &viewport);
        m_commands->RSSetScissorRects(1, &scissor);
        m_commands->SetGraphicsRootSignature(m_root.get());
        const float constants[8] = {phase.tag[0], phase.tag[1], phase.tag[2], m_white,
                                    float(phase.width), float(phase.height), 0, 0};
        m_commands->SetGraphicsRoot32BitConstants(0, 8, constants, 0);
        m_commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commands->DrawInstanced(3, 1, 0, 0);
        barrier = CD3DX12_RESOURCE_BARRIER::Transition(target, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                       D3D12_RESOURCE_STATE_PRESENT);
        m_commands->ResourceBarrier(1, &barrier);
        check_hresult(m_commands->Close());
        ID3D12CommandList *lists[] = {m_commands.get()};
        m_queue->ExecuteCommandLists(1, lists);
        check_hresult(m_swapChain->Present(1, 0));
        m_frameFence[frame] = ++m_fenceValue;
        check_hresult(m_queue->Signal(m_fence.get(), m_fenceValue));
    }

    void WaitForFence(UINT64 value)
    {
        if (m_fence->GetCompletedValue() >= value)
            return;
        check_hresult(m_fence->SetEventOnCompletion(value, m_fenceEvent.get()));
        WaitForSingleObjectEx(m_fenceEvent.get(), INFINITE, FALSE);
    }

    void WaitForGpu()
    {
        if (!m_queue)
            return;
        check_hresult(m_queue->Signal(m_fence.get(), ++m_fenceValue));
        WaitForFence(m_fenceValue);
    }

    CoreWindow m_window{nullptr};
    bool m_closed = false;
    bool m_ready = false;
    bool m_reported[std::size(phases)]{};
    std::vector<double> m_intervals;
    LONGLONG m_lastPresent = 0;
    float m_white = 1;

    com_ptr<ID3D12Device> m_device;
    com_ptr<ID3D12CommandQueue> m_queue;
    com_ptr<ID3D12CommandAllocator> m_allocators[FrameCount];
    com_ptr<ID3D12GraphicsCommandList> m_commands;
    com_ptr<ID3D12Fence> m_fence;
    handle m_fenceEvent;
    UINT64 m_fenceValue = 0;
    UINT64 m_frameFence[FrameCount]{};
    com_ptr<IDXGISwapChain3> m_swapChain;
    com_ptr<ID3D12Resource> m_targets[FrameCount];
    com_ptr<ID3D12DescriptorHeap> m_rtvHeap;
    UINT m_rtvStride = 0;
    com_ptr<ID3D12RootSignature> m_root;
    std::vector<std::pair<DXGI_FORMAT, com_ptr<ID3D12PipelineState>>> m_pipelines;
};

} // namespace

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    init_apartment();
    CoreApplication::Run(make<Probe>());
}
