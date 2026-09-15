// Closest-hit shader for the parity test: reports the world-space hit position.

struct RayPayload
{
    float4 colorAndDistance; // rgb = hit color, a = hit distance
    float4 scatterDirection;
};

[shader("closesthit")]
void main(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    const float3 worldPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    payload.colorAndDistance = float4(worldPos, RayTCurrent());
    payload.scatterDirection = float4(0.0);
}
