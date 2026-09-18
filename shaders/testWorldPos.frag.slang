// Minimal raster fragment shader for the ray-tracing parity test.
// Writes the interpolated world-space position as the color.

struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float3 worldPos;
};

struct FS_OUTPUT
{
    float4 color : SV_Target0;
};

[shader("fragment")]
FS_OUTPUT main(VS_OUTPUT input)
{
    FS_OUTPUT output;
    output.color = float4(input.worldPos, 1.0);
    return output;
}
