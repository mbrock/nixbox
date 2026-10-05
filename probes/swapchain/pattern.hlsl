// A test pattern in swapchain pixels: a band in the phase's tag colour, then
// one-pixel vertical stripes on the left and horizontal ones on the right.
// The console's screenshot shows whether a stripe stays one output pixel wide.
cbuffer Pattern : register(b0)
{
    float3 tag;
    float white;
    float2 size;
    float2 padding;
};

float4 vertexMain(uint v : SV_VertexID) : SV_Position
{
    return float4(v == 1 ? 3 : -1, v == 2 ? 3 : -1, 0, 1);
}

float4 pixelMain(float4 p : SV_Position) : SV_Target
{
    if (p.y < size.y * 0.125)
        return float4(tag * white, 1);
    const uint2 q = uint2(p.xy);
    const float on = p.x < size.x * 0.5 ? (q.x & 1) : (q.y & 1);
    return float4(on.xxx * white, 1);
}
