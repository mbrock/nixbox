// Reports what Direct3D 12 offers a Developer Mode UWP app on this console:
// feature level, shader model, mesh shaders, ray tracing, wave operations,
// and the rest of the capabilities a modern renderer depends on; format
// support; the display; live tests of binding and render-target patterns;
// and measured throughput. The report is drawn on screen and written to
// LocalState/nixbox/d3d12-caps/d3d12-caps.txt.
#include "report.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <d3d12.h>
#include <dxgi1_4.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

std::vector<std::string> lines;

void line(const char *format, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    lines.emplace_back(buffer);
    SDL_Log("%s", buffer);
}

namespace {

template <typename T>
bool query(ID3D12Device *device, D3D12_FEATURE feature, T &data)
{
    return SUCCEEDED(device->CheckFeatureSupport(feature, &data, sizeof data));
}

const char *feature_level_name(D3D_FEATURE_LEVEL level)
{
    switch (level) {
    case D3D_FEATURE_LEVEL_12_2: return "12_2";
    case D3D_FEATURE_LEVEL_12_1: return "12_1";
    case D3D_FEATURE_LEVEL_12_0: return "12_0";
    case D3D_FEATURE_LEVEL_11_1: return "11_1";
    case D3D_FEATURE_LEVEL_11_0: return "11_0";
    default: return "?";
    }
}

void describe_adapter()
{
    IDXGIFactory4 *factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void **>(&factory)))) {
        line("DXGI factory: unavailable");
        return;
    }
    IDXGIAdapter1 *adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        char name[128]{};
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof name, nullptr, nullptr);
        line("adapter %u: %s  vendor %04x device %04x", i, name, desc.VendorId, desc.DeviceId);
        line("  dedicated video %llu MB, dedicated system %llu MB, shared %llu MB",
             desc.DedicatedVideoMemory >> 20, desc.DedicatedSystemMemory >> 20,
             desc.SharedSystemMemory >> 20);
        adapter->Release();
    }
    factory->Release();
}

void describe_device(ID3D12Device *device)
{
    const D3D_FEATURE_LEVEL wanted[] = {
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_2,
    };
    D3D12_FEATURE_DATA_FEATURE_LEVELS levels{};
    levels.NumFeatureLevels = static_cast<UINT>(std::size(wanted));
    levels.pFeatureLevelsRequested = wanted;
    if (query(device, D3D12_FEATURE_FEATURE_LEVELS, levels))
        line("max feature level: %s", feature_level_name(levels.MaxSupportedFeatureLevel));

    const D3D_SHADER_MODEL models[] = {
        D3D_SHADER_MODEL_6_9, D3D_SHADER_MODEL_6_8, D3D_SHADER_MODEL_6_7,
        D3D_SHADER_MODEL_6_6, D3D_SHADER_MODEL_6_5, D3D_SHADER_MODEL_6_4,
        D3D_SHADER_MODEL_6_3, D3D_SHADER_MODEL_6_2, D3D_SHADER_MODEL_6_1,
        D3D_SHADER_MODEL_6_0, D3D_SHADER_MODEL_5_1,
    };
    for (D3D_SHADER_MODEL model : models) {
        D3D12_FEATURE_DATA_SHADER_MODEL data{model};
        if (query(device, D3D12_FEATURE_SHADER_MODEL, data)) {
            line("highest shader model: %d.%d", data.HighestShaderModel >> 4,
                 data.HighestShaderModel & 0xf);
            break;
        }
    }

    D3D12_FEATURE_DATA_ARCHITECTURE1 architecture{};
    if (query(device, D3D12_FEATURE_ARCHITECTURE1, architecture))
        line("UMA %d, cache-coherent UMA %d, isolated MMU %d", architecture.UMA,
             architecture.CacheCoherentUMA, architecture.IsolatedMMU);

    D3D12_FEATURE_DATA_D3D12_OPTIONS o0{};
    if (query(device, D3D12_FEATURE_D3D12_OPTIONS, o0))
        line("binding tier %d, tiled resources tier %d, conservative raster tier %d, "
             "ROVs %d, typed UAV load additional formats %d",
             o0.ResourceBindingTier, o0.TiledResourcesTier,
             o0.ConservativeRasterizationTier, o0.ROVsSupported,
             o0.TypedUAVLoadAdditionalFormats);

    if (query(device, D3D12_FEATURE_D3D12_OPTIONS, o0))
        line("max GPU VA bits per resource %u, standard swizzle 64K %d, double %d, "
             "PS stencil ref %d, RT array index from VS %d",
             o0.MaxGPUVirtualAddressBitsPerResource, o0.StandardSwizzle64KBSupported,
             o0.DoublePrecisionFloatShaderOps, o0.PSSpecifiedStencilRefSupported,
             o0.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation);

    D3D12_FEATURE_DATA_ROOT_SIGNATURE root{D3D_ROOT_SIGNATURE_VERSION_1_1};
    if (query(device, D3D12_FEATURE_ROOT_SIGNATURE, root))
        line("root signature version 1.%d", root.HighestVersion == D3D_ROOT_SIGNATURE_VERSION_1_1);

    D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT va{};
    if (query(device, D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, va))
        line("GPU VA bits: %u per resource, %u per process",
             va.MaxGPUVirtualAddressBitsPerResource, va.MaxGPUVirtualAddressBitsPerProcess);

    D3D12_FEATURE_DATA_SHADER_CACHE cache{};
    if (query(device, D3D12_FEATURE_SHADER_CACHE, cache))
        line("shader cache flags 0x%x", cache.SupportFlags);

    D3D12_FEATURE_DATA_D3D12_OPTIONS1 o1{};
    if (query(device, D3D12_FEATURE_D3D12_OPTIONS1, o1))
        line("wave ops %d, lanes %u..%u, int64 shader ops %d", o1.WaveOps,
             o1.WaveLaneCountMin, o1.WaveLaneCountMax, o1.Int64ShaderOps);

    D3D12_FEATURE_DATA_D3D12_OPTIONS2 o2{};
    if (query(device, D3D12_FEATURE_D3D12_OPTIONS2, o2))
        line("depth bounds test %d, programmable sample positions tier %d",
             o2.DepthBoundsTestSupported, o2.ProgrammableSamplePositionsTier);

    D3D12_FEATURE_DATA_D3D12_OPTIONS3 o3{};
    if (query(device, D3D12_FEATURE_D3D12_OPTIONS3, o3))
        line("view instancing tier %d, barycentrics %d, copy queue timestamps %d, "
             "write buffer immediate 0x%x",
             o3.ViewInstancingTier, o3.BarycentricsSupported, o3.CopyQueueTimestampQueriesSupported,
             o3.WriteBufferImmediateSupportFlags);

    D3D12_FEATURE_DATA_D3D12_OPTIONS4 o4{};
    if (query(device, D3D12_FEATURE_D3D12_OPTIONS4, o4))
        line("native 16-bit shader ops %d", o4.Native16BitShaderOpsSupported);

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
    if (query(device, D3D12_FEATURE_D3D12_OPTIONS5, o5))
        line("raytracing tier %d, render passes tier %d", o5.RaytracingTier,
             o5.RenderPassesTier);

    D3D12_FEATURE_DATA_D3D12_OPTIONS6 o6{};
    if (query(device, D3D12_FEATURE_D3D12_OPTIONS6, o6))
        line("variable shading rate tier %d", o6.VariableShadingRateTier);

    D3D12_FEATURE_DATA_D3D12_OPTIONS7 o7{};
    if (query(device, D3D12_FEATURE_D3D12_OPTIONS7, o7))
        line("MESH SHADER TIER %d, sampler feedback tier %d", o7.MeshShaderTier,
             o7.SamplerFeedbackTier);
    else
        line("MESH SHADER TIER: query failed");

    D3D12_FEATURE_DATA_D3D12_OPTIONS9 o9{};
    if (query(device, D3D12_FEATURE_D3D12_OPTIONS9, o9))
        line("mesh pipeline stats %d, atomic int64 typed %d, groupshared %d, "
             "wave MMA tier %d",
             o9.MeshShaderPipelineStatsSupported,
             o9.AtomicInt64OnTypedResourceSupported,
             o9.AtomicInt64OnGroupSharedSupported, o9.WaveMMATier);

    D3D12_FEATURE_DATA_D3D12_OPTIONS8 o8{};
    if (query(device, D3D12_FEATURE_D3D12_OPTIONS8, o8))
        line("unaligned block textures %d", o8.UnalignedBlockTexturesSupported);

    D3D12_FEATURE_DATA_D3D12_OPTIONS12 o12{};
    if (query(device, D3D12_FEATURE_D3D12_OPTIONS12, o12))
        line("enhanced barriers %d", o12.EnhancedBarriersSupported);
    else
        line("enhanced barriers: query failed");

    D3D12_FEATURE_DATA_D3D12_OPTIONS13 o13{};
    if (query(device, D3D12_FEATURE_D3D12_OPTIONS13, o13))
        line("unrestricted copy pitch %d, inverted viewport height %d",
             o13.UnrestrictedBufferTextureCopyPitchSupported, o13.InvertedViewportHeightFlipsYSupported);

    D3D12_FEATURE_DATA_D3D12_OPTIONS16 o16{};
    if (query(device, D3D12_FEATURE_D3D12_OPTIONS16, o16))
        line("GPU upload heap %d, dynamic depth bias %d", o16.GPUUploadHeapSupported,
             o16.DynamicDepthBiasSupported);

    D3D12_FEATURE_DATA_D3D12_OPTIONS21 o21{};
    if (query(device, D3D12_FEATURE_D3D12_OPTIONS21, o21))
        line("work graphs tier %d", o21.WorkGraphsTier);
    else
        line("work graphs: query failed");
}

void write_report()
{
    char *dir = SDL_GetPrefPath("nixbox", "d3d12-caps");
    std::string path = std::string(dir ? dir : "") + "d3d12-caps.txt";
    SDL_free(dir);
    if (FILE *file = std::fopen(path.c_str(), "w")) {
        for (const std::string &text : lines)
            std::fprintf(file, "%s\n", text.c_str());
        std::fclose(file);
        SDL_Log("report: %s", path.c_str());
    } else {
        SDL_Log("report: could not write %s", path.c_str());
    }
}

} // namespace

int main(int, char **)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("D3D12 capabilities", 1920, 1080, SDL_WINDOW_FULLSCREEN,
                                     &window, &renderer)) {
        SDL_Log("Window or renderer failed: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    int width = 0, height = 0;
    SDL_GetRenderOutputSize(renderer, &width, &height);
    line("SDL window: %d x %d pixels, renderer %s", width, height, SDL_GetRendererName(renderer));

    MEMORYSTATUSEX memory{sizeof memory};
    if (GlobalMemoryStatusEx(&memory))
        line("process memory: %llu MB physical available of %llu MB, %llu MB commit",
             memory.ullAvailPhys >> 20, memory.ullTotalPhys >> 20, memory.ullTotalPageFile >> 20);
    describe_display();
    line("-- device");
    describe_adapter();
    ID3D12Device *device = nullptr;
    const HRESULT created = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                              __uuidof(ID3D12Device),
                                              reinterpret_cast<void **>(&device));
    if (SUCCEEDED(created)) {
        describe_device(device);
        describe_formats(device);
        write_report();
        run_gpu_tests(device);
        device->Release();
    } else {
        line("D3D12CreateDevice failed: 0x%08lx", static_cast<unsigned long>(created));
    }
    write_report();

    // Columns of debug text, scaled so a 4K output reads like 1080p.
    const float scale = height >= 2000 ? 2.0f : 1.0f;
    const float logical_width = width / scale, logical_height = height / scale;
    const float step = 10, top = 32;
    const int per_column = int((logical_height - top - 16) / step);
    for (bool running = true; running;) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_EVENT_QUIT)
                running = false;
        SDL_SetRenderDrawColorFloat(renderer, 0.05f, 0.07f, 0.11f, 1);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawColorFloat(renderer, 1, 1, 1, 1);
        SDL_SetRenderScale(renderer, scale, scale);
        SDL_RenderDebugText(renderer, 16, 12, "Direct3D 12 capabilities (Developer Mode UWP)");
        for (size_t i = 0; i < lines.size(); ++i) {
            const int column = int(i) / per_column, row = int(i) % per_column;
            SDL_RenderDebugText(renderer, 16 + column * logical_width / 2,
                                top + row * step, lines[i].c_str());
        }
        SDL_SetRenderScale(renderer, 1, 1);
        SDL_RenderPresent(renderer);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
