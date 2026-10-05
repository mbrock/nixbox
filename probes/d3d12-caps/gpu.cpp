// Live GPU tests: each one records a small command list, runs it, reads the
// result back, and checks it against the CPU's own answer. The benchmarks
// that follow use timestamp queries around single dispatches and draws.
#include "report.h"

#include <d3dx12.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include <shader_bindless.h>
#include <shader_copy_stream.h>
#include <shader_fill_pixel.h>
#include <shader_fill_vertex.h>
#include <shader_fma_chain.h>
#include <shader_heap_indexing.h>
#include <shader_indirect_mark.h>
#include <shader_layered_pixel.h>
#include <shader_layered_vertex.h>
#include <shader_model_6_5.h>
#include <shader_packed_dot.h>
#include <shader_root_address.h>
#include <shader_tube_pixel.h>
#include <shader_tube_vertex.h>
#include <shader_waves.h>
#include <shader_wide.h>

using Microsoft::WRL::ComPtr;

namespace {

#define BYTECODE(name) D3D12_SHADER_BYTECODE{name, sizeof name}

constexpr UINT heap_size = 1000000;

struct Gpu {
    ID3D12Device *device = nullptr;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    UINT64 fence_value = 0;
    HANDLE event = nullptr;
    UINT64 frequency = 0;
    ComPtr<ID3D12QueryHeap> timestamps;
    ComPtr<ID3D12Resource> timestamp_readback;
    ComPtr<ID3D12DescriptorHeap> heap;
    UINT increment = 0;
    ComPtr<ID3D12RootSignature> compute_root;
    ComPtr<ID3D12RootSignature> graphics_root;
    bool lost = false;

    // Closes, executes, and waits for the list, then opens it again.
    bool run()
    {
        if (lost)
            return false;
        HRESULT hr = list->Close();
        if (FAILED(hr)) {
            line("  command list close failed: 0x%08lx", static_cast<unsigned long>(hr));
            list->Reset(allocator.Get(), nullptr);
            return false;
        }
        ID3D12CommandList *lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        queue->Signal(fence.Get(), ++fence_value);
        fence->SetEventOnCompletion(fence_value, event);
        if (WaitForSingleObjectEx(event, 10000, FALSE) != WAIT_OBJECT_0) {
            line("  GPU did not finish within 10 s");
            lost = true;
            return false;
        }
        hr = device->GetDeviceRemovedReason();
        if (FAILED(hr)) {
            line("  device removed: 0x%08lx", static_cast<unsigned long>(hr));
            lost = true;
            return false;
        }
        allocator->Reset();
        list->Reset(allocator.Get(), nullptr);
        return true;
    }

    ComPtr<ID3D12Resource> buffer(UINT64 size, D3D12_HEAP_TYPE type,
                                  D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
    {
        const CD3DX12_HEAP_PROPERTIES properties(type);
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(size, flags);
        const D3D12_RESOURCE_STATES state =
            type == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ
            : type == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST
                                               : D3D12_RESOURCE_STATE_COMMON;
        ComPtr<ID3D12Resource> resource;
        device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state,
                                        nullptr, IID_PPV_ARGS(&resource));
        return resource;
    }

    ComPtr<ID3D12Resource> upload(const void *data, UINT64 size)
    {
        ComPtr<ID3D12Resource> resource = buffer(size, D3D12_HEAP_TYPE_UPLOAD);
        void *mapped = nullptr;
        if (resource && SUCCEEDED(resource->Map(0, nullptr, &mapped))) {
            std::memcpy(mapped, data, size);
            resource->Unmap(0, nullptr);
        }
        return resource;
    }

    ComPtr<ID3D12Resource> uav_buffer(UINT64 size)
    {
        return buffer(size, D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    }

    void barrier(ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                 D3D12_RESOURCE_STATES after)
    {
        const auto b = CD3DX12_RESOURCE_BARRIER::Transition(resource, before, after);
        list->ResourceBarrier(1, &b);
    }

    // Copies a UAV buffer written in this list to a readback buffer, runs the
    // list, and returns the words.
    std::vector<uint32_t> read_back(ID3D12Resource *source, UINT64 size)
    {
        ComPtr<ID3D12Resource> readback = buffer(size, D3D12_HEAP_TYPE_READBACK);
        barrier(source, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(readback.Get(), 0, source, 0, size);
        std::vector<uint32_t> words(size / 4);
        if (!run())
            return {};
        void *mapped = nullptr;
        const D3D12_RANGE range{0, static_cast<SIZE_T>(size)};
        if (SUCCEEDED(readback->Map(0, &range, &mapped))) {
            std::memcpy(words.data(), mapped, size);
            readback->Unmap(0, nullptr);
        }
        return words;
    }

    ComPtr<ID3D12PipelineState> compute(D3D12_SHADER_BYTECODE code, ID3D12RootSignature *root,
                                        HRESULT *result = nullptr)
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root ? root : compute_root.Get();
        desc.CS = code;
        ComPtr<ID3D12PipelineState> pipeline;
        const HRESULT hr = device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipeline));
        if (result)
            *result = hr;
        else if (FAILED(hr))
            line("  compute pipeline failed: 0x%08lx", static_cast<unsigned long>(hr));
        return pipeline;
    }

    void bind_compute(ID3D12PipelineState *pipeline)
    {
        list->SetPipelineState(pipeline);
        list->SetComputeRootSignature(compute_root.Get());
        ID3D12DescriptorHeap *heaps[] = {heap.Get()};
        list->SetDescriptorHeaps(1, heaps);
    }

    void constants(UINT a, UINT b = 0, UINT c = 0, UINT d = 0)
    {
        const UINT k[4] = {a, b, c, d};
        list->SetComputeRoot32BitConstants(0, 4, k, 0);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE cpu_slot(UINT slot)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(slot) * increment;
        return h;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE gpu_slot(UINT slot)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE h = heap->GetGPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<UINT64>(slot) * increment;
        return h;
    }

    // Times what `record` encodes, in milliseconds; the best of `repeats`.
    double timed(const std::function<void()> &record, int repeats = 3)
    {
        double best = -1;
        for (int r = 0; r < repeats; ++r) {
            list->EndQuery(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
            record();
            list->EndQuery(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
            list->ResolveQueryData(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                                   timestamp_readback.Get(), 0);
            if (!run())
                return -1;
            UINT64 *ticks = nullptr;
            const D3D12_RANGE range{0, 16};
            if (FAILED(timestamp_readback->Map(0, &range, reinterpret_cast<void **>(&ticks))))
                return -1;
            const double ms = double(ticks[1] - ticks[0]) * 1000.0 / double(frequency);
            timestamp_readback->Unmap(0, nullptr);
            if (best < 0 || ms < best)
                best = ms;
        }
        return best;
    }
};

const char *verdict(bool ok)
{
    return ok ? "ok" : "FAILED";
}

float half_to_float(uint16_t h)
{
    const uint32_t sign = (h >> 15) & 1, exponent = (h >> 10) & 31, mantissa = h & 1023;
    float value;
    if (exponent == 0)
        value = std::ldexp(float(mantissa), -24);
    else if (exponent == 31)
        value = mantissa ? NAN : INFINITY;
    else
        value = std::ldexp(float(mantissa | 1024), int(exponent) - 25);
    return sign ? -value : value;
}

bool setup(Gpu &gpu, ID3D12Device *device)
{
    gpu.device = device;
    D3D12_COMMAND_QUEUE_DESC queue{D3D12_COMMAND_LIST_TYPE_DIRECT};
    if (FAILED(device->CreateCommandQueue(&queue, IID_PPV_ARGS(&gpu.queue)))
        || FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 IID_PPV_ARGS(&gpu.allocator)))
        || FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            gpu.allocator.Get(), nullptr,
                                            IID_PPV_ARGS(&gpu.list)))
        || FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gpu.fence)))) {
        line("  could not create a queue, list, and fence");
        return false;
    }
    gpu.event = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    gpu.queue->GetTimestampFrequency(&gpu.frequency);
    line("direct queue timestamp frequency: %llu Hz", gpu.frequency);

    D3D12_QUERY_HEAP_DESC queries{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2};
    device->CreateQueryHeap(&queries, IID_PPV_ARGS(&gpu.timestamps));
    gpu.timestamp_readback = gpu.buffer(16, D3D12_HEAP_TYPE_READBACK);

    D3D12_DESCRIPTOR_HEAP_DESC heap{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, heap_size,
                                    D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    const HRESULT heap_result = device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&gpu.heap));
    line("shader-visible heap of %u descriptors: %s", heap_size,
         SUCCEEDED(heap_result) ? "ok" : "FAILED");
    if (FAILED(heap_result))
        return false;
    gpu.increment =
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Compute: four root constants, a root UAV and SRV by address, and two
    // unbounded tables (buffers in space 1, textures in space 2).
    CD3DX12_DESCRIPTOR_RANGE buffers, textures;
    buffers.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 1);
    textures.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 2);
    CD3DX12_ROOT_PARAMETER parameters[5];
    parameters[0].InitAsConstants(4, 0);
    parameters[1].InitAsUnorderedAccessView(0);
    parameters[2].InitAsShaderResourceView(0);
    parameters[3].InitAsDescriptorTable(1, &buffers);
    parameters[4].InitAsDescriptorTable(1, &textures);
    const CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_POINT);
    const CD3DX12_ROOT_SIGNATURE_DESC compute_desc(5, parameters, 1, &sampler);

    // Graphics: root constants for every stage and a root SRV for pulling.
    CD3DX12_ROOT_PARAMETER graphics_parameters[2];
    graphics_parameters[0].InitAsConstants(4, 0);
    graphics_parameters[1].InitAsShaderResourceView(0);
    const CD3DX12_ROOT_SIGNATURE_DESC graphics_desc(2, graphics_parameters);

    auto make_root = [&](const CD3DX12_ROOT_SIGNATURE_DESC &desc,
                         ComPtr<ID3D12RootSignature> &root) {
        ComPtr<ID3DBlob> blob, error;
        if (SUCCEEDED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob,
                                                  &error))
            && SUCCEEDED(device->CreateRootSignature(0, blob->GetBufferPointer(),
                                                     blob->GetBufferSize(),
                                                     IID_PPV_ARGS(&root))))
            return true;
        line("  root signature failed%s%s", error ? ": " : "",
             error ? static_cast<const char *>(error->GetBufferPointer()) : "");
        return false;
    };
    if (!make_root(compute_desc, gpu.compute_root) || !make_root(graphics_desc, gpu.graphics_root))
        return false;
    return true;
}

void test_root_address(Gpu &gpu)
{
    std::vector<uint32_t> input(64);
    for (uint32_t i = 0; i < 64; ++i)
        input[i] = i;
    auto source = gpu.upload(input.data(), 256);
    auto target = gpu.uav_buffer(256);
    auto pipeline = gpu.compute(BYTECODE(shader_root_address), nullptr);
    if (!pipeline)
        return;
    gpu.bind_compute(pipeline.Get());
    gpu.constants(5);
    gpu.list->SetComputeRootUnorderedAccessView(1, target->GetGPUVirtualAddress());
    gpu.list->SetComputeRootShaderResourceView(2, source->GetGPUVirtualAddress());
    gpu.list->Dispatch(1, 1, 1);
    auto words = gpu.read_back(target.Get(), 256);
    bool ok = words.size() == 64;
    for (uint32_t i = 0; ok && i < 64; ++i)
        ok = words[i] == i * 3 + 5;
    line("root descriptors by GPU address (cs_6_0): %s", verdict(ok));
}

void test_bindless(Gpu &gpu)
{
    constexpr UINT count = 64;
    // Buffers: one array, one single-element view per slot.
    std::vector<uint32_t> values(count);
    for (UINT j = 0; j < count; ++j)
        values[j] = j * 11 + 5;
    auto value_buffer = gpu.upload(values.data(), count * 4);
    for (UINT j = 0; j < count; ++j) {
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Buffer.FirstElement = j;
        view.Buffer.NumElements = 1;
        view.Buffer.StructureByteStride = 4;
        gpu.device->CreateShaderResourceView(value_buffer.Get(), &view, gpu.cpu_slot(j));
    }

    // Textures: separate 1x1 resources, filled through placed footprints.
    std::vector<ComPtr<ID3D12Resource>> textures(count);
    auto staging = gpu.buffer(count * D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT,
                              D3D12_HEAP_TYPE_UPLOAD);
    uint8_t *mapped = nullptr;
    staging->Map(0, nullptr, reinterpret_cast<void **>(&mapped));
    const CD3DX12_HEAP_PROPERTIES default_heap(D3D12_HEAP_TYPE_DEFAULT);
    const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, 1);
    for (UINT j = 0; j < count; ++j) {
        gpu.device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &desc,
                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                            IID_PPV_ARGS(&textures[j]));
        const UINT64 offset = UINT64(j) * D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
        const uint8_t texel[4] = {uint8_t(j * 3), 0, 0, 255};
        std::memcpy(mapped + offset, texel, 4);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{
            offset, {DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT}};
        const CD3DX12_TEXTURE_COPY_LOCATION to(textures[j].Get(), 0);
        const CD3DX12_TEXTURE_COPY_LOCATION from(staging.Get(), footprint);
        gpu.list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        gpu.barrier(textures[j].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        gpu.device->CreateShaderResourceView(textures[j].Get(), nullptr,
                                             gpu.cpu_slot(count + j));
    }
    staging->Unmap(0, nullptr);

    auto target = gpu.uav_buffer(64 * 8);
    auto pipeline = gpu.compute(BYTECODE(shader_bindless), nullptr);
    if (!pipeline)
        return;
    gpu.bind_compute(pipeline.Get());
    gpu.constants(count);
    gpu.list->SetComputeRootUnorderedAccessView(1, target->GetGPUVirtualAddress());
    gpu.list->SetComputeRootDescriptorTable(3, gpu.gpu_slot(0));
    gpu.list->SetComputeRootDescriptorTable(4, gpu.gpu_slot(count));
    gpu.list->Dispatch(1, 1, 1);
    auto words = gpu.read_back(target.Get(), 64 * 8);
    bool ok = words.size() == 128;
    for (uint32_t i = 0; ok && i < 64; ++i) {
        const uint32_t which = (i * 7) % count;
        ok = words[i * 2] == which * 11 + 5 && words[i * 2 + 1] == which * 3;
    }
    line("unbounded SRV tables, non-uniform index (cs_6_0): %s", verdict(ok));
}

void test_waves(Gpu &gpu)
{
    auto target = gpu.uav_buffer(64 * 12);
    auto pipeline = gpu.compute(BYTECODE(shader_waves), nullptr);
    if (!pipeline)
        return;
    gpu.bind_compute(pipeline.Get());
    gpu.list->SetComputeRootUnorderedAccessView(1, target->GetGPUVirtualAddress());
    gpu.list->Dispatch(1, 1, 1);
    auto words = gpu.read_back(target.Get(), 64 * 12);
    if (words.size() != 64 * 3)
        return;
    const uint32_t lanes = words[0];
    bool ok = lanes > 0;
    for (uint32_t i = 0; ok && i < 64; ++i) {
        const uint32_t first = i / lanes * lanes;
        uint32_t sum = 0;
        for (uint32_t j = first; j < first + lanes; ++j)
            sum += j;
        ok = words[i * 3] == lanes && words[i * 3 + 1] == sum && words[i * 3 + 2] == i - first;
    }
    line("wave intrinsics in compute: %s, %u lanes", verdict(ok), lanes);
}

void test_packed_dot(Gpu &gpu)
{
    auto target = gpu.uav_buffer(512);
    HRESULT hr;
    auto pipeline = gpu.compute(BYTECODE(shader_packed_dot), nullptr, &hr);
    if (!pipeline) {
        line("dot4add_u8packed (cs_6_4): pipeline FAILED 0x%08lx", static_cast<unsigned long>(hr));
        return;
    }
    gpu.bind_compute(pipeline.Get());
    gpu.list->SetComputeRootUnorderedAccessView(1, target->GetGPUVirtualAddress());
    gpu.list->Dispatch(1, 1, 1);
    auto words = gpu.read_back(target.Get(), 512);
    if (words.size() != 128)
        return;
    uint32_t wrong = 0, first = 64, got = 0, wanted = 0, control = 0;
    for (uint32_t i = 0; i < 64; ++i) {
        const uint32_t a = 0x01020304u, b = 0x05060708u + i;
        uint32_t expected = 0;
        for (int byte = 0; byte < 4; ++byte)
            expected += ((a >> (byte * 8)) & 255) * ((b >> (byte * 8)) & 255);
        if (words[i * 2] != expected && wrong++ == 0)
            first = i, got = words[i * 2], wanted = expected;
        control += words[i * 2 + 1] == i + 1000;
    }
    line("dot4add_u8packed (cs_6_4): %s; same shader's plain store %u/64", verdict(wrong == 0),
         control);
    if (wrong)
        line("  %u of 64 lanes wrong; lane %u gave %u (0x%08x), expected %u", wrong, first, got,
             got, wanted);
}

void test_wide(Gpu &gpu)
{
    auto target = gpu.uav_buffer(64 * 8);
    auto pipeline = gpu.compute(BYTECODE(shader_wide), nullptr);
    if (!pipeline)
        return;
    gpu.bind_compute(pipeline.Get());
    gpu.list->SetComputeRootUnorderedAccessView(1, target->GetGPUVirtualAddress());
    gpu.list->Dispatch(1, 1, 1);
    auto words = gpu.read_back(target.Get(), 64 * 8);
    bool ok = words.size() == 128;
    for (uint32_t i = 0; ok && i < 64; ++i) {
        const uint64_t v = (uint64_t(i) << 40) * 3 + 7;
        ok = words[i * 2] == uint32_t(v) && words[i * 2 + 1] == uint32_t(v >> 32);
    }
    line("64-bit integer arithmetic: %s", verdict(ok));
}

void test_indirect(Gpu &gpu)
{
    struct Command {
        UINT k[4];
        D3D12_DISPATCH_ARGUMENTS dispatch;
    };
    D3D12_INDIRECT_ARGUMENT_DESC arguments[2]{};
    arguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    arguments[0].Constant = {0, 0, 4};
    arguments[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    const D3D12_COMMAND_SIGNATURE_DESC desc{sizeof(Command), 2, arguments, 0};
    ComPtr<ID3D12CommandSignature> signature;
    HRESULT hr = gpu.device->CreateCommandSignature(&desc, gpu.compute_root.Get(),
                                                    IID_PPV_ARGS(&signature));
    if (FAILED(hr)) {
        line("ExecuteIndirect with root constants: signature FAILED 0x%08lx",
             static_cast<unsigned long>(hr));
        return;
    }
    Command commands[3];
    for (UINT c = 0; c < 3; ++c)
        commands[c] = {{100 + c, c * 64, 0, 0}, {1, 1, 1}};
    auto argument_buffer = gpu.upload(commands, sizeof commands);
    const UINT32 count = 2;
    auto count_buffer = gpu.upload(&count, 4);
    auto pipeline = gpu.compute(BYTECODE(shader_indirect_mark), nullptr);
    if (!pipeline)
        return;

    for (bool counted : {false, true}) {
        auto target = gpu.uav_buffer(192 * 4);
        gpu.bind_compute(pipeline.Get());
        gpu.list->SetComputeRootUnorderedAccessView(1, target->GetGPUVirtualAddress());
        gpu.list->ExecuteIndirect(signature.Get(), 3, argument_buffer.Get(), 0,
                                  counted ? count_buffer.Get() : nullptr, 0);
        auto words = gpu.read_back(target.Get(), 192 * 4);
        bool ok = words.size() == 192;
        for (uint32_t i = 0; ok && i < 192; ++i) {
            const uint32_t c = i / 64;
            ok = words[i] == (counted && c == 2 ? 0 : 100 + c);
        }
        line("ExecuteIndirect, root constants + dispatch%s: %s",
             counted ? ", GPU count" : "", verdict(ok));
    }
}

void test_newer_models(Gpu &gpu)
{
    HRESULT hr;
    auto pipeline = gpu.compute(BYTECODE(shader_model_6_5), nullptr, &hr);
    line("cs_6_5 pipeline: %s (0x%08lx)", pipeline ? "created" : "rejected",
         static_cast<unsigned long>(hr));

    // Shader model 6.6 dynamic resources need a root signature that says
    // the shader indexes the heap directly.
    CD3DX12_ROOT_PARAMETER1 constants;
    constants.InitAsConstants(4, 0);
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc;
    desc.Init_1_1(1, &constants, 0, nullptr,
                  D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
    ComPtr<ID3DBlob> blob, error;
    ComPtr<ID3D12RootSignature> root;
    hr = D3D12SerializeVersionedRootSignature(&desc, &blob, &error);
    if (SUCCEEDED(hr))
        hr = gpu.device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                             IID_PPV_ARGS(&root));
    line("heap-indexed root signature: %s (0x%08lx)", root ? "created" : "rejected",
         static_cast<unsigned long>(hr));
    if (!root)
        return;
    pipeline = gpu.compute(BYTECODE(shader_heap_indexing), root.Get(), &hr);
    line("cs_6_6 ResourceDescriptorHeap pipeline: %s (0x%08lx)",
         pipeline ? "created" : "rejected", static_cast<unsigned long>(hr));
}

// Three triangles with reversed-Z depth into a 4x multisampled RGBA16F, RG16F,
// and R8 target set over D32F S8: the nearest (green) one must win.
void test_multisampled_targets(Gpu &gpu)
{
    constexpr UINT size = 64, samples = 4;
    const DXGI_FORMAT formats[3] = {DXGI_FORMAT_R16G16B16A16_FLOAT,
                                    DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R8_UNORM};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = gpu.graphics_root.Get();
    desc.VS = BYTECODE(shader_layered_vertex);
    desc.PS = BYTECODE(shader_layered_pixel);
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 3;
    for (int i = 0; i < 3; ++i)
        desc.RTVFormats[i] = formats[i];
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    desc.SampleDesc = {samples, 0};
    ComPtr<ID3D12PipelineState> pipeline;
    HRESULT hr = gpu.device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline));
    if (FAILED(hr)) {
        line("4x MSAA MRT pipeline FAILED 0x%08lx", static_cast<unsigned long>(hr));
        return;
    }

    ComPtr<ID3D12DescriptorHeap> rtv_heap, dsv_heap;
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3};
    D3D12_DESCRIPTOR_HEAP_DESC dsv_desc{D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1};
    gpu.device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap));
    gpu.device->CreateDescriptorHeap(&dsv_desc, IID_PPV_ARGS(&dsv_heap));
    const UINT rtv_step =
        gpu.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    const CD3DX12_HEAP_PROPERTIES default_heap(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> targets[3], resolved[3], depth;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3];
    for (int i = 0; i < 3; ++i) {
        const auto target_desc = CD3DX12_RESOURCE_DESC::Tex2D(
            formats[i], size, size, 1, 1, samples, 0,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        const float black[4] = {};
        const CD3DX12_CLEAR_VALUE clear(formats[i], black);
        gpu.device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &target_desc,
                                            D3D12_RESOURCE_STATE_RENDER_TARGET, &clear,
                                            IID_PPV_ARGS(&targets[i]));
        const auto resolved_desc = CD3DX12_RESOURCE_DESC::Tex2D(formats[i], size, size, 1, 1);
        gpu.device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE,
                                            &resolved_desc, D3D12_RESOURCE_STATE_RESOLVE_DEST,
                                            nullptr, IID_PPV_ARGS(&resolved[i]));
        rtvs[i] = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        rtvs[i].ptr += i * rtv_step;
        gpu.device->CreateRenderTargetView(targets[i].Get(), nullptr, rtvs[i]);
    }
    const auto depth_desc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_D32_FLOAT_S8X24_UINT, size, size, 1, 1, samples, 0,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    const CD3DX12_CLEAR_VALUE depth_clear(DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 0.0f, 0);
    gpu.device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &depth_desc,
                                        D3D12_RESOURCE_STATE_DEPTH_WRITE, &depth_clear,
                                        IID_PPV_ARGS(&depth));
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
    gpu.device->CreateDepthStencilView(depth.Get(), nullptr, dsv);

    const float layers[3][4] = {{1, 0, 0, 0.2f}, {0, 1, 0, 0.8f}, {0, 0, 1, 0.5f}};
    auto layer_buffer = gpu.upload(layers, sizeof layers);

    auto &list = gpu.list;
    for (int i = 0; i < 3; ++i) {
        const float black[4] = {};
        list->ClearRenderTargetView(rtvs[i], black, 0, nullptr);
    }
    list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.0f, 0,
                                0, nullptr);
    list->OMSetRenderTargets(3, rtvs, FALSE, &dsv);
    list->SetPipelineState(pipeline.Get());
    list->SetGraphicsRootSignature(gpu.graphics_root.Get());
    list->SetGraphicsRootShaderResourceView(1, layer_buffer->GetGPUVirtualAddress());
    const D3D12_VIEWPORT viewport{0, 0, float(size), float(size), 0, 1};
    const D3D12_RECT scissor{0, 0, LONG(size), LONG(size)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 3, 0, 0);

    std::vector<ComPtr<ID3D12Resource>> readbacks(3);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[3];
    for (int i = 0; i < 3; ++i) {
        gpu.barrier(targets[i].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        list->ResolveSubresource(resolved[i].Get(), 0, targets[i].Get(), 0, formats[i]);
        gpu.barrier(resolved[i].Get(), D3D12_RESOURCE_STATE_RESOLVE_DEST,
                    D3D12_RESOURCE_STATE_COPY_SOURCE);
        const auto resolved_desc = resolved[i]->GetDesc();
        UINT64 total = 0;
        gpu.device->GetCopyableFootprints(&resolved_desc, 0, 1, 0, &footprints[i], nullptr,
                                          nullptr, &total);
        readbacks[i] = gpu.buffer(total, D3D12_HEAP_TYPE_READBACK);
        const CD3DX12_TEXTURE_COPY_LOCATION to(readbacks[i].Get(), footprints[i]);
        const CD3DX12_TEXTURE_COPY_LOCATION from(resolved[i].Get(), 0);
        list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    }
    if (!gpu.run())
        return;

    auto pixel = [&](int i) {
        uint8_t *bytes = nullptr;
        readbacks[i]->Map(0, nullptr, reinterpret_cast<void **>(&bytes));
        const UINT texel = i == 0 ? 8 : i == 1 ? 4 : 1;
        std::vector<uint8_t> out(texel);
        std::memcpy(out.data(), bytes + footprints[i].Offset
                                    + footprints[i].Footprint.RowPitch * (size / 2)
                                    + texel * (size / 2),
                    texel);
        readbacks[i]->Unmap(0, nullptr);
        return out;
    };
    auto color = pixel(0), motion = pixel(1), mask = pixel(2);
    uint16_t h[4], m[2];
    std::memcpy(h, color.data(), 8);
    std::memcpy(m, motion.data(), 4);
    const float r = half_to_float(h[0]), g = half_to_float(h[1]), b = half_to_float(h[2]);
    const float mx = half_to_float(m[0]), my = half_to_float(m[1]);
    const bool ok = r == 0 && g == 1 && b == 0 && mx == 0.25f && my == -0.5f
                    && (mask[0] == 127 || mask[0] == 128);
    line("4x MSAA RGBA16F+RG16F+R8, D32F S8, reversed-Z, resolve: %s", verdict(ok));
    if (!ok)
        line("  centre %.3f %.3f %.3f, motion %.3f %.3f, mask %u", r, g, b, mx, my, mask[0]);
}

void benchmark_compute(Gpu &gpu)
{
    auto pipeline = gpu.compute(BYTECODE(shader_fma_chain), nullptr);
    auto sink = gpu.uav_buffer(256);
    if (!pipeline)
        return;
    constexpr UINT groups = 4096, threads = groups * 256, iterations = 1024;
    auto dispatch = [&] {
        gpu.bind_compute(pipeline.Get());
        gpu.constants(iterations);
        gpu.list->SetComputeRootUnorderedAccessView(1, sink->GetGPUVirtualAddress());
        gpu.list->Dispatch(groups, 1, 1);
    };
    const double ms = gpu.timed(dispatch);
    if (ms > 0)
        line("fp32 FMA: %.2f TFLOPS (%.2f ms)", double(threads) * iterations * 512 / ms / 1e9,
             ms);

    constexpr UINT64 bytes = 256ull << 20;
    auto source = gpu.uav_buffer(bytes), target = gpu.uav_buffer(bytes);
    auto stream = gpu.compute(BYTECODE(shader_copy_stream), nullptr);
    if (!source || !target || !stream) {
        line("  could not allocate two 256 MB buffers");
        return;
    }
    constexpr UINT stream_groups = 8192;
    const double stream_ms = gpu.timed([&] {
        gpu.bind_compute(stream.Get());
        gpu.constants(UINT(bytes), stream_groups * 256);
        gpu.list->SetComputeRootUnorderedAccessView(1, target->GetGPUVirtualAddress());
        gpu.list->SetComputeRootShaderResourceView(2, source->GetGPUVirtualAddress());
        gpu.list->Dispatch(stream_groups, 1, 1);
        const auto b = CD3DX12_RESOURCE_BARRIER::UAV(target.Get());
        gpu.list->ResourceBarrier(1, &b);
    });
    if (stream_ms > 0)
        line("compute copy, 256 MB: %.0f GB/s read+write (%.2f ms)",
             2.0 * bytes / stream_ms / 1e6, stream_ms);
    const double copy_ms = gpu.timed([&] {
        gpu.list->CopyBufferRegion(target.Get(), 0, source.Get(), 0, bytes);
    });
    if (copy_ms > 0)
        line("CopyBufferRegion, 256 MB: %.0f GB/s read+write (%.2f ms)",
             2.0 * bytes / copy_ms / 1e6, copy_ms);
}

ComPtr<ID3D12Resource> render_target(Gpu &gpu, DXGI_FORMAT format, UINT width, UINT height,
                                     D3D12_CPU_DESCRIPTOR_HANDLE rtv)
{
    const CD3DX12_HEAP_PROPERTIES default_heap(D3D12_HEAP_TYPE_DEFAULT);
    const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, 1, 1, 0,
                                                   D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    ComPtr<ID3D12Resource> target;
    gpu.device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &desc,
                                        D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                        IID_PPV_ARGS(&target));
    if (target)
        gpu.device->CreateRenderTargetView(target.Get(), nullptr, rtv);
    return target;
}

void benchmark_raster(Gpu &gpu)
{
    ComPtr<ID3D12DescriptorHeap> rtv_heap, dsv_heap;
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3};
    D3D12_DESCRIPTOR_HEAP_DESC dsv_desc{D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1};
    gpu.device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap));
    gpu.device->CreateDescriptorHeap(&dsv_desc, IID_PPV_ARGS(&dsv_heap));
    const UINT rtv_step =
        gpu.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    auto rtv = [&](UINT i) {
        D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += i * rtv_step;
        return h;
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC base{};
    base.pRootSignature = gpu.graphics_root.Get();
    base.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    base.SampleMask = UINT_MAX;
    base.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    base.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    base.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    base.NumRenderTargets = 1;
    base.SampleDesc = {1, 0};

    // Fill rate at 4K: additive RGBA16F, then opaque RGBA8.
    constexpr UINT width = 3840, height = 2160, layers = 16;
    struct Fill {
        const char *name;
        DXGI_FORMAT format;
        bool blend;
    };
    const Fill fills[] = {{"RGBA16F additive", DXGI_FORMAT_R16G16B16A16_FLOAT, true},
                          {"RGBA8 opaque", DXGI_FORMAT_R8G8B8A8_UNORM, false}};
    for (UINT f = 0; f < 2; ++f) {
        auto desc = base;
        desc.VS = BYTECODE(shader_fill_vertex);
        desc.PS = BYTECODE(shader_fill_pixel);
        desc.RTVFormats[0] = fills[f].format;
        desc.DepthStencilState.DepthEnable = FALSE;
        if (fills[f].blend) {
            auto &blend = desc.BlendState.RenderTarget[0];
            blend.BlendEnable = TRUE;
            blend.SrcBlend = blend.DestBlend = D3D12_BLEND_ONE;
            blend.SrcBlendAlpha = blend.DestBlendAlpha = D3D12_BLEND_ONE;
        }
        ComPtr<ID3D12PipelineState> pipeline;
        if (FAILED(gpu.device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline))))
            continue;
        auto target = render_target(gpu, fills[f].format, width, height, rtv(f));
        if (!target)
            continue;
        const D3D12_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
        const D3D12_RECT scissor{0, 0, LONG(width), LONG(height)};
        const double ms = gpu.timed([&] {
            const auto handle = rtv(f);
            gpu.list->OMSetRenderTargets(1, &handle, FALSE, nullptr);
            gpu.list->SetPipelineState(pipeline.Get());
            gpu.list->SetGraphicsRootSignature(gpu.graphics_root.Get());
            gpu.list->RSSetViewports(1, &viewport);
            gpu.list->RSSetScissorRects(1, &scissor);
            gpu.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            gpu.list->DrawInstanced(3, layers, 0, 0);
        });
        if (ms > 0)
            line("fill, %s 4K x%u: %.1f Gpixel/s (%.2f ms)", fills[f].name, layers,
                 double(width) * height * layers / ms / 1e6, ms);
    }

    // Instanced tubes from an index buffer and the vertex index: the shape
    // of an instanced, vertex-pulled trunk forest, at 1080p with depth.
    std::vector<uint16_t> indices;
    for (uint16_t ring = 0; ring < 8; ++ring)
        for (uint16_t facet = 0; facet < 16; ++facet) {
            const uint16_t a = ring * 17 + facet, b = a + 1, c = a + 17, d = c + 1;
            indices.insert(indices.end(), {a, b, c, b, d, c});
        }
    auto index_buffer = gpu.upload(indices.data(), indices.size() * 2);
    const D3D12_INDEX_BUFFER_VIEW index_view{index_buffer->GetGPUVirtualAddress(),
                                             UINT(indices.size() * 2), DXGI_FORMAT_R16_UINT};
    auto desc = base;
    desc.VS = BYTECODE(shader_tube_vertex);
    desc.PS = BYTECODE(shader_tube_pixel);
    desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    ComPtr<ID3D12PipelineState> pipeline;
    if (FAILED(gpu.device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline))))
        return;
    constexpr UINT tube_width = 1920, tube_height = 1080, columns = 200, rows = 100;
    auto target = render_target(gpu, DXGI_FORMAT_R16G16B16A16_FLOAT, tube_width, tube_height,
                                rtv(2));
    const CD3DX12_HEAP_PROPERTIES default_heap(D3D12_HEAP_TYPE_DEFAULT);
    const auto depth_desc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_D32_FLOAT, tube_width, tube_height, 1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    ComPtr<ID3D12Resource> depth;
    gpu.device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &depth_desc,
                                        D3D12_RESOURCE_STATE_DEPTH_WRITE, nullptr,
                                        IID_PPV_ARGS(&depth));
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
    gpu.device->CreateDepthStencilView(depth.Get(), nullptr, dsv);
    const D3D12_VIEWPORT viewport{0, 0, float(tube_width), float(tube_height), 0, 1};
    const D3D12_RECT scissor{0, 0, LONG(tube_width), LONG(tube_height)};
    const double ms = gpu.timed([&] {
        const auto handle = rtv(2);
        gpu.list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
        gpu.list->OMSetRenderTargets(1, &handle, FALSE, &dsv);
        gpu.list->SetPipelineState(pipeline.Get());
        gpu.list->SetGraphicsRootSignature(gpu.graphics_root.Get());
        const UINT k[4] = {columns, rows, 0, 0};
        gpu.list->SetGraphicsRoot32BitConstants(0, 4, k, 0);
        gpu.list->RSSetViewports(1, &viewport);
        gpu.list->RSSetScissorRects(1, &scissor);
        gpu.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        gpu.list->IASetIndexBuffer(&index_view);
        gpu.list->DrawIndexedInstanced(UINT(indices.size()), columns * rows, 0, 0, 0);
    });
    const double triangles = double(indices.size() / 3) * columns * rows;
    if (ms > 0)
        line("instanced tubes, %.1fM triangles at 1080p: %.0f Mtri/s (%.2f ms)",
             triangles / 1e6, triangles / ms / 1e3, ms);
}

} // namespace

void run_gpu_tests(ID3D12Device *device)
{
    line("-- live GPU tests");
    Gpu gpu;
    if (!setup(gpu, device))
        return;
    using Test = void (*)(Gpu &);
    for (Test test : {test_root_address, test_bindless, test_waves, test_packed_dot, test_wide,
                      test_indirect, test_multisampled_targets, test_newer_models}) {
        if (gpu.lost)
            break;
        test(gpu);
    }
    line("-- throughput");
    if (!gpu.lost)
        benchmark_compute(gpu);
    if (!gpu.lost)
        benchmark_raster(gpu);
    if (gpu.event)
        CloseHandle(gpu.event);
}
