// Vertex Shader Input Structure
struct VS_INPUT
{
    [[vk::location(0)]] float2 inPos : POSITION;
    [[vk::location(1)]] float2 inUV : TEXCOORD0;
    [[vk::location(2)]] float4 inColor : COLOR0;
};

// Vertex Shader Output Structure
struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float2 outUV : TEXCOORD0;
    [[vk::location(1)]] float4 outColor : COLOR0;
};

// Push Constants (in HLSL, typically implemented as a constant buffer)
[[vk::push_constant]]
cbuffer PushConstants
{
    float2 scale;
    float2 translate;
};

// Vertex Shader
[shader("vertex")]
VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output = (VS_OUTPUT)0;
    
    output.outUV = input.inUV;
    output.outColor = input.inColor;
    output.pos = float4(input.inPos * scale + translate, 0.0, 1.0);
    
    return output;
}
