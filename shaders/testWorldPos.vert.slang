// Minimal raster vertex shader for the ray-tracing parity test.
// Outputs the world-space position so it can be compared against the primary-ray
// closest-hit result. Uses an identity world matrix (the test geometry is in world space).

#include "shared/RenderGraph/bindings.h"

struct VS_INPUT
{
    [[vk::location(0)]] float3 inPos : POSITION;
};

struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float3 worldPos;
};

CAMERA()

[shader("vertex")]
VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    output.worldPos = input.inPos;
    output.pos = mul(perView.mProj, mul(perView.mView, float4(input.inPos, 1.0)));
    return output;
}
