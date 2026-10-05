// Format support: one line per format with a compact flag string, then the
// multisample counts for the formats a scene would render into.
#include "report.h"

#include <cstdio>
#include <iterator>

namespace {

struct Format {
    DXGI_FORMAT format;
    const char *name;
};

const Format formats[] = {
    {DXGI_FORMAT_R16G16B16A16_FLOAT, "RGBA16F"},
    {DXGI_FORMAT_R32G32B32A32_FLOAT, "RGBA32F"},
    {DXGI_FORMAT_R11G11B10_FLOAT, "RG11B10F"},
    {DXGI_FORMAT_R10G10B10A2_UNORM, "RGB10A2"},
    {DXGI_FORMAT_R8G8B8A8_UNORM, "RGBA8"},
    {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, "RGBA8 sRGB"},
    {DXGI_FORMAT_B8G8R8A8_UNORM, "BGRA8"},
    {DXGI_FORMAT_R16G16_FLOAT, "RG16F"},
    {DXGI_FORMAT_R16G16_SNORM, "RG16 snorm"},
    {DXGI_FORMAT_R32_FLOAT, "R32F"},
    {DXGI_FORMAT_R32G32_FLOAT, "RG32F"},
    {DXGI_FORMAT_R16_FLOAT, "R16F"},
    {DXGI_FORMAT_R8_UNORM, "R8"},
    {DXGI_FORMAT_R8G8_UNORM, "RG8"},
    {DXGI_FORMAT_R32_UINT, "R32 uint"},
    {DXGI_FORMAT_R32G32B32A32_UINT, "RGBA32 uint"},
    {DXGI_FORMAT_D32_FLOAT, "D32F"},
    {DXGI_FORMAT_D32_FLOAT_S8X24_UINT, "D32F S8"},
    {DXGI_FORMAT_D24_UNORM_S8_UINT, "D24 S8"},
    {DXGI_FORMAT_D16_UNORM, "D16"},
    {DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, "D32F S8 as SRV"},
    {DXGI_FORMAT_BC1_UNORM, "BC1"},
    {DXGI_FORMAT_BC4_UNORM, "BC4"},
    {DXGI_FORMAT_BC5_SNORM, "BC5 snorm"},
    {DXGI_FORMAT_BC6H_UF16, "BC6H"},
    {DXGI_FORMAT_BC7_UNORM, "BC7"},
};

const DXGI_FORMAT multisampled[] = {
    DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16_FLOAT,
    DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM,
    DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
};

const char *name_of(DXGI_FORMAT format)
{
    for (const Format &f : formats)
        if (f.format == format)
            return f.name;
    return "?";
}

} // namespace

void describe_formats(ID3D12Device *device)
{
    line("-- formats: s sample, c compare, g gather, m mip, R target, B blend,");
    line("   M msaa target, v msaa resolve, l msaa load, D depth, U typed UAV,");
    line("   L typed UAV load, A UAV atomics");
    for (const Format &f : formats) {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{f.format};
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                               &support, sizeof support))) {
            line("%-15s unsupported", f.name);
            continue;
        }
        const auto s1 = support.Support1;
        const auto s2 = support.Support2;
        char flags[16];
        int n = 0;
        auto flag = [&](bool on, char c) { flags[n++] = on ? c : '.'; };
        flag(s1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE, 's');
        flag(s1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_COMPARISON, 'c');
        flag(s1 & D3D12_FORMAT_SUPPORT1_SHADER_GATHER, 'g');
        flag(s1 & D3D12_FORMAT_SUPPORT1_MIP, 'm');
        flag(s1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET, 'R');
        flag(s1 & D3D12_FORMAT_SUPPORT1_BLENDABLE, 'B');
        flag(s1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET, 'M');
        flag(s1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE, 'v');
        flag(s1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD, 'l');
        flag(s1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL, 'D');
        flag(s1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW, 'U');
        flag(s2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD, 'L');
        flag(s2 & D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD, 'A');
        flags[n] = 0;
        line("%-15s %s", f.name, flags);
    }

    for (DXGI_FORMAT format : multisampled) {
        char counts[64] = "";
        int n = 0;
        for (UINT count : {2u, 4u, 8u, 16u}) {
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels{format, count};
            if (SUCCEEDED(device->CheckFeatureSupport(
                    D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof levels))
                && levels.NumQualityLevels > 0)
                n += std::snprintf(counts + n, sizeof counts - n, " %ux", count);
        }
        line("msaa %-12s%s", name_of(format), n ? counts : " none");
    }
}
