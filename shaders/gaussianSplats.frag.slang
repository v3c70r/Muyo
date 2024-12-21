struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float2 outUV : TEXCOORD0;
    [[vk::location(1)]] float4 outColor : COLOR0;
};
struct FS_OUTPUT
{
    float4 color : SV_TARGET;
};
[shader("fragment")]
FS_OUTPUT main(VS_OUTPUT input)
{
    FS_OUTPUT output = {0};
    output.color = input.outColor;
    return output;
}
