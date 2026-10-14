// Fragment shader for the test that a graphics node is no longer limited to the engine's built-in
// descriptor sets 0-2. It declares set 3 binding 0, a set index the old semantic table rejected, and
// keeps the load live so the binding reaches reflection.

struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float3 worldPos;
};

struct FS_OUTPUT
{
    float4 color : SV_Target0;
};

[[vk::binding(0, 3)]] StructuredBuffer<float4> extraValues;

[shader("fragment")]
FS_OUTPUT main(VS_OUTPUT input)
{
    FS_OUTPUT output;
    output.color = float4(input.worldPos, 1.0);
    // A runtime-dependent branch keeps the load live: if it were folded away the binding would not
    // appear in reflection and the case would not test what it claims to.
    if (extraValues[0].x > 1e30F)
    {
        output.color = extraValues[0];
    }
    return output;
}
