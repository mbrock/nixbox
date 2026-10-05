// The display as DXGI and Windows.Graphics.Display see it: the HDMI mode the
// console is driving, which modes and HDR signalling it could switch to, and
// any DXGI outputs a UWP process can enumerate.
#include "report.h"

#include <dxgi1_6.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Display.Core.h>
#include <winrt/Windows.Graphics.Display.h>

#include <algorithm>

using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Graphics::Display;
using namespace winrt::Windows::Graphics::Display::Core;

namespace {

const char *color_space_name(HdmiDisplayColorSpace space)
{
    switch (space) {
    case HdmiDisplayColorSpace::RgbLimited: return "RGB limited";
    case HdmiDisplayColorSpace::RgbFull: return "RGB full";
    case HdmiDisplayColorSpace::BT2020: return "BT.2020";
    case HdmiDisplayColorSpace::BT709: return "BT.709";
    }
    return "?";
}

const char *encoding_name(HdmiDisplayPixelEncoding encoding)
{
    switch (encoding) {
    case HdmiDisplayPixelEncoding::Rgb444: return "RGB 4:4:4";
    case HdmiDisplayPixelEncoding::Ycc444: return "YCC 4:4:4";
    case HdmiDisplayPixelEncoding::Ycc422: return "YCC 4:2:2";
    case HdmiDisplayPixelEncoding::Ycc420: return "YCC 4:2:0";
    }
    return "?";
}

void describe_mode(const char *label, const HdmiDisplayMode &mode)
{
    line("%s %ux%u @ %.2f Hz, %u bpp, %s, %s, HDR10 %s", label,
         mode.ResolutionWidthInRawPixels(), mode.ResolutionHeightInRawPixels(),
         mode.RefreshRate(), mode.BitsPerPixel(), color_space_name(mode.ColorSpace()),
         encoding_name(mode.PixelEncoding()), mode.IsSmpte2084Supported() ? "yes" : "no");
}

void describe_hdmi()
{
    try {
        HdmiDisplayInformation hdmi = HdmiDisplayInformation::GetForCurrentView();
        if (!hdmi) {
            line("HDMI display information: none for this view");
            return;
        }
        describe_mode("HDMI mode:", hdmi.GetCurrentDisplayMode());
        auto modes = hdmi.GetSupportedDisplayModes();
        UINT widest = 0, tallest = 0, hdr = 0;
        double fastest_4k = 0, fastest = 0;
        for (const HdmiDisplayMode &mode : modes) {
            const UINT w = mode.ResolutionWidthInRawPixels(), h = mode.ResolutionHeightInRawPixels();
            if (w * h > widest * tallest)
                widest = w, tallest = h;
            fastest = std::max(fastest, mode.RefreshRate());
            if (w >= 3840)
                fastest_4k = std::max(fastest_4k, mode.RefreshRate());
            hdr += mode.IsSmpte2084Supported();
        }
        line("HDMI modes: %u, largest %ux%u, fastest %.0f Hz, at 4K %.0f Hz, %u with HDR10",
             modes.Size(), widest, tallest, fastest, fastest_4k, hdr);
    } catch (const winrt::hresult_error &error) {
        line("HDMI display information: 0x%08lx", static_cast<unsigned long>(error.code()));
    }

    try {
        DisplayInformation display = DisplayInformation::GetForCurrentView();
        line("view: %u x %u raw pixels, %.0f%% scale, %.1f dpi",
             display.ScreenWidthInRawPixels(), display.ScreenHeightInRawPixels(),
             display.RawPixelsPerViewPixel() * 100, display.RawDpiX());
        AdvancedColorInfo color = display.GetAdvancedColorInfo();
        const char *kind = "SDR";
        switch (color.CurrentAdvancedColorKind()) {
        case AdvancedColorKind::StandardDynamicRange: kind = "SDR"; break;
        case AdvancedColorKind::WideColorGamut: kind = "WCG"; break;
        case AdvancedColorKind::HighDynamicRange: kind = "HDR"; break;
        }
        line("advanced colour: %s, max %.0f nits, full frame %.0f nits, SDR white %.0f nits",
             kind, color.MaxLuminanceInNits(), color.MaxAverageFullFrameLuminanceInNits(),
             color.SdrWhiteLevelInNits());
    } catch (const winrt::hresult_error &error) {
        line("display information: 0x%08lx", static_cast<unsigned long>(error.code()));
    }
}

void describe_outputs()
{
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))
        || FAILED(factory->EnumAdapters1(0, &adapter)))
        return;
    UINT count = 0;
    ComPtr<IDXGIOutput> output;
    for (; adapter->EnumOutputs(count, &output) != DXGI_ERROR_NOT_FOUND; ++count) {
        ComPtr<IDXGIOutput6> output6;
        DXGI_OUTPUT_DESC1 desc{};
        if (SUCCEEDED(output.As(&output6)) && SUCCEEDED(output6->GetDesc1(&desc)))
            line("DXGI output %u: %ld x %ld, %u bits, colour space %d, %.0f..%.0f nits", count,
                 desc.DesktopCoordinates.right - desc.DesktopCoordinates.left,
                 desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top,
                 desc.BitsPerColor, desc.ColorSpace, desc.MinLuminance, desc.MaxLuminance);
        output.Reset();
    }
    if (count == 0)
        line("DXGI outputs: none enumerable");
}

} // namespace

void describe_display()
{
    line("-- display");
    describe_hdmi();
    describe_outputs();
}
