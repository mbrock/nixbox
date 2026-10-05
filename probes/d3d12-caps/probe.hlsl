// GPU programs for the probe. Each compute entry point writes results the CPU
// checks; the graphics entry points draw into multisampled targets with
// vertex pulling and no input layout.

cbuffer Constants : register(b0)
{
    uint4 k;
};

RWByteAddressBuffer output : register(u0);
ByteAddressBuffer input : register(t0);
StructuredBuffer<uint> buffers[] : register(t0, space1);
Texture2D<float4> textures[] : register(t0, space2);
SamplerState point_sampler : register(s0);

// A buffer reached through a root descriptor: a bare GPU virtual address.
[numthreads(64, 1, 1)]
void root_address(uint i : SV_DispatchThreadID)
{
    output.Store(i * 4, input.Load(i * 4) * 3 + k.x);
}

// Unbounded descriptor arrays indexed per lane: bindless in the
// shader-model-5.1 style, which binding tier 3 allows.
[numthreads(64, 1, 1)]
void bindless(uint i : SV_DispatchThreadID)
{
    uint which = (i * 7) % k.x;
    uint b = buffers[NonUniformResourceIndex(which)][0];
    float4 t = textures[NonUniformResourceIndex(which)]
        .SampleLevel(point_sampler, float2(0.5, 0.5), 0);
    output.Store2(i * 8, uint2(b, (uint)(t.x * 255.0 + 0.5)));
}

[numthreads(64, 1, 1)]
void waves(uint i : SV_DispatchThreadID)
{
    output.Store3(i * 12, uint3(WaveGetLaneCount(), WaveActiveSum(i),
                                WavePrefixSum(1u)));
}

#define AT_LEAST(major, minor) (__SHADER_TARGET_MAJOR > major \
    || (__SHADER_TARGET_MAJOR == major && __SHADER_TARGET_MINOR >= minor))

#if AT_LEAST(6, 4)
// Shader model 6.4: packed 8-bit dot product.
[numthreads(64, 1, 1)]
void packed_dot(uint i : SV_DispatchThreadID)
{
    output.Store2(i * 8, uint2(dot4add_u8packed(0x01020304u, 0x05060708u + i, 0u),
                               i + 1000));
}
#endif

[numthreads(64, 1, 1)]
void wide(uint i : SV_DispatchThreadID)
{
    uint64_t v = ((uint64_t)i << 40) * 3 + 7;
    output.Store2(i * 8, uint2((uint)v, (uint)(v >> 32)));
}

// ExecuteIndirect changes the root constants between dispatches.
[numthreads(64, 1, 1)]
void indirect_mark(uint i : SV_DispatchThreadID)
{
    output.Store((k.y + i) * 4, k.x);
}

#if AT_LEAST(6, 5)
// Shader model 6.5: only here to see whether the runtime accepts it.
[numthreads(64, 1, 1)]
void model_6_5(uint i : SV_DispatchThreadID)
{
    output.Store(i * 4, WaveMatch(i & 3).x);
}
#endif

#if AT_LEAST(6, 6)
// Shader model 6.6 dynamic resources: descriptors named by heap index.
[numthreads(64, 1, 1)]
void heap_indexing(uint i : SV_DispatchThreadID)
{
    StructuredBuffer<uint> source = ResourceDescriptorHeap[k.y];
    RWByteAddressBuffer target = ResourceDescriptorHeap[k.z];
    target.Store(i * 4, source[0] + i);
}
#endif

// -- throughput ---------------------------------------------------------

// Four independent float4 chains, sixteen fused multiply-adds each per
// iteration: 512 flops per thread per iteration.
[numthreads(256, 1, 1)]
void fma_chain(uint i : SV_DispatchThreadID)
{
    float4 m = float4(0.999, 0.998, 0.997, 0.996) + i * 1e-9;
    float4 c = float4(1e-3, 2e-3, 3e-3, 4e-3);
    float4 a = i * 1e-6, b = a + 1, d = a + 2, e = a + 3;
    [loop]
    for (uint n = 0; n < k.x; ++n) {
        [unroll]
        for (uint u = 0; u < 16; ++u) {
            a = mad(a, m, c);
            b = mad(b, m, c);
            d = mad(d, m, c);
            e = mad(e, m, c);
        }
    }
    float s = dot(a + b, d + e);
    if (s == 1234.5678)
        output.Store(0, asuint(s));
}

// Streams 16 bytes in and 16 out per thread per step, grid-strided.
[numthreads(256, 1, 1)]
void copy_stream(uint i : SV_DispatchThreadID)
{
    for (uint at = i * 16; at < k.x; at += k.y * 16)
        output.Store4(at, input.Load4(at));
}

// -- graphics -----------------------------------------------------------

struct Varyings {
    float4 position : SV_Position;
    float4 color : COLOR;
};

struct Targets {
    float4 color : SV_Target0;
    float2 motion : SV_Target1;
    float mask : SV_Target2;
};

// Instance i draws one triangle at depth input[i].w with colour input[i].xyz,
// covering the centre of the target. Reversed-Z: larger depth is nearer.
Varyings layered_vertex(uint v : SV_VertexID, uint instance : SV_InstanceID)
{
    const float2 corners[3] = { float2(-1, -1), float2(3, -1), float2(-1, 3) };
    float4 layer = asfloat(input.Load4(instance * 16));
    Varyings o;
    o.position = float4(corners[v] * 0.9, layer.w, 1);
    o.color = float4(layer.xyz, 1);
    return o;
}

Targets layered_pixel(Varyings v)
{
    Targets t;
    t.color = v.color;
    t.motion = float2(0.25, -0.5);
    t.mask = 0.5;
    return t;
}

// Fullscreen triangle for fill-rate measurement.
float4 fill_vertex(uint v : SV_VertexID) : SV_Position
{
    return float4(v == 1 ? 3 : -1, v == 2 ? 3 : -1, 0.5, 1);
}

float4 fill_pixel(float4 p : SV_Position) : SV_Target
{
    return float4(0.001, 0.002, 0.003, 0.25);
}

// A tube of 16 facets by 8 rings, generated from the vertex index: the
// shape of a trunk drawn by instanced vertex pulling. k.x instances per row.
float4 tube_vertex(uint v : SV_VertexID, uint instance : SV_InstanceID)
    : SV_Position
{
    uint facet = v % 17, ring = v / 17;
    float angle = facet * (6.2831853 / 16);
    float row = instance / k.x, column = instance % k.x;
    float3 p = float3(cos(angle) * 0.3 + column * 1.1,
                      ring * 0.5 + row * 4.4,
                      sin(angle) * 0.3);
    float2 ndc = p.xy / float2(k.x * 1.1, k.y * 4.4) * 2 - 1;
    return float4(ndc, 0.5 + p.z * 0.1, 1);
}

float4 tube_pixel(float4 p : SV_Position) : SV_Target
{
    return float4(0.2, 0.3, 0.1, 1);
}
