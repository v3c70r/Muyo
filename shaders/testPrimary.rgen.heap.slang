// B1b-5b-5: ray generation on the descriptor-heap path for the ray-tracing / rasterization parity test.
// The per-view inverse matrices arrive inline in the node's push data (root data); the TLAS and output
// image arrive as heap indices. The inverse matrices are pushed rather than read through a PerViewData
// heap handle for the #233 offset-drop reason used by forward.vert.heap: mViewInv/mProjInv live at bytes
// 128/192, and the push block reproduces their RowMajor/stride-16 layout at offsets 0/64.

#include "shared/RenderGraph/Camera.h"

struct Push
{
    float4x4 mViewInv;
    float4x4 mProjInv;
    uint2    tlas;
    uint2    output;
};

[[vk::push_constant]]
ConstantBuffer<Push> push;

struct RayPayload
{
    float4 colorAndDistance; // rgb = hit color, a = hit distance
    float4 scatterDirection; // unused here (primary rays only)
};

[shader("raygeneration")]
void main()
{
    const uint2 pixel = DispatchRaysIndex().xy;
    const uint2 size = DispatchRaysDimensions().xy;
    const float2 inUV = (float2(pixel) + 0.5) / float2(size);

    // Match the raster pass NDC: x right, y down (Vulkan screen space).
    const float2 d = inUV * 2.0 - 1.0;

    const float4 originWorld = mul(push.mViewInv, float4(0.0, 0.0, 0.0, 1.0));
    const float4 target = mul(push.mProjInv, float4(d.x, d.y, 1.0, 1.0));
    const float4 directionWorld = mul(push.mViewInv, float4(normalize(target.xyz), 0.0));

    RayDesc ray;
    ray.Origin = originWorld.xyz;
    ray.Direction = directionWorld.xyz;
    ray.TMin = 0.1;
    ray.TMax = 100.0;

    RayPayload payload;
    payload.colorAndDistance = float4(0.0, 0.0, 0.0, -1.0);
    payload.scatterDirection = float4(0.0);

    RaytracingAccelerationStructure tlas = RaytracingAccelerationStructure.Handle(push.tlas);
    TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

    RWTexture2D<float4> output = RWTexture2D<float4>.Handle(push.output);
    output[pixel] = float4(payload.colorAndDistance.rgb, 1.0);
}
