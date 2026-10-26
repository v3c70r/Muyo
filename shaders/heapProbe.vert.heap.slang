// B0 probe: the vertex stage of the null-layout graphics pipeline. It uses no heap descriptor, so its
// module carries no descriptor-heap extension; the gate still requires it to have no Binding decoration.

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOut main(uint id : SV_VertexID)
{
    VSOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.position = float4(p * 2.0 - 1.0, 0.0, 1.0);
    o.uv = p;
    return o;
}
