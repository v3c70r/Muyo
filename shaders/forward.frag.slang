struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float2 outTexCoords0 : TEXCOORD0;
    [[vk::location(1)]] float2 outTexCoords1 : TEXCOORD1;
    [[vk::location(2)]] float4 outWorldPos;
    [[vk::location(3)]] float4 outWorldNormal;
    [[vk::location(4)]] uint2 outObjSubmeshIndex;
};

struct FS_OUTPUT
{
    float4 color : SV_Target0;
    float depth : SV_Depth;
};

[shader("fragment")]
FS_OUTPUT main(VS_OUTPUT input)
{
    FS_OUTPUT output = {0};
    output.depth = input.pos.z;
    output.color = input.outWorldPos;
    return output;
}




