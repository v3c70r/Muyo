// B0 probe: one trace through a null-layout, heap-flagged ray tracing pipeline. The TLAS is read through a
// heap handle, and the result is written through a heap storage buffer.

#include "heapProbePayload.h"

struct Push
{
    uint asIndex;
    uint outputIndex;
};

[[vk::push_constant]]
ConstantBuffer<Push> push;

[shader("raygeneration")]
void main()
{
    RaytracingAccelerationStructure tlas = RaytracingAccelerationStructure.Handle(uint2(push.asIndex, 0));
    RayDesc ray;
    ray.Origin = float3(0.0, 0.0, -2.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0.0;
    ray.TMax = 100.0;

    HeapProbePayload payload = {0};
    TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

    RWStructuredBuffer<uint> output = RWStructuredBuffer<uint>.Handle(uint2(push.outputIndex, 0));
    output[0] = payload.hit;
}
