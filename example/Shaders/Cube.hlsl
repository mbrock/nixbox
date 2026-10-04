cbuffer Scene : register(b0)
{
    float angle;
    float aspect;
    float2 padding;
};

struct VertexInput
{
    float3 position : POSITION;
    float3 color : COLOR;
    float3 normal : NORMAL;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 color : COLOR;
};

float3 rotate(float3 value)
{
    float s, c;
    sincos(angle, s, c);
    value = float3(c * value.x + s * value.z, value.y, -s * value.x + c * value.z);
    sincos(-0.45, s, c);
    return float3(value.x, c * value.y - s * value.z, s * value.y + c * value.z);
}

VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    float3 position = rotate(input.position);
    position.z += 5.0;
    // Left-handed perspective, near = 0.1, far = 100, with D3D's 0..1 depth.
    output.position = float4(position.x * 2.1 / aspect, position.y * 2.1,
                             (100.0 * position.z - 10.0) / 99.9, position.z);
    float light = 0.3 + 0.7 * saturate(dot(rotate(input.normal), normalize(float3(-1, 2, -3))));
    output.color = input.color * light;
    return output;
}

float4 pixelMain(VertexOutput input) : SV_TARGET
{
    return float4(input.color, 1);
}
