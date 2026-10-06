// Miss shader for the parity test: background is black (no geometry).

struct RayPayload
{
    float4 colorAndDistance;
    float4 scatterDirection;
};

[shader("miss")]
void main(inout RayPayload payload)
{
    payload.colorAndDistance = float4(0.0, 0.0, 0.0, -1.0);
    payload.scatterDirection = float4(0.0);
}
