// B1b-5b-4: minimal raster vertex stage on the descriptor-heap path for the ray-tracing parity test.
// The per-view matrices arrive inline in the node's push data (root data, not a descriptor); see
// forward.vert.heap.slang for why the camera is not read through a PerViewData heap handle (#233).

#include "shared/RenderGraph/Camera.h"

struct Push
{
    float4x4 mView;
    float4x4 mProj;
    uint2    perObjData;  // unused here; shared with forward.vert.heap so one host block serves both
};

[[vk::push_constant]]
ConstantBuffer<Push> push;

struct VS_INPUT
{
    [[vk::location(0)]] float3 inPos : POSITION;
};

struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float3 worldPos;
};

[shader("vertex")]
VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    output.worldPos = input.inPos;
    output.pos = mul(push.mProj, mul(push.mView, float4(input.inPos, 1.0)));
    return output;
}
