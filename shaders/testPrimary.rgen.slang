// Ray generation shader for the ray-tracing / rasterization parity test.
// Casts one primary ray per pixel using the same camera matrices as the raster pass and
// stores the closest-hit world position (or background) into a storage image.

#include "shared/RenderGraph/Camera.h"

struct RayPayload
{
    float4 colorAndDistance; // rgb = hit color, a = hit distance
    float4 scatterDirection; // unused here (primary rays only)
};

[[vk::binding(0, 0)]] ConstantBuffer<PerViewData> gPerView;
[[vk::binding(1, 0)]] RaytracingAccelerationStructure gTLAS;
[[vk::binding(2, 0)]] RWTexture2D<float4> gOutput;

[shader("raygeneration")]
void main()
{
    const uint2 pixel = DispatchRaysIndex().xy;
    const uint2 size = DispatchRaysDimensions().xy;
    const float2 inUV = (float2(pixel) + 0.5) / float2(size);

    // Match the raster pass NDC: x right, y down (Vulkan screen space).
    const float2 d = inUV * 2.0 - 1.0;

    const float4 originWorld = mul(gPerView.mViewInv, float4(0.0, 0.0, 0.0, 1.0));
    const float4 target = mul(gPerView.mProjInv, float4(d.x, d.y, 1.0, 1.0));
    const float4 directionWorld = mul(gPerView.mViewInv, float4(normalize(target.xyz), 0.0));

    RayDesc ray;
    ray.Origin = originWorld.xyz;
    ray.Direction = directionWorld.xyz;
    ray.TMin = 0.1;
    ray.TMax = 100.0;

    RayPayload payload;
    payload.colorAndDistance = float4(0.0, 0.0, 0.0, -1.0);
    payload.scatterDirection = float4(0.0);

    TraceRay(gTLAS, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

    gOutput[pixel] = float4(payload.colorAndDistance.rgb, 1.0);
}
